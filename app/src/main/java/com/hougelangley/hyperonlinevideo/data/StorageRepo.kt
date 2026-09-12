package com.hougelangley.hyperonlinevideo.data

import android.content.Context
import android.os.Environment
import java.io.File

/**
 * 本地存储职责（B2 拆分第一步）：下载目录、本地库列表、重命名、删除、容量上限、缓存清理。
 *
 * 拆出来的意义：Repo 原本 891 行，下载/搜索/解析/存储混在一起；
 * 这里只保留"碰文件系统"的逻辑，便于单测与复用。
 * Repo 保留同名薄委托，所有调用点无需改动。
 */
object StorageRepo {

    private const val DOWNLOADS_CAP = 2L * 1024 * 1024 * 1024   // 2GB LRU

    @Volatile private var appContext: Context? = null

    fun init(ctx: Context) {
        appContext = ctx.applicationContext
    }

    fun downloadsDir(): File =
        File(appContext!!.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), "videos")

    /** 超过 2GB 时按最旧优先删除（LRU） */
    fun enforceCap(dir: File) {
        val files = dir.listFiles()?.filter { it.isFile }?.sortedBy { it.lastModified() } ?: return
        var total = files.sumOf { it.length() }
        for (f in files) {
            if (total <= DOWNLOADS_CAP) break
            total -= f.length()
            f.delete()
        }
    }

    fun listDownloads(sort: Repo.LocalSort = Repo.LocalSort.RECENT, query: String = ""): List<LocalFile> {
        val all = downloadsDir().listFiles()
            ?.filter { it.isFile && isMediaFile(it) }
            ?.map { LocalFile(it.name, it.absolutePath, it.length(), it.lastModified()) }
            ?: emptyList()
        val filtered = if (query.isBlank()) all else {
            val q = query.trim().lowercase()
            all.filter { it.name.lowercase().contains(q) || it.artist.lowercase().contains(q) }
        }
        return when (sort) {
            Repo.LocalSort.RECENT -> filtered.sortedByDescending { it.mtime }
            Repo.LocalSort.NAME -> filtered.sortedBy { it.name.lowercase() }
            Repo.LocalSort.SIZE -> filtered.sortedByDescending { it.sizeBytes }
            Repo.LocalSort.TYPE -> filtered.sortedWith(
                compareBy({ if (it.isAudio) 1 else 0 }, { -it.mtime })
            )
        }
    }

    /**
     * 重命名本地文件（M16）：保留扩展名，同名 .srt/.ass 外挂字幕联动重命名。
     * 返回新路径；失败返回 null。
     */
    fun renameDownload(path: String, newBaseName: String): String? {
        val f = File(path)
        if (f.parentFile?.absolutePath != downloadsDir().absolutePath) return null
        val safe = newBaseName.trim()
            .replace(Regex("[\\\\/:*?\"<>|]"), "_")     // 文件名非法字符
            .trim('.', ' ')
        if (safe.isEmpty()) return null
        val ext = f.extension
        val target = File(f.parentFile, if (ext.isEmpty()) safe else "$safe.$ext")
        if (target.absolutePath == f.absolutePath) return f.absolutePath
        if (target.exists()) return null                                   // 不覆盖已有文件
        if (!f.renameTo(target)) return null
        // 外挂字幕联动（同名字幕跟着改，否则改名后字幕就找不到了）
        val oldBase = f.absolutePath.substringBeforeLast('.')
        val newBase = target.absolutePath.substringBeforeLast('.')
        for (e in listOf("srt", "ass", "ssa", "vtt")) {
            val s = File("$oldBase.$e")
            if (s.exists()) runCatching { s.renameTo(File("$newBase.$e")) }
        }
        android.util.Log.i("HOV", "重命名: ${f.name} → ${target.name}")
        return target.absolutePath
    }

    /** 媒体文件判定：本地库只列音视频，避免字幕(.srt)、封面(.jpg)等混进来 */
    private fun isMediaFile(f: File): Boolean = f.extension.lowercase() in MEDIA_EXTENSIONS

    private val MEDIA_EXTENSIONS = setOf(
        "mp4", "mkv", "webm", "mov", "avi", "ts", "m4v", "flv", "wmv",
        "mp3", "flac", "m4a", "aac", "wav", "ogg", "opus", "ape", "wma",
    )

    fun deleteDownload(path: String): Boolean {
        val f = File(path)
        if (f.parentFile?.absolutePath != downloadsDir().absolutePath) return false
        return f.delete()
    }

    fun storageStats(): Triple<Int, Long, Long> { // (文件数, 下载字节, 缓存字节)
        val files = downloadsDir().listFiles()?.filter { it.isFile } ?: emptyList()
        fun size(f: File?): Long = f?.walkTopDown()?.filter { it.isFile }?.sumOf { it.length() } ?: 0L
        return Triple(files.size, files.sumOf { it.length() }, size(appContext?.cacheDir))
    }

    fun clearCache() {
        appContext?.cacheDir?.deleteRecursively()
        appContext?.codeCacheDir?.deleteRecursively()
    }

    fun clearDownloads() {
        downloadsDir().deleteRecursively()
        downloadsDir().mkdirs()
    }
}
