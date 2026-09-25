package com.hougelangley.hyperonlinevideo.data

import android.text.Html
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

/**
 * B 站原生 API（不走 yt-dlp）：
 * 手机侧对该平台的风控策略下，Java HTTP + buvid3 通道稳定，且搜索条目自带标题/封面
 */
object BiliApi {

    private const val UA =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"

    @Volatile private var buvid3: String? = null
    @Volatile private var sessdata: String? = null

    private fun httpGet(urlStr: String, needCookie: Boolean): String {
        val conn = URL(urlStr).openConnection() as HttpURLConnection
        return try {
            conn.setRequestProperty("User-Agent", UA)
            conn.setRequestProperty("Referer", "https://search.bilibili.com")
            if (needCookie) {
                if (buvid3 == null) {
                    buvid3 = try {
                        val spi = JSONObject(httpGet("https://api.bilibili.com/x/frontend/finger/spi", false))
                        spi.getJSONObject("data").getString("b_3")
                    } catch (e: Exception) { null }
                }
                if (sessdata == null) sessdata = readSessdata()
                val cookie = buildString {
                    buvid3?.let { append("buvid3=").append(it) }
                    sessdata?.let { if (isNotEmpty()) append("; "); append("SESSDATA=").append(it) }
                }
                if (cookie.isNotEmpty()) conn.setRequestProperty("Cookie", cookie)
            }
            conn.connectTimeout = 15000
            conn.readTimeout = 15000
            conn.inputStream.bufferedReader().use { it.readText() }
        } finally {
            conn.disconnect()
        }
    }

    /** 从登录 Cookie 文件读取 SESSDATA（Netscape 格式；登录后解锁更高清晰度 + 字幕 CDN ✓） */
    fun readSessdata(): String? = try {   // public：app 模块的字幕抓取也要用（跨模块 internal 不可见 ✗）
        val f = StorageRepo.cookieFile("bilibili")
        if (!f.exists()) null
        else f.readLines().firstNotNullOfOrNull { line ->
            val parts = line.split("\t")
            if (parts.size >= 7 && parts[5] == "SESSDATA") parts[6] else null
        }
    } catch (e: Exception) { null }

    /** 搜索视频（分页 + 排序/时长过滤） */
    fun search(query: String, page: Int, order: String, duration: Int): List<VideoItem> {
        val enc = URLEncoder.encode(query, "UTF-8")
        val raw = httpGet(
            "https://api.bilibili.com/x/web-interface/search/type?search_type=video" +
                "&keyword=$enc&page=$page&page_size=$SEARCH_PAGE_SIZE" +
                "&order=$order&duration=$duration",
            true
        )
        val resp = JSONObject(raw)
        if (resp.optInt("code") != 0) throw Exception("bilibili code=${resp.optInt("code")}")
        val result = resp.getJSONObject("data").optJSONArray("result") ?: JSONArray()
        val out = ArrayList<VideoItem>()
        for (i in 0 until result.length()) {
            val r = result.getJSONObject(i)
            val title = Html.fromHtml(r.optString("title"), Html.FROM_HTML_MODE_LEGACY).toString()
            val parts = r.optString("duration").split(":").mapNotNull { it.toIntOrNull() }
            var secs = 0; for (n in parts) secs = secs * 60 + n
            out.add(
                VideoItem(
                    id = r.optString("bvid"),
                    title = title,
                    url = "https://www.bilibili.com/video/" + r.optString("bvid"),
                    durationSec = secs,
                    uploader = r.optString("author"),
                    cover = "https:" + r.optString("pic"),
                )
            )
        }
        return out
    }

    /** 解析单视频为直链（html5 mp4；qn 可指定清晰度，登录 Cookie 解锁更高） */
    /** 按"视频清晰度上限"给出推荐 qn（仅降不升；0=自动 → 80=1080P，与既有默认行为一致 ✓）
     *  注意：仅作用于**默认解析**；播放器内手动切清晰度（显式传 qn）不受限 ✓ 与桌面 V 键语义一致 ✓ */
    private fun cappedQn(): Int {
        val cap = Settings.videoMaxHeight
        return when {
            cap <= 0 -> 80          // 自动（平台默认 ✓）
            cap >= 1080 -> 80       // 1080/1440/2160 都取 B站最高（1080P；4K 需更高权益，现状不支持）
            cap >= 720 -> 64
            cap >= 480 -> 32
            else -> 16              // 360 及以下
        }
    }

    fun resolve(url: String, qn: Int = cappedQn()): VideoDetail {
        val bvid = Regex("BV[0-9A-Za-z]+").find(url)?.value
            ?: throw Exception("无法提取 BV 号")
        val view = JSONObject(httpGet("https://api.bilibili.com/x/web-interface/view?bvid=$bvid", true))
        if (view.optInt("code") != 0) throw Exception("view code=${view.optInt("code")}")
        val v = view.getJSONObject("data")
        val cid = v.getLong("cid")
        val play = JSONObject(httpGet(
            "https://api.bilibili.com/x/player/playurl?bvid=$bvid&cid=$cid&qn=$qn&fnval=1&fourk=1&platform=html5&high_quality=1",
            true
        ))
        if (play.optInt("code") != 0) throw Exception("playurl code=${play.optInt("code")}")
        val p = play.getJSONObject("data")
        val curQ = p.optInt("quality")
        val descs = p.optJSONArray("accept_description") ?: JSONArray()
        val quals = p.optJSONArray("accept_quality") ?: JSONArray()
        fun labelOf(q: Int): String = (0 until quals.length()).firstOrNull { quals.optInt(it) == q }
            ?.let { descs.optString(it) } ?: "默认"
        val qLabel = labelOf(curQ)
        // 平台可用清晰度（B站需按 qn 重新解析取流）
        fun resLabel(desc: String): String =
            Regex("(\\d{3,4}[Pp])").find(desc)?.value?.uppercase() ?: desc.ifEmpty { "默认" }
        val qualities = (0 until quals.length()).mapNotNull { i ->
            val q = quals.optInt(i)
            if (q <= 0) null else QualityOption(
                id = q.toString(),
                label = resLabel(descs.optString(i)),
                url = "",
                qn = q,
            )
        }.distinctBy { it.id }
        val durls = p.optJSONArray("durl") ?: JSONArray()
        val formats = ArrayList<MediaFormat>()
        for (i in 0 until durls.length()) {
            val d = durls.getJSONObject(i)
            formats.add(MediaFormat(d.getString("url"), qLabel, "mp4", d.optLong("size", 0)))
        }
        // 在线 CC 字幕列表（M14b；需登录态，无字幕/未登录时为空）
        val subTracks = ArrayList<SubTrack>()
        runCatching {
            val aid = v.optLong("aid")
            val pv = JSONObject(httpGet("https://api.bilibili.com/x/player/v2?aid=$aid&cid=$cid", true))
            val subs = pv.optJSONObject("data")?.optJSONObject("subtitle")?.optJSONArray("subtitles")
            for (i in 0 until (subs?.length() ?: 0)) {
                val s = subs!!.optJSONObject(i) ?: continue
                var u = s.optString("subtitle_url")
                if (u.isEmpty()) continue
                if (u.startsWith("//")) u = "https:$u"
                val doc = s.optString("lan_doc").ifEmpty { s.optString("lan") }
                val isAuto = s.optInt("type", 0) == 1
                // 标注回归 lan_doc 原文（"中文"）：旧实现加"（自动翻译）"后缀 ✗ → 语言匹配 rank=9
                // → 自动选轨静默失效（用户实测 2026-09-25 ✗ 见 Subtitles.languageRank ✓）
                subTracks.add(SubTrack(doc, u, s.optString("lan"), isAuto))
            }
            android.util.Log.i("HOV", "B站字幕轨: ${subTracks.size} 条")
        }
        return VideoDetail(
            title = v.optString("title"),
            uploader = v.optJSONObject("owner")?.optString("name") ?: "",
            durationSec = v.optDouble("duration", 0.0),
            cover = v.optString("pic"),
            webpageUrl = url,
            formats = formats,
            audioUrl = "",
            qualities = qualities,
            currentQualityId = curQ.toString(),
            subtitleTracks = subTracks,
        )
    }
}
