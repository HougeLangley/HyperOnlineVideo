import AppKit
import Cmpv

/// macOS 端主控制器：窗口 + libmpv 渲染 + 网络层 + 界面接线
final class AppDelegate: NSObject, NSApplicationDelegate {
    // 界面
    var window: NSWindow!
    var player: MpvView!
    let sourcePopup = NSPopUpButton()
    let searchField = NSSearchField()
    let resultsTable = NSTableView()
    let resultsScroll = NSScrollView()
    var prevButton: NSButton!, playButton: NSButton!, nextButton: NSButton!
    var seekSlider: NSSlider!, timeLabel: NSTextField!, speedPopup: NSPopUpButton!, volumeSlider: NSSlider!
    var statusLabel: NSTextField!
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
    var playKey = "", playLabel = ""
    var lastProgressSave: Int64 = 0, lastProgressFlush: Int64 = 0
    var musicCovers: [String: String] = [:]      // key → 封面地址（搜索时顺手记录）
    var musicLabels: [String: String] = [:]      // key → 标题（播放时用）
    var rows: [Row] = []                         // 列表数据（结果 + 日志混排）

    // CLI / 自检
    var initialTarget: String?, initialAudio: String?, initialLabel: String?
    private var started = Date()
    private var target = ""
    private var exitAfter: Double = 0
    private var selfTest = false
    private var maxPosition: Double = 0
    private var dumpFramePath = ""
    private var dumpAt: Double = 2.5
    private var dumped = false
    private var dumpUiTextPath = ""
    private var dumpGlPath = ""

    func applicationDidFinishLaunching(_ notification: Notification) {
        let args = CommandLine.arguments
        if let i = args.firstIndex(of: "--open"), i + 1 < args.count { target = args[i + 1] }
        if let t = initialTarget, !t.isEmpty { target = t }
        if let i = args.firstIndex(of: "--exit-after"), i + 1 < args.count { exitAfter = Double(args[i + 1]) ?? 0 }
        if let i = args.firstIndex(of: "--dump-frame"), i + 1 < args.count { dumpFramePath = args[i + 1] }
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
        if args.contains("--test-mediakeys") {
            DispatchQueue.main.asyncAfter(deadline: .now() + 6) { [weak self] in   // 等媒体加载完，now-playing 才有东西
                guard let self else { return }
                let before = self.player.paused()
                self.mediaKeys.handlerToggle()
                let afterToggle = self.player.paused()
                self.mediaKeys.handlerPause()
                let afterPause = self.player.paused()
                self.mediaKeys.handlerPlay()
                let afterPlay = self.player.paused()
                Config.log("[SELFTEST] 媒体键: 初始paused=\(before) toggle后=\(afterToggle) pause后=\(afterPause) play后=\(afterPlay)")
                Config.log("[SELFTEST] now-playing: \(self.mediaKeys.nowPlayingSummary())")
            }
        }
        selfTest = args.contains("--selfcheck")

        NSApp.appearance = NSAppearance(named: .darkAqua)   // 深色主题（与 Linux/Qt、Android 端一致）
        settings.load()
        progress.load()
        favorites.load()
        _ = queue.load()

        window = NSWindow(contentRect: NSRect(x: 100, y: 100, width: 1180, height: 720),
                          styleMask: [.titled, .closable, .resizable, .miniaturizable],
                          backing: .buffered, defer: false)
        window.appearance = NSAppearance(named: .darkAqua)   // 与另两端的深色观感一致
        buildUi()
        mediaKeys = MediaKeys(player: player)
        mediaKeys.delegate = self
        mediaKeys.setup()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        started = Date()

        // 界面自动化：--source N 预选来源；--query <kw> 自动搜索；--dump-ui <png> 抓整窗
        if let i = args.firstIndex(of: "--source"), i + 1 < args.count, let n = Int(args[i + 1]), n >= 0, n <= 3 {
            sourcePopup.selectItem(at: n)
            if n == 3 { showLocalLibrary() }
        }
        if let i = args.firstIndex(of: "--query"), i + 1 < args.count {
            searchField.stringValue = args[i + 1]
            onSearch()
            if args.contains("--play-first") {                 // 自动化：搜索完成后播放第一条结果
                DispatchQueue.main.asyncAfter(deadline: .now() + 4) { [weak self] in
                    guard let self, let r = self.rows.first(where: { !$0.key.isEmpty }) else { return }
                    self.playResult(key: r.key, label: r.text)
                }
            }
        }
        startFinishWatch()      // 抓图/自检/自动退出的统一心跳（不依赖"窗口激活"事件）
        if !target.isEmpty { playKey(target: target, label: initialLabel ?? target, audio: initialAudio) }
        else { setStatus("就绪 · 来源 \(sourcePopup.titleOfSelectedItem ?? "-")") }
    }

    /// 统一起播入口（与 Qt 端的 playItem/playResolved 对应）
    func playKey(target: String, label: String, audio: String? = nil) {
        _ = progress.save()                       // 切内容前先把上一项落盘
        playKey = target
        playLabel = label
        lastProgressSave = 0
        player.overlay.cues = []
        player.tracks = []
        trackIndex = -1
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
    private func resumeAfterLoad(_ sec: Double) {
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

    func tickCommon() {
        maxPosition = max(maxPosition, player.position())
        // 触发条件：有媒体时按"播放位置"，纯界面场景按"启动后秒数"（否则永远不触发）
        let dumpReady = player.duration() >= 0.5 ? player.position() > dumpAt
                                                 : Date().timeIntervalSince(started) > dumpAt
        if !dumped, (!dumpFramePath.isEmpty || !dumpGlPath.isEmpty), dumpReady {
            if !dumpFramePath.isEmpty {
                dumped = dumpUi(to: dumpFramePath)
                appendLog(String(format: "抓帧%@: %@（pos=%.1f 帧数=%d）", dumped ? "成功" : "失败", dumpFramePath,
                                 player.position(), player.renderedFrames))
            } else {
                dumped = true
            }
            if !dumpGlPath.isEmpty {
                let glOk = player?.dumpFrame(to: dumpGlPath) ?? false
                appendLog("GL 帧已导出(\(glOk ? "成功" : "失败")): \(dumpGlPath)")
            }
            if !dumpUiTextPath.isEmpty, dumpUiText(to: dumpUiTextPath) {
                appendLog("界面状态已导出: \(dumpUiTextPath)")
            }
        }
        if exitAfter > 0, Date().timeIntervalSince(started) >= exitAfter { finish() }
    }

    private var finishTimer: Timer?

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
        var lines: [String] = []
        lines.append("窗口: \(Int(window.frame.width))x\(Int(window.frame.height)) 标题=\(window.title)")
        func walk(_ v: NSView, _ depth: Int) {
            let pad = String(repeating: "  ", count: min(depth, 6))
            if let b = v as? NSButton { lines.append("\(pad)按钮: \(b.title)") }
            if let t = v as? NSTextField, !(v is NSSearchField) { lines.append("\(pad)文本: \(t.stringValue)") }
            if let s = v as? NSSearchField { lines.append("\(pad)搜索框: \(s.stringValue)") }
            if let p = v as? NSPopUpButton { lines.append("\(pad)下拉: \(p.titleOfSelectedItem ?? "-")") }
            if let sl = v as? NSSlider { lines.append(String(format: "\(pad)滑块: %.1f / %.1f", sl.doubleValue, sl.maxValue)) }
            if let tv = v as? NSTableView {
                lines.append("\(pad)表格: \(tv.numberOfRows) 行")
                for i in 0..<min(tv.numberOfRows, 30) where i < rows.count { lines.append("\(pad)  [\(i)] \(rows[i].text)") }
            }
            for sub in v.subviews { walk(sub, depth + 1) }
        }
        if let c = window.contentView { walk(c, 0) }
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
