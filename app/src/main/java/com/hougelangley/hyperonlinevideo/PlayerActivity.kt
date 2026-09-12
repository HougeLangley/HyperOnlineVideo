package com.hougelangley.hyperonlinevideo

import android.app.Activity
import android.content.pm.ActivityInfo
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.GestureDetector
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.*
import android.widget.ImageButton
import com.hougelangley.hyperonlinevideo.data.Lyrics
import android.widget.ScrollView
import com.hougelangley.hyperonlinevideo.data.PlayQueue
import com.hougelangley.hyperonlinevideo.data.VideoDetail
import com.hougelangley.hyperonlinevideo.data.MediaFormat
import com.hougelangley.hyperonlinevideo.data.Repo
import com.hougelangley.hyperonlinevideo.data.Settings
import com.hougelangley.hyperonlinevideo.data.Subtitles
import com.hougelangley.hyperonlinevideo.data.SubTrack
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import com.hougelangley.hyperonlinevideo.data.StreamProxy
import android.widget.ImageView
import coil.load
import com.hougelangley.hyperonlinevideo.data.BiliApi
import com.hougelangley.hyperonlinevideo.data.PlaybackHistory
import com.hougelangley.hyperonlinevideo.data.QualityOption
import `is`.xyz.mpv.BaseMPVView
import `is`.xyz.mpv.MPV
import `is`.xyz.mpv.MPVNode

/** 全屏播放器（libmpv 硬解直链流） */
class PlayerActivity : Activity() {

    /** BaseMPVView 是抽象类，子类化配置 mpv 选项 */
    class HyperMPVView(context: android.content.Context) : BaseMPVView(context, null) {
        var referer: String = ""
        var logPath: String = ""
        override fun initOptions() {
            if (logPath.isNotEmpty()) mpv.setOptionString("log-file", logPath)
            mpv.setOptionString("hwdec", "mediacodec")
            mpv.setOptionString("cache", "auto")
            mpv.setOptionString("cache-secs", "30")
            if (referer.isNotEmpty()) mpv.setOptionString("http-header-fields", "Referer: $referer")
        }
        override fun postInitOptions() {}
        override fun observeProperties() {}
    }

    private lateinit var mpvView: HyperMPVView
    private lateinit var controls: LinearLayout
    private lateinit var btnPlay: ImageButton
    private lateinit var btnRewind: ImageButton
    private lateinit var btnForward: ImageButton
    private lateinit var btnFullscreen: ImageButton
    private val gestureDetector by lazy {
        GestureDetector(this, object : GestureDetector.SimpleOnGestureListener() {
            override fun onSingleTapConfirmed(e: MotionEvent): Boolean {
                android.util.Log.i("HOV", "手势: 单击 → 切换控件")
                toggleControls()                      // 单击：唤出/隐藏控件
                return true
            }

            override fun onDoubleTap(e: MotionEvent): Boolean {
                val w = mpvView.width
                android.util.Log.i("HOV", "手势: 双击 x=${e.x} 屏宽=$w")
                if (w > 0) {
                    if (e.x > w / 2f) seekBy(5.0) else seekBy(-5.0)   // 右半屏快进 / 左半屏快退
                }
                return true
            }
        })
    }
    private lateinit var btnQuality: TextView
    private lateinit var btnSpeed: TextView
    private lateinit var seek: SeekBar
    private lateinit var timeLabel: TextView
    private var isFullscreen = false
    private var controlsShown = true
    private val autoHideRunnable = Runnable { hideControls(true) }

    // 进度记忆 / 清晰度切换 / 倍速
    private var progressKey = ""
    private var qualitiesList: List<QualityOption> = emptyList()
    private var currentQualityId = ""
    private var currentAudioUrl = ""
    private var pendingSeekSec = -1.0
    private var pendingSeekIsResume = false
    private var pendingReloadSeek = -1.0   // 换流（切清晰度/切音质）后要对齐的位置
    private var reloadSeekToken = 0        // 用户手动拖动进度条时作废对齐任务
    private var currentSpeed = 1.0f
    private var saveTick = 0
    private val handler = Handler(Looper.getMainLooper())
    private var userSeeking = false
    private val audioManager by lazy { getSystemService(AUDIO_SERVICE) as AudioManager }
    private var focusRequest: AudioFocusRequest? = null
    private var pausedByFocus = false
    private var platform = ""
    private var videoTitle = ""
    // ---- 播放队列（Phase 1）----
    private lateinit var titleLabel: TextView
    private var coverView: ImageView? = null
    private var btnQueue: TextView? = null
    private var btnMode: TextView? = null
    private var btnPrev: ImageButton? = null
    private var btnNext: ImageButton? = null
    private var switching = false        // 自动续播切换中（防止重复触发）
    // ---- 手势（P2）：左半屏上下滑=亮度，右半屏上下滑=音量 ----
    private var gestureStartX = 0f
    private var gestureStartY = 0f
    private var gestureMode = 0            // 0=未定 1=亮度 2=音量
    private var gestureActive = false
    private var gestureBaseBrightness = 0f
    private var gestureBaseVolume = 0
    private var gestureOverlay: TextView? = null
    // ---- 歌词（P3，音乐模式）----
    private var btnLyrics: TextView? = null
    private var lyricsPanel: ScrollView? = null
    private var lyricsBox: LinearLayout? = null
    private val lyricViews = mutableListOf<TextView>()
    private var lyricLines: List<Lyrics.Line> = emptyList()
    private var lyricIndex = -1
    private var lyricsShown = false
    private var btnPip: ImageButton? = null
    private var btnSubtitle: TextView? = null
    private var subtitleView: TextView? = null          // App 层字幕覆盖层（libass 在本构建下渲染不出字）
    private var localCues: List<Subtitles.Cue> = emptyList()
    private var subsEnabled = true
    private var pendingSubtitleFile: java.io.File? = null
    private var localSubFile: java.io.File? = null        // 本地同名外挂字幕（菜单回切用）
    private var subtitleTracks: List<SubTrack> = emptyList()  // 在线字幕轨（M14b）
    private var activeSubUrl = ""                         // 当前生效的在线字幕地址（本地为空）
    private var subLoadToken = 0                          // 在线字幕加载世代号（防串场）
    private var lastCueKey: String = ""
    private var songId = ""                              // 音乐歌曲 id（ne:xxx / qq:mid）
    private var inPip = false
    private var playbackStarted = false  // 已经真正开始过播放（排除 mpv 初始 idle 误触发）
    private var finishedHandled = false  // 本次播放的结束已处理
    private val playerScope = kotlinx.coroutines.CoroutineScope(
        kotlinx.coroutines.Dispatchers.Main + kotlinx.coroutines.SupervisorJob()
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val url = intent.getStringExtra("url") ?: run { finish(); return }
        val audioUrl = intent.getStringExtra("audioUrl").orEmpty()
        val title = intent.getStringExtra("title").orEmpty()
        platform = intent.getStringExtra("platform").orEmpty()
        val coverUrl = intent.getStringExtra("cover").orEmpty()
        // 音乐流经本机代理中转：mpv 是 native 代码无法逐 socket 绑定网络，
        // 走 127.0.0.1 由 App 用直连网络拉流，保证不受系统 VPN 影响
        val playUrl = if (platform == "netease" || platform == "qqmusic") {
            StreamProxy.serveStream(url)?.also { android.util.Log.i("HOV", "音乐流经本机代理: $it") } ?: url
        } else {
            url
        }
        videoTitle = title.ifEmpty { "正在播放" }
        progressKey = intent.getStringExtra("key").orEmpty()
        @Suppress("UNCHECKED_CAST")
        qualitiesList = (intent.getSerializableExtra("qualities") as? ArrayList<QualityOption>) ?: arrayListOf()
        currentQualityId = intent.getStringExtra("currentQualityId").orEmpty()
        currentAudioUrl = audioUrl
        @Suppress("UNCHECKED_CAST")
        subtitleTracks = (intent.getSerializableExtra("subtitles") as? ArrayList<SubTrack>) ?: arrayListOf()
        // 音乐平台：构建音质档位（受设置上限裁剪）+ 从当前音质标签推断档位（M20 播放器内切音质）
        songId = intent.getStringExtra("songId").orEmpty()
        if (isMusicPlatform(platform)) {
            val qualityLabel = intent.getStringExtra("qualityLabel").orEmpty()
            val tiers = when (Settings.musicQuality) {
                Settings.QUALITY_HIGH -> listOf("exhigh" to "320k", "standard" to "128k")
                Settings.QUALITY_STANDARD -> listOf("standard" to "128k")
                else -> listOf("lossless" to "无损", "exhigh" to "320k", "standard" to "128k")
            }
            qualitiesList = tiers.map { (tid, label) -> QualityOption(tid, label, "", -1) }
            currentQualityId = when {
                qualityLabel.startsWith("无损") -> "lossless"
                qualityLabel.startsWith("320") -> "exhigh"
                else -> "standard"
            }
        }
        // 进度恢复：上次播放位置 > 5 秒才续播（可在设置里关闭进度记忆）
        val savedPos = PlaybackHistory.positionSec(progressKey)
        if (Settings.resumePlayback && savedPos > 5) {
            pendingSeekSec = savedPos
            pendingSeekIsResume = true
        }

        val root = FrameLayout(this)
        root.setBackgroundColor(0xFF000000.toInt())

        mpvView = HyperMPVView(this)
        if (platform == "bilibili") mpvView.referer = "https://www.bilibili.com"
        // 诊断日志（外置可读）
        runCatching {
            val lf = java.io.File(externalCacheDir ?: cacheDir, "mpv.log")
            lf.delete()
            mpvView.logPath = lf.absolutePath
        }
        android.util.Log.i("HOV", "播放 url=${url.take(100)} audio=${audioUrl.take(100)} platform=$platform")
        root.addView(mpvView, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        // 音乐模式：居中展示专辑封面（音频流本身无画面）
        if (isMusicPlatform(platform) || platform == "local") {
            val cv = ImageView(this).apply {
                scaleType = ImageView.ScaleType.CENTER_CROP
                setBackgroundColor(0xFF15151C.toInt())
                layoutParams = FrameLayout.LayoutParams(dp(300), dp(300), Gravity.CENTER)
            }
            coverView = cv
            if (coverUrl.isNotEmpty()) {
                // 在线音乐：直接加载平台封面
                cv.load(coverUrl) { crossfade(true) }
                root.addView(cv)
            } else if (platform == "local") {
                // 本地音频：读取内嵌专辑封面（异步，避免阻塞）
                Thread {
                    val bmp = try {
                        val mmr = android.media.MediaMetadataRetriever()
                        mmr.setDataSource(url)
                        val pic = mmr.embeddedPicture
                        mmr.release()
                        pic?.let { android.graphics.BitmapFactory.decodeByteArray(it, 0, it.size) }
                    } catch (e: Exception) {
                        null
                    }
                    if (bmp != null) runOnUiThread {
                        cv.setImageBitmap(bmp)
                        if (cv.parent == null) root.addView(cv)
                    }
                }.start()
            }
        }

        val tapLayer = View(this)
        tapLayer.setOnTouchListener { _, event ->
            handleTouch(event)   // 单击=控件显隐；双击=±5 秒；上下滑=亮度/音量
            true
        }
        root.addView(tapLayer, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        // 手势浮层（亮度/音量百分比，居中显示，不拦截触摸）
        gestureOverlay = TextView(this).apply {
            setBackgroundResource(R.drawable.bg_pill_button)
            setTextColor(0xFFFFFFFF.toInt())
            textSize = 16f
            setPadding(dp(24), dp(12), dp(24), dp(12))
            visibility = View.GONE
            isClickable = false
            layoutParams = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.CENTER)
        }
        gestureOverlay?.let { root.addView(it) }

        // 歌词面板（音乐模式，点「歌词」切换显示；不拦截触摸，手势照常）
        lyricsBox = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(18), dp(18), dp(18), dp(18))
        }
        lyricsPanel = ScrollView(this).apply {
            visibility = View.GONE
            isClickable = false
            setBackgroundResource(R.drawable.bg_lyrics_panel)
            addView(lyricsBox)
            layoutParams = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                (resources.displayMetrics.heightPixels * 0.52f).toInt(),
                Gravity.CENTER
            ).apply { marginStart = dp(24); marginEnd = dp(24) }
        }
        lyricsPanel?.let { root.addView(it) }

        // 字幕覆盖层（本地视频 + 同名 .srt；白字黑描边，位于控件栏上方）
        subtitleView = TextView(this).apply {
            setTextColor(0xFFFFFFFF.toInt())
            textSize = 17f
            gravity = Gravity.CENTER
            setPadding(dp(16), dp(6), dp(16), dp(6))
            setShadowLayer(4f, 0f, 1f, 0xFF000000.toInt())
            visibility = View.GONE
            isClickable = false
            layoutParams = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL
            ).apply { bottomMargin = dp(if (isLandscapeNow()) 24 else 120) }
        }
        subtitleView?.let { root.addView(it) }

        controls = buildControls(title)
        root.addView(controls, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM))
        setContentView(root)

        mpvView.initialize(filesDir.resolve("mpv").absolutePath, cacheDir.resolve("mpv").absolutePath)
        // 库在 postInitOptions 之后强制 idle=once（播完第一个文件就退出 mpv 核心，
        // 导致队列续播的 loadfile 无效）→ 运行时改回 idle=yes，保持核心常驻
        runCatching {
            mpvView.mpv.setPropertyString("idle", "yes")
            android.util.Log.i("HOV", "mpv idle=${mpvView.mpv.getPropertyString("idle")}")
        }
        // 播完检测：mpv 结束后 time-pos/duration 会被清空，轮询不可靠 → 观察 eof-reached / idle-active
        runCatching {
            val m = mpvView.mpv
            m.observeProperty("eof-reached", MPV.mpvFormat.MPV_FORMAT_FLAG)
            m.observeProperty("idle-active", MPV.mpvFormat.MPV_FORMAT_FLAG)
            m.addObserver(object : MPV.EventObserver {
                override fun eventProperty(property: String) {}
                override fun eventProperty(property: String, value: Long) {}
                override fun eventProperty(property: String, value: String) {}
                override fun eventProperty(property: String, value: Double) {}
                override fun eventProperty(property: String, value: MPVNode) {}
                override fun event(event: Int, data: MPVNode) {}
                override fun eventProperty(property: String, value: Boolean) {
                    if (value && (property == "eof-reached" || property == "idle-active")) {
                        handler.post { onPlaybackEnded() }
                    }
                }
            })
        }
        mpvView.playFile(playUrl)
        if (platform == "local") autoLoadSubtitle(url)
        handler.postDelayed({ applyFillMode() }, 900)   // 起播后应用显示模式（铺满/适应）

        // 后台播放：注册播放器桥 + 前台服务（媒体通知/锁屏控制）+ 音频焦点
        PlaybackController.listener = object : PlaybackController.Listener {
            override fun onToggle() = togglePlay()
            override fun onNext() = playNext(auto = false)
            override fun onPrev() = playPrev()
            override fun onStopPlayback() {
                finish()
            }
        }
        requestAudioFocus()
        PlaybackController.title = videoTitle
        PlaybackController.playing = true
        PlaybackService.start(this, videoTitle)
        // YouTube DASH/HLS 分离音轨：延迟 700ms 发 audio-add 命令
        // （playFile 内部异步 loadfile，必须保证命令次序在它之后；命令参数原样传递无解析问题）
        if (audioUrl.isNotEmpty()) {
            handler.postDelayed({
                mpvView.mpv.command("audio-add", audioUrl, "select")
                android.util.Log.i("HOV", "audio-add 已执行")
            }, 700)
        }
        startProgressLoop()
        loadLyricsAsync()          // 首次播放即尝试拉歌词（音乐平台）
        showControls(autoHide = true)
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    /** 圆形涟漪底的图标按钮 */
    private fun iconButton(res: Int, desc: String, sizeDp: Int = 46, onClick: () -> Unit): ImageButton =
        ImageButton(this).apply {
            setImageResource(res)
            contentDescription = desc
            background = getDrawable(R.drawable.bg_circle_button)
            scaleType = ImageView.ScaleType.CENTER_INSIDE
            val pad = dp(if (sizeDp >= 46) 11 else 9)
            setPadding(pad, pad, pad, pad)
            layoutParams = LinearLayout.LayoutParams(dp(sizeDp), dp(sizeDp))
            setOnClickListener { onClick() }
        }

    private fun buildControls(title: String): LinearLayout {
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundResource(R.drawable.bg_controls_gradient)
            setPadding(dp(20), dp(12), dp(20), dp(20))
        }
        val topRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        titleLabel = TextView(this).apply {
            text = title
            setTextColor(0xFFFFFFFF.toInt())
            textSize = 15f
            maxLines = 1
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        val closeBtn = iconButton(R.drawable.ic_close, "关闭") { finish() }
        topRow.addView(titleLabel)
        // 队列 / 循环模式（音乐、视频通用）
        btnLyrics = pillButton("歌词") { toggleLyrics() }
        btnLyrics?.visibility = View.GONE          // 有歌词时才显示
        (btnLyrics!!.layoutParams as LinearLayout.LayoutParams).marginEnd = dp(8)
        topRow.addView(btnLyrics)
        btnMode = pillButton(PlayQueue.mode.value.label) { cycleLoopMode() }
        (btnMode!!.layoutParams as LinearLayout.LayoutParams).marginEnd = dp(8)
        btnQueue = pillButton("列表") { showQueueDialog() }
        (btnQueue!!.layoutParams as LinearLayout.LayoutParams).marginEnd = dp(8)
        topRow.addView(btnQueue)
        topRow.addView(btnMode)
        topRow.addView(closeBtn)
        bar.addView(topRow)

        seek = SeekBar(this).apply {
            max = 1000
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {}
                override fun onStartTrackingTouch(sb: SeekBar?) {
                    userSeeking = true
                    reloadSeekToken++                  // 用户手动拖动：作废换流后的自动对齐
                    showControls(autoHide = false)     // 拖动中不隐藏
                }
                override fun onStopTrackingTouch(sb: SeekBar?) {
                    sb?.let {
                        val dur = mpvView.mpv.getPropertyDouble("duration") ?: 0.0
                        if (dur > 0) mpvView.mpv.setPropertyDouble("time-pos", dur * it.progress / 1000.0)
                    }
                    userSeeking = false
                    showControls(autoHide = true)
                }
            })
        }
        val seekRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        btnRewind = iconButton(R.drawable.ic_rewind, "快退 5 秒", sizeDp = 40) { seekBy(-5.0) }
        (btnRewind.layoutParams as LinearLayout.LayoutParams).marginEnd = dp(6)
        btnForward = iconButton(R.drawable.ic_forward, "快进 5 秒", sizeDp = 40) { seekBy(5.0) }
        (btnForward.layoutParams as LinearLayout.LayoutParams).marginStart = dp(6)
        seekRow.addView(btnRewind)
        seekRow.addView(seek)
        seekRow.addView(btnForward)
        bar.addView(seekRow)

        val ctrlRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        btnPlay = iconButton(R.drawable.ic_pause, "播放/暂停") { togglePlay() }
        timeLabel = TextView(this).apply {
            setTextColor(0xFFFFFFFF.toInt())
            textSize = 13f
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(14), 0, 0, 0)
            maxLines = 1
        }
        // 上一首 / 下一首（队列多于 1 项时显示）
        val queueSize = PlayQueue.size
        btnPrev = iconButton(R.drawable.ic_skip_prev, "上一首", sizeDp = 40) { playPrev() }
        btnNext = iconButton(R.drawable.ic_skip_next, "下一首", sizeDp = 40) { playNext(auto = false) }
        if (queueSize <= 1) {
            btnPrev?.visibility = View.GONE
            btnNext?.visibility = View.GONE
        }
        btnFullscreen = iconButton(R.drawable.ic_fullscreen, "全屏") { toggleFullscreen() }
        btnFullscreen.setOnLongClickListener {      // 长按：快速切「铺满 / 适应」
            toggleFillScreen()
            true
        }
        // 画中画（仅视频平台；音乐有后台播放，不需要小窗）
        if (!isMusicPlatform(platform) && android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
            btnPip = iconButton(R.drawable.ic_pip, "画中画") { enterPip() }
            (btnPip!!.layoutParams as LinearLayout.LayoutParams).marginEnd = dp(8)
        }
        ctrlRow.addView(btnPrev)
        ctrlRow.addView(btnPlay)
        ctrlRow.addView(btnNext)
        ctrlRow.addView(timeLabel)
        ctrlRow.addView(View(this).apply { layoutParams = LinearLayout.LayoutParams(0, 1, 1f) })
        btnPip?.let { ctrlRow.addView(it) }
        ctrlRow.addView(btnFullscreen)
        bar.addView(ctrlRow)

        // 第二行：功能药丸（字幕 / 画质 / 倍速）单独一行。
        // 竖屏可用宽度只有 360dp 左右，与传输控件挤在同一行时，
        // 右侧药丸（如 1080p）会被挤出屏幕外看不到。
        val curQualityLabel = qualitiesList.firstOrNull { it.id == currentQualityId }?.label ?: ""
        btnQuality = pillButton(curQualityLabel.ifEmpty { "画质" }) { showQualityMenu() }
        if (qualitiesList.isEmpty()) btnQuality.visibility = View.GONE
        btnSpeed = pillButton(fmtSpeed(currentSpeed)) { showSpeedMenu() }
        // 字幕（视频平台）：本地同名字幕自动加载，这里可切换/关闭
        if (!isMusicPlatform(platform)) {
            btnSubtitle = pillButton("字幕") { showSubtitlePicker() }
            (btnSubtitle!!.layoutParams as LinearLayout.LayoutParams).marginEnd = dp(6)
        }
        val pillRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.END
            setPadding(0, dp(8), 0, 0)
        }
        btnSubtitle?.let { pillRow.addView(it) }
        pillRow.addView(btnQuality)
        pillRow.addView(btnSpeed)
        bar.addView(pillRow)
        return bar
    }

    /** 药丸形文字按钮（画质/倍速） */
    private fun pillButton(text: String, onClick: () -> Unit): TextView =
        TextView(this).apply {
            this.text = text
            setTextColor(0xFFFFFFFF.toInt())
            textSize = 12f
            gravity = Gravity.CENTER
            background = getDrawable(R.drawable.bg_pill_button)
            setPadding(dp(12), 0, dp(12), 0)
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dp(40)
            ).apply { marginStart = dp(8) }
            setOnClickListener { onClick() }
        }

    private fun fmtSpeed(v: Float): String {
        val s = if (v % 1f == 0f) "%.1f".format(v) else v.toString().trimEnd('0').trimEnd('.')
        return "${s}x"
    }

    // ---------- 倍速 ----------

    // ---------- 字幕（M14：本地外挂字幕） ----------

    /**
     * 本地文件：登记同名字幕文件（.srt/.ass/.ssa/.vtt），等文件真正加载后再解析。
     */
    private fun autoLoadSubtitle(mediaPath: String) {
        val f = Subtitles.findFor(mediaPath)
        localSubFile = f
        pendingSubtitleFile = f
        localCues = emptyList()
        activeSubUrl = ""
        lastCueKey = ""
        subtitleView?.visibility = View.GONE
    }

    /** 文件加载完成后解析字幕（在进度循环里调用） */
    private fun attachPendingSubtitle() {
        val sub = pendingSubtitleFile ?: return
        pendingSubtitleFile = null
        playerScope.launch {
            val cues = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { Subtitles.load(sub) }
            localCues = cues
            android.util.Log.i("HOV", "字幕加载: ${sub.name} → ${cues.size} 条")
            if (cues.isEmpty()) Toast.makeText(this@PlayerActivity, "字幕解析为空：${sub.name}", Toast.LENGTH_SHORT).show()
            else Toast.makeText(this@PlayerActivity, "已加载字幕 ${sub.name}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun isLandscapeNow(): Boolean =
        resources.configuration.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE

    /** 字幕位置随屏幕方向调整：横屏贴近底部（不挡画面），竖屏放在视频下方黑边里 */
    private fun applySubtitlePosition() {
        val v = subtitleView ?: return
        val lp = v.layoutParams as? FrameLayout.LayoutParams ?: return
        val bottom = dp(if (isLandscapeNow()) 24 else 120)
        if (lp.bottomMargin != bottom) {
            lp.bottomMargin = bottom
            v.layoutParams = lp
        }
    }

    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) {
        super.onConfigurationChanged(newConfig)
        applySubtitlePosition()
    }

    /** 按播放进度更新字幕覆盖层 */
    private fun syncSubtitleOverlay(posSec: Double) {
        val v = subtitleView ?: return
        if (!subsEnabled || localCues.isEmpty()) {
            if (v.visibility != View.GONE) v.visibility = View.GONE
            return
        }
        val cue = Subtitles.cueAt(localCues, (posSec * 1000).toLong())
        val key = cue?.let { "${it.startMs}:${it.text}" } ?: ""
        if (key == lastCueKey) return
        lastCueKey = key
        if (cue == null) {
            v.visibility = View.GONE
        } else {
            v.text = cue.text
            v.visibility = View.VISIBLE
        }
    }

    /** 字幕菜单：关闭 / 本地同名 / 在线轨（B站 CC、YouTube 人工·自动）（M14b） */
    private fun showSubtitlePicker() {
        val acts = ArrayList<Pair<String, () -> Unit>>()
        acts.add((if (!subsEnabled) "● " else "") + "关闭字幕" to { setSubsEnabled(false) })
        localSubFile?.let { f ->
            val mark = if (subsEnabled && activeSubUrl.isEmpty() && localCues.isNotEmpty()) "● " else ""
            acts.add("${mark}本地：${f.name}" to { loadLocalSubs(f) })
        }
        subtitleTracks.forEach { t ->
            val mark = if (subsEnabled && activeSubUrl == t.url) "● " else ""
            acts.add("$mark${t.label}" to { loadOnlineSubs(t) })
        }
        if (localSubFile == null && subtitleTracks.isEmpty()) {
            Toast.makeText(this, "无可用字幕：在线字幕需平台提供（B站 CC / YouTube），本地可放同名 .srt", Toast.LENGTH_LONG).show()
            return
        }
        val popup = PopupMenu(this, btnSubtitle)
        acts.forEachIndexed { i, p -> popup.menu.add(0, i, i, p.first) }
        popup.setOnMenuItemClickListener { item ->
            acts.getOrNull(item.itemId)?.second?.invoke()
            true
        }
        popup.setOnDismissListener { showControls(autoHide = true) }
        showControls(autoHide = false)
        popup.show()
    }

    private fun setSubsEnabled(on: Boolean) {
        subsEnabled = on
        btnSubtitle?.text = if (on) "字幕" else "字幕关"
        subtitleView?.visibility = View.GONE
        lastCueKey = ""
        Toast.makeText(this, if (on) "字幕已开启" else "字幕已关闭", Toast.LENGTH_SHORT).show()
        showControls(autoHide = true)
    }

    /** 切回本地同名外挂字幕 */
    private fun loadLocalSubs(f: java.io.File) {
        playerScope.launch {
            val cues = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { Subtitles.load(f) }
            localCues = cues
            activeSubUrl = ""
            lastCueKey = ""
            subtitleView?.visibility = View.GONE
            if (cues.isEmpty()) {
                Toast.makeText(this@PlayerActivity, "本地字幕为空：${f.name}", Toast.LENGTH_SHORT).show()
            } else {
                subsEnabled = true
                btnSubtitle?.text = "字幕"
                Toast.makeText(this@PlayerActivity, "已加载本地字幕 ${f.name}（${cues.size} 行）", Toast.LENGTH_SHORT).show()
            }
            showControls(autoHide = true)
        }
    }

    /** 加载在线字幕（异步拉取 + 解析，复用 App 层字幕渲染） */
    private fun loadOnlineSubs(t: SubTrack) {
        val token = ++subLoadToken
        Toast.makeText(this, "加载字幕：${t.label}…", Toast.LENGTH_SHORT).show()
        playerScope.launch {
            val cues = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                runCatching { Repo.fetchSubtitleCues(t) }.getOrDefault(emptyList())
            }
            if (token != subLoadToken) return@launch        // 用户又选了别的轨，丢弃本次结果
            if (cues.isEmpty()) {
                Toast.makeText(this@PlayerActivity, "字幕加载失败或为空：${t.label}", Toast.LENGTH_SHORT).show()
                return@launch
            }
            localCues = cues
            activeSubUrl = t.url
            lastCueKey = ""
            subsEnabled = true
            btnSubtitle?.text = "字幕"
            subtitleView?.visibility = View.GONE
            Toast.makeText(this@PlayerActivity, "已加载 ${t.label}（${cues.size} 行）", Toast.LENGTH_SHORT).show()
        }
    }

    private fun showSpeedMenu() {
        val speeds = listOf(0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f)
        val popup = PopupMenu(this, btnSpeed)
        speeds.forEachIndexed { i, v ->
            popup.menu.add(0, i, i, (if (v == currentSpeed) "● " else "") + fmtSpeed(v))
        }
        popup.setOnMenuItemClickListener { item -> setSpeed(speeds[item.itemId]); true }
        popup.setOnDismissListener { showControls(autoHide = true) }
        showControls(autoHide = false)
        popup.show()
    }

    private fun setSpeed(v: Float) {
        currentSpeed = v
        mpvView.mpv.setPropertyDouble("speed", v.toDouble())
        btnSpeed.text = fmtSpeed(v)
    }

    // ---------- 清晰度 ----------

    private fun showQualityMenu() {
        if (qualitiesList.isEmpty()) return
        val popup = PopupMenu(this, btnQuality)
        qualitiesList.forEachIndexed { i, q ->
            popup.menu.add(0, i, i, (if (q.id == currentQualityId) "● " else "") + q.label)
        }
        popup.setOnMenuItemClickListener { item -> switchQuality(qualitiesList[item.itemId]); true }
        popup.setOnDismissListener { showControls(autoHide = true) }
        showControls(autoHide = false)
        popup.show()
    }

    private fun switchQuality(q: QualityOption) {
        if (q.id == currentQualityId) return
        val prevId = currentQualityId
        val prevLabel = btnQuality.text.toString()
        val pos = runCatching { mpvView.mpv.getPropertyDouble("time-pos") ?: 0.0 }.getOrDefault(0.0)
        // 换流会覆盖进度循环里的定位（实测切音质后从 0:00 重放），改为换流完成后由对齐器恢复
        pendingSeekSec = -1.0
        pendingReloadSeek = pos
        pendingSeekIsResume = false          // 切清晰度不弹"继续播放"提示
        currentQualityId = q.id
        btnQuality.text = q.label
        Toast.makeText(this, "切换到 ${q.label}", Toast.LENGTH_SHORT).show()
        if (q.url.isNotEmpty()) {
            reloadStream(q.url)              // YouTube：直链变体直接切
        } else if (isMusicPlatform(platform) && songId.isNotEmpty()) {
            Thread {                         // 音乐：按档位重新取流（失败则回滚）
                val s = kotlinx.coroutines.runBlocking { Repo.musicStreamAt(songId, platform, q.id) }
                runOnUiThread {
                    if (s == null) {
                        currentQualityId = prevId
                        btnQuality.text = prevLabel
                        Toast.makeText(this, "该音质暂不可用（VIP 或版权限制）", Toast.LENGTH_SHORT).show()
                        showControls(autoHide = true)
                    } else {
                        val raw = s.url
                        // 音乐流必须经本机代理转发，保持“无视 VPN 直连”
                        val proxied = StreamProxy.serveStream(raw) ?: raw
                        reloadStream(proxied)
                    }
                }
            }.start()
        } else if (q.qn > 0 && progressKey.isNotEmpty()) {
            Thread {                         // B站：按 qn 重新解析
                try {
                    val d = BiliApi.resolve(progressKey, q.qn)
                    val u = d.formats.firstOrNull()?.url
                    runOnUiThread {
                        if (u.isNullOrEmpty()) {
                            Toast.makeText(this, "该清晰度暂不可用", Toast.LENGTH_SHORT).show()
                        } else {
                            reloadStream(u)
                        }
                        showControls(autoHide = true)
                    }
                } catch (e: Exception) {
                    runOnUiThread {
                        Toast.makeText(this, "切换失败: ${e.message?.take(40)}", Toast.LENGTH_SHORT).show()
                        showControls(autoHide = true)
                    }
                }
            }.start()
        }
    }

    /**
     * 换流后恢复到指定位置。
     * reloadStream 后 mpv 是异步加载新流的，立即 seek 会被随后的加载覆盖
     * （实测音乐切音质后从 0:00 重放），因此在数秒内反复对齐，直到位置正确。
     * 用户手动拖动进度条会作废本次对齐（reloadSeekToken）。
     */
    private fun seekAfterReload(target: Double) {
        if (target <= 1.0) return
        val myToken = ++reloadSeekToken
        val deadline = System.currentTimeMillis() + 8000
        val tick = object : Runnable {
            override fun run() {
                if (myToken != reloadSeekToken) return          // 用户已手动拖动，放弃
                val m = mpvView.mpv
                val dur = runCatching { m.getPropertyDouble("duration") ?: 0.0 }.getOrDefault(0.0)
                val pos = runCatching { m.getPropertyDouble("time-pos") ?: 0.0 }.getOrDefault(0.0)
                val aligned = dur > 0 && pos > 0 && kotlin.math.abs(pos - target) < 2.5
                if (!aligned && dur > target + 3) {
                    runCatching { m.setPropertyDouble("time-pos", target) }
                    runCatching { m.setPropertyDouble("speed", currentSpeed.toDouble()) }   // 重载后恢复倍速
                    android.util.Log.i("HOV", "换流后对齐: 目标=%.1fs 当前=%.1fs".format(target, pos))
                }
                if (!aligned && System.currentTimeMillis() < deadline) handler.postDelayed(this, 350)
            }
        }
        handler.postDelayed(tick, 350)
    }

    /** 重新加载流（清晰度切换）：保持播放位置与倍速
     *  注意：lib 的 playFile() 只更新字段、loadfile 仅在 surfaceCreated 时执行，
     *  运行时换流必须直接发 mpv 命令（replace 模式）。 */
    private fun reloadStream(newUrl: String) {
        val seekTarget = pendingReloadSeek
        mpvView.mpv.command("loadfile", newUrl, "replace")
        if (currentAudioUrl.isNotEmpty()) {
            handler.postDelayed({
                mpvView.mpv.command("audio-add", currentAudioUrl, "select")
            }, 700)
        }
        handler.postDelayed({ applyFillMode() }, 900)
        seekAfterReload(seekTarget)
        showControls(autoHide = true)
    }

    // ---------- 控件显隐（不遮挡视频：播放中 3.5 秒自动淡出） ----------

    private fun showControls(autoHide: Boolean) {
        controlsShown = true
        handler.removeCallbacks(autoHideRunnable)
        controls.animate().cancel()
        controls.visibility = View.VISIBLE
        controls.animate().alpha(1f).setDuration(150).start()
        if (autoHide) {
            val paused = runCatching { mpvView.mpv.getPropertyBoolean("pause") }.getOrNull() ?: false
            if (!paused) handler.postDelayed(autoHideRunnable, 3500)
        }
    }

    private fun hideControls(animate: Boolean) {
        controlsShown = false
        handler.removeCallbacks(autoHideRunnable)
        controls.animate().cancel()
        if (animate) {
            controls.animate().alpha(0f).setDuration(200)
                .withEndAction { if (!controlsShown) controls.visibility = View.GONE }
                .start()
        } else {
            controls.alpha = 0f
            controls.visibility = View.GONE
        }
    }

    private fun toggleControls() {
        if (controlsShown) hideControls(true) else showControls(true)
    }

    // ---------- 全屏（强制横屏 + 沉浸式；手动旋转不触发，保持现状） ----------

    // ---------- 画中画（P4，视频平台） ----------

    /** 进入画中画（比例按视频宽高比，退化为 16:9） */
    private fun enterPip() {
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.O) return
        runCatching {
            val w = mpvView.mpv.getPropertyDouble("width") ?: 0.0
            val h = mpvView.mpv.getPropertyDouble("height") ?: 0.0
            val ratio = if (w > 1 && h > 1) {
                android.util.Rational(w.toInt().coerceAtMost(239), h.toInt().coerceAtMost(239))
            } else {
                android.util.Rational(16, 9)
            }
            val params = android.app.PictureInPictureParams.Builder()
                .setAspectRatio(ratio)
                .build()
            enterPictureInPictureMode(params)
        }.onFailure {
            Toast.makeText(this, "画中画不可用", Toast.LENGTH_SHORT).show()
        }
    }

    /** PiP 切换：小窗里隐藏全部控件/浮层，回到全屏再恢复 */
    override fun onPictureInPictureModeChanged(isInPictureInPictureMode: Boolean, newConfig: android.content.res.Configuration) {
        super.onPictureInPictureModeChanged(isInPictureInPictureMode, newConfig)
        inPip = isInPictureInPictureMode
        android.util.Log.i("HOV", "画中画: ${if (inPip) "进入" else "退出"}")
        if (inPip) {
            handler.removeCallbacksAndMessages(null)
            controls.visibility = View.GONE
            gestureOverlay?.visibility = View.GONE
            lyricsPanel?.visibility = View.GONE
        } else {
            controlsShown = false
            showControls(autoHide = true)
            if (lyricsShown) lyricsPanel?.visibility = View.VISIBLE
            startProgressLoop()
        }
    }

    /** 视频按 Home 键自动进小窗（音乐保持后台播放） */
    override fun onUserLeaveHint() {
        super.onUserLeaveHint()
        if (!isMusicPlatform(platform) && !inPip && android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
            enterPip()
        }
    }

    private fun toggleFullscreen() {
        isFullscreen = !isFullscreen
        if (isFullscreen) {
            requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
            setSystemBarsHidden(true)
            btnFullscreen.setImageResource(R.drawable.ic_fullscreen_exit)
            btnFullscreen.contentDescription = "退出全屏"
        } else {
            requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            setSystemBarsHidden(false)
            btnFullscreen.setImageResource(R.drawable.ic_fullscreen)
            btnFullscreen.contentDescription = "全屏"
        }
        applyFillMode()
        handler.postDelayed({ applyFillMode() }, 700)   // 旋转完成后按横屏尺寸重新判定
        showControls(autoHide = true)
    }

    /**
     * 显示模式：全屏且开启「铺满」时 mpv panscan=1（裁切填满，无左右黑边），否则 panscan=0（保持比例）。
     * 裁切倍率 = 屏幕宽高比 / 视频宽高比：
     *  ≤1    视频比屏幕更宽（黑边在上下）→ 不裁切，避免切掉画面两侧
     *  ≤1.4  轻微左右黑边（如 16:9 视频在 20:9 屏上，倍率 1.22）→ 裁切填满
     *  >1.4  竖版/窄视频 → 裁切会丢失大半画面，保持完整
     */
    private fun applyFillMode() {
        var fill = isFullscreen && Settings.fillScreen
        var reason = "全屏铺满"
        var detail = ""
        try {
            if (fill) {
                val vw = mpvView.mpv.getPropertyDouble("width") ?: 0.0
                val vh = mpvView.mpv.getPropertyDouble("height") ?: 0.0
                val sw = resources.displayMetrics.widthPixels.toDouble()
                val sh = resources.displayMetrics.heightPixels.toDouble()
                if (vw > 0 && vh > 0 && sw > 0 && sh > 0) {
                    val factor = (sw / sh) / (vw / vh)
                    detail = "视频 ${vw.toInt()}x${vh.toInt()} 屏 ${sw.toInt()}x${sh.toInt()} 倍率=%.2f".format(factor)
                    when {
                        factor <= 1.0 -> { fill = false; reason = "视频比屏幕宽，留上下黑边" }
                        factor > 1.4 -> { fill = false; reason = "竖版/窄视频不裁切" }
                        else -> reason = "全屏铺满"
                    }
                }
            } else {
                reason = if (!isFullscreen) "非全屏保持比例" else "设置关闭铺满"
            }
        } catch (e: Exception) {
            android.util.Log.w("HOV", "读取视频尺寸失败: ${e.message}")
        }
        runCatching { mpvView.mpv.command("set", "panscan", if (fill) "1.0" else "0.0") }
            .onFailure { android.util.Log.w("HOV", "设置 panscan 失败: ${it.message}") }
        android.util.Log.i("HOV", "显示模式: ${if (fill) "铺满" else "适应"} ($reason) $detail")
    }

    /** 长按全屏按钮：快速切「铺满 / 适应」（与设置页同一开关） */
    private fun toggleFillScreen() {
        Settings.fillScreen = !Settings.fillScreen
        applyFillMode()
        Toast.makeText(
            this,
            if (Settings.fillScreen) "全屏铺满已开启（裁切黑边）" else "全屏铺满已关闭（保持比例留黑边）",
            Toast.LENGTH_SHORT
        ).show()
    }

    private fun setSystemBarsHidden(hidden: Boolean) {
        val c = window.insetsController ?: return
        if (hidden) {
            c.hide(WindowInsets.Type.systemBars())
            c.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        } else {
            c.show(WindowInsets.Type.systemBars())
        }
    }

    /** 快进/快退（正数=快进，负数=快退）；按钮与双击手势共用 */
    // ---------- 歌词（P3） ----------

    /** 拉取当前曲目的歌词（音乐平台才有），异步填充面板 */
    private fun loadLyricsAsync() {
        lyricLines = emptyList()
        lyricIndex = -1
        lyricsShown = false
        lyricsPanel?.visibility = View.GONE
        btnLyrics?.visibility = View.GONE
        btnLyrics?.text = "歌词"
        rebuildLyricViews()
        val entry = PlayQueue.current ?: return
        if (!isMusicPlatform(entry.platform)) return
        playerScope.launch {
            val lines = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                Lyrics.fetch(entry.item, entry.platform)
            }
            if (PlayQueue.current != entry) return@launch      // 已切歌，丢弃
            lyricLines = lines
            rebuildLyricViews()
            btnLyrics?.visibility = if (lines.isEmpty()) View.GONE else View.VISIBLE
            android.util.Log.i("HOV", "歌词加载[${entry.platform}] ${entry.item.title.take(20)}: ${lines.size} 行")
        }
    }

    private fun rebuildLyricViews() {
        val box = lyricsBox ?: return
        box.removeAllViews()
        lyricViews.clear()
        for (line in lyricLines) {
            val tv = TextView(this).apply {
                text = line.text
                setTextColor(0xFF9E9E9E.toInt())
                textSize = 15f
                gravity = Gravity.CENTER
                setPadding(dp(6), dp(7), dp(6), dp(7))
            }
            box.addView(tv)
            lyricViews.add(tv)
        }
    }

    private fun toggleLyrics() {
        if (lyricLines.isEmpty()) {
            Toast.makeText(this, "暂无歌词", Toast.LENGTH_SHORT).show()
            return
        }
        lyricsShown = !lyricsShown
        lyricsPanel?.visibility = if (lyricsShown) View.VISIBLE else View.GONE
        btnLyrics?.text = if (lyricsShown) "收起" else "歌词"
        lyricIndex = -1                                  // 强制刷新高亮
        if (lyricsShown) syncLyrics(runCatching { mpvView.mpv.getPropertyDouble("time-pos") ?: 0.0 }.getOrDefault(0.0))
    }

    /** 播放进度 → 歌词高亮与自动滚动（进度循环每 500ms 调用） */
    private fun syncLyrics(posSec: Double) {
        if (!lyricsShown || lyricLines.isEmpty()) return
        val ms = (posSec * 1000).toLong()
        var idx = -1
        for (i in lyricLines.indices) {
            if (lyricLines[i].timeMs <= ms) idx = i else break
        }
        if (idx == lyricIndex) return
        lyricIndex = idx
        lyricViews.forEachIndexed { i, tv ->
            val cur = i == idx
            tv.setTextColor(if (cur) 0xFFFFFFFF.toInt() else 0xFF9E9E9E.toInt())
            tv.setTypeface(null, if (cur) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
        }
        if (idx in lyricViews.indices) {
            val panel = lyricsPanel ?: return
            val v = lyricViews[idx]
            panel.post { panel.smoothScrollTo(0, (v.top - panel.height / 2 + v.height / 2).coerceAtLeast(0)) }
        }
    }

    // ---------- 手势：亮度 / 音量 ----------

    /** 触摸总入口：先喂给 GestureDetector（单击/双击），再处理上下滑调节 */
    private fun handleTouch(event: MotionEvent) {
        gestureDetector.onTouchEvent(event)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                gestureStartX = event.x
                gestureStartY = event.y
                gestureMode = 0
                gestureActive = false
                gestureBaseBrightness = currentBrightness()
                gestureBaseVolume = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)
            }
            MotionEvent.ACTION_MOVE -> {
                if (!Settings.gesturesEnabled) return      // 设置里关掉手势调节
                val dy = gestureStartY - event.y                 // 上滑为正
                val dx = event.x - gestureStartX
                if (gestureMode == 0) {
                    // 竖直位移足够大且明显大于水平位移，才认定为上下滑
                    if (kotlin.math.abs(dy) > dp(24) && kotlin.math.abs(dy) > kotlin.math.abs(dx) * 1.2f) {
                        gestureMode = if (gestureStartX < mpvView.width / 2f) 1 else 2
                        gestureActive = true
                    }
                }
                if (gestureMode == 1) {
                    val ratio = dy / (mpvView.height * 0.7f)     // 约 70% 屏高 = 满量程
                    val v = (gestureBaseBrightness + ratio).coerceIn(0.02f, 1f)
                    setBrightness(v)
                    showGestureOverlay("亮度 ${(v * 100).toInt()}%")
                } else if (gestureMode == 2) {
                    val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC).coerceAtLeast(1)
                    val ratio = dy / (mpvView.height * 0.7f)
                    val v = (gestureBaseVolume + ratio * max).toInt().coerceIn(0, max)
                    if (v != audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)) {
                        audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, v, 0)
                    }
                    showGestureOverlay("音量 ${(v * 100 / max)}%")
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (gestureActive) {
                    android.util.Log.i("HOV", "手势: ${if (gestureMode == 1) "亮度" else "音量"}调节结束")
                    scheduleHideGestureOverlay()
                }
                gestureMode = 0
                gestureActive = false
            }
        }
    }

    private fun currentBrightness(): Float {
        val lp = window.attributes
        if (lp.screenBrightness >= 0f) return lp.screenBrightness
        val sys = try {
            android.provider.Settings.System.getInt(contentResolver, android.provider.Settings.System.SCREEN_BRIGHTNESS)
        } catch (e: Exception) {
            128
        }
        return (sys / 255f).coerceIn(0.02f, 1f)
    }

    private fun setBrightness(v: Float) {
        val lp = window.attributes
        lp.screenBrightness = v
        window.attributes = lp
    }

    private fun showGestureOverlay(text: String) {
        val ov = gestureOverlay ?: return
        handler.removeCallbacksAndMessages(null)   // 手势期间不动控件自动隐藏计时
        ov.animate().cancel()
        ov.alpha = 1f
        ov.text = text
        ov.visibility = View.VISIBLE
        showControls(autoHide = false)
    }

    private fun scheduleHideGestureOverlay() {
        val ov = gestureOverlay ?: return
        handler.postDelayed({
            ov.animate().alpha(0f).setDuration(250).withEndAction {
                ov.visibility = View.GONE
                ov.alpha = 1f
            }.start()
            showControls(autoHide = true)
        }, 600)
    }

    private fun seekBy(sec: Double) {
        val m = mpvView.mpv
        val dur = m.getPropertyDouble("duration") ?: 0.0
        android.util.Log.i("HOV", "seekBy $sec 当前pos=${m.getPropertyDouble("time-pos")} dur=$dur")
        if (dur <= 0) return
        val pos = m.getPropertyDouble("time-pos") ?: 0.0
        val target = (pos + sec).coerceIn(0.0, dur - 0.5)
        m.setPropertyDouble("time-pos", target)
        if (!userSeeking) seek.progress = (target / dur * 1000).toInt()
        timeLabel.text = "${fmt(target)} / ${fmt(dur)}"
        showControls(autoHide = true)
        Toast.makeText(
            this,
            if (sec > 0) "快进 ${sec.toInt()} 秒" else "快退 ${(-sec).toInt()} 秒",
            Toast.LENGTH_SHORT
        ).show()
    }

    /** 暂停/继续（按钮、媒体通知、耳机键共用入口） */
    private fun togglePlay() {
        val m = mpvView.mpv
        val paused = m.getPropertyBoolean("pause") ?: false
        m.setPropertyBoolean("pause", !paused)
        val nowPlaying = paused   // 之前是暂停态 → 现在开始播放
        if (nowPlaying) requestAudioFocus()
        PlaybackController.playing = nowPlaying
        PlaybackService.update(this, videoTitle, nowPlaying)
        btnPlay.setImageResource(if (nowPlaying) R.drawable.ic_pause else R.drawable.ic_play)
        if (!nowPlaying) saveProgressNow()    // 暂停即记忆
        showControls(autoHide = nowPlaying)   // 暂停时保持显示；播放时重新计时
    }

    private fun pausePlayback() {
        if (mpvView.mpv.getPropertyBoolean("pause") == false) {
            mpvView.mpv.setPropertyBoolean("pause", true)
            PlaybackController.playing = false
            PlaybackService.update(this, videoTitle, false)
        }
    }

    private fun resumePlayback() {
        if (mpvView.mpv.getPropertyBoolean("pause") == true) {
            mpvView.mpv.setPropertyBoolean("pause", false)
            PlaybackController.playing = true
            PlaybackService.update(this, videoTitle, true)
        }
    }

    /** 立即保存当前进度（暂停/退出时） */
    private fun saveProgressNow() {
        if (!Settings.resumePlayback) return
        if (progressKey.isEmpty()) return
        runCatching {
            val pos = mpvView.mpv.getPropertyDouble("time-pos") ?: 0.0
            val dur = mpvView.mpv.getPropertyDouble("duration") ?: 0.0
            if (dur > 0) {
                if (pos >= dur - 1) PlaybackHistory.clear(progressKey)
                else PlaybackHistory.save(progressKey, pos, videoTitle)
            }
        }
    }

    private fun requestAudioFocus() {
        val req = focusRequest ?: AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE)
                    .build()
            )
            .setWillPauseWhenDucked(false)
            .setOnAudioFocusChangeListener({ change -> onFocusChange(change) }, Handler(Looper.getMainLooper()))
            .build()
            .also { focusRequest = it }
        audioManager.requestAudioFocus(req)
    }

    /** 其它 App 抢焦点时的应对（回调在主线程） */
    private fun onFocusChange(change: Int) {
        when (change) {
            AudioManager.AUDIOFOCUS_LOSS -> {
                pausedByFocus = false
                pausePlayback()
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
                pausedByFocus = true
                pausePlayback()
            }
            AudioManager.AUDIOFOCUS_GAIN -> {
                if (pausedByFocus) {
                    pausedByFocus = false
                    resumePlayback()
                }
            }
            // AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK：不处理（不降音量，保持简单）
        }
    }

    // ---------- 播放队列（Phase 1：音乐 / 视频 / 本地通用） ----------

    private fun isMusicPlatform(p: String) = p == "netease" || p == "qqmusic"

    /** 播完：有下一项则自动续播，队列到头则退出播放器 */
    private fun onPlaybackEnded() {
        if (!playbackStarted || finishedHandled || switching) return
        finishedHandled = true
        switching = true
        if (!Settings.autoNext) {
            android.util.Log.i("HOV", "自动连播已关闭 → 播完退出")
            finish()
            return
        }
        saveProgressNow()
        PlaybackHistory.clear(progressKey)                       // 播完不留续播点
        if (PlaybackController.started) PlaybackService.update(this, videoTitle, false)
        playNext(auto = true)
    }

    /** 切歌：解析队列中的下一项；auto=true 表示"播完自动续播" */
    private fun playNext(auto: Boolean) {
        val entry = PlayQueue.peekNext(auto)
        if (entry == null) {
            finish()          // 队列到头（顺序模式）→ 正常退出
            return
        }
        playEntry(entry)
    }

    private fun playPrev() {
        val entry = PlayQueue.peekPrev()
        if (entry == null) {
            Toast.makeText(this, "已经是第一首", Toast.LENGTH_SHORT).show()
            return
        }
        playEntry(entry)
    }

    /** 异步解析并播放队列项（音乐走 resolveMusic，视频走 resolve，本地直读文件） */
    private fun playEntry(entry: PlayQueue.Entry) {
        Toast.makeText(this, "切换: ${entry.item.title.take(24)}", Toast.LENGTH_SHORT).show()
        playerScope.launch {
            try {
                val d: VideoDetail = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                    when (entry.platform) {
                        "local" -> VideoDetail(
                            title = entry.item.title, uploader = "", durationSec = 0.0, cover = "",
                            webpageUrl = entry.item.url,
                            formats = listOf(MediaFormat(entry.item.url, "", "", 0)), audioUrl = ""
                        )
                        "netease", "qqmusic" -> Repo.resolveMusic(entry.item, entry.platform)
                        else -> Repo.resolve(entry.item.url, entry.platform)
                    }
                }
                if (d.formats.isEmpty()) throw Exception("无可用播放格式")
                applyEntry(entry, d)
            } catch (e: Exception) {
                android.util.Log.w("HOV", "切歌失败: ${e.message}")
                switching = false
                Toast.makeText(this@PlayerActivity, "切歌失败: ${e.message?.take(60)}", Toast.LENGTH_LONG).show()
            }
        }
    }

    /** 切歌落地：更新界面状态并换流 */
    private fun applyEntry(entry: PlayQueue.Entry, d: VideoDetail) {
        PlayQueue.moveTo(entry)
        platform = entry.platform
        progressKey = entry.item.url
        videoTitle = d.title.ifEmpty { entry.item.title }
        titleLabel.text = videoTitle
        qualitiesList = d.qualities
        currentQualityId = d.currentQualityId
        currentAudioUrl = d.audioUrl
        btnQuality.text = qualitiesList.firstOrNull { it.id == currentQualityId }?.label ?: "画质"
        btnQuality.visibility = if (qualitiesList.isEmpty()) View.GONE else View.VISIBLE
        // B 站需要 Referer
        mpvView.referer = if (platform == "bilibili") "https://www.bilibili.com" else ""
        // 封面（音乐/本地）
        if (isMusicPlatform(platform) || platform == "local") {
            coverView?.let { cv ->
                if (d.cover.isNotEmpty()) {
                    cv.visibility = View.VISIBLE
                    cv.load(d.cover) { crossfade(true) }
                } else {
                    cv.visibility = View.GONE
                }
            }
        } else {
            coverView?.visibility = View.GONE
        }
        // 续播位置（上一首保存、这一首恢复）
        val saved = PlaybackHistory.positionSec(progressKey)
        if (Settings.resumePlayback && saved > 5) {
            pendingSeekSec = saved
            pendingSeekIsResume = true
        } else {
            pendingSeekSec = -1.0
            pendingSeekIsResume = false
        }
        saveTick = 0
        switching = false
        playbackStarted = false
        finishedHandled = false
        // 换流（音乐经本机代理，保持直连）
        val rawUrl = d.formats[0].url
        val playUrl = if (isMusicPlatform(platform)) StreamProxy.serveStream(rawUrl) ?: rawUrl else rawUrl
        reloadStream(playUrl)
        if (platform == "local") autoLoadSubtitle(rawUrl)
        PlaybackController.title = videoTitle
        if (PlaybackController.started) PlaybackService.update(this, videoTitle, PlaybackController.playing)
        loadLyricsAsync()          // 切歌后重新拉歌词
    }

    /** 循环模式切换（顺序 / 单曲 / 随机） */
    private fun cycleLoopMode() {
        PlayQueue.cycleMode()
        val m = PlayQueue.mode.value
        btnMode?.text = m.label
        Toast.makeText(this, "循环模式：${m.label}", Toast.LENGTH_SHORT).show()
    }

    /** 队列面板：查看/跳转 */
    private fun showQueueDialog() {
        val entries = PlayQueue.entries.value
        if (entries.isEmpty()) {
            Toast.makeText(this, "队列为空", Toast.LENGTH_SHORT).show()
            return
        }
        val names = entries.map {
            (if (isMusicPlatform(it.platform)) "♪ " else "") + it.item.title
        }.toTypedArray()
        android.app.AlertDialog.Builder(this)
            .setTitle("播放队列（${entries.size}）")
            .setSingleChoiceItems(names, PlayQueue.index.value) { dlg, which ->
                dlg.dismiss()
                if (which == PlayQueue.index.value) {
                    Toast.makeText(this, "正在播放", Toast.LENGTH_SHORT).show()
                } else {
                    switching = true
                    playEntry(entries[which])
                }
            }
            .setNegativeButton("关闭", null)
            .show()
    }

    private fun startProgressLoop() {
        handler.postDelayed(object : Runnable {
            override fun run() {
                val m = mpvView.mpv
                val pos = m.getPropertyDouble("time-pos") ?: 0.0
                val dur = m.getPropertyDouble("duration") ?: 0.0

                // 进度恢复 / 切清晰度后的定位（等 duration 就绪）
                if (pendingSeekSec > 0 && dur > 0) {
                    val target = pendingSeekSec
                    val isResume = pendingSeekIsResume
                    pendingSeekSec = -1.0
                    pendingSeekIsResume = false
                    m.setPropertyDouble("speed", currentSpeed.toDouble())   // 重载后恢复倍速
                    if (target < dur - 3) {
                        m.setPropertyDouble("time-pos", target)
                        if (isResume) Toast.makeText(this@PlayerActivity, "已从 ${fmt(target)} 继续播放", Toast.LENGTH_SHORT).show()
                    } else if (isResume) {
                        PlaybackHistory.clear(progressKey)                  // 接近结尾：记录作废
                    }
                }

                // 定时保存进度（每 5 秒；可在设置里关闭进度记忆）
                saveTick++
                if (Settings.resumePlayback && saveTick % 10 == 0 && progressKey.isNotEmpty() && dur > 0) {
                    if (pos >= dur - 1) PlaybackHistory.clear(progressKey)
                    else PlaybackHistory.save(progressKey, pos, videoTitle)
                }
                if (dur > 0 && pendingSubtitleFile != null) attachPendingSubtitle()
                if (dur > 1 && pos > 0.5) playbackStarted = true
                // 兜底：个别情况事件会丢，轮询 mpv 标志位再确认一次
                if (playbackStarted && !finishedHandled && !switching &&
                    (m.getPropertyBoolean("eof-reached") == true || m.getPropertyBoolean("idle-active") == true)
                ) {
                    onPlaybackEnded()
                    handler.postDelayed(this, 500)
                    return
                }
                if (dur > 0 && !userSeeking) seek.progress = (pos / dur * 1000).toInt()
                syncLyrics(pos)
                syncSubtitleOverlay(pos)
                timeLabel.text = "${fmt(pos)} / ${fmt(dur)}"
                btnPlay.setImageResource(
                    if (m.getPropertyBoolean("pause") == true) R.drawable.ic_play else R.drawable.ic_pause
                )

                handler.postDelayed(this, 500)
            }
        }, 500)
    }

    private fun fmt(sec: Double): String {
        val s = sec.toInt()
        return "%d:%02d:%02d".format(s / 3600, (s % 3600) / 60, s % 60)
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (isFullscreen) toggleFullscreen() else finish()
    }

    override fun onDestroy() {
        saveProgressNow()
        playerScope.cancel()
        handler.removeCallbacksAndMessages(null)
        PlaybackController.listener = null
        PlaybackService.stop(this)
        focusRequest?.let { audioManager.abandonAudioFocusRequest(it) }
        mpvView.destroy()
        super.onDestroy()
    }
}
