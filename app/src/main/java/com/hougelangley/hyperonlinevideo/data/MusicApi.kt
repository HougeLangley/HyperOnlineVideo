package com.hougelangley.hyperonlinevideo.data

import android.util.Base64
import org.json.JSONArray
import org.json.JSONObject
import java.net.URLEncoder
import java.security.MessageDigest
import javax.crypto.Cipher
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.SecretKeySpec

/**
 * 音乐平台 API（2026-09 协议实测）：
 * - 网易云：搜索走公开 /api 路径；取流走 weapi（AES-128-CBC + RSA 加密）；封面由 picId 算法构造
 * - QQ音乐：统一走 u.y.qq.com/cgi-bin/musicu.fcg（JSON，无加密）；封面由 albummid 构造
 * 免登录：免费歌 128k；登录（VIP）后可取 320k / 无损 FLAC
 */

/** 音乐音源（统一类型）：ext = mp3 / flac / m4a / ape */
data class MusicStream(val url: String, val ext: String, val br: Int)

// ============ 通用 ============

internal object MusicHttp {
    const val UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"

    fun get(url: String, referer: String, cookie: String? = null, timeoutMs: Int = 15000): String {
        val conn = NetRoute.open(url, timeoutMs)   // 直连网络（不随系统 VPN）
        return try {
            conn.setRequestProperty("User-Agent", UA)
            conn.setRequestProperty("Referer", referer)
            if (!cookie.isNullOrEmpty()) conn.setRequestProperty("Cookie", cookie)
            conn.inputStream.bufferedReader().use { it.readText() }
        } finally {
            conn.disconnect()
        }
    }

    fun postForm(url: String, referer: String, body: String, cookie: String? = null, timeoutMs: Int = 15000): String {
        val conn = NetRoute.open(url, timeoutMs)
        return try {
            conn.requestMethod = "POST"
            conn.doOutput = true
            conn.setRequestProperty("User-Agent", UA)
            conn.setRequestProperty("Referer", referer)
            conn.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
            if (!cookie.isNullOrEmpty()) conn.setRequestProperty("Cookie", cookie)
            conn.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
            conn.inputStream.bufferedReader().use { it.readText() }
        } finally {
            conn.disconnect()
        }
    }

    fun postJson(url: String, referer: String, json: String, cookie: String? = null, timeoutMs: Int = 15000): String {
        val conn = NetRoute.open(url, timeoutMs)
        return try {
            conn.requestMethod = "POST"
            conn.doOutput = true
            conn.setRequestProperty("User-Agent", UA)
            conn.setRequestProperty("Referer", referer)
            conn.setRequestProperty("Content-Type", "application/json")
            if (!cookie.isNullOrEmpty()) conn.setRequestProperty("Cookie", cookie)
            conn.outputStream.use { it.write(json.toByteArray(Charsets.UTF_8)) }
            conn.inputStream.bufferedReader().use { it.readText() }
        } finally {
            conn.disconnect()
        }
    }
}

// ============ 网易云音乐 ============

object NeteaseApi {
    private const val BASE = "https://music.163.com"
    private const val REFERER = "https://music.163.com/"

    // weapi 加密常量（2026 实测有效）
    private const val NONCE = "0CoJUm6Qyw8W8jud"
    private val IV = "0102030405060708".toByteArray(Charsets.UTF_8)   // 注意：是 ASCII 字符串，非原始字节
    private const val MODULUS =
        "00e0b509f6259df8642dbc35662901477df22677ec152b5ff68ace615bb7b725152b3ab17a876aea8a5aa76d2e417629ec4ee341f56135fccf695280104e0312ecbda92557c93870114af6c9d05c4f7f0c3685b7a46bee255932575cce10b424d813cfe4875d3e82047b97ddef52741d546b8e289dc6935b3ece0462db0a22b8e7"
    private const val PUBKEY = "010001"
    private const val CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

    /** 登录 Cookie（MUSIC_U 等）；由 Repo 注入 */
    @Volatile var cookieHeader: String? = null

    private fun aesCbcB64(text: ByteArray, key: ByteArray): String {
        val cipher = Cipher.getInstance("AES/CBC/PKCS5Padding")
        cipher.init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "AES"), IvParameterSpec(IV))
        return Base64.encodeToString(cipher.doFinal(text), Base64.NO_WRAP)
    }

    /** 构造 weapi 请求体（params + encSecKey） */
    private fun weapiBody(payload: JSONObject): String {
        val secret = (1..16).map { CHARS.random() }.joinToString("")
        val enc1 = aesCbcB64(payload.toString().toByteArray(Charsets.UTF_8), NONCE.toByteArray(Charsets.UTF_8))
        val enc2 = aesCbcB64(enc1.toByteArray(Charsets.UTF_8), secret.toByteArray(Charsets.UTF_8))
        val rev = secret.reversed().toByteArray(Charsets.UTF_8)
        val m = java.math.BigInteger(1, rev)
        val rsa = m.modPow(java.math.BigInteger(PUBKEY, 16), java.math.BigInteger(MODULUS, 16))
        val encSecKey = rsa.toString(16).padStart(256, '0')
        return "params=" + URLEncoder.encode(enc2, "UTF-8") + "&encSecKey=" + encSecKey
    }

    private fun weapi(path: String, payload: JSONObject, cookie: String? = cookieHeader): String =
        MusicHttp.postForm(BASE + path + "?csrf_token=", REFERER, weapiBody(payload), cookie)

    /** picId → 专辑封面 URL（XOR + MD5 + base64 算法） */
    fun coverUrl(picId: Long): String {
        if (picId <= 0) return ""
        val magic = "3go8&$8*3*3h0k(2)2"
        val s = picId.toString()
        val xored = ByteArray(s.length) { i -> (s[i].code xor magic[i % magic.length].code).toByte() }
        val md5 = MessageDigest.getInstance("MD5").digest(xored)
        val tag = Base64.encodeToString(md5, Base64.NO_WRAP).replace('/', '_').replace('+', '-')
        return "https://p1.music.126.net/$tag/$picId.jpg"
    }

    /** 搜索（公开路径，无需加密）：返回 id/歌名/歌手/专辑/picId/时长 */
    fun search(query: String, page: Int, limit: Int = 20): List<VideoItem> {
        val enc = URLEncoder.encode(query, "UTF-8")
        val offset = (page - 1) * limit
        val raw = MusicHttp.get("$BASE/api/search/get/web?csrf_token=&s=$enc&type=1&offset=$offset&limit=$limit", REFERER, cookieHeader)
        val j = JSONObject(raw)
        val songs = j.optJSONObject("result")?.optJSONArray("songs") ?: JSONArray()
        val out = ArrayList<VideoItem>()
        for (i in 0 until songs.length()) {
            val s = songs.optJSONObject(i) ?: continue
            val id = s.optLong("id")
            if (id <= 0) continue
            val artists = s.optJSONArray("artists") ?: JSONArray()
            val artist = (0 until artists.length()).joinToString(" / ") { artists.optJSONObject(it)?.optString("name").orEmpty() }
            val album = s.optJSONObject("album")
            out.add(
                VideoItem(
                    id = "ne:$id",
                    title = s.optString("name"),
                    url = "$BASE/song?id=$id",
                    durationSec = (s.optLong("duration") / 1000).toInt(),
                    uploader = artist,
                    cover = coverUrl(album?.optLong("picId") ?: 0L),
                    album = album?.optString("name").orEmpty(),
                )
            )
        }
        return out
    }

    /**
     * 取流（weapi v1）：level = standard 128k / higher / exhigh 320k / lossless 无损 / hires
     * anon=true 时不带登录 cookie（地区限制导致 VIP CDN 403 时的降级通道）
     */
    fun stream(songId: Long, level: String = "exhigh", anon: Boolean = false): MusicStream? {
        val payload = JSONObject().apply {
            put("ids", "[$songId]")
            put("level", level)
            put("encodeType", "flac")
        }
        val raw = weapi("/weapi/song/enhance/player/url/v1", payload, if (anon) null else cookieHeader)
        val j = JSONObject(raw)
        val d = j.optJSONArray("data")?.optJSONObject(0) ?: return null
        val url = d.optString("url")
        if (url.isEmpty() || url == "null") return null
        return MusicStream(url, d.optString("type").ifEmpty { "mp3" }, d.optInt("br"))
    }

    /** 匿名取流（不带 cookie）：VIP 音源受地区限制时退回免费音质 */
    fun streamAnon(songId: Long): MusicStream? = stream(songId, "standard", anon = true)

    /** 按可用性依次尝试：无损 → 320k → 128k（受"音质上限"设置限制） */
    fun bestStream(songId: Long): MusicStream? {
        val levels = if (cookieHeader.isNullOrEmpty()) {
            listOf("standard")
        } else when (Settings.musicQuality) {
            Settings.QUALITY_HIGH -> listOf("exhigh", "standard")
            Settings.QUALITY_STANDARD -> listOf("standard")
            else -> listOf("lossless", "exhigh", "standard")
        }
        for (lv in levels) {
            val s = stream(songId, lv)
            if (s != null) return s
        }
        return null
    }
}

// ============ QQ 音乐 ============

object QQMusicApi {
    private const val CGI = "https://u.y.qq.com/cgi-bin/musicu.fcg"
    private const val REFERER = "https://y.qq.com/"
    private const val GUID = "10000"

    /** 登录 Cookie（uin / qm_keyst 等）；由 Repo 注入 */
    @Volatile var cookieHeader: String? = null

    /** 登录 uin（cookie 原值，可能带 o 前缀；0=未登录） */
    @Volatile var uin: String = "0"

    /** uin 的纯数字形式（部分场景服务端只认数字） */
    @Volatile var uinNumeric: String = ""

    /** 音乐密钥（qm_keyst / qqmusic_key），VIP 鉴权的关键参数，放进 comm.authst */
    @Volatile var musickey: String = ""

    /** 最近一次 vkey 请求的 result 码：0=成功，104003=该账号对此歌/此音质无权限 */
    @Volatile var lastResultCode: Int = 0

    fun coverUrl(albumMid: String): String =
        if (albumMid.isEmpty()) "" else "https://y.gtimg.cn/music/photo_new/T002R500x500M000$albumMid.jpg"

    fun search(query: String, page: Int, limit: Int = 20): List<VideoItem> {
        val body = JSONObject().apply {
            put("comm", JSONObject().apply { put("ct", "19"); put("cv", "1859"); put("uin", uin) })
            put("req", JSONObject().apply {
                put("module", "music.search.SearchCgiService")
                put("method", "DoSearchForQQMusicMobile")
                put("param", JSONObject().apply {
                    put("query", query)
                    put("num_per_page", limit.toString())
                    put("page_num", page.toString())
                    put("search_type", "0")
                })
            })
        }
        val raw = MusicHttp.postJson(CGI, REFERER, body.toString(), cookieHeader)
        val songs = JSONObject(raw).optJSONObject("req")?.optJSONObject("data")
            ?.optJSONObject("body")?.optJSONArray("item_song") ?: JSONArray()
        val out = ArrayList<VideoItem>()
        for (i in 0 until songs.length()) {
            val s = songs.optJSONObject(i) ?: continue
            val mid = s.optString("mid")
            if (mid.isEmpty()) continue
            val singers = s.optJSONArray("singer") ?: JSONArray()
            val artist = (0 until singers.length()).joinToString(" / ") { singers.optJSONObject(it)?.optString("name").orEmpty() }
            val album = s.optJSONObject("album")
            out.add(
                VideoItem(
                    id = "qq:$mid",
                    title = s.optString("name"),
                    url = "https://y.qq.com/n/ryqq/songDetail/$mid",
                    durationSec = s.optInt("interval", 0),
                    uploader = artist,
                    cover = coverUrl(album?.optString("mid").orEmpty()),
                    album = album?.optString("name").orEmpty(),
                )
            )
        }
        return out
    }

    /**
     * 取流：按档位尝试。免登录仅 128k；登录后（VIP）可 320k / FLAC
     * prefix: C400=m4a128 / M500=mp3128 / M800=mp3320 / F000=flac / A000=ape
     */
    private fun vkey(mid: String, filename: String, uinOverride: String? = null, anon: Boolean = false): String? {
        // 登录态：uin 用 cookie 原值（QQ 登录可能形如 o123456），密钥走 comm.authst；anon=true 时全部不带
        val effUin = if (anon) "0" else (uinOverride ?: uin).ifEmpty { "0" }
        val body = JSONObject().apply {
            put("comm", JSONObject().apply {
                put("uin", effUin)
                put("format", "json"); put("ct", 19); put("cv", 0)
                if (!anon && musickey.isNotEmpty()) put("authst", musickey)   // VIP 鉴权关键
            })
            put("req_0", JSONObject().apply {
                put("module", "vkey.GetVkeyServer")
                put("method", "CgiGetVkey")
                put("param", JSONObject().apply {
                    put("guid", GUID)
                    put("songmid", JSONArray().put(mid))
                    put("songtype", JSONArray().put(0))
                    put("uin", effUin)
                    put("loginflag", 1)
                    put("platform", "20")
                    put("filename", JSONArray().put(filename))
                })
            })
        }
        val query = "?-=getplaysongvkey&g_tk=5381&loginUin=" + URLEncoder.encode(effUin, "UTF-8") +
            "&hostUin=0&format=json&inCharset=utf8&outCharset=utf-8&notice=0&platform=yqq.json&needNewCode=0"
        val raw = MusicHttp.postJson(CGI + query, REFERER, body.toString(), if (anon) null else cookieHeader)
        val j = JSONObject(raw).optJSONObject("req_0") ?: return null
        val d = j.optJSONObject("data") ?: return null
        val sip = d.optJSONArray("sip")?.optString(0).orEmpty()
        val mu = d.optJSONArray("midurlinfo")?.optJSONObject(0) ?: return null
        if (!anon) lastResultCode = mu.optInt("result", 0)   // 仅记录登录态（用户相关）的结果码
        val purl = mu.optString("purl")
        if (purl.isEmpty() || purl == "null") return null
        val base = if (sip.isNotEmpty()) sip else "http://dl.stream.qqmusic.qq.com/"
        return base + purl
    }

    private val QUALITY_CHAIN = listOf(
        Triple("F000", "flac", 999000),
        Triple("M800", "mp3", 320000),
        Triple("M500", "mp3", 128000),
        Triple("C400", "m4a", 128000),
    )

    /**
     * 取流：逐档降级。先用 cookie 原值 uin 试一轮；若全档失败且存在纯数字 uin，
     * 再用数字形式重试一轮（QQ/微信登录的 uin 形式在不同场景被服务端接受度不同）
     */
    /** 按指定档位取流（播放器内即时切换音质）：tier = lossless / exhigh / standard */
    fun streamAt(mid: String, tier: String, anon: Boolean = false): MusicStream? {
        val (prefix, ext, br) = when (tier) {
            "lossless" -> Triple("F000", "flac", 999000)
            "exhigh" -> Triple("M800", "mp3", 320000)
            else -> Triple("M500", "mp3", 128000)
        }
        val filename = "$prefix$mid$mid.$ext"
        if (anon) return vkey(mid, filename, null, anon = true)?.let { MusicStream(it, ext, br) }
        vkey(mid, filename)?.let { return MusicStream(it, ext, br) }
        val alt = uinNumeric
        if (alt.isNotEmpty() && alt != uin) {
            vkey(mid, filename, alt)?.let { return MusicStream(it, ext, br) }
        }
        return null
    }

    /** 匿名取流（无 cookie、无 authst、uin=0） */
    fun streamAnon(mid: String): MusicStream? {
        for ((prefix, ext, br) in QUALITY_CHAIN) {
            vkey(mid, "$prefix$mid$mid.$ext", null, anon = true)?.let { return MusicStream(it, ext, br) }
        }
        return null
    }

    fun bestStream(mid: String): MusicStream? {
        val chain = when (Settings.musicQuality) {
            Settings.QUALITY_HIGH -> QUALITY_CHAIN.filter { it.second != "flac" }          // 320k 起
            Settings.QUALITY_STANDARD -> QUALITY_CHAIN.filter { it.first == "M500" || it.first == "C400" } // 仅 128k
            else -> QUALITY_CHAIN
        }
        for ((prefix, ext, br) in chain) {
            vkey(mid, "$prefix$mid$mid.$ext")?.let { return MusicStream(it, ext, br) }
        }
        val alt = uinNumeric
        if (alt.isNotEmpty() && alt != uin) {
            for ((prefix, ext, br) in QUALITY_CHAIN) {
                vkey(mid, "$prefix$mid$mid.$ext", alt)?.let { return MusicStream(it, ext, br) }
            }
        }
        return null
    }
}
