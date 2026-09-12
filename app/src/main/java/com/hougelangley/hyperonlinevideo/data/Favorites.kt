package com.hougelangley.hyperonlinevideo.data

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * 收藏（音乐 / 视频通用）
 *
 * 落盘为 filesDir/favorites.json（无需引入数据库依赖），键 = VideoItem.id。
 * 收藏列表本身也是一条可播放队列（进播放器后可"下一首/自动连播"）。
 */
object Favorites {

    data class Fav(
        val id: String,
        val title: String,
        val uploader: String,
        val cover: String,
        val album: String,
        val durationSec: Int,
        val platform: String,
        val addedAt: Long,
        val url: String = "",
    ) {
        fun toItem(): VideoItem = VideoItem(
            id = id, title = title, url = url,
            durationSec = durationSec, uploader = uploader, cover = cover, album = album,
        )
    }

    private val _items = MutableStateFlow<List<Fav>>(emptyList())
    val items: StateFlow<List<Fav>> = _items

    @Volatile private var appContext: Context? = null

    fun init(ctx: Context) {
        appContext = ctx.applicationContext
        load()
    }

    fun isFav(id: String): Boolean = _items.value.any { it.id == id }

    /** 收藏/取消收藏；返回是否已收藏 */
    fun toggle(item: VideoItem, platform: String): Boolean {
        val exists = isFav(item.id)
        _items.value = if (exists) {
            _items.value.filterNot { it.id == item.id }
        } else {
            listOf(
                Fav(
                    id = item.id, title = item.title, uploader = item.uploader,
                    cover = item.cover, album = item.album, durationSec = item.durationSec,
                    platform = platform, addedAt = System.currentTimeMillis(), url = item.url,
                )
            ) + _items.value
        }
        persist()
        return !exists
    }

    fun remove(id: String) {
        _items.value = _items.value.filterNot { it.id == id }
        persist()
    }

    fun clear() {
        _items.value = emptyList()
        persist()
    }

    // ---------- 持久化 ----------

    private fun file(): File = File(appContext?.filesDir, "favorites.json")

    private fun load() {
        try {
            val f = file()
            if (!f.exists()) return
            val arr = JSONArray(f.readText())
            val out = ArrayList<Fav>(arr.length())
            for (i in 0 until arr.length()) {
                val o = arr.optJSONObject(i) ?: continue
                out.add(
                    Fav(
                        id = o.optString("id"),
                        title = o.optString("title"),
                        uploader = o.optString("uploader"),
                        cover = o.optString("cover"),
                        album = o.optString("album"),
                        durationSec = o.optInt("durationSec"),
                        platform = o.optString("platform"),
                        addedAt = o.optLong("addedAt"),
                        url = o.optString("url"),
                    )
                )
            }
            _items.value = out
        } catch (e: Exception) {
            android.util.Log.w("HOV", "收藏读取失败: ${e.message}")
        }
    }

    private fun persist() {
        try {
            val arr = JSONArray()
            _items.value.forEach { f ->
                arr.put(
                    JSONObject().apply {
                        put("id", f.id); put("title", f.title); put("uploader", f.uploader)
                        put("cover", f.cover); put("album", f.album); put("durationSec", f.durationSec)
                        put("platform", f.platform); put("addedAt", f.addedAt); put("url", f.url)
                    }
                )
            }
            file().writeText(arr.toString())
        } catch (e: Exception) {
            android.util.Log.w("HOV", "收藏写入失败: ${e.message}")
        }
    }
}
