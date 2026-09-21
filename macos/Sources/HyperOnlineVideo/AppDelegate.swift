import AppKit
import Cmpv

/// macOS 端主控制器：窗口 + libmpv 渲染 + 网络层 + 界面接线
final class AppDelegate: NSObject, NSApplicationDelegate {
    // 界面
    var window: NSWindow!
    var player: MpvView!
    let sourcePopup = NSPopUpButton()
    let searchField = NSSearchField()
    // 结果列表：NSCollectionView 卡片流（自适应多列 = 瀑布流；窄了自动单列）
    let resultsGrid = ResultsGridView()
    let resultsScroll = NSScrollView()
    var prevButton: NSButton!, playButton: NSButton!, nextButton: NSButton!
    var seekSlider: NSSlider!, timeLabel: NSTextField!, speedPopup: NSPopUpButton!, volumeSlider: NSSlider!
    var qualityPopup: NSPopUpButton!       // B1：质量下拉 —— 放视频=清晰度，放音乐=音质（用户实测反馈）
    var qualityWidth: NSLayoutConstraint?  // 音质标签更长（"较高 320k"），要能动态放宽
    var topBarView: NSStackView!           // B5：画中画时隐藏
    var controlsBarView: NSStackView!      // B5：画中画时隐藏
    var pipBtn: NSButton!                  // B5：画中画开关
    var loginButton: NSButton?             // 登录按钮（登录状态变化时刷新文案）
    // 分页状态（瀑布流：滚到底自动加载下一页）
    var searchQuery = ""
    var searchSourceLabel = ""
    var searchPage = 1
    var searchHasMore = true
    var searchLoadingMore = false
    var searchRequestId = 0               // 搜索代次：异步结果回来时校验，防'切来源后旧结果落地'
    var lastSearchBySource: [String: String] = [:]   // 每个来源记住上次的搜索词（切回去自动重现）
    var searchSort = 1                   // 搜索过滤器（与 Android SearchFilters 对齐）：1=相关度 2=最新 3=播放最多
    var searchDuration = 0               // 0=全部 1=短视频 2=中等 3=长篇
    // 连播 / 纯视频全屏
    var autoNextHandled = false            // 防止一次结尾触发多次
    var resolveTick: Timer?                // 解析中的秒数提示
    var playRequestId = 0                  // 每次播放请求的序号（丢弃过期解析结果，避免「后点的被先点的顶掉」）
    var lastEofState = false               // eof 状态（只在翻转时打日志）
    var videoFullscreen = false            // 纯视频全屏（隐藏界面，鼠标靠近边缘弹出浮层）
    // ── buildUi() 状态提升（审计 #166 重启版·第 1 步 ✓ 只提升状态 ✗ 不搬代码 ✓）──
    //   原来这些是 buildUi 的局部变量 ✓ 被多个分区交叉引用 ✓ 导致"整段外迁"必须临时补参数 ✗
    //   先提升为属性（本步单独验证 ✓）→ 下一步再搬迁分区 ✓（两步分开 = 本轮定的流程 ✓）
    var topBarStack: NSStackView!          // 顶部栏容器
    var playerBoxStack: NSStackView!       // 播放器容器
    var fullscreenBtn: NSButton!           // 全屏按钮
    var subtitleToggleBtn: NSButton!       // 字幕开关
    var subtitleBtn: NSButton?             // 控制条「字幕」开关
    var autoNextBtn: NSButton!             // 控制条「连播」开关（改 IUO：buildUi 里必然赋值 ✓ if let 依旧可用 ✓）
    var fsHintLabel: NSTextField?          // 进入全屏时的提示（几秒后消失）
    var hoverTimer: Timer?                 // 全屏时的鼠标位置轮询
    var middleStack: NSStackView?          // 结果区原来的父栈（退出全屏要放回）
    var controlsParent: NSStackView?       // 控制条原来的父栈
    var middleSplit: CleanSplitView!       // 左结果区 / 右播放器的分隔视图（IUO ✓ 同上）
    var playerBoxView: NSStackView?        // 右侧播放器栏（退出全屏时按原顺序插回结果区）
    // 第二个抓帧槽（用于"操作前/操作后"各一张截图对比，自动化验证布局变化）
    var dumpFramePathB = ""
    var dumpAtB = 0.0
    var dumpedB = false
    var listPaneWidth: CGFloat = 0         // 进全屏前记下的结果区宽度（退出时按原样恢复）
    var restoringPaneWidth = false         // 正在恢复宽度（避免重排回调把它当"用户拖动"）
    var paneWidthSaveWork: DispatchWorkItem?
    var paneWidthInitialized = false       // 初始宽度已应用（此前的重排回调不算用户拖动）
    var fsTransitioning = false            // 全屏进出过渡中（重排不算用户拖动）
    var listOverlayVisible = false         // 全屏时左侧列表浮层是否展开（迟滞状态）
    var glassPanel: NSVisualEffectView?    // 全屏时的毛玻璃列表面板
    /// 全屏时控制条浮层离底部的距离（pt）：避开 macOS Dock 弹出区（鼠标触底会顶出 Dock）—— 用户实测反馈要更高
    let fsBarBottomInset: CGFloat = 80
    var barOverlayVisible = false          // 全屏时底部控制条浮层是否展开
    var initialFiles: [String] = []        // --open 支持多个文件（多文件连播）
    var lastFillApplied = true             // B7：上次下发的铺满状态（初值 true → 启动首拍必落一次 panscan=0 基线，便于取证）
    var settingsPanel: SettingsPanel?      // B6
    var favoritesPanel: FavoritesPanel?    // B6
    var queuePanel: QueuePanel?            // B6
    var pipActive = false
    var prePipFrame: NSRect = .zero
    var statusLabel: NSTextField!
    var filterButton: NSButton!             // 搜索过滤器入口（排序 + 时长）
    var modeButton: NSButton!               // 播放模式按钮（顺序/单曲/随机/列表循环）
    var volumeIcon: NSImageView!            // 音量小喇叭（第二排视觉分组）
    var filterWindow: NSWindow?             // 过滤器面板
    var filterSortBtns: [NSButton] = []     // 排序单选
    var filterDurBtns: [NSButton] = []      // 时长单选
    var ticker: Timer?
    var logLines: [String] = []

    // 逻辑层（与另两端共用配置文件）
    let settings = Settings()
    let progress = ProgressStore()
    let favorites = Favorites()
    let queue = PlayQueue()
    let resolver = UrlResolver()
    var mediaKeys: MediaKeys!
    var trackIndex = -1
    var tickCount = 0
    var playResultIndex = 0        // --play-index：播放第 N 条搜索结果
    var playPlatform = ""          // 当前播放内容所属平台（状态栏显示）
    var sessionAudioLevel = ""     // 会话音质档位（空=跟随设置；Q 键 / --quality 会改）
    let dl = DownloadManager()                // B3：下载管理器
    var lastStreamUrl = ""                    // 当前播放直链（下载按钮用）
    var lastAudioUrl = ""                     // 当前播放的**音轨**直链（YouTube DASH 分离音轨：下载必须一起取，否则没声音）

    var lastSeekAt = Date.distantPast         // 最近一次用户拖动进度条的时间（防 tick 覆盖滑块）
    var lastLRC = ""                         // 当前歌词原文（LRC；下载音乐时写成同名 .lrc 侧车文件）
    var thumbCache: [String: NSImage] = [:]   // B4：缩略图内存缓存
    var thumbLoaded = 0                       // 已加载缩略图计数（自动化判据）
    var playNextAfter: Double = 0  // --play-next-after：N 秒后自动换下一条（复现快速切歌竞态）
    var playKey = "", playLabel = ""

    // ── 直链过期自愈用（与 Android/Linux 三端同法；存储属性必须放在**类体**里，
    //    不能放 extension —— Swift 报 "extensions must not contain stored properties"）──
    var hovHealLastResolvedAt: Date?
    var hovHealTried = false

    /// 内存趋势探针（每 5 分钟一行）：用于判断 mpv libmpv 的 glFenceSync 泄漏在 macOS 上是否累积。
    /// 背景：同一上游缺陷（mpv #17217）在 Linux+virgl 下每帧漏 1 个**内核 fd**（已修）；
    /// macOS/Apple GL 下 GLsync 只是 GL 对象、不占 fd → 只会体现为**内存/对象缓慢增长**，
    /// 需实测确认是否值得移植同一套 shim（方案 A：先观测再决定）。
    func startMemoryProbe() {
        Timer.scheduledTimer(withTimeInterval: 300, repeats: true) { _ in
            var info = mach_task_basic_info()
            var count = mach_msg_type_number_t(MemoryLayout<mach_task_basic_info>.size / MemoryLayout<natural_t>.size)
            let kr = withUnsafeMutablePointer(to: &info) { ptr in
                ptr.withMemoryRebound(to: integer_t.self, capacity: Int(count)) { ip in
                    task_info(mach_task_self_, task_flavor_t(MACH_TASK_BASIC_INFO), ip, &count)
                }
            }
            let mb = kr == KERN_SUCCESS ? Double(info.resident_size) / 1048576.0 : -1
            Config.log(String(format: "[MEM] 常驻内存 %.1f MB（若持续上涨，说明 GLsync 在累积 → 需移植 fence shim）", mb))
        }
    }
    var lastProgressSave: Int64 = 0, lastProgressFlush: Int64 = 0
    var musicCovers: [String: String] = [:]      // key → 封面地址（搜索时顺手记录）
    var musicLabels: [String: String] = [:]      // key → 标题（播放时用）
    var rows: [Row] = []                         // 列表数据（结果 + 日志混排）

    // CLI / 自检
    var initialTarget: String?, initialAudio: String?, initialLabel: String?
    private var started = Date()
    private var target = ""
    private var exitAfter: Double = 0
    var selfTest = false            // internal：Automation.swift（同模块扩展）需要读它 ✓
    private var maxPosition: Double = 0
    private var dumpFramePath = ""
    private var dumpAt: Double = 2.5
    private var dumped = false
    private var dumpUiTextPath = ""
    private var dumpGlPath = ""

    func applicationDidFinishLaunching(_ notification: Notification) {
        buildMainMenu()      // 标准 macOS 菜单（⌘Q 退出 / ⌘, 设置 / ⌘O 打开 / ⌘W 关闭 / ⌘M 最小化…）
        // 登录窗口写入 cookie、或用户从浏览器切回本应用时，刷新登录按钮文案
        NotificationCenter.default.addObserver(forName: LoginWindow.cookiesChanged, object: nil,
                                               queue: .main) { [weak self] _ in
            self?.refreshLoginBadge()
        }
        let args = CommandLine.arguments
        if let i = args.firstIndex(of: "--open"), i + 1 < args.count {
            var files: [String] = []
            for j in (i + 1)..<args.count {
                if args[j].hasPrefix("--") { break }
                files.append(args[j])
            }
            if files.count > 1 { initialFiles = files } else { target = files.first ?? target }
        }
        if let t = initialTarget, !t.isEmpty { target = t }
        if let i = args.firstIndex(of: "--exit-after"), i + 1 < args.count { exitAfter = Double(args[i + 1]) ?? 0 }
        if let i = args.firstIndex(of: "--dump-frame"), i + 1 < args.count { dumpFramePath = args[i + 1] }
        if let i = args.firstIndex(of: "--dump-frame-b"), i + 1 < args.count { dumpFramePathB = args[i + 1] }
        if let i = args.firstIndex(of: "--dump-at-b"), i + 1 < args.count { dumpAtB = Double(args[i + 1]) ?? 0 }
        if let i = args.firstIndex(of: "--dump-at"), i + 1 < args.count { dumpAt = Double(args[i + 1]) ?? 2.5 }
        if let i = args.firstIndex(of: "--dump-ui-text"), i + 1 < args.count { dumpUiTextPath = args[i + 1] }
        if let i = args.firstIndex(of: "--dump-gl"), i + 1 < args.count { dumpGlPath = args[i + 1] }
        if let i = args.firstIndex(of: "--login"), i + 1 < args.count {
            let site = args[i + 1]
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
                LoginWindow.open(site: site)
                DispatchQueue.main.asyncAfter(deadline: .now() + 12) {
                    LoginWindow.shared?.dumpCookies { n in
                        Config.log("[SELFTEST] 登录 cookie: site=\(site) 条数=\(n) 文件=\(Config.cookiePath(site))")
                    }
                }
            }
        }
        if let i = args.firstIndex(of: "--login-probe"), i + 1 < args.count {
            let site = args[i + 1]
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
                let w = LoginWindow.open(site: site)
                w.probe { text in
                    Config.log("[PROBE] site=\(site) 可见文本: \(text)")
                }
            }
        }
        // 自动化探针（已拆分到 Automation.swift ✓ 见该文件头注释）
        installAutomationProbes(args)


        // 整窗毛玻璃：内容区透明（由 NSVisualEffectView 模糊桌面）。
        // 注意：**标题栏不要透明** —— 透明后标题会与内容区挤在一起（用户实测反馈）
        window.isOpaque = false
        window.backgroundColor = .clear
        window.titlebarAppearsTransparent = false
        buildUi()
        mediaKeys = MediaKeys(player: player)
        mediaKeys.delegate = self
        mediaKeys.setup()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        started = Date()

        // 界面自动化：--source N 预选来源；--query <kw> 自动搜索；--dump-ui <png> 抓整窗
        if let i = args.firstIndex(of: "--source"), i + 1 < args.count, let n = Int(args[i + 1]), n >= 0, n <= 4 {
            sourcePopup.selectItem(at: n)
            if n == 4 { showLocalLibrary() }        // 4=本地库（原来写成 3=QQ音乐，--source 4 直接越界被忽略）
        }
        if let i = args.firstIndex(of: "--play-index"), i + 1 < args.count,
           let n = Int(args[i + 1]) { playResultIndex = n }
        if let i = args.firstIndex(of: "--play-next-after"), i + 1 < args.count,
           let sec = Double(args[i + 1]) { playNextAfter = sec }
        if let i = args.firstIndex(of: "--query"), i + 1 < args.count {
            searchField.stringValue = args[i + 1]
            onSearch()
            if args.contains("--play-first") || playResultIndex > 0 || playNextAfter > 0 {
                DispatchQueue.main.asyncAfter(deadline: .now() + 4) { [weak self] in
                    guard let self else { return }
                    let results = self.rows.filter { !$0.key.isEmpty }
                    guard results.count > self.playResultIndex else { return }
                    let r = results[self.playResultIndex]
                    self.syncQueueFromRows(playIndex: self.rows.firstIndex(where: { $0.key == r.key }))
                    self.playResult(key: r.key, label: r.text)
                    if self.playNextAfter > 0 {                 // 复现"快速切歌"：N 秒后换下一条
                        DispatchQueue.main.asyncAfter(deadline: .now() + self.playNextAfter) {
                            guard let next = results.dropFirst(self.playResultIndex + 1).first else { return }
                            Config.log("--- 自动切到下一条：\(next.text) ---")
                            self.playResult(key: next.key, label: next.text)
                        }
                    }
                }
            }
        }
        // B3：下载管理器接线（进度写进结果列表，与另两端一致；完成/失败都有明确提示）
        dl.setDir(settings.string("download.dir"))
        dl.log = { [weak self] msg in
            DispatchQueue.main.async { self?.appendLog(msg) }
        }
        dl.onUpdate = { [weak self] job in
            guard let self else { return }
            if job.state == .failed { self.setStatus("下载失败：\(job.title)") }
        }
        if let i = args.firstIndex(of: "--download"), i + 1 < args.count {
            let url = args[i + 1]
            let nm = (i + 2 < args.count && !args[i + 2].hasPrefix("--")) ? args[i + 2] : ""
            _ = dl.enqueue(url: url, title: nm.isEmpty ? url : nm, fileNameHint: nm)
        }
        if args.contains("--cleanup-downloads") {
            let mb = args.firstIndex(of: "--max-total-mb").flatMap { idx -> Int64? in
                (idx + 1 < args.count) ? Int64(args[idx + 1]) : nil
            } ?? Int64(settings.number("download.maxSizeMb", 2048))
            let r = dl.cleanupLru(maxBytes: mb * 1024 * 1024)
            Config.log("[SELFTEST] 下载目录清理: \(r.beforeBytes/1048576)MB → \(r.afterBytes/1048576)MB，删除 \(r.removed.count) 个（上限 \(mb)MB）")
            exit(0)
        }
        startFinishWatch()      // 抓图/自检/自动退出的统一心跳（不依赖"窗口激活"事件）
        startMainThreadWatchdog()
        startMemoryProbe()            // 方案A：内存趋势观测（判断 GLsync 是否累积）
        // 播放中 → 下载并发降到 1（别和流媒体抢带宽/连接；用户实测：后台下载时切歌会卡很久）
        dl.isPlaying = { [weak self] in
            guard let self, let p = self.player else { return false }
            return p.duration() > 1 && !p.paused()
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) { [weak self] in
            self?.applySavedPaneWidth()             // 宽度：设置优先，其次默认 38%（拖动后会记住）
        }
        if !initialFiles.isEmpty {
            openFiles(initialFiles)              // --open a b c：多文件连播（列表即队列，播完自动下一条）
        } else if !target.isEmpty, initialAudio == nil || initialAudio!.isEmpty {
            // 网页 URL 必须走解析（否则 mpv 会拿到一个 HTML 页面 —— Qt 端踩过同一个坑）
            playResult(key: target, label: initialLabel ?? target)
        } else if !target.isEmpty {
            playKey(target: target, label: initialLabel ?? target, audio: initialAudio)   // 已解析好的直链
        }
        else { setStatus("就绪 · 来源 \(sourcePopup.titleOfSelectedItem ?? "-")") }
    }

    /// 统一起播入口（与 Qt 端的 playItem/playResolved 对应）
    /// - Parameters:
    ///   - target: 真正交给播放器的地址（本地路径或**直链**；直链会过期）
    ///   - contentKey: 内容标识（netease:<id> / qq:<mid> / 网页 URL / 本地路径）。
    ///     进度记忆与封面守卫都必须用它 —— 早期误用直链当键，导致"封面永远不更新 + 进度记不住"两个 bug。
    /// CLI 用：直接设置会话档位（在动作分发前同步生效）
    func wSwitchAudioQuality(level: String) { switchAudioQuality(to: level) }

    func playKey(target: String, label: String, audio: String? = nil, contentKey: String? = nil) {
        _ = progress.save()                       // 切内容前先把上一项落盘
        playKey = contentKey ?? target
        if target.hasPrefix("http") { lastStreamUrl = target }      // 下载按钮用
        lastAudioUrl = audio ?? ""                                  // 音轨直链（下载时与视频流一起封装）
        if playPlatform.isEmpty { followSource(UrlResolver.serviceOf(target)) }
        playLabel = label
        lastProgressSave = 0
        player.overlay.cues = []
        player.tracks = []
        Config.log("[SUB] 播放前清空字幕轨（新内容；在线字幕会在开播之后再挂载）")
        trackIndex = -1
        player.setCoverImage(nil)        // 切内容先清封面（否则没有封面的曲目/视频会留着上一首的封面）
        player.musicTitle = ""           // 标题同理：音乐界面右栏的大字标题不能带到下一项
        refreshQualityPopup()            // 质量下拉：音乐显示音质、视频显示清晰度（切歌/切视频时跟着换）
        // 本地文件：自动挂同名外挂字幕（中文优先）
        if target.hasPrefix("/"), FileManager.default.fileExists(atPath: target) {
            let tracks = Subtitles.findSidecarTracks(target)
            player.tracks = tracks
            if !tracks.isEmpty { applyTrack(0) }
            let resume = settings.bool("playback.rememberProgress", true) ? progress.resumePos(target) : 0
            player.playResolved(video: target, audio: "")
            if resume > 0.5 { resumeAfterLoad(resume) }
            setStatus("正在播放：\(label)")
            return
        }
        player.playResolved(video: target, audio: audio ?? "")
        let resume = settings.bool("playback.rememberProgress", true) ? progress.resumePos(target) : 0
        if resume > 0.5 { resumeAfterLoad(resume) }
        setStatus("正在播放：\(label)")
    }

    /// 续播：等媒体加载完（duration 就绪且还没走）再 seek —— 与 Qt 端同一条经验
    func resumeAfterLoad(_ sec: Double) {   // 扩展（UiActions）里也要用，故不是 private
        var tries = 0
        Timer.scheduledTimer(withTimeInterval: 0.3, repeats: true) { [weak self] t in
            guard let self else { t.invalidate(); return }
            tries += 1
            if self.player.duration() >= 1, self.player.position() < 1.0 {
                self.player.seek(to: sec)
                self.setStatus("从 \(Self.clock(sec)) 继续播放")
                t.invalidate()
            } else if tries > 30 || self.player.position() > sec + 5 {
                t.invalidate()
            }
        }
    }

    // MARK: - 定时收尾（自检 / 抓帧 / 自动退出）

    // MARK: B7 全屏铺满
    /// 期望状态：全屏 && 设置开启（默认开）→ panscan=1
    var wantFill: Bool {
        window.styleMask.contains(.fullScreen) && settings.bool("video.fillScreen", true)
    }

    /// 把 panscan 调到与期望一致（只在变化时动，避免每 0.5 秒重复下发）
    func applyFillMode(force: Bool = false) {
        let want = wantFill
        if !force, want == lastFillApplied { return }
        lastFillApplied = want
        player.setPanscan(want ? 1.0 : 0.0)
        let actual = player.panscan
        Config.log(String(format: "[SELFTEST] 全屏铺满：期望=%@ panscan回读=%.2f（全屏=%@ 设置=%@）",
                          want ? "开" : "关", actual,
                          window.styleMask.contains(.fullScreen) ? "是" : "否",
                          settings.bool("video.fillScreen", true) ? "开" : "关"))
        setStatus(want ? "铺满模式（裁切黑边，回读 panscan=\(String(format: "%.1f", actual))）" : "保持比例（panscan=0）")
    }

    /// 主线程卡顿看门狗：后台每秒 Ping 一次主队列，超过 1.5 秒才响应就记一条日志。
    /// 目的：用户报"卡死很久"时，能直接从 app.log 看到卡了多久、什么时间卡的，不靠猜。
    func startMainThreadWatchdog() {
        DispatchQueue.global(qos: .utility).async {
            var lastLog = Date.distantPast
            while true {
                Thread.sleep(forTimeInterval: 1.0)
                let t0 = Date()
                let sem = DispatchSemaphore(value: 0)
                DispatchQueue.main.async { sem.signal() }
                _ = sem.wait(timeout: .now() + 5.0)
                let wait = Date().timeIntervalSince(t0)
                guard wait > 1.5 else { continue }
                if Date().timeIntervalSince(lastLog) < 5 { continue }      // 持续卡顿时不刷屏
                lastLog = Date()
                let txt = String(format: "[WATCHDOG] 主线程卡顿 %.1f 秒（下载/嵌标签/清理时应为 0）", wait)
                DispatchQueue.main.async { Config.log(txt) }
            }
        }
    }

    /// Z 键：临时开关"全屏时铺满"（同时写入设置，与面板/Android 同键）
    func toggleFillScreen() {
        let now = settings.bool("video.fillScreen", true)
        _ = settings.apply(key: "video.fillScreen", value: now ? "false" : "true")
        applyFillMode(force: true)
    }

    func tickCommon() {
        applyFillMode()      // B7：进出全屏/改设置后自动跟随
        maxPosition = max(maxPosition, player.position())
        // 触发条件：有媒体时按"播放位置"，纯界面场景按"启动后秒数"（否则永远不触发）
        let dumpReady = player.duration() >= 0.5 ? player.position() > dumpAt
                                                 : Date().timeIntervalSince(started) > dumpAt
        if !dumped, (!dumpFramePath.isEmpty || !dumpGlPath.isEmpty || !dumpUiTextPath.isEmpty), dumpReady {
            if dumpFramePath.isEmpty {
                dumped = true
            } else {
                dumped = dumpUi(to: dumpFramePath)
                appendLog(String(format: "抓帧%@: %@（pos=%.1f 帧数=%d）", dumped ? "成功" : "失败", dumpFramePath,
                                 player.position(), player.renderedFrames))
            }
            if !dumpGlPath.isEmpty {
                let glOk = player?.dumpFrame(to: dumpGlPath) ?? false
                appendLog("GL 帧已导出(\(glOk ? "成功" : "失败")): \(dumpGlPath)")
            }
            if !dumpUiTextPath.isEmpty, dumpUiText(to: dumpUiTextPath) {
                appendLog("界面状态已导出: \(dumpUiTextPath)")
            }
        }
        // 第二个抓帧槽（--dump-frame-b / --dump-at-b）：对比"操作前/操作后"的界面
        // 第二槽按"启动后秒数"判断（不受播放位置影响，方便做"操作前/操作后"对比）
        if !dumpedB, !dumpFramePathB.isEmpty, Date().timeIntervalSince(started) > dumpAtB {
            dumpedB = dumpUi(to: dumpFramePathB)
            appendLog("二次抓帧\(dumpedB ? "成功" : "失败"): \(dumpFramePathB)")
        }
        if exitAfter > 0, Date().timeIntervalSince(started) >= exitAfter { finish() }
    }

    private var finishTimer: Timer?

    /// 把设置里影响运行时的项立即生效（面板保存后调用）
    func applySettingsRuntime() {
        settings.load()
        player?.overlay.fontScale = settings.number("subtitle.fontScale", 1.0)
        UrlResolver.maxHeight = Int(settings.number("video.maxHeight", 0))
        UrlResolver.cookiesFromBrowser = settings.string("network.cookiesFromBrowser")
        refreshKaraokeFlag()      // 只重算，不直接赋值（普通字幕必须是贴底布局）
        dl.setDir(settings.string("download.dir"))
        refreshQualityPopup()            // 音质上限改了 → 下拉里的档位/选中项跟着变
        ThemeController.shared.applyFromSettings(settings)   // ui.theme 改动立即生效 ✓（无需重启）
        setStatus("设置已应用（字号 \(String(format: "%.2f", settings.number("subtitle.fontScale", 1.0)))，清晰度 \(UrlResolver.qualityLabel(UrlResolver.maxHeight))）")
    }

    // MARK: B6 面板
    func openSettingsPanel() {
        if settingsPanel == nil { settingsPanel = SettingsPanel() }
        settingsPanel?.show()
    }
    func openFavoritesPanel() {
        if favoritesPanel == nil { favoritesPanel = FavoritesPanel(); favoritesPanel?.app = self }
        favoritesPanel?.show()
    }
    func openQueuePanel() {
        if queuePanel == nil { queuePanel = QueuePanel(); queuePanel?.app = self }
        queuePanel?.show()
    }

    // MARK: - 主菜单（Native macOS 标准快捷键）
    /// 说明：SwiftPM 启动的 AppKit 应用没有 nib，`NSApp.mainMenu` 是空的 ——
    /// 所以 ⌘Q / ⌘, 这类系统级快捷键默认都不存在（用户实测反馈）。这里手工建一套。
    func buildMainMenu() {
        let name = "聚合视频"
        let main = NSMenu()

        // ---- 应用菜单 ----
        let appItem = NSMenuItem()
        main.addItem(appItem)
        let appMenu = NSMenu(title: name)
        appItem.submenu = appMenu
        appMenu.addItem(withTitle: "关于 \(name)", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
        appMenu.addItem(.separator())
        let miSettings = NSMenuItem(title: "设置…", action: #selector(menuSettings), keyEquivalent: ",")
        miSettings.target = self
        appMenu.addItem(miSettings)
        appMenu.addItem(.separator())
        let servicesItem = NSMenuItem(title: "服务", action: nil, keyEquivalent: "")
        let servicesMenu = NSMenu(title: "服务")
        servicesItem.submenu = servicesMenu
        NSApp.servicesMenu = servicesMenu
        appMenu.addItem(servicesItem)
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "隐藏 \(name)", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
        let hideOthers = NSMenuItem(title: "隐藏其他", action: #selector(NSApplication.hideOtherApplications(_:)), keyEquivalent: "h")
        hideOthers.keyEquivalentModifierMask = [.command, .option]
        appMenu.addItem(hideOthers)
        appMenu.addItem(withTitle: "全部显示", action: #selector(NSApplication.unhideAllApplications(_:)), keyEquivalent: "")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "退出 \(name)", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")

        // ---- 文件菜单 ----
        let fileItem = NSMenuItem()
        main.addItem(fileItem)
        let fileMenu = NSMenu(title: "文件")
        fileItem.submenu = fileMenu
        let miOpen = NSMenuItem(title: "打开本地文件…", action: #selector(menuOpenFile), keyEquivalent: "o")
        miOpen.target = self
        fileMenu.addItem(miOpen)
        let miDl = NSMenuItem(title: "下载面板", action: #selector(menuDownloads), keyEquivalent: "d")
        miDl.keyEquivalentModifierMask = [.command, .shift]
        miDl.target = self
        fileMenu.addItem(miDl)
        fileMenu.addItem(.separator())
        fileMenu.addItem(withTitle: "关闭窗口", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")

        // ---- 编辑菜单（搜索框/输入框需要）----
        let editItem = NSMenuItem()
        main.addItem(editItem)
        let editMenu = NSMenu(title: "编辑")
        editItem.submenu = editMenu
        editMenu.addItem(withTitle: "撤销", action: Selector(("undo:")), keyEquivalent: "z")
        let redo = NSMenuItem(title: "重做", action: Selector(("redo:")), keyEquivalent: "z")
        redo.keyEquivalentModifierMask = [.command, .shift]
        editMenu.addItem(redo)
        editMenu.addItem(.separator())
        editMenu.addItem(withTitle: "剪切", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
        editMenu.addItem(withTitle: "拷贝", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        editMenu.addItem(withTitle: "粘贴", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
        editMenu.addItem(withTitle: "全选", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")

        // ---- 播放菜单（与键盘单键快捷键一一对应）----
        let playItem = NSMenuItem()
        main.addItem(playItem)
        let playMenu = NSMenu(title: "播放")
        playItem.submenu = playMenu
        func addPlay(_ title: String, _ sel: Selector) {
            let mi = NSMenuItem(title: title, action: sel, keyEquivalent: "")
            mi.target = self
            playMenu.addItem(mi)
        }
        addPlay("播放 / 暂停（空格）", #selector(menuPlayPause))
        addPlay("上一首", #selector(menuPrev))
        addPlay("下一首", #selector(menuNext))
        playMenu.addItem(.separator())
        addPlay("字幕轨：切换（C）", #selector(menuCycleSubtitle))
        addPlay("字幕：显示 / 隐藏（S）", #selector(menuToggleSubtitles))
        playMenu.addItem(.separator())
        addPlay("视频清晰度：循环（V）", #selector(menuCycleVideoQuality))
        addPlay("音乐音质：循环（Q）", #selector(menuCycleAudioQuality))
        playMenu.addItem(.separator())
        addPlay("倍速：循环", #selector(menuCycleSpeed))
        addPlay("收藏当前项（A）", #selector(menuFavorite))
        addPlay("队列模式（M）", #selector(menuCycleQueueMode))
        playMenu.addItem(.separator())
        addPlay("铺满：开 / 关（Z）", #selector(menuToggleFill))
        addPlay("画中画：开 / 关（P）", #selector(menuTogglePip))

        // 全屏项放在「播放」菜单里，不单独建「视图/显示」菜单：
        // 实测只要标题叫「视图/显示」，AppKit 在 NSApp.mainMenu 赋值阶段会**再补一条同名全屏项**（重复）。
        // 放进「播放」菜单没有这个行为，且 ⌃⌘F 仍然可用（F 单键由播放器处理）。
        // 刻意不给它 keyEquivalent：实测只要带上 ⌃⌘F，AppKit 在 mainMenu 赋值时会把这条项**复制一份**（重复菜单项）。
        // 单键 F 由播放器自己的按键处理（handleKey），这里只做菜单入口。
        let miFull = NSMenuItem(title: "进入 / 退出全屏（F）", action: #selector(menuFullscreen), keyEquivalent: "")
        miFull.target = self
        playMenu.addItem(miFull)
        // ---- 视图菜单（列表显示相关；不放全屏项，避免 AppKit 自动补一条重复的同名项）----
        let viewItem = NSMenuItem()
        main.addItem(viewItem)
        let viewMenu = NSMenu(title: "视图")
        viewItem.submenu = viewMenu
        let sizes: [(String, Double)] = [("小（48）", 48), ("中（84，默认，与手机一致）", 84),
                                         ("大（120）", 120), ("特大（160）", 160)]
        for (title, v) in sizes {
            let mi = NSMenuItem(title: title, action: #selector(menuThumbSize(_:)), keyEquivalent: "")
            mi.target = self
            mi.representedObject = v
            viewMenu.addItem(mi)
        }
        viewMenu.addItem(.separator())
        let miBigger = NSMenuItem(title: "增大缩略图", action: #selector(menuThumbBigger), keyEquivalent: "=")
        miBigger.target = self
        viewMenu.addItem(miBigger)
        let miSmaller = NSMenuItem(title: "减小缩略图", action: #selector(menuThumbSmaller), keyEquivalent: "-")
        miSmaller.target = self
        viewMenu.addItem(miSmaller)

        // ---- 窗口菜单 ----
        let winItem = NSMenuItem()
        main.addItem(winItem)
        let winMenu = NSMenu(title: "窗口")
        winItem.submenu = winMenu
        winMenu.addItem(withTitle: "最小化", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
        winMenu.addItem(withTitle: "缩放", action: #selector(NSWindow.performZoom(_:)), keyEquivalent: "")
        winMenu.addItem(.separator())
        winMenu.addItem(withTitle: "前置全部窗口", action: #selector(NSApplication.arrangeInFront(_:)), keyEquivalent: "")
        NSApp.windowsMenu = winMenu

        // ---- 帮助菜单 ----
        let helpItem = NSMenuItem()
        main.addItem(helpItem)
        let helpMenu = NSMenu(title: "帮助")
        helpItem.submenu = helpMenu
        let miKeys = NSMenuItem(title: "快捷键说明", action: #selector(menuShortcuts), keyEquivalent: "?")
        miKeys.target = self
        helpMenu.addItem(miKeys)
        let miCfg = NSMenuItem(title: "打开配置目录", action: #selector(menuOpenConfigDir), keyEquivalent: "")
        miCfg.target = self
        helpMenu.addItem(miCfg)
        let miCookie = NSMenuItem(title: "打开 cookie 目录", action: #selector(menuOpenCookieDir), keyEquivalent: "")
        miCookie.target = self
        helpMenu.addItem(miCookie)
        NSApp.helpMenu = helpMenu

        NSApp.mainMenu = main
        // 一次性核对：各菜单的非分隔项数（自动化里能一眼看出有没有"挂空"的项）
        let summary = main.items.compactMap { item -> String? in
            guard let sub = item.submenu else { return nil }
            return "\(sub.title)=\(sub.items.filter { !$0.isSeparatorItem }.count)"
        }.joined(separator: " ")
        Config.log("[MENU] 构建完成：\(summary)")
    }

    // ---- 菜单动作（把菜单接到既有播放控制上；单键快捷键仍由 handleKey 处理）----
    @objc private func menuSettings() { openSettingsPanel() }
    @objc private func menuThumbSize(_ sender: NSMenuItem) {
        applyThumbSize((sender.representedObject as? Double) ?? 84, note: sender.title)
    }
    @objc private func menuThumbBigger() { applyThumbSize(Double(thumbSize()) + 16, note: "增大") }
    @objc private func menuThumbSmaller() { applyThumbSize(Double(thumbSize()) - 16, note: "减小") }
    @objc private func menuOpenFile() { onOpenFile() }
    @objc private func menuDownloads() { onDownloads() }
    @objc private func menuPlayPause() { onPlayPause() }
    @objc private func menuPrev() { onPrev() }
    @objc private func menuNext() { onNext() }
    @objc private func menuCycleSubtitle() { cycleSubtitleTrack() }
    @objc private func menuToggleSubtitles() { toggleSubtitleOverlay() }
    @objc private func menuCycleVideoQuality() { cycleVideoQuality() }
    @objc private func menuCycleAudioQuality() { cycleAudioQuality() }
    @objc private func menuCycleSpeed() { onSpeed() }
    @objc private func menuFavorite() { toggleFavorite() }
    @objc private func menuCycleQueueMode() { queue.cycleMode(); setStatus("队列模式：\(queue.modeLabel)") }
    @objc private func menuToggleFill() { toggleFillScreen() }
    @objc private func menuTogglePip() { togglePip() }
    @objc private func menuFullscreen() { toggleVideoFullscreen() }
    @objc private func menuOpenConfigDir() { NSWorkspace.shared.open(URL(fileURLWithPath: Config.dir)) }
    @objc private func menuOpenCookieDir() {
        let dir = Config.dir + "/cookies"
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        NSWorkspace.shared.open(URL(fileURLWithPath: dir))
    }
    @objc private func menuShortcuts() {
        let alert = NSAlert()
        alert.messageText = "快捷键"
        alert.informativeText = """
        ⌘Q 退出 · ⌘, 设置 · ⌘O 打开本地文件 · ⌘W 关闭窗口 · ⌘M 最小化
        ⌘⇧D 下载面板 · ⌘= / ⌘- 缩放列表缩略图（也可在「视图」菜单或设置面板里调）

        播放：空格 播放/暂停 · ← → 快退/快进 5 秒 · ↑ ↓ 音量
        C 字幕轨 · S 字幕显隐 · V 视频清晰度 · Q 音质 · Z 铺满 · P 画中画
        A 收藏 · M 队列模式

        单键快捷键只在「播放器持有键盘焦点」时生效：
        · 点击画面 = 把焦点交给播放器（启动时默认如此）
        · 在搜索框里打字时自动让位，不会误触发（按 Esc 回到播放器）
        · ⌘Q / ⌘, / ⌘O / ⌘W / ⌘M 等组合键始终由菜单处理
        """
        alert.addButton(withTitle: "好")
        alert.runModal()
    }

    /// 缩略图尺寸变化（Ui.applyThumbSize 回调到这里记录日志，便于自动化核对）
    func thumbSizeChanged(_ size: Double) {
        Config.log("[THUMB] 列表缩略图高度 = \(Int(size)) pt（行高 \(Int(size) + 6)）")
    }

    /// NSAlert 的返回码规则：**第 N 个按钮（从 0 数）= 1000 + N**
    /// （`.alertFirstButtonReturn` 本身就是 1000 —— 之前把第 4 个按钮也写成 1000，撞号导致"点 QQ音乐 没反应"，用户实测）
    static func alertButtonCode(_ index: Int) -> NSApplication.ModalResponse {
        NSApplication.ModalResponse(1000 + index)
    }

    /// 登录状态刷新：原来只在构建界面时算一次，登录成功也要重启才显示「登录 ✓」
    func refreshLoginBadge() {
        let badge = loginBadge()
        // 登录按钮是**图标式**：只换图标与提示，绝不设 title —— 否则文字和图标叠在一起（用户实测"右上角按钮拥挤"）
        loginButton?.title = ""
        if let img = NSImage(systemSymbolName: badge.isEmpty ? "person.crop.circle" : "checkmark.circle",
                             accessibilityDescription: "登录") {
            loginButton?.image = img
            loginButton?.imagePosition = .imageOnly
        }
        loginButton?.toolTip = badge.isEmpty ? "未检测到 cookie —— 点此登录" : badge
    }

    func applicationDidBecomeActive(_ notification: Notification) {
        refreshLoginBadge()   // 从浏览器切回来 / 登录窗口操作完后，立刻反映登录状态
    }

    func startFinishWatch() {
        finishTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            self?.tickCommon()
        }
    }

    private func finish() {
        finishTimer?.invalidate()
        ticker?.invalidate()
        _ = progress.save()
        if selfTest {
            let dur = player.duration()
            let hasVideo = !player.videoSize().contains("无")
            let ok = dur > 0.5 && maxPosition > 0.5 && (hasVideo ? player.renderedFrames > 5 : true)
            let ao = player.audioOut(), vs = player.videoSize()
            Config.log(String(format: "[SPIKE] dur=%.1f maxPos=%.1f 渲染帧=%d 视频=%@ 音频=%@",
                              dur, maxPosition, player.renderedFrames, vs, ao.isEmpty ? "无" : ao))
            Config.log("[SPIKE] 结果=\(ok ? "PASS" : "FAIL")（播放\(dur > 0.5 ? "✓" : "✗") "
                       + (hasVideo ? "渲染\(player.renderedFrames > 5 ? "✓" : "✗")" : "纯音频(无需渲染)")
                       + " 音频\(ao.isEmpty ? "✗" : "✓")）")
        }
        NSApp.terminate(nil)
        exit(selfTest ? 0 : 0)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    /// 导出界面文本状态（客观判据：不依赖截图权限也能验证"界面上到底显示了什么"）
    @discardableResult
    func dumpUiText(to path: String) -> Bool {
        // 登录窗口打开时优先导出它
        if let lw = LoginWindow.shared?.window, lw.isVisible {
            var out = "登录窗口：\(lw.title)  \(Int(lw.frame.width))x\(Int(lw.frame.height))\n"
            out += "载荷 URL: \(LoginWindow.shared?.webView?.url?.absoluteString ?? "(未加载)")\n"
            out += "可见性: isVisible=\(lw.isVisible) isKey=\(lw.isKeyWindow)\n"
            return (try? out.write(toFile: path, atomically: true, encoding: .utf8)) != nil
        }
        // B6：如果开着面板，优先导出面板内容（面板才是当前操作对象）
        if let panelWindow = Panels.activeWindow, panelWindow.isVisible {
            var out = "面板窗口：\(panelWindow.title)  \(Int(panelWindow.frame.width))x\(Int(panelWindow.frame.height))\n"
            out += Panels.activeDumpText?() ?? "(面板无导出回调)\n"
            return (try? out.write(toFile: path, atomically: true, encoding: .utf8)) != nil
        }
        var lines: [String] = []
        lines.append("窗口: \(Int(window.frame.width))x\(Int(window.frame.height)) 标题=\(window.title)")
        let fr = window.firstResponder
        let frName: String
        if fr is NSTextView { frName = "输入框（单键快捷键让位）" }
        else if fr === player { frName = "播放器（单键快捷键生效）" }
        else { frName = "\(type(of: fr as Any))" }
        lines.append("键盘焦点: \(frName)")
        // 主菜单（含快捷键）——菜单是原生 UI，普通控件遍历看不到，这里单独导出便于自动化核对
        if let m = NSApp.mainMenu {
            lines.append("菜单: \(m.items.compactMap { $0.submenu?.title }.joined(separator: " / "))")
            for item in m.items {
                guard let sub = item.submenu else { continue }
                for (idx, mi) in sub.items.enumerated() where !mi.isSeparatorItem {
                    var key = ""
                    if !mi.keyEquivalent.isEmpty {
                        let m = mi.keyEquivalentModifierMask
                        let mods = (m.contains(.control) ? "⌃" : "") + (m.contains(.option) ? "⌥" : "")
                                 + (m.contains(.shift) ? "⇧" : "") + "⌘"
                        key = "  \(mods)\(mi.keyEquivalent.uppercased())"
                    }
                    // 是否真的有人处理（菜单项挂空 = 点了没反应，这种要在自动化里能看出来）
                    let handled: String
                    if let act = mi.action {
                        handled = NSApp.target(forAction: act, to: mi.target, from: mi) != nil ? "→可处理" : "→无处理者✗"
                    } else { handled = "（父项）" }
                    lines.append("  [\(sub.title)#\(idx)] \(mi.title)\(key) \(handled) action=\(mi.action.map(NSStringFromSelector) ?? "-")")
                }
            }
        }
        func walk(_ v: NSView, _ depth: Int) {
            let pad = String(repeating: "  ", count: min(depth, 6))
            if let b = v as? NSButton { lines.append("\(pad)按钮: \(b.title)") }
            if let t = v as? NSTextField, !(v is NSSearchField) { lines.append("\(pad)文本: \(t.stringValue)") }
            if let s = v as? NSSearchField { lines.append("\(pad)搜索框: \(s.stringValue)") }
            if let p = v as? NSPopUpButton { lines.append("\(pad)下拉: \(p.titleOfSelectedItem ?? "-")") }
            if let sl = v as? NSSlider { lines.append(String(format: "\(pad)滑块: %.1f / %.1f", sl.doubleValue, sl.maxValue)) }
            if let tv = v as? NSTableView {
                lines.append("\(pad)表格: \(tv.numberOfRows) 行 行高=\(Int(tv.rowHeight))")
                if tv.numberOfRows > 0, let cell = tv.view(atColumn: 0, row: 0, makeIfNecessary: true) as? NSTableCellView {
                    let f = cell.textField?.frame ?? .zero
                    let iv = cell.imageView.map { "图=\(Int($0.frame.width))x\(Int($0.frame.height)) 隐=\($0.isHidden)" } ?? "图=无"
                    lines.append("\(pad)  首格: 文本=\(cell.textField?.stringValue.prefix(24) ?? "-") "
                                 + "frame=\(Int(f.origin.x)),\(Int(f.origin.y)) \(Int(f.width))x\(Int(f.height)) "
                                 + "字号=\(cell.textField?.font?.pointSize ?? -1) \(iv) "
                                 + "表宽=\(Int(tv.bounds.width)) 列宽=\(Int(tv.tableColumns.first?.width ?? -1)) 单元格宽=\(Int(cell.frame.width))")
                    // 命中测试：双击缩略图/文字应当落到**表格**上；若被子视图接走，双击就不会触发播放
                    for (name, p) in [("缩略图中心", NSPoint(x: cell.imageView?.frame.midX ?? 0, y: cell.imageView?.frame.midY ?? 0)),
                                      ("文字中心", NSPoint(x: cell.textField?.frame.midX ?? 0, y: cell.textField?.frame.midY ?? 0))] {
                        let hit = cell.hitTest(p)
                        lines.append("\(pad)  命中\(name): \(hit.map { String(describing: type(of: $0)) } ?? "nil → 穿透到表格 ✓")")
                    }
                }
                for i in 0..<min(tv.numberOfRows, 30) where i < rows.count { lines.append("\(pad)  [\(i)] \(rows[i].text)") }
            }
            for sub in v.subviews { walk(sub, depth + 1) }
        }
        if let c = window.contentView { walk(c, 0) }
        // 网格内容（结果卡片）：便于自动化核对"列表里现在是哪一源的结果"
        let items = rows.filter { !$0.key.isEmpty }
        lines.append("网格条目: \(items.count) 条")
        for (i, r) in items.prefix(8).enumerated() { lines.append("  [\(i)] \(r.text)") }
        return (try? lines.joined(separator: "\n").write(toFile: path, atomically: true, encoding: .utf8)) != nil
    }

    // MARK: - UI 抓图（AppKit 界面 + GL 画面合成，不依赖屏幕录制权限）

    @discardableResult
    func dumpUi(to path: String) -> Bool {
        // 首选：整窗截图（CGWindowListCreateImage 对"自己进程的窗口"通常可用，
        // 比 cacheDisplay 完整：控件文字、layer 内容都在）
        if let img = grabWindowImage() { return writePng(img, path) }
        return dumpUiComposed(to: path)
    }

    private func grabWindowImage() -> NSImage? {
        // 登录窗口打开时优先抓它（用户排查登录问题最需要看这个窗口）
        if let lw = LoginWindow.shared?.window, lw.isVisible {
            let n = CGWindowID(lw.windowNumber)
            if let cg = CGWindowListCreateImage(.null, .optionIncludingWindow, n,
                                                [.boundsIgnoreFraming, .bestResolution]) {
                return NSImage(cgImage: cg, size: NSSize(width: cg.width, height: cg.height))
            }
        }
        // 搜索过滤器面板打开时也优先抓它（便于自动化取证）
        if let fw = filterWindow, fw.isVisible {
            let n = CGWindowID(fw.windowNumber)
            if let cg = CGWindowListCreateImage(.null, .optionIncludingWindow, n,
                                                [.boundsIgnoreFraming, .bestResolution]) {
                return NSImage(cgImage: cg, size: NSSize(width: cg.width, height: cg.height))
            }
        }
        // B6：面板打开时抓面板窗口
        if let panelWindow = Panels.activeWindow, panelWindow.isVisible {
            let n = CGWindowID(panelWindow.windowNumber)
            if let cg = CGWindowListCreateImage(.null, .optionIncludingWindow, n,
                                                [.boundsIgnoreFraming, .bestResolution]) {
                return NSImage(cgImage: cg, size: NSSize(width: cg.width, height: cg.height))
            }
        }
        let num = CGWindowID(window.windowNumber)
        guard num > 0,
              let cg = CGWindowListCreateImage(.null, .optionIncludingWindow, num,
                                               [.boundsIgnoreFraming, .bestResolution]) else { return nil }
        return NSImage(cgImage: cg, size: NSSize(width: cg.width, height: cg.height))
    }

    private func writePng(_ img: NSImage, _ path: String) -> Bool {
        guard let tiff = img.tiffRepresentation, let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:]) else { return false }
        return (try? png.write(to: URL(fileURLWithPath: path))) != nil
    }

    /// 回退：cacheDisplay 合成（控件文字可能缺失，但布局与 GL 画面在）
    private func dumpUiComposed(to path: String) -> Bool {
        guard let content = window.contentView,
              let rep = content.bitmapImageRepForCachingDisplay(in: content.bounds) else { return false }
        content.cacheDisplay(in: content.bounds, to: rep)
        let img = NSImage(size: content.bounds.size)
        img.addRepresentation(rep)
        if let glImage = player?.grabImage(), let pv = player {
            let frameInContent = content.convert(pv.bounds, from: pv)
            img.lockFocus()
            glImage.draw(in: frameInContent)
            img.unlockFocus()
        }
        return writePng(img, path)
    }


}
