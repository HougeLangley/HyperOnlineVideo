package com.hougelangley.hyperonlinevideo.data

import java.io.File
import java.io.OutputStream
import java.io.RandomAccessFile
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

/**
 * 本机流代理（只监听 127.0.0.1，仅本 App 使用）
 *
 * 为什么需要它：
 *  - mpv / ffmpeg 是 native 代码，无法逐 socket 指定网络；系统 VPN 会把它们的流量带走
 *  - 做法：mpv 播 `http://127.0.0.1:PORT/s/<token>`，本代理在 Java 层用 [NetRoute] 的直连网络
 *    去拉上游 CDN，再把字节转发给 mpv（支持 Range，进度条/拖动正常）
 *
 * 另一个用途：yt-dlp 下载音乐时，音频与封面已由 Java 预取到本地，通过 `/f/<token>` 提供给
 * yt-dlp 做本地封装（封面/标签），整个过程不依赖 VPN，也不会绕过直连策略。
 */
object StreamProxy {

    @Volatile private var server: ServerSocket? = null
    @Volatile var port: Int = 0
        private set

    /** 当前播放流（token → 远端 URL） */
    @Volatile private var streamToken: String? = null
    @Volatile private var streamUrl: String? = null

    /** 本地文件（token → File），供 yt-dlp 使用 */
    private val files = ConcurrentHashMap<String, File>()

    /** 注册媒体流并返回 mpv 可用的本机地址；失败返回 null（调用方退回原 URL） */
    @Synchronized
    fun serveStream(remoteUrl: String): String? {
        if (!ensureServer()) return null
        val token = UUID.randomUUID().toString().replace("-", "")
        streamUrl = remoteUrl
        streamToken = token
        return "http://127.0.0.1:$port/s/$token"
    }

    /** 注册本地文件并返回本机地址（供 yt-dlp 读取） */
    @Synchronized
    fun serveFile(file: File): String? {
        if (!ensureServer()) return null
        val token = UUID.randomUUID().toString().replace("-", "")
        files[token] = file
        return "http://127.0.0.1:$port/f/$token"
    }

    /** 注销本地文件（下载结束后清理） */
    fun releaseFile(file: File) {
        files.entries.removeAll { it.value.absolutePath == file.absolutePath }
    }

    @Synchronized
    private fun ensureServer(): Boolean {
        server?.let { if (!it.isClosed) return true }
        return try {
            // 注意：InetAddress.getLoopbackAddress() 在 Android 上可能返回 IPv6 ::1，
            // 而 mpv/yt-dlp 连的是 127.0.0.1 → 必须显式绑定 IPv4 回环
            val ss = ServerSocket(0, 16, InetAddress.getByName("127.0.0.1"))
            server = ss
            port = ss.localPort
            Thread {
                while (!ss.isClosed) {
                    val sock = try { ss.accept() } catch (e: Exception) { break }
                    Thread { handle(sock) }.apply { isDaemon = true }.start()
                }
            }.apply { isDaemon = true; name = "stream-proxy" }.start()
            true
        } catch (e: Exception) {
            false
        }
    }

    private fun handle(sock: Socket) {
        try {
            sock.soTimeout = 20000
            val ins = sock.getInputStream().bufferedReader(Charsets.ISO_8859_1)   // 只读请求头（HTTP 头为 latin1 安全）
            val requestLine = ins.readLine() ?: return
            val parts = requestLine.split(" ")
            if (parts.size < 2) return
            val method = parts[0]
            val path = parts[1]
            var range: String? = null
            while (true) {
                val line = ins.readLine() ?: break
                if (line.isEmpty()) break
                if (line.startsWith("Range:", ignoreCase = true)) range = line.substringAfter(":").trim()
            }
            val out = sock.getOutputStream()
            when {
                path.startsWith("/s/") -> proxyStream(path.removePrefix("/s/").substringBefore('?'), range, method, out)
                path.startsWith("/f/") -> serveLocal(path.removePrefix("/f/").substringBefore('?'), range, method, out)
                else -> writeSimple(out, 404, "Not Found")
            }
            out.flush()
        } catch (e: Exception) {
            // 播放器/下载器提前断开属正常现象
        } finally {
            try { sock.close() } catch (e: Exception) { }
        }
    }

    /** 转发远端媒体流（Range 透传） */
    private fun proxyStream(token: String, range: String?, method: String, out: OutputStream) {
        if (token != streamToken) { writeSimple(out, 404, "Not Found"); return }
        val url = streamUrl ?: run { writeSimple(out, 404, "Not Found"); return }
        var conn: java.net.HttpURLConnection? = null
        try {
            conn = NetRoute.open(url, 20000)
            conn.requestMethod = if (method == "HEAD") "HEAD" else "GET"
            conn.setRequestProperty("User-Agent", MusicHttp.UA)
            if (!range.isNullOrEmpty()) conn.setRequestProperty("Range", range)
            val code = conn.responseCode
            if (code < 200 || code >= 300) {
                writeSimple(out, code, "Upstream error")
                return
            }
            val header = buildString {
                append("HTTP/1.1 ").append(code).append(if (code == 206) " Partial Content" else " OK").append("\r\n")
                append("Content-Type: ").append(conn.contentType ?: "application/octet-stream").append("\r\n")
                append("Accept-Ranges: bytes\r\n")
                conn.getHeaderField("Content-Length")?.let { append("Content-Length: ").append(it).append("\r\n") }
                conn.getHeaderField("Content-Range")?.let { append("Content-Range: ").append(it).append("\r\n") }
                append("Connection: close\r\n\r\n")
            }
            out.write(header.toByteArray())
            out.flush()
            if (method != "HEAD") {
                conn.inputStream.use { ins -> ins.copyTo(out, 64 * 1024) }
            }
        } catch (e: Exception) {
            try { writeSimple(out, 502, "Bad Gateway") } catch (e2: Exception) { }
        } finally {
            try { conn?.disconnect() } catch (e: Exception) { }
        }
    }

    /** 提供本地文件（支持 Range，供 yt-dlp / 播放器读取） */
    private fun serveLocal(token: String, range: String?, method: String, out: OutputStream) {
        val f = files[token] ?: run { writeSimple(out, 404, "Not Found"); return }
        if (!f.exists()) { writeSimple(out, 404, "Not Found"); return }
        val total = f.length()
        var start = 0L
        var end = total - 1
        var partial = false
        if (!range.isNullOrEmpty() && range.startsWith("bytes=")) {
            val spec = range.removePrefix("bytes=").split("-")
            start = spec.getOrNull(0)?.toLongOrNull() ?: 0L
            end = spec.getOrNull(1)?.toLongOrNull()?.takeIf { it in 0..<total } ?: (total - 1)
            if (start > end || start >= total) {
                writeSimple(out, 416, "Range Not Satisfiable")
                return
            }
            partial = true
        }
        val length = end - start + 1
        val mime = when (f.extension.lowercase()) {
            "jpg", "jpeg" -> "image/jpeg"
            "png" -> "image/png"
            "mp3" -> "audio/mpeg"
            "flac" -> "audio/flac"
            "m4a" -> "audio/mp4"
            else -> "application/octet-stream"
        }
        val header = buildString {
            append("HTTP/1.1 ").append(if (partial) "206 Partial Content" else "200 OK").append("\r\n")
            append("Content-Type: ").append(mime).append("\r\n")
            append("Accept-Ranges: bytes\r\n")
            append("Content-Length: ").append(length).append("\r\n")
            if (partial) append("Content-Range: bytes ").append(start).append("-").append(end).append("/").append(total).append("\r\n")
            append("Connection: close\r\n\r\n")
        }
        out.write(header.toByteArray())
        out.flush()
        if (method == "HEAD") return
        RandomAccessFile(f, "r").use { raf ->
            raf.seek(start)
            val buf = ByteArray(64 * 1024)
            var remaining = length
            while (remaining > 0) {
                val n = raf.read(buf, 0, minOf(buf.size.toLong(), remaining).toInt())
                if (n <= 0) break
                out.write(buf, 0, n)
                remaining -= n
            }
        }
    }

    private fun writeSimple(out: OutputStream, code: Int, text: String) {
        val body = text.toByteArray()
        val header = "HTTP/1.1 $code ${if (code == 200) "OK" else text}\r\n" +
            "Content-Type: text/plain; charset=utf-8\r\n" +
            "Content-Length: ${body.size}\r\nConnection: close\r\n\r\n"
        out.write(header.toByteArray())
        out.write(body)
    }
}
