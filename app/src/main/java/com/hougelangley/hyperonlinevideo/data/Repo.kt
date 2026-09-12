package com.hougelangley.hyperonlinevideo.data

import android.content.Context
import android.os.Environment
import android.util.Log
import com.yausername.ffmpeg.FFmpeg
import com.yausername.youtubedl_android.YoutubeDL
import com.yausername.youtubedl_android.YoutubeDLRequest
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.ConcurrentHashMap

/**
 * 应用数据仓库：yt-dlp 生命周期 / 搜索 / 解析 / 下载 / Cookie / 存储管理
 * （由已验证的 Godot 插件逻辑迁移重构）
 */
object Repo {

    private const val DOWNLOADS_CAP = 2L * 1024 * 1024 * 1024 // 2GB LRU 上限

    private lateinit var appContext: Context
    @Volatile private var ytdlpReady = false

    private val _ytdlpStatus = MutableStateFlow("正在初始化...")
    val ytdlpStatus: StateFlow<String> = _ytdlpStatus

    private val _downloads = MutableStateFlow<List<DownloadTask>>(emptyList())
    val downloads: StateFlow<List<DownloadTask>> = _downloads

    private val processIds = ConcurrentHashMap<String, String>() // taskId -> yt-dlp processId

    // ---- 下载队列（M12）：并发上限 + 排队 + 重试 ----
    private const val MAX_CONCURRENT_DOWNLOADS = 2
    private val queueLock = Any()
    private val pendingQueue = java.util.concurrent.ConcurrentLinkedQueue<DownloadRequest>()
    private val requests = ConcurrentHashMap<String, DownloadRequest>()   // 供重试
    private var runningCount = 0

    // ---------- 生命周期 ----------

    suspend fun init(context: Context) = withContext(Dispatchers.IO) {
        appContext = context.applicationContext
        PlaybackHistory.init(appContext)
        NetRoute.init(appContext)   // 音乐平台走直连网络（不随系统 VPN）
        Settings.init(appContext)
        StorageRepo.init(appContext)
        Favorites.init(appContext)
        if (ytdlpReady) return@withContext
        try {
            YoutubeDL.getInstance().init(appContext)
            FFmpeg.getInstance().init(appContext)
            // YouTube 反爬频繁，启动即检查 yt-dlp 更新（已最新时秒回，不阻塞）
            try {
                val st = YoutubeDL.getInstance().updateYoutubeDL(appContext, YoutubeDL.UpdateChannel.STABLE)
                Log.i("HOV", "yt-dlp update: $st version=${YoutubeDL.getInstance().version(appContext)}")
            } catch (e: Exception) {
                Log.w("HOV", "yt-dlp update skipped: ${e.message}")
            }
            ytdlpReady = true
            val ver = runCatching { YoutubeDL.getInstance().version(appContext) }.getOrNull()
            _ytdlpStatus.value = if (ver.isNullOrEmpty()) "就绪" else "$ver 就绪"
        } catch (t: Throwable) {
            // 注意：必须是 Throwable —— 原生库初始化失败常抛 ExceptionInInitializerError / UnsatisfiedLinkError（Error 系），
            // 只 catch Exception 会让异常逃逸到协程外，直接把整个 App 崩掉（Release+R8 实测踩到过）
            Log.e("HOV", "yt-dlp 初始化失败: ${t.message}")
            _ytdlpStatus.value = "初始化失败: ${t.message?.take(60)}"
        }
    }

    val isReady: Boolean get() = ytdlpReady

    /** 下载目录容量上限（字节，供 UI 显示） */
    val downloadsCapBytes: Long get() = DOWNLOADS_CAP

    // ---------- Cookie ----------

    fun cookieFile(platform: String) = File(appContext.filesDir, "cookies/$platform.txt")

    fun hasCookies(platform: String) = cookieFile(platform).exists()

    private fun applyCookies(req: YoutubeDLRequest, platform: String) {
        val f = cookieFile(platform)
        if (f.exists()) req.addOption("--cookies", f.absolutePath)
    }

    /**
     * 音源预检：网易云/QQ音乐的 CDN 会对 VIP/版权内容做地域校验（海外 IP → 403），
     * 提前探测可把「静默无声」变成明确提示。返回 HTTP 状态码，-1 表示探测失败（网络异常等）
     */
    private fun probeStream(url: String, referer: String): Int {
        return try {
            val conn = NetRoute.open(url, 10000)   // 直连网络
            try {
                conn.setRequestProperty("User-Agent", MusicHttp.UA)
                conn.setRequestProperty("Referer", referer)
                conn.setRequestProperty("Range", "bytes=0-1")
                conn.responseCode
            } finally {
                conn.disconnect()
            }
        } catch (e: Exception) {
            -1
        }
    }

    /** 把 CDN 拒绝翻译成用户能看懂的提示 */
    private fun streamRejectMessage(platform: String, code: Int): String = when {
        code == 403 -> "音源被平台拒绝(403)：${if (platform == "netease") "网易云" else "QQ音乐"}的 VIP/版权内容" +
            "可能因 VPN 或海外 IP 被限制，请关闭 VPN（或设为分应用代理）后重试"
        code == 404 -> "音源已失效(404)，请重新搜索后再试"
        code >= 400 -> "音源不可用(HTTP $code)，请稍后重试"
        else -> "音源不可用（预检失败），请检查网络后重试"
    }

    /** 音源为空的提示：区分"没登录/受限"与"账号无权限（VIP/数字专辑）" */
    private fun musicUnavailableMessage(platform: String): String {
        if (platform == "qqmusic" && QQMusicApi.lastResultCode == 104003) {
            return "该 QQ 账号无此歌曲权限（VIP 或数字专辑），或会员在微信账号上（QQ 与微信账号权益不互通）"
        }
        return "无可用音源（可能需要登录或该曲目受限）"
    }

    /** 读取登录 Cookie 文件为键值对（Netscape 格式） */
    private fun cookiePairs(platform: String): Map<String, String> {
        val f = cookieFile(platform)
        if (!f.exists()) return emptyMap()
        val out = mutableMapOf<String, String>()
        f.readLines().forEach { line ->
            if (line.startsWith("#") || line.isBlank()) return@forEach
            val p = line.split("\t")
            if (p.size >= 7) out[p[5]] = p[6]
        }
        return out
    }

    /** 把登录 Cookie 注入音乐 API（每次搜索/解析前调用，登录后即时生效） */
    /** 当前是否 WiFi / 以太网（"仅 WiFi 下载"开关判断用） */
    private fun isOnWifiOrEthernet(): Boolean {
        return try {
            val cm = appContext.getSystemService(android.content.Context.CONNECTIVITY_SERVICE)
                as android.net.ConnectivityManager
            cm.allNetworks.any { n ->
                val cap = cm.getNetworkCapabilities(n) ?: return@any false
                cap.hasCapability(android.net.NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
                    !cap.hasTransport(android.net.NetworkCapabilities.TRANSPORT_VPN) &&
                    (cap.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI) ||
                        cap.hasTransport(android.net.NetworkCapabilities.TRANSPORT_ETHERNET))
            }
        } catch (e: Exception) {
            true
        }
    }

    /** 直连网络状态（诊断用） */
    private fun logRoute(tag: String) {
        val net = NetRoute.directNetwork()
        android.util.Log.i("HOV", "音乐路由[$tag] vpn=${NetRoute.vpnActive()} 直连网络=${net?.toString() ?: "无（将走系统默认）"}")
    }

    private fun syncMusicCookies() {
        // 注意：必须带上【完整】cookie 集（MUSIC_U/NMTID/JSESSIONID-WYYY/__csrf/_ntes_nuid 等），
        // 只发 MUSIC_U 时网易云生成的播放 URL 会被 CDN 判为无效签名（403）
        val ne = cookiePairs("netease")
        NeteaseApi.cookieHeader = if (ne.isEmpty()) null else ne.entries.joinToString("; ") { "${it.key}=${it.value}" }
        val qq = cookiePairs("qqmusic")
        QQMusicApi.cookieHeader = if (qq.isEmpty()) null else qq.entries.joinToString("; ") { "${it.key}=${it.value}" }
        // QQ 登录 → uin；微信登录 → wxuin / web_uin；luin 为数字 uin。原值交给 API（服务端对形式敏感）
        val rawUin = (qq["uin"] ?: qq["wxuin"] ?: qq["luin"] ?: qq["web_uin"] ?: "").trim()
        QQMusicApi.uin = rawUin.ifEmpty { "0" }
        QQMusicApi.uinNumeric = rawUin.filter { it.isDigit() }
        // 密钥：QQ 登录写 qm_keyst，微信登录写 qqmusic_key（两者等价）
        val rawKey = (qq["qm_keyst"] ?: qq["qqmusic_key"] ?: "").trim()
        QQMusicApi.musickey = if (rawKey.contains('%')) {
            try { java.net.URLDecoder.decode(rawKey, "UTF-8") } catch (e: Exception) { rawKey }
        } else rawKey
    }

    /** 音乐平台判断 */
    private fun isMusicPlatform(platform: String) = platform == "netease" || platform == "qqmusic"

    private fun applyBiliHeaders(req: YoutubeDLRequest) {
        req.addOption("--add-header", "Referer: https://www.bilibili.com")
        req.addOption("--add-header", "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36")
    }

    // ---------- 搜索（分页 + 过滤器） ----------

    /** YouTube 搜索 URL 的 sp 参数（protobuf 手工编码：排序/时长） */
    internal fun buildSp(filters: SearchFilters): String {
        val buf = java.io.ByteArrayOutputStream()
        // field1 sort: 2=最新 3=播放最多（1=相关度时为默认，不编码）
        if (filters.sort == 2) { buf.write(0x08); buf.write(0x02) }
        if (filters.sort == 3) { buf.write(0x08); buf.write(0x03) }
        // field2(type=video)+field3(duration): 12 02 18 XX
        if (filters.duration in 1..3) {
            buf.write(0x12); buf.write(0x02); buf.write(0x18); buf.write(filters.duration)
        }
        if (buf.size() == 0) return ""
        return android.util.Base64.encodeToString(
            buf.toByteArray(), android.util.Base64.URL_SAFE or android.util.Base64.NO_PADDING
        )
    }

    suspend fun search(
        platform: String,
        query: String,
        page: Int,
        filters: SearchFilters = SearchFilters(),
    ): List<VideoItem> = withContext(Dispatchers.IO) {
        if (isMusicPlatform(platform)) {
            syncMusicCookies()
            logRoute("搜索")
            return@withContext if (platform == "netease") {
                NeteaseApi.search(query, page)
            } else {
                QQMusicApi.search(query, page)
            }
        }
        if (platform == "bilibili") {
            val order = when (filters.sort) { 2 -> "pubdate"; 3 -> "click"; else -> "totalrank" }
            // B站 API：1=<10min 2=10-30min 3=30-60min 4=>60min；UI"长篇"取 4（>60min）
            val durParam = if (filters.duration == 3) 4 else filters.duration
            return@withContext BiliApi.search(query, page, order, durParam)
        }
        // YouTube：搜索 URL + sp 过滤 + playlist-start/end 真分页
        val enc = java.net.URLEncoder.encode(query, "UTF-8")
        val sp = buildSp(filters)
        val url = "https://www.youtube.com/results?search_query=$enc" + if (sp.isEmpty()) "" else "&sp=$sp"
        val req = YoutubeDLRequest(url)
        req.addOption("--dump-single-json")
        req.addOption("--flat-playlist")
        req.addOption("--no-warnings")
        req.addOption("--playlist-start", ((page - 1) * SEARCH_PAGE_SIZE + 1).toString())
        req.addOption("--playlist-end", (page * SEARCH_PAGE_SIZE).toString())
        applyCookies(req, "youtube")
        val resp = YoutubeDL.getInstance().execute(req, "search-${System.currentTimeMillis()}")
        if (resp.exitCode != 0) throw Exception(resp.err.take(300))
        val data = JSONObject(resp.out)
        val entries = data.optJSONArray("entries") ?: JSONArray()
        val out = ArrayList<VideoItem>()
        for (i in 0 until entries.length()) {
            val e = entries.optJSONObject(i) ?: continue
            val id = e.optString("id")
            if (id.isEmpty()) continue
            out.add(
                VideoItem(
                    id = id,
                    title = e.optString("title"),
                    url = "https://www.youtube.com/watch?v=$id",
                    durationSec = e.optDouble("duration", 0.0).toInt(),
                    uploader = e.optString("uploader"),
                    cover = "https://i.ytimg.com/vi/$id/hqdefault.jpg",
                )
            )
        }
        out
    }

    // ---------- 解析 ----------

    suspend fun resolve(url: String, platform: String): VideoDetail = withContext(Dispatchers.IO) {
        if (platform == "bilibili") return@withContext BiliApi.resolve(url)

        val req = YoutubeDLRequest(url)
        req.addOption("--dump-single-json")
        req.addOption("--no-warnings")
        applyCookies(req, "youtube")
        val resp = YoutubeDL.getInstance().execute(req, "resolve-${System.currentTimeMillis()}")
        if (resp.exitCode != 0) throw Exception(resp.err.take(300))
        val j = JSONObject(resp.out)

        // ---- 选流策略（2026 YouTube 无合流格式）----
        // 播放：HLS 媒体列表精确配对（视频变体 + EXT-X-MEDIA 音频轨，ffmpeg 秒开）
        // 音频轨 234=原声 优先，233=自动配音(如 uk) 兜底；无 HLS 时退 DASH 对
        fun norm(sv: String) = if (sv.isEmpty() || sv == "null") "none" else sv
        data class Cand(val mf: MediaFormat, val fid: String, val proto: String, val height: Int, val tbr: Double, val abr: Double, val vcodec: String, val acodec: String, val ext: String, val lang: String, val fps: Int)
        val cands = ArrayList<Cand>()
        val arr = j.optJSONArray("formats") ?: JSONArray()
        for (i in 0 until arr.length()) {
            val f = arr.optJSONObject(i) ?: continue
            val u = f.optString("url"); if (u.isEmpty()) continue
            val vcodec = norm(f.optString("vcodec"))
            val acodec = norm(f.optString("acodec"))
            if (vcodec == "none" && acodec == "none" && !f.optString("protocol").startsWith("m3u8")) continue
            cands.add(
                Cand(
                    mf = MediaFormat(u, f.optString("format_note").ifEmpty { f.optString("resolution") }, f.optString("ext"), 0),
                    fid = f.optString("format_id"),
                    proto = f.optString("protocol"),
                    height = f.optInt("height", 0),
                    tbr = f.optDouble("tbr", 0.0),
                    abr = f.optDouble("abr", 0.0),
                    vcodec = vcodec, acodec = acodec, ext = f.optString("ext"),
                    lang = f.optString("language"),
                    fps = f.optInt("fps", 0),
                )
            )
        }
        val isHls = { c: Cand -> c.proto.startsWith("m3u8") }
        // ---- 音频轨排序（避开自动配音）----
        // 依据：format_note 含 "original"/"(default)" 加分；与视频语言一致加分；
        //       含 "dubbed" 减分；DRC(动态压缩)变体减分
        val videoLang = j.optString("language", "")
        fun audioRank(c: Cand): Int {
            val note = c.mf.label.lowercase()
            var r = 0
            if (note.contains("original") || note.contains("(default)")) r += 100
            if (videoLang.isNotEmpty() && c.lang.equals(videoLang, ignoreCase = true)) r += 50
            if (!note.contains("dubbed")) r += 25
            if (!note.contains("drc")) r += 5
            if (c.mf.url.contains("dubbed")) r -= 200   // URL 级保险：配音轨直接判负
            return r
        }
        val audioCmp = compareBy<Cand> { audioRank(it) }
            .thenBy { if (it.ext == "m4a" || it.ext == "mp4") 1 else 0 }
            .thenBy { it.abr }
        fun vRank(v: String) = when {
            v.startsWith("avc1") || v.startsWith("h264") -> 3
            v.startsWith("vp9") || v.startsWith("vp09") -> 2
            v.startsWith("av01") -> 1
            else -> 0
        }
        // 视频候选：优先 HLS 视频变体（vcodec 有、acodec 空）
        val hlsVideos = cands.filter { isHls(it) && it.vcodec != "none" && it.acodec == "none" }
        val dashVideos = cands.filter { !isHls(it) && it.vcodec != "none" && it.acodec == "none" }
        // 音频候选
        val hlsAudios = cands.filter { isHls(it) && it.vcodec == "none" }          // 233/234
        val dashAudios = cands.filter { !isHls(it) && it.acodec != "none" && it.vcodec == "none" }

        // 清晰度选项（同分辨率去重，取编码/码率最优者）
        fun buildQualities(pool: List<Cand>): List<QualityOption> =
            pool.filter { it.height > 0 }
                .groupBy { it.height }
                .map { (_, list) ->
                    list.maxWithOrNull(compareBy<Cand> { vRank(it.vcodec) }.thenBy { it.tbr })!!
                }
                .sortedByDescending { it.height }
                .map {
                    QualityOption(
                        id = it.fid,
                        label = "${it.height}p" + if (it.fps >= 50) "60" else "",
                        url = it.mf.url,
                        qn = -1,
                    )
                }

        val formats = ArrayList<MediaFormat>()
        var audioUrl = ""
        val hlsInCap = hlsVideos.filter { it.height in 1..1080 }
        val hlsPool = if (hlsInCap.isNotEmpty()) hlsInCap else hlsVideos
        val hlsVideoPick = hlsPool.maxWithOrNull(compareBy<Cand> { it.height }.thenBy { vRank(it.vcodec) }.thenBy { it.tbr })
        val qualities = if (hlsVideos.isNotEmpty()) buildQualities(hlsVideos) else buildQualities(dashVideos)
        if (hlsVideoPick != null) {
            formats.add(hlsVideoPick.mf)
            // 音频轨优先 DASH（url 无逗号，列表选项安全）；无则退 HLS 音频（234 原声优先）
            val dashAudioPick = dashAudios.maxWithOrNull(audioCmp)
            val hlsAudioPick = hlsAudios.maxWithOrNull(audioCmp)
            audioUrl = dashAudioPick?.mf?.url ?: hlsAudioPick?.mf?.url ?: ""
        } else {
            // DASH 兜底
            val inCap = dashVideos.filter { it.height in 1..1080 }
            val pool = if (inCap.isNotEmpty()) inCap else dashVideos
            val videoPick = pool.maxWithOrNull(compareBy<Cand> { it.height }.thenBy { vRank(it.vcodec) }.thenBy { it.tbr })
            val audioPick = dashAudios.maxWithOrNull(audioCmp) ?: hlsAudios.maxWithOrNull(audioCmp)
            if (videoPick != null) formats.add(videoPick.mf)
            audioUrl = audioPick?.mf?.url ?: ""
        }

        // ---- 在线字幕（M14b）：人工字幕全收；自动字幕只取常见语言；优先 json3 格式 ----
        val subTracks = ArrayList<SubTrack>()
        fun pickCaption(langKey: String, arr: JSONArray?, auto: Boolean) {
            if (arr == null || subTracks.size >= 8) return
            var best: JSONObject? = null
            for (i in 0 until arr.length()) {
                val o = arr.optJSONObject(i) ?: continue
                if (best == null) best = o
                if (o.optString("ext") == "json3") {
                    best = o
                    break
                }
            }
            val u = best?.optString("url").orEmpty()
            if (u.isEmpty()) return
            val name = best?.optString("name").orEmpty().ifEmpty { langKey }
            subTracks.add(SubTrack(if (auto) "$name（自动）" else name, u, langKey, auto))
        }
        val manualSubs = j.optJSONObject("subtitles")
        manualSubs?.keys()?.forEach { lang -> pickCaption(lang, manualSubs.optJSONArray(lang), false) }
        val autoCaps = j.optJSONObject("automatic_captions")
        val wanted = listOf(j.optString("language"), "zh-Hans", "zh-CN", "zh-Hant", "en")
            .filter { it.isNotEmpty() }.distinct()
        autoCaps?.keys()?.forEach { lang ->
            if (lang in wanted) pickCaption(lang, autoCaps.optJSONArray(lang), true)
        }

        VideoDetail(
            title = j.optString("title"),
            uploader = j.optString("uploader"),
            durationSec = j.optDouble("duration", 0.0),
            cover = j.optString("thumbnail"),
            webpageUrl = url,
            formats = formats,
            audioUrl = audioUrl,
            qualities = qualities,
            currentQualityId = hlsVideoPick?.fid ?: qualities.firstOrNull()?.id ?: "",
            subtitleTracks = subTracks,
        )
    }

    /** 音乐解析：用搜索结果自带的元数据（歌名/歌手/专辑/封面）+ 取流 */
    suspend fun resolveMusic(item: VideoItem, platform: String): VideoDetail = withContext(Dispatchers.IO) {
        syncMusicCookies()
        logRoute("播放")
        val referer = if (platform == "netease") "https://music.163.com/" else "https://y.qq.com/"
        var stream = if (platform == "netease") {
            NeteaseApi.bestStream(item.id.removePrefix("ne:").toLong())
        } else {
            QQMusicApi.bestStream(item.id.removePrefix("qq:"))
        } ?: throw Exception(musicUnavailableMessage(platform))
        // 预检：地域/VIP 限制会在这一步暴露（否则播放器只会静默无声）
        var code = probeStream(stream.url, referer)
        android.util.Log.i("HOV", "音源预检 code=$code node=${stream.url.substringAfter("//").substringBefore("/")}")
        if (code == 403) {
            // 登录态音源被 CDN 拒绝（海外 IP 版权校验）→ 退回匿名免费音质，保证能听
            val anon = if (platform == "netease") {
                NeteaseApi.streamAnon(item.id.removePrefix("ne:").toLong())
            } else {
                QQMusicApi.streamAnon(item.id.removePrefix("qq:"))
            }
            if (anon != null) {
                val c2 = probeStream(anon.url, referer)
                android.util.Log.i("HOV", "匿名音源预检 code=$c2 node=${anon.url.substringAfter("//").substringBefore("/")}")
                if (c2 == 200 || c2 == 206) {
                    stream = anon
                    code = c2
                    android.util.Log.i("HOV", "音源降级: 登录态 403，已回退匿名免费音质 (br=${anon.br})")
                }
            }
        }
        if (code != 200 && code != 206) throw Exception(streamRejectMessage(platform, code))
        val label = when {
            stream.ext == "flac" || stream.ext == "ape" -> "无损 ${stream.br / 1000}k"
            stream.br >= 320000 -> "320k"
            stream.br >= 128000 -> "128k"
            else -> "${stream.br / 1000}k"
        }
        android.util.Log.i("HOV", "音质[$platform] ${item.title.take(24)}: $label (type=${stream.ext}, br=${stream.br})")
        VideoDetail(
            title = item.title,
            uploader = item.uploader,
            durationSec = item.durationSec.toDouble(),
            cover = item.cover,
            webpageUrl = item.url,
            formats = listOf(MediaFormat(stream.url, label, stream.ext, 0)),
            audioUrl = "",
        )
    }

    /**
     * 播放器内即时切换音质（M20）：按指定档位重新取流并预检。
     * 返回 null 表示该档位不可用（VIP/版权限制），调用方提示并保持原音质。
     */
    suspend fun musicStreamAt(songId: String, platform: String, tier: String): MusicStream? =
        withContext(Dispatchers.IO) {
            syncMusicCookies()
            val referer = if (platform == "netease") "https://music.163.com/" else "https://y.qq.com/"
            val idPart = songId.removePrefix("ne:").removePrefix("qq:")
            var s = if (platform == "netease") {
                NeteaseApi.stream(idPart.toLongOrNull() ?: 0L, tier)
            } else {
                QQMusicApi.streamAt(idPart, tier)
            } ?: return@withContext null
            var code = probeStream(s.url, referer)
            if (code == 403) {
                // 登录态音源被 CDN 拒（地区/VIP）→ 回退匿名免费音质（与首次解析一致）
                val anon = if (platform == "netease") {
                    NeteaseApi.streamAnon(idPart.toLongOrNull() ?: 0L)
                } else {
                    QQMusicApi.streamAt(idPart, "standard", anon = true)
                }
                if (anon != null && probeStream(anon.url, referer).let { it == 200 || it == 206 }) {
                    android.util.Log.i("HOV", "音质切换降级: 403 → 匿名免费音质 (br=${anon.br})")
                    s = anon
                    code = 200
                }
            }
            android.util.Log.i("HOV", "音质切换[$platform] tier=$tier code=$code br=${s.br} type=${s.ext}")
            if (code == 200 || code == 206) s else null
        }

    /**
     * 拉取在线字幕内容并解析（B站 CC / YouTube json3），供播放器字幕菜单使用（M14b）。
     * 注意：走系统默认网络（不走直连路由）—— YouTube 的 timedtext 地址与解析时的出口 IP 绑定，
     * 而解析（yt-dlp）走的是系统网络，若这里走直连网络会因 IP 不一致被 403。
     */
    suspend fun fetchSubtitleCues(track: SubTrack): List<Subtitles.Cue> = withContext(Dispatchers.IO) {
        val referer = if (track.url.contains("hdslb")) "https://www.bilibili.com/" else "https://www.youtube.com/"
        val raw = plainGet(track.url, referer)
        android.util.Log.i("HOV", "字幕原文 ${raw.length} 字节: ${raw.take(90).replace("\n", " ")}")
        val cues = Subtitles.parseAny(raw)
        android.util.Log.i(
            "HOV",
            "在线字幕[${track.label}] ${cues.size} 条 (${track.lang}${if (track.auto) ",自动" else ""})"
        )
        cues
    }

    /** 普通 GET（系统默认网络，用于字幕等不需要直连策略的请求） */
    private fun plainGet(url: String, referer: String): String {
        val conn = java.net.URL(url).openConnection() as java.net.HttpURLConnection
        return try {
            conn.connectTimeout = 15000
            conn.readTimeout = 20000
            conn.setRequestProperty("User-Agent", MusicHttp.UA)
            conn.setRequestProperty("Referer", referer)
            conn.setRequestProperty("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8")
            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else conn.errorStream
            val body = stream?.bufferedReader()?.use { it.readText() }.orEmpty()
            if (code !in 200..299) {
                android.util.Log.w("HOV", "字幕抓取 HTTP $code: ${body.take(120)}")
                throw Exception("HTTP $code")
            }
            body
        } finally {
            conn.disconnect()
        }
    }

    // ---------- 下载 ----------

    /** 开始下载；进度写入 downloads StateFlow。B 站走直链 + 真名命名；音乐平台带封面嵌入 */
    fun startDownload(
        taskId: String,
        url: String,
        title: String,
        platform: String,
        artist: String = "",
        album: String = "",
        coverUrl: String = "",
    ) {
        val req = DownloadRequest(taskId, url, title, platform, artist, album, coverUrl)
        requests[taskId] = req
        _downloads.value = _downloads.value + DownloadTask(taskId, title, 0f, 0, DownloadTask.State.QUEUED)
        pendingQueue.offer(req)
        pumpQueue()
    }

    /** 重试失败/取消的下载（沿用原参数重新排队） */
    fun retryDownload(taskId: String) {
        val req = requests[taskId] ?: return
        updateTask(taskId) { it.copy(progress = 0f, etaSec = 0, state = DownloadTask.State.QUEUED, error = "") }
        pendingQueue.offer(req)
        pumpQueue()
    }

    /** 队列推进：并发上限内取任务执行 */
    private fun pumpQueue() {
        synchronized(queueLock) {
            while (runningCount < MAX_CONCURRENT_DOWNLOADS) {
                val req = pendingQueue.poll() ?: break
                val st = _downloads.value.firstOrNull { it.id == req.taskId }?.state
                if (st == DownloadTask.State.CANCELED) continue      // 排队期间被取消
                runningCount++
                Thread { executeDownload(req) }.start()
            }
        }
    }

    /** 单个下载任务的执行体（由队列调度，最多 MAX_CONCURRENT_DOWNLOADS 个并行） */
    private fun executeDownload(req: DownloadRequest) {
        val taskId = req.taskId
        val url = req.url
        val title = req.title
        val platform = req.platform
        val artist = req.artist
        val album = req.album
        val coverUrl = req.coverUrl
        val tempFiles = mutableListOf<File>()   // 音乐下载的本地中间文件（无论成败都要清理）
        try {
            updateTask(taskId) { it.copy(state = DownloadTask.State.RUNNING) }
            val processId = "dl-$taskId"          // 供 cancelDownload 终止 yt-dlp
            processIds[taskId] = processId
            try {
                val dir = downloadsDir().apply { mkdirs() }
                enforceCap(dir)
                // 仅 WiFi 下载：移动网络下直接失败（提示清楚，避免偷跑流量）
                if (Settings.wifiOnlyDownload && !isOnWifiOrEthernet()) {
                    throw Exception("已开启「仅 WiFi 下载」，当前是移动网络")
                }
                val req: YoutubeDLRequest
                if (isMusicPlatform(platform)) {
                    // 音乐：先用 App 自身（直连网络）把音频与封面抓到本地，
                    // 再交给 yt-dlp 做纯本地的封面/标签封装 —— 全程不经过系统 VPN
                    syncMusicCookies()
                    logRoute("下载")
                    var stream = if (platform == "netease") {
                        NeteaseApi.bestStream(url.removePrefix("ne:").toLong())
                    } else {
                        QQMusicApi.bestStream(url.removePrefix("qq:"))
                    } ?: throw Exception(musicUnavailableMessage(platform))
                    val dlReferer = if (platform == "netease") "https://music.163.com/" else "https://y.qq.com/"
                    var dlCode = probeStream(stream.url, dlReferer)
                    if (dlCode == 403) {
                        // 与播放一致的降级：登录态被地区限制时改下匿名免费音质
                        val anonDl = if (platform == "netease") {
                            NeteaseApi.streamAnon(url.removePrefix("ne:").toLong())
                        } else {
                            QQMusicApi.streamAnon(url.removePrefix("qq:"))
                        }
                        if (anonDl != null) {
                            val c2 = probeStream(anonDl.url, dlReferer)
                            if (c2 == 200 || c2 == 206) {
                                stream = anonDl
                                dlCode = c2
                                android.util.Log.i("HOV", "下载降级: 登录态 403，已回退匿名免费音质")
                            }
                        }
                    }
                    if (dlCode != 200 && dlCode != 206) throw Exception(streamRejectMessage(platform, dlCode))

                    // 1) 音频本体（直连下载，进度占 0~75%）
                    val audioFile = File(appContext.cacheDir, "dl_${taskId}.${stream.ext}")
                    tempFiles += audioFile
                    downloadToFile(stream.url, audioFile, dlReferer, taskId) { done, total ->
                        if (total > 0) updateTask(taskId) {
                            it.copy(progress = (done.toFloat() / total * 0.75f).coerceIn(0f, 1f))
                        }
                    }
                    // 2) 封面（小文件，失败不阻断音频下载）
                    var coverFile: File? = null
                    if (coverUrl.isNotEmpty()) {
                        val cf = File(appContext.cacheDir, "dl_${taskId}.jpg")
                        runCatching { downloadToFile(coverUrl, cf, null, taskId, null) }
                        if (cf.length() > 0) {
                            coverFile = cf
                            tempFiles += cf
                        }
                    }
                    // 3) 本地地址交给 yt-dlp（--load-info-json + 本机 http），封装在本地完成
                    val localAudio = StreamProxy.serveFile(audioFile) ?: throw Exception("本机代理启动失败")
                    val info = org.json.JSONObject().apply {
                        put("id", url.substringAfter(':'))
                        put("title", title.ifEmpty { "audio" })
                        put("artist", artist)
                        put("album", album)
                        put("url", localAudio)
                        put("ext", stream.ext)
                        put("webpage_url", if (platform == "netease") "https://music.163.com/song?id=${url.removePrefix("ne:")}" else "https://y.qq.com/n/ryqq/songDetail/${url.removePrefix("qq:")}")
                        coverFile?.let { cf ->
                            StreamProxy.serveFile(cf)?.let { cu ->
                                put("thumbnails", org.json.JSONArray().put(org.json.JSONObject().put("url", cu)))
                            }
                        }
                    }
                    val infoFile = File(appContext.cacheDir, "music_$taskId.json")
                    infoFile.writeText(info.toString())
                    req = YoutubeDLRequest("infojson:" + infoFile.absolutePath).apply {
                        addOption("--load-info-json", infoFile.absolutePath)
                        addOption("--embed-metadata")
                        if (coverFile != null) addOption("--embed-thumbnail")
                        if (stream.ext == "flac" || stream.ext == "ape") {
                            addOption("--extract-audio"); addOption("--audio-format", "flac")
                        } else {
                            addOption("--extract-audio"); addOption("--audio-format", "mp3"); addOption("--audio-quality", "0")
                        }
                        addOption("-o", "$dir/%(artist)s - %(title)s.%(ext)s")
                        addOption("--paths", "temp:" + appContext.cacheDir.absolutePath)   // 中间文件不进下载目录
                        addOption("--windows-filenames")
                    }
                } else if (platform == "bilibili") {
                    val detail = BiliApi.resolve(url)
                    if (detail.formats.isEmpty()) throw Exception("无可用流")
                    // 多段视频：直链只覆盖第一段（静默丢半段不可接受），显式失败
                    if (detail.formats.size > 1) throw Exception("多段视频（${detail.formats.size} 段）暂不支持下载")
                    req = YoutubeDLRequest(detail.formats[0].url)
                    val safe = title.replace(Regex("[\\\\/:*?\"<>|]"), "_").trim().take(60)
                        .ifBlank { "video_" + System.currentTimeMillis() }
                    var out = File(dir, "$safe.mp4")
                    var n = 2
                    while (out.exists()) { out = File(dir, "$safe ($n).mp4"); n++ }
                    req.addOption("-o", out.absolutePath)
                    applyBiliHeaders(req)
                } else {
                    req = YoutubeDLRequest(url)
                    req.addOption("-f", "bv*+ba/b")
                    req.addOption("-o", "$dir/%(title)s.%(ext)s")
                    applyCookies(req, "youtube")
                }
                req.addOption("--newline")
                req.addOption("--no-warnings")

                val resp = YoutubeDL.getInstance().execute(req, processId) { progress, eta, _ ->
                    // yt-dlp 启动瞬间回调 -1（未知），钳制避免显示负数
                    updateTask(taskId) { it.copy(progress = (progress / 100f).coerceIn(0f, 1f), etaSec = eta) }
                }
                processIds.remove(taskId)
                if (resp.exitCode == 0) {
                    updateTask(taskId) { it.copy(progress = 1f, state = DownloadTask.State.DONE) }
                } else {
                    Log.e("HOV", "yt-dlp exit=${resp.exitCode} err=${resp.err.take(500)}")
                    updateTask(taskId) {
                        it.copy(state = if (it.state == DownloadTask.State.CANCELED) it.state else DownloadTask.State.FAILED)
                    }
                }
            } catch (e: Exception) {
                Log.e("HOV", "download failed: task=$taskId", e)
                processIds.remove(taskId)
                updateTask(taskId) {
                    it.copy(
                        state = if (it.state == DownloadTask.State.CANCELED) it.state else DownloadTask.State.FAILED,
                        error = e.message?.take(80).orEmpty(),
                    )
                }
            } finally {
                tempFiles.forEach { f ->
                    StreamProxy.releaseFile(f)
                    runCatching { f.delete() }
                }
            }
        } catch (e: Exception) {
            Log.e("HOV", "download executor error: task=$taskId", e)
            updateTask(taskId) { it.copy(state = DownloadTask.State.FAILED, error = e.message?.take(80).orEmpty()) }
        } finally {
            processIds.remove(taskId)
            synchronized(queueLock) { runningCount-- }
            pumpQueue()          // 空出并发槽位，继续下一个排队任务
        }
    }

    /**
     * 直连网络下载到本地文件（音乐平台专用：App 自身流量走 [NetRoute] 的直连网络，
     * 不受系统 VPN 影响）。taskId 非空时会检查取消状态；onProgress(已下载, 总长度)
     */
    private fun downloadToFile(
        url: String,
        dest: File,
        referer: String?,
        taskId: String?,
        onProgress: ((Long, Long) -> Unit)?,
    ) {
        val conn = NetRoute.open(url, 20000)
        try {
            conn.setRequestProperty("User-Agent", MusicHttp.UA)
            if (!referer.isNullOrEmpty()) conn.setRequestProperty("Referer", referer)
            val code = conn.responseCode
            if (code !in 200..299) throw Exception("HTTP $code")
            val total = conn.contentLengthLong.takeIf { it > 0 } ?: -1L
            var done = 0L
            conn.inputStream.use { ins ->
                dest.outputStream().use { outs ->
                    val buf = ByteArray(128 * 1024)
                    while (true) {
                        if (taskId != null &&
                            _downloads.value.firstOrNull { it.id == taskId }?.state == DownloadTask.State.CANCELED
                        ) throw Exception("已取消")
                        val n = ins.read(buf)
                        if (n <= 0) break
                        outs.write(buf, 0, n)
                        done += n
                        onProgress?.invoke(done, total)
                    }
                }
            }
        } finally {
            conn.disconnect()
        }
    }

    fun cancelDownload(taskId: String) {
        processIds.remove(taskId)?.let { YoutubeDL.getInstance().destroyProcessById(it) }
        updateTask(taskId) { it.copy(state = DownloadTask.State.CANCELED) }
    }

    fun dismissDownload(taskId: String) {
        _downloads.value = _downloads.value.filterNot { it.id == taskId }
    }

    private fun updateTask(id: String, transform: (DownloadTask) -> DownloadTask) {
        _downloads.value = _downloads.value.map { if (it.id == id) transform(it) else it }
    }

    // ---------- 存储 ----------

    /** 本地库排序方式（M16；StorageRepo 与 UI 共用） */
    enum class LocalSort(val label: String) {
        RECENT("最近"), NAME("名称"), SIZE("大小"), TYPE("类型")
    }

    // ---------- 本地存储（B2 拆分：实现已移至 StorageRepo，这里只保留委托，调用点零改动） ----------

    fun downloadsDir(): File = StorageRepo.downloadsDir()

    private fun enforceCap(dir: File) = StorageRepo.enforceCap(dir)

    fun listDownloads(sort: LocalSort = LocalSort.RECENT, query: String = ""): List<LocalFile> =
        StorageRepo.listDownloads(sort, query)

    fun renameDownload(path: String, newBaseName: String): String? =
        StorageRepo.renameDownload(path, newBaseName)

    fun deleteDownload(path: String): Boolean = StorageRepo.deleteDownload(path)

    fun storageStats(): Triple<Int, Long, Long> = StorageRepo.storageStats()

    fun clearCache() = StorageRepo.clearCache()

    fun clearDownloads() = StorageRepo.clearDownloads()
}


/** 下载请求参数（内部使用：排队与重试都要用） */
data class DownloadRequest(
    val taskId: String,
    val url: String,
    val title: String,
    val platform: String,
    val artist: String = "",
    val album: String = "",
    val coverUrl: String = "",
)
