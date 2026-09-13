package com.hougelangley.hyperonlinevideo.data

import android.util.Base64
import org.json.JSONObject

/**
 * 歌词（P3）：网易云 / QQ音乐
 * - 网易云：GET /api/song/lyric（无需加密），lrc + tlyric（译文）
 * - QQ音乐：musicu.fcg `music.musichallSong.PlayLyricInfo/GetPlayLyricInfo`，lyric 字段为 base64 的 LRC
 * 时间轴支持 [mm:ss.xx] 与 [mm:ss:xx]（部分曲库用冒号分隔）
 */
object Lyrics {

    data class Line(val timeMs: Long, val text: String)

    private val TIME_RE = Regex("""\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?]""")

    /** 拉取歌词；失败或无歌词返回空列表（不抛异常） */
    suspend fun fetch(item: VideoItem, platform: String): List<Line> = try {
        when (platform) {
            "netease" -> netease(item.id.removePrefix("ne:"))
            "qqmusic" -> qq(item.id.removePrefix("qq:"))
            else -> emptyList()
        }
    } catch (e: Exception) {
        android.util.Log.w("HOV", "歌词拉取失败[${platform}]: ${e.message}")
        emptyList()
    }

    private fun netease(songId: String): List<Line> {
        val raw = MusicHttp.get(
            "https://music.163.com/api/song/lyric?id=$songId&lv=-1&kv=-1&tv=-1",
            "https://music.163.com/",
            NeteaseApi.cookieHeader,
        )
        val j = JSONObject(raw)
        val origin = parseLrc(j.optJSONObject("lrc")?.optString("lyric").orEmpty())
        if (origin.isEmpty()) return emptyList()
        val trans = parseLrc(j.optJSONObject("tlyric")?.optString("lyric").orEmpty())
        return mergeTranslation(origin, trans)
    }

    private fun qq(songMid: String): List<Line> {
        val body = JSONObject().apply {
            put("comm", JSONObject().apply {
                put("uin", QQMusicApi.uin)
                put("format", "json")
                put("ct", 24)
                put("cv", 0)
                if (QQMusicApi.musickey.isNotEmpty()) put("authst", QQMusicApi.musickey)
            })
            put("req", JSONObject().apply {
                put("module", "music.musichallSong.PlayLyricInfo")
                put("method", "GetPlayLyricInfo")
                put("param", JSONObject().apply {
                    put("songMID", songMid)
                    put("format", "json")
                })
            })
        }
        val raw = MusicHttp.postJson("https://u.y.qq.com/cgi-bin/musicu.fcg", "https://y.qq.com/", body.toString(), QQMusicApi.cookieHeader)
        val data = JSONObject(raw).optJSONObject("req")?.optJSONObject("data") ?: return emptyList()
        val b64 = data.optString("lyric")
        if (b64.isEmpty()) return emptyList()
        val lrc = try {
            String(Base64.decode(b64, Base64.DEFAULT), Charsets.UTF_8)
        } catch (e: Exception) {
            b64
        }
        val origin = parseLrc(lrc)
        val trans = parseLrc(try {
            String(Base64.decode(data.optString("trans"), Base64.DEFAULT), Charsets.UTF_8)
        } catch (e: Exception) {
            ""
        })
        return mergeTranslation(origin, trans)
    }

    /** 解析 LRC 文本（容错：忽略无时间戳/元信息行） */
    fun parseLrc(text: String): List<Line> {
        if (text.isBlank()) return emptyList()
        val out = ArrayList<Line>()
        for (raw in text.split('\n')) {
            val m = TIME_RE.find(raw) ?: continue
            val min = m.groupValues[1].toLongOrNull() ?: continue
            val sec = m.groupValues[2].toLongOrNull() ?: continue
            val fracStr = m.groupValues[3]
            val frac = when (fracStr.length) {
                0 -> 0L
                1 -> fracStr.toLong() * 100
                2 -> fracStr.toLong() * 10
                else -> fracStr.take(3).toLong()
            }
            val content = raw.substring(m.range.last + 1).trim()
            if (content.isEmpty()) continue
            out.add(Line(min * 60_000 + sec * 1000 + frac, content))
        }
        return out.sortedBy { it.timeMs }
    }

    /** 把译文按时间对齐合并：同一时间点显示"原文\n译文" */
    private fun mergeTranslation(origin: List<Line>, trans: List<Line>): List<Line> {
        if (trans.isEmpty()) return origin
        val map = trans.associate { it.timeMs to it.text }
        return origin.map { line ->
            val t = map[line.timeMs]
            if (t.isNullOrBlank() || t == line.text) line else Line(line.timeMs, line.text + "\n" + t)
        }
    }
}
