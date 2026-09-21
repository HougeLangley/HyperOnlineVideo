import AppKit

/// 主界面（Phase 5.3）—— 与 Qt 端同布局：顶部来源/搜索栏 + 左侧结果列表 + 右侧播放器 + 底部控制条
extension AppDelegate {
    func buildUi() {
        window.title = "聚合视频 · Hyper Online Video（macOS）"
        window.setContentSize(NSSize(width: 1180, height: 720))
        window.minSize = NSSize(width: 900, height: 560)

        let root = NSView(frame: NSRect(x: 0, y: 0, width: 1180, height: 720))
        root.autoresizingMask = [.width, .height]
        // 整窗毛玻璃底（模糊窗口后面的桌面）：放在最底层，其余界面盖在它上面
        let windowGlass = NSVisualEffectView(frame: root.bounds)
        windowGlass.autoresizingMask = [.width, .height]
        windowGlass.material = .underWindowBackground
        windowGlass.blendingMode = .behindWindow
        windowGlass.state = .followsWindowActiveState
        root.addSubview(windowGlass)

        // 四个分区（顺序与原构造一致 ✓）：顶栏 → 中部 → 控制条 → 状态栏
        buildTopBarSection(root)
        buildMiddleSection(root)
        buildControlBarSection(root)
        buildStatusBarSection(root)
    }

    // MARK: 状态与日志

    func setStatus(_ s: String) {
        // CLI 参数处理早于 buildUi()，此时控件还不存在 —— 不能直接碰 IUO（实测崩在这里）
        statusLabel?.stringValue = s
        Config.log("[状态] \(s)")
    }

    func appendLog(_ s: String) {
        logLines.append(s)
        Config.log("[列表] \(s)")        // 同时也进 stderr：自动化只看日志也能判定
        if logLines.count > 400 { logLines.removeFirst() }
        let wasAtBottom = isNearBottom()
        resultsGrid.reloadData()
        // 只在"用户本来就在底部"时才自动跟随（否则会打断翻阅）
        if !rows.isEmpty, wasAtBottom { scrollToResult(rows.count - 1) }
    }

    // MARK: 定时刷新

    /// 字幕按钮状态（开/关）
    func updateSubtitleButton() {
        let on = !player.overlay.hidden
        // 图标式按钮：只换图标与着色，绝不设 title（否则文字与图标叠在一起）
        subtitleBtn?.title = ""
        subtitleBtn?.image = NSImage(systemSymbolName: on ? "captions.bubble.fill" : "captions.bubble",
                                     accessibilityDescription: "字幕")
        subtitleBtn?.contentTintColor = on ? .controlAccentColor : .secondaryLabelColor
        subtitleBtn?.toolTip = on ? "字幕/歌词：显示中（S 键开关）" : "字幕/歌词：已隐藏（S 键开关）"
    }

    /// 连播按钮状态
    func updateAutoNextButton() {
        let on = settings.bool("playback.autoNext", true)
        autoNextBtn?.title = ""
        autoNextBtn?.image = NSImage(systemSymbolName: "infinity", accessibilityDescription: "连播")
        autoNextBtn?.contentTintColor = on ? .controlAccentColor : .secondaryLabelColor
        autoNextBtn?.toolTip = on ? "连播：开（播完自动下一条，A 键开关）" : "连播：关（A 键开关）"
    }

    @objc func onSubtitleToggle() {
        toggleSubtitleOverlay()
        updateSubtitleButton()
    }

    @objc func onToggleAutoNext() {
        let now = settings.bool("playback.autoNext", true)
        _ = settings.apply(key: "playback.autoNext", value: now ? "false" : "true")
        updateAutoNextButton()
        setStatus(now ? "已关闭连播（播完停在结尾）" : "已开启连播（按列表顺序自动播下一条）")
        Config.log("[AUTO] 连播开关：\(now ? "关" : "开")")
    }

    /// 播放结束后按列表顺序播下一条（连播开关 + 设置项共同控制）
    func handlePlaybackEnded() {
        guard settings.bool("playback.autoNext", true) else {
            setStatus("播放结束（连播已关闭）")
            return
        }
        guard let cur = rows.firstIndex(where: { !$0.key.isEmpty && $0.key == playKey }) else {
            setStatus("播放结束")
            return
        }
        // 播放模式（顺序/单曲循环/随机/列表循环）——与队列面板、菜单里选的是同一个值
        switch queue.mode {
        case .repeatOne:
            Config.log("[AUTO] 单曲循环 → 重播本曲")
            setStatus("单曲循环：\(playLabel.prefix(24))")
            playResult(key: playKey, label: playLabel)
            return
        case .shuffle:
            let pool = rows.filter { !$0.key.isEmpty && $0.key != playKey }
            guard let pick = pool.randomElement() else { setStatus("播放结束（列表只有一项）"); return }
            Config.log("[AUTO] 随机播放 → \(pick.text.prefix(40))")
            setStatus("随机播放：\(pick.text.prefix(24))")
            playResult(key: pick.key, label: pick.text)
            return
        case .sequential, .repeatAll:
            var next = rows[(cur + 1)...].first(where: { !$0.key.isEmpty })
            if next == nil, queue.mode == .repeatAll {                 // 列表循环：回到第一项
                next = rows.first(where: { !$0.key.isEmpty })
                Config.log("[AUTO] 列表循环 → 回到开头")
            }
            guard let next else {
                setStatus("播放结束（已是最后一条）")
                Config.log("[AUTO] 已到列表末尾，停止（当前为顺序播放）")
                return
            }
            Config.log("[AUTO] 播放完成 → 自动播放下一条：\(next.text.prefix(40))")
            setStatus("连播：\(next.text.prefix(28))")
            playResult(key: next.key, label: next.text)
        }
    }

    /// 播放结束检测（mpv 的 eof-reached；只在边缘触发一次）
    func checkEof() {
        guard let p = player, p.duration() > 1 else { return }
        let eof = p.eofReached()
        if eof != lastEofState {                      // 状态翻转时记一笔（排查连播问题很有用）
            Config.log("[AUTO] eof 状态: \(lastEofState) → \(eof)（pos=\(String(format: "%.1f", p.position()))/dur=\(String(format: "%.1f", p.duration()))）")
            lastEofState = eof
        }
        if eof {
            guard !autoNextHandled else { return }
            autoNextHandled = true
            Config.log("[AUTO] 检测到播放结束（eof-reached）")
            handlePlaybackEnded()
        } else {
            autoNextHandled = false
        }
    }

    private func startTicker() {
        let t = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            guard let self else { return }
            self.tickCount += 1
            if CommandLine.arguments.contains("--diag"), self.tickCount % 6 == 1 {
                Config.log("[diag] tick #\(self.tickCount) playerNil=\(self.player == nil) mediaKeysNil=\(self.mediaKeys == nil) labelLen=\(self.playLabel.count)")
            }
            self.player?.refreshStatus()
            self.checkEof()                     // 连播：播完了就自动下一条
            if let p = self.player {
                let dur = p.duration(), pos = p.position()
                self.seekSlider.maxValue = max(dur, 1)
                // 拖动/刚松手时不要用 tick 的值覆盖滑块（原来只看 isHighlighted，不够可靠 —— 用户实测拖动后画面/声音对不上）
                let dragging = self.seekSlider.isHighlighted || Date().timeIntervalSince(self.lastSeekAt) < 1.2
                if !dragging { self.seekSlider.doubleValue = pos }
                self.timeLabel.stringValue = String(format: "%@ / %@", Self.clock(pos), Self.clock(dur))
                // 状态栏显示"我们自己的标题"而不是 mpv 的 media-title
                // （流媒体的 media-title 是一长串 URL，没法看）
                let extra = p.overlay.cues.isEmpty ? "" : "   字幕:\(p.overlay.sourceName)(\(p.overlay.cues.count))"
                // 正在播放信息（控制中心/锁屏读它）
                let artist = self.playLabel.contains(" — ") ? String(self.playLabel.split(separator: " — ").last ?? "") : ""
                self.mediaKeys?.updateNowPlaying(title: self.playLabel, artist: artist,
                                                 duration: dur, paused: p.paused(), position: pos)
                let platform = self.playPlatform.isEmpty ? "" : "[\(self.playPlatform)] "
                let head = self.playLabel.isEmpty ? p.lastStatus : "\(platform)\(self.playLabel)   \(Self.clock(pos)) / \(Self.clock(dur))\(p.paused() ? "  [暂停]" : "")"
                self.statusLabel.stringValue = "\(head)\(extra)"
                // 进度记忆（与另两端同一规则）
                if !self.playKey.isEmpty, dur >= 1, pos >= ProgressStore.recordMin {
                    let now = ProgressStore.nowMs()
                    if now - self.lastProgressSave > 5000 {
                        self.lastProgressSave = now
                        _ = self.progress.remember(self.playKey, pos: pos, dur: dur, title: self.playLabel)
                        if now - self.lastProgressFlush > 30000 {
                            self.lastProgressFlush = now
                            _ = self.progress.save()
                        }
                    }
                }
            }
        }
        // 再补一次 .common 模式注册（scheduledTimer 只进 default 模式，
        // 拖拽滑块/弹窗期间会暂停 → 媒体键与状态栏不更新）
        RunLoop.main.add(t, forMode: .common)
        ticker = t
    }

    static func clock(_ s: Double) -> String {
        let t = Int(max(0, s))
        return String(format: "%d:%02d", t / 60, t % 60)
    }

    // MARK: 键盘（与 Qt 端同一套快捷键）

    /// 当前列表缩略图高度（pt）——设置项 ui.thumbSize，默认 84（与 Android 端一致）
    func thumbSize() -> CGFloat {
        let v = settings.number("ui.thumbSize", 84)
        return CGFloat(min(200, max(24, v)))
    }

    /// 卡片尺寸：宽按 16:9 缩略图 + 内边距，高 = 缩略图 + 标题/来源区
    func gridItemSize() -> NSSize {
        let h = thumbSize()
        let w = h * 16.0 / 9.0 + 16
        let titleH = min(36, max(16, h * 0.30))
        return NSSize(width: max(180, w), height: h + titleH + 8 + 15 + 12)
    }

    /// 是否已接近底部（用于"跟随最新"与"懒加载"判断）
    func isNearBottom() -> Bool {
        let clip = resultsScroll.contentView
        return clip.bounds.maxY >= resultsGrid.frame.height - 40
    }

    /// 兼容帮助：结果条数 / 刷新 / 选中行 / 滚动到某行
    func reloadResults() { resultsGrid.reloadData() }
    var selectedResultRow: Int { resultsGrid.selectionIndexPaths.first?.item ?? -1 }
    func scrollToResult(_ i: Int) {
        guard i >= 0, i < rows.count else { return }
        // 关键：reloadData() 只是把布局标脏，布局要等下一次 layout pass。
        // 紧接着 scrollToItems 会去问 layoutAttributesForItemAtIndexPath → 布局里还是旧条目数
        // → AppKit 抛 NSInternalInconsistencyException 'Parameter indexPath (0,0) out of bounds'（实测崩溃）。
        // 先强制走一次布局，并二次校验条目数，双保险。
        resultsGrid.layoutSubtreeIfNeeded()
        guard resultsGrid.numberOfSections > 0, resultsGrid.numberOfItems(inSection: 0) > i else { return }
        resultsGrid.scrollToItems(at: Set([IndexPath(item: i, section: 0)]), scrollPosition: .bottom)
    }

    /// 应用新的缩略图尺寸（改卡片尺寸 + 重画；不动数据）
    func applyThumbSize(_ newValue: Double, note: String = "") {
        let clamped = min(200, max(24, newValue.rounded()))
        _ = settings.apply(key: "ui.thumbSize", value: String(Int(clamped)))
        if let l = resultsGrid.collectionViewLayout as? NSCollectionViewFlowLayout {
            l.itemSize = gridItemSize()          // 卡片尺寸跟随设置，列数自动重排
        }
        resultsGrid.reloadData()
        thumbSizeChanged(clamped)      // 交给 AppDelegate 记录状态/日志
        setStatus("列表缩略图：\(Int(clamped)) pt\(note.isEmpty ? "" : "（\(note)）")")
    }

    func handleKey(_ ev: NSEvent) -> Bool {
        // 全屏时 Esc = 退出全屏（优先级最高）
        if videoFullscreen, ev.keyCode == 53 {
            exitVideoFullscreen()
            return true
        }
        // 输入框里按 Esc：退出输入状态，把键盘控制权还给播放器
        let editing = (NSApp.keyWindow?.firstResponder as? NSTextView) != nil
        if editing, ev.keyCode == 53, NSApp.keyWindow === window {
            window.makeFirstResponder(player)
            setStatus("已退出输入，单键快捷键可用")
            return true
        }
        // 两条硬规则（用户实测踩出来的）：带 ⌘/⌃/⌥ 的组合键交给菜单；正在输入时不抢键
        let mainIsKey = (NSApp.keyWindow === window) && (NSApp.modalWindow == nil)
        guard PlayerKeyPolicy.shouldHandle(keyCode: ev.keyCode,
                                           characters: ev.charactersIgnoringModifiers,
                                           modifiers: ev.modifierFlags,
                                           isEditing: editing,
                                           isMainWindowKey: mainIsKey) else { return false }
        switch ev.keyCode {
        case 49: onPlayPause(); return true                    // 空格
        case 123: seekBy(-5); return true                      // ←
        case 124: seekBy(5); return true                       // →
        case 126: setVolume(player.volume() + 5); return true   // ↑
        case 125: setVolume(player.volume() - 5); return true   // ↓
        default: break
        }
        switch ev.charactersIgnoringModifiers?.lowercased() {
        case "f": window.toggleFullScreen(nil); return true
        case "z": toggleFillScreen(); return true   // B7：全屏铺满开关
        case "c": cycleSubtitleTrack(); return true
        case "s": toggleSubtitleOverlay(); return true
        case "a": toggleFavorite(); return true
        case "m": queue.cycleMode(); setStatus("队列模式：\(queue.modeLabel)"); return true
        case "v": cycleVideoQuality(); return true
        case "q": cycleAudioQuality(); return true
        case "p": togglePip(); return true
        default: return false
        }
    }

    /// 播放器单键快捷键的"该不该接管"策略（纯逻辑，不依赖 UI —— 便于自检覆盖）
    ///
    /// 两条硬规则都是实测踩出来的：
    ///  1. 带 ⌘/⌃/⌥ 的组合键**一律不接管**：否则 ⌘Q 会被 "q"（音质循环）吃掉，菜单快捷键全废；
    ///  2. **正在文本框里输入时不接管**：否则在搜索框里打 "p" 会开画中画、打空格会暂停。
    enum PlayerKeyPolicy {
        static let letterKeys = "fzcsamvqp"     // 与 handleKey 的 case 一一对应

        static func shouldHandle(keyCode: UInt16, characters: String?,
                                 modifiers: NSEvent.ModifierFlags, isEditing: Bool,
                                 isMainWindowKey: Bool = true) -> Bool {
            let m = modifiers.intersection(.deviceIndependentFlagsMask)
            if m.contains(.command) || m.contains(.control) || m.contains(.option) { return false }
            if isEditing { return false }
            // 3. 当前按键窗口不是主窗口（弹窗/设置面板/登录窗口在前）时一律不抢：
            //    否则弹窗里按空格会去切"播放/暂停"，而不是触发默认按钮。
            if !isMainWindowKey { return false }
            switch keyCode {
            case 49, 123, 124, 125, 126: return true      // 空格 / ← → ↑ ↓
            default: break
            }
            guard let c = characters?.lowercased(), c.count == 1 else { return false }
            return letterKeys.contains(c)
        }
    }

    /// 字幕/歌词显示开关（S 键与「播放」菜单共用）
    func toggleSubtitleOverlay() {
        player.overlay.hidden.toggle()
        updateSubtitleButton()          // 单一真相：开关一改，图标/提示立刻同步（不等定时刷新）
        setStatus(player.overlay.hidden ? "字幕：关" : "字幕：开")
        Config.log("[SUB] 开关 → hidden=\(player.overlay.hidden) 轨数=\(player.tracks.count) 行数=\(player.overlay.cues.count)")
    }

    func seekBy(_ d: Double) {
        let t = max(0, player.position() + d)
        player.seek(to: t)
        setStatus("跳转到 \(Self.clock(t))")
    }

    func setVolume(_ v: Int) {
        let nv = max(0, min(200, v))
        player.setVolume(nv)
        volumeSlider.doubleValue = Double(nv)
        setStatus("音量 \(nv)")
    }

    /// ⚠️ overlay.karaoke 的**唯一**写入口。
    /// 判据 = 「当前轨是歌词类（lrc/yrc/qrc）」**且**「用户设置 subtitle.karaoke 为开」。
    /// 历史 bug：playKey / 设置刷新处曾直接写成 `= settings.bool("subtitle.karaoke", true)` →
    /// 把普通字幕（SRT/CC）也置成歌词模式 → 同一视频第二次打开时字幕变成"大字号居中歌词"（用户实测截图）。
    /// 对照：Linux 端用「轨道自带标记」(t.karaoke) 决定，从未出过这个问题。
    func refreshKaraokeFlag() {
        let isLyricTrack: Bool = {
            guard trackIndex >= 0, trackIndex < player.tracks.count else { return false }
            let ext = (player.tracks[trackIndex].source as NSString).pathExtension.lowercased()
            return ["lrc", "yrc", "qrc"].contains(ext)
        }()
        player.overlay.karaoke = isLyricTrack && settings.bool("subtitle.karaoke", true)
        Config.log("[SUB] karaoke=\(player.overlay.karaoke)（轨=歌词类:\(isLyricTrack) 设置=\(settings.bool("subtitle.karaoke", true))）")
    }

    func cycleSubtitleTrack() {
        guard !player.tracks.isEmpty else {
            setStatus("没有可切换的字幕轨")
            return
        }
        trackIndex = (trackIndex + 1 >= player.tracks.count) ? -1 : trackIndex + 1
        applyTrack(trackIndex)
    }

    func applyTrack(_ idx: Int) {
        guard idx >= 0, idx < player.tracks.count else {
            player.overlay.cues = []
            player.overlay.sourceName = ""
            updateSubtitleButton()
            setStatus("字幕：关")
            Config.log("[SUB] 切到「关」（无当前轨）")
            return
        }
        let t = player.tracks[idx]
        player.overlay.cues = t.cues.isEmpty ? Subtitles.loadFile(t.source) : t.cues
        player.overlay.sourceName = t.label
        player.overlay.hidden = false       // 挂上轨就是要显示（修：开关显示"开"却没有字幕）
        updateSubtitleButton()
        Config.log("[SUB] 挂载轨 \(t.label)：\(player.overlay.cues.count) 行，可见=\(!player.overlay.hidden)")
        refreshKaraokeFlag()          // 唯一定义处：见下（普通字幕 SRT/CC 永远走贴底布局）
        setStatus("字幕轨：\(t.label)（\(player.overlay.cues.count) 行）")
    }

    /// 拆自 buildUi()（原 253 行 ✗ 审计 #166 重启版 ✓ 纯代码搬移 ✓）
    private func buildTopBarSection(_ root: NSView) {
// ---- 顶部栏 ----
sourcePopup.addItems(withTitles: ["YouTube", "B站", "网易云音乐", "QQ音乐", "本地库"])
sourcePopup.target = self
sourcePopup.action = #selector(onSourceChanged)
searchField.placeholderString = "输入关键词后回车搜索（Esc 回车播放控制；来源见左侧下拉）"
searchField.target = self
searchField.action = #selector(onSearch)
searchField.frame = NSRect(x: 0, y: 0, width: 380, height: 24)

topBarStack = NSStackView()
topBarStack.orientation = .horizontal
topBarStack.spacing = 8
topBarStack.edgeInsets = NSEdgeInsets(top: 8, left: 10, bottom: 8, right: 10)
topBarStack.addArrangedSubview(sourcePopup)
// 搜索过滤器（与 Android FilterSheet 对齐）：排序（相关度/最新/播放最多）+ 时长（全部/短视频/中等/长篇）
filterButton = NSButton(title: "过滤", target: self, action: #selector(onFilters))
filterButton.bezelStyle = .rounded
filterButton.toolTip = "搜索过滤器：排序 相关度/最新/播放最多；时长 全部/短视频/中等/长篇"
topBarStack.addArrangedSubview(filterButton)
topBarStack.addArrangedSubview(searchField)
let badge = loginBadge()
// 工具栏按钮：主操作留文字（搜索），其余用**图标 + 悬浮提示**——按钮一多就拥挤，图标省一半宽度。
// (标题, 图标名, 提示, 动作)；图标名是 SF Symbols。
// 只保留 4 个高频按钮：搜索（文字）/ 下载 / 更多▾ / 登录。
// 其余（下载面板、打开本地文件、收藏、队列、设置）收进"更多"菜单 —— 原来一行 8 个按钮太挤（用户实测反馈）。
let items: [(String, String, String, Selector)] = [
    ("搜索", "magnifyingglass", "搜索（回车）", #selector(onSearch)),
    ("下载", "arrow.down.circle", "下载当前正在播放的内容", #selector(onDownload)),
    ("更多", "ellipsis.circle", "更多：下载面板 / 打开本地文件 / 收藏 / 队列 / 设置", #selector(onMoreMenu)),
    (badge.isEmpty ? "登录" : "登录 ✓", badge.isEmpty ? "person.crop.circle" : "checkmark.circle",
     badge.isEmpty ? "未检测到 cookie —— 点此登录" : badge, #selector(onLogin)),
]
for (title, symbol, tip, sel) in items {
    let isSearch = sel == #selector(onSearch)
    let b = NSButton(title: isSearch ? title : "", target: self, action: sel)
    b.bezelStyle = .rounded
    if !isSearch, let img = NSImage(systemSymbolName: symbol, accessibilityDescription: title) {
        b.image = img
        b.imagePosition = .imageOnly
    }
    if isSearch { b.keyEquivalent = "\r" }
    b.toolTip = tip
    topBarStack.addArrangedSubview(b)
    if isSearch { b.widthAnchor.constraint(greaterThanOrEqualToConstant: 56).isActive = true }
    if sel == #selector(onLogin) { loginButton = b }
}
    }

    /// 拆自 buildUi()（原 253 行 ✗ 审计 #166 重启版 ✓ 纯代码搬移 ✓）
    private func buildMiddleSection(_ root: NSView) {
// ---- 中部：左结果列表 / 右播放器 ----
// 卡片尺寸跟随设置 ui.thumbSize；列数由容器宽度自动决定（窄=单列，宽=多列瀑布流）
let layout = NSCollectionViewFlowLayout()
layout.itemSize = gridItemSize()
layout.minimumInteritemSpacing = 10
layout.minimumLineSpacing = 10
layout.sectionInset = NSEdgeInsets(top: 10, left: 10, bottom: 10, right: 10)
resultsGrid.collectionViewLayout = layout
resultsGrid.dataSource = self
resultsGrid.delegate = self
resultsGrid.isSelectable = true
resultsGrid.allowsMultipleSelection = false
resultsGrid.backgroundColors = [.clear]
resultsGrid.register(ResultCardItem.self, forItemWithIdentifier: ResultCardItem.identifier)
resultsScroll.documentView = resultsGrid
resultsScroll.hasVerticalScroller = true
// 滚到接近底部就加载下一页（瀑布流）
resultsScroll.contentView.postsBoundsChangedNotifications = true
NotificationCenter.default.addObserver(forName: NSView.boundsDidChangeNotification,
                                       object: resultsScroll.contentView, queue: .main) { [weak self] _ in
    self?.loadMoreIfNeeded()
}
resultsScroll.borderType = .noBorder

player = MpvView(frame: NSRect(x: 0, y: 0, width: 800, height: 560))
// 双击画面：默认进"纯视频全屏"；**画中画时改为退出画中画**（标题栏写的就是"双击或按 P 退出"，
// 之前双击会进全屏，与实际提示不符 —— 用户实测反馈）
player.onDoubleClick = { [weak self] in
    guard let self else { return }
    if self.pipActive { self.togglePip() } else { self.toggleVideoFullscreen() }
}
player.autoresizingMask = [.width, .height]
if CommandLine.arguments.contains("--diag") { player.diag = true }
if CommandLine.arguments.contains("--no-overlay") { player.overlay.hidden = true }
    }

    /// 拆自 buildUi()（原 253 行 ✗ 审计 #166 重启版 ✓ 纯代码搬移 ✓）
    private func buildControlBarSection(_ root: NSView) {
// ---- 控制条 ----
// 播放模式按钮（顺序 → 单曲循环 → 随机 → 列表循环），点击循环切换
// 播放模式：用标准图标（顺序/单曲/随机/列表循环），比文字胶囊省一半宽度
modeButton = NSButton(title: "", target: self, action: #selector(onCycleMode))
modeButton.bezelStyle = .rounded
modeButton.toolTip = "播放模式：顺序播放 → 单曲循环 → 随机播放 → 列表循环"
modeButton.image = NSImage(systemSymbolName: "arrow.forward", accessibilityDescription: "顺序播放")
modeButton.imagePosition = .imageOnly
modeButton.widthAnchor.constraint(greaterThanOrEqualToConstant: 36).isActive = true
prevButton = NSButton(title: "⏮", target: self, action: #selector(onPrev))
playButton = NSButton(title: "⏯", target: self, action: #selector(onPlayPause))
nextButton = NSButton(title: "⏭", target: self, action: #selector(onNext))
for b in [prevButton!, playButton!, nextButton!] {
    b.bezelStyle = .rounded
    b.widthAnchor.constraint(equalToConstant: 34).isActive = true
}
seekSlider = NSSlider(value: 0, minValue: 0, maxValue: 100, target: self, action: #selector(onSeek))
seekSlider.isContinuous = false
seekSlider.widthAnchor.constraint(greaterThanOrEqualToConstant: 100).isActive = true   // 窗口窄时优先压缩它
timeLabel = NSTextField(labelWithString: "0:00 / 0:00")
timeLabel.widthAnchor.constraint(greaterThanOrEqualToConstant: 72).isActive = true
timeLabel.font = .monospacedDigitSystemFont(ofSize: 12, weight: .regular)
qualityPopup = NSPopUpButton()    // 质量（B1：播放中可切，保进度）；具体是"清晰度"还是"音质"由 playKey 决定
qualityPopup.addItems(withTitles: Self.videoQualityTitles)   // 含 2160p/1440p，与设置面板共用一份
qualityWidth = qualityPopup.widthAnchor.constraint(equalToConstant: 104)   // 初始就给够宽（"2160p 4K"/"较高 320k" 都放得下）
qualityWidth?.isActive = true
qualityPopup.toolTip = "清晰度（V 键循环切换）"
qualityPopup.target = self
qualityPopup.action = #selector(onQuality)
qualityPopup.selectItem(at: qualityIndex(forHeight: settings.number("video.maxHeight", 0)))
speedPopup = NSPopUpButton()      // IUO 必须先建实例（曾漏掉 → 启动即 nil trap）
speedPopup.addItems(withTitles: ["0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "1.75x", "2.0x"])
speedPopup.selectItem(withTitle: "1.0x")
speedPopup.widthAnchor.constraint(equalToConstant: 58).isActive = true
speedPopup.target = self
speedPopup.action = #selector(onSpeed)
volumeSlider = NSSlider(value: 100, minValue: 0, maxValue: 100, target: self, action: #selector(onVolume))
volumeSlider.widthAnchor.constraint(greaterThanOrEqualToConstant: 60).isActive = true
volumeSlider.widthAnchor.constraint(lessThanOrEqualToConstant: 90).isActive = true
volumeSlider.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)   // 音量条可压缩
volumeSlider.setContentHuggingPriority(.defaultLow, for: .horizontal)                // 与进度条一起分配多余空间
volumeSlider.toolTip = "音量（↑ / ↓ 键也可调）"
fullscreenBtn = iconBarButton("arrow.up.left.and.arrow.down.right", "全屏（⌃⌘F / 双击画面）", #selector(onFullscreen), self)
volumeIcon = NSImageView()
volumeIcon.image = NSImage(systemSymbolName: "speaker.wave.2", accessibilityDescription: "音量")
volumeIcon.contentTintColor = .secondaryLabelColor
volumeIcon.toolTip = "音量（↑/↓ 微调）"
fullscreenBtn.widthAnchor.constraint(equalToConstant: 52).isActive = true
pipBtn = iconBarButton("pip.enter", "画中画（P 键）", #selector(onPip), self)
pipBtn.widthAnchor.constraint(equalToConstant: 62).isActive = true
// 字幕开关（点击显示/隐藏字幕与歌词）
subtitleToggleBtn = iconBarButton("captions.bubble", "字幕开关（S 键）", #selector(onSubtitleToggle), self)
subtitleToggleBtn.widthAnchor.constraint(equalToConstant: 62).isActive = true
subtitleBtn = subtitleToggleBtn
updateSubtitleButton()
// 连播开关（播放完成后自动播放下一条；设置项 playback.autoNext）
autoNextBtn = iconBarButton("infinity", "连播：播完自动下一条（A 键）", #selector(onToggleAutoNext), self)
autoNextBtn.widthAnchor.constraint(equalToConstant: 62).isActive = true
        updateAutoNextButton()
fullscreenBtn.bezelStyle = .rounded


playerBoxStack = NSStackView()
playerBoxStack.orientation = .vertical
playerBoxStack.spacing = 0
playerBoxStack.addArrangedSubview(player)
// 控制条在下面组装完成后挂上来（两排布局）

// 左结果区 / 右播放器：用 NSSplitView —— 分隔条可拖动，且 autosaveName 会**自动记住**位置
// （原来用 NSStackView + "宽度=一半" 约束：既不能拖，退出全屏时约束还会被 Auto Layout 丢弃 → 缩水）
middleSplit = CleanSplitView()
middleSplit.isVertical = true                 // 竖直分隔条（左右分栏）
middleSplit.dividerStyle = .thin
middleSplit.delegate = self
middleSplit.translatesAutoresizingMaskIntoConstraints = false
middleSplit.addSubview(resultsScroll)
middleSplit.addSubview(playerBoxStack)
resultsScroll.widthAnchor.constraint(greaterThanOrEqualToConstant: 260).isActive = true
playerBoxStack.widthAnchor.constraint(greaterThanOrEqualToConstant: 320).isActive = true
resultsScroll.setContentHuggingPriority(.defaultLow, for: .horizontal)
playerBoxStack.setContentHuggingPriority(.defaultLow, for: .horizontal)
middleSplit.setContentHuggingPriority(.defaultLow, for: .vertical)
        playerBoxView = playerBoxStack
    }

    /// 拆自 buildUi()（原 253 行 ✗ 审计 #166 重启版 ✓ 纯代码搬移 ✓）
    private func buildStatusBarSection(_ root: NSView) {
// ---- 状态栏（关键状态写进界面，便于抓图取证）----
statusLabel = NSTextField(labelWithString: "就绪")
statusLabel.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
statusLabel.lineBreakMode = .byTruncatingTail

let body = NSStackView()
body.orientation = .vertical
body.spacing = 0
topBarView = topBarStack                       // B5：PiP 时隐藏
body.addArrangedSubview(topBarStack)
body.addArrangedSubview(middleSplit)
// 两排布局（用户建议）：第一排 = 播放键 + 模式 + **整条进度条** + 时间；第二排 = 质量/速度/音量 + 四个开关。
// 一排塞不下就只能挤（滑块变得极窄、按钮挤成一团），两排后进度条几乎占满窗口宽度，拖拽手感完全不同。
let row1 = NSStackView()
row1.orientation = .horizontal
row1.spacing = 8
row1.edgeInsets = NSEdgeInsets(top: 8, left: 12, bottom: 2, right: 12)
let row2 = NSStackView()
row2.orientation = .horizontal
row2.spacing = 8
row2.edgeInsets = NSEdgeInsets(top: 2, left: 12, bottom: 8, right: 12)
// 按用途把已有控件分配到两排（控件本身不变，只是换父亲）
for v in [prevButton, playButton, nextButton, modeButton, seekSlider, timeLabel] {
    row1.addArrangedSubview(v!)
}
row1.setCustomSpacing(14, after: modeButton)
// 第二排布局：左侧 [质量][速度] …弹性空白… 右侧 [🔊 音量][💬 ∞ ⤢ ⧉]
// 空白放**中段**，让音量与右侧按钮整体贴右 → 与第一排（进度条 + 右端时间）左右边缘对齐，视觉平衡。
// （之前空白放在行尾，把按钮全推到了左边，两排右边缘不齐 —— 用户实测反馈）
row2.addArrangedSubview(qualityPopup)
row2.addArrangedSubview(speedPopup)
row2.addArrangedSubview(volumeIcon)
row2.addArrangedSubview(volumeSlider)
for v in [subtitleToggleBtn, autoNextBtn, fullscreenBtn, pipBtn] {
    row2.addArrangedSubview(v!)
}
// 音量条加宽（原来太短不好拖）：给足下限，且允许它吸收中段空白以外的余量
// 音量条：加宽到 170~280pt，并让整排按**均分间距**铺开
//（原来中段放了一块弹性空白，视觉上就是一个大空缺 —— 用户实测反馈；均分后既没有空洞、又保持左右贴边对齐）
volumeSlider.widthAnchor.constraint(greaterThanOrEqualToConstant: 170).isActive = true
volumeSlider.widthAnchor.constraint(lessThanOrEqualToConstant: 280).isActive = true
row2.distribution = .equalSpacing
let controlsStack = NSStackView()
controlsStack.orientation = .vertical
controlsStack.spacing = 0
controlsStack.addArrangedSubview(row1)
controlsStack.addArrangedSubview(row2)
controlsBarView = controlsStack        // B5：PiP 时隐藏
playerBoxStack.addArrangedSubview(controlsStack)   // 挂到播放器栏（两排）
// 按钮/下拉一律**不允许被压扁**（否则窄窗口下会挤成竖条甚至被挤出画面 —— 用户实测反馈）；
// 只有两根滑块允许压缩：它们才是应该"占满剩余空间"的控件。
for v in [row1, row2].flatMap({ $0.arrangedSubviews }) where v !== seekSlider && v !== volumeSlider {
    v.setContentCompressionResistancePriority(.required, for: .horizontal)
}
seekSlider.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
seekSlider.setContentHuggingPriority(.defaultLow, for: .horizontal)     // 进度条独占第一排剩余宽度
volumeSlider.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
body.addArrangedSubview(statusLabel)
body.frame = root.bounds
body.autoresizingMask = [.width, .height]
root.addSubview(body)
window.contentView = root
window.makeFirstResponder(player)      // 启动即把键盘交给播放器（否则焦点在搜索框，单键快捷键全被吃掉）

startTicker()      // 0.5 秒刷新控制条与状态
appendLog("就绪（来源：\(sourcePopup.titleOfSelectedItem ?? "-")）")
NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] ev in
    self?.handleKey(ev) == true ? nil : ev
}
    }
}


/// 不画分隔线的分栏视图。
/// 那条 NSSplitView 分隔线在播放器视图的上下两截露在外面（上有留白、下是控制条），
/// 看起来就是"搜索结果与播放窗口之间有两条黑线"（用户实测反馈）。
/// 不画之后该处透出窗口背景（毛玻璃），与两栏底色一致，视觉上消失。
final class CleanSplitView: NSSplitView {
    override func drawDivider(in rect: NSRect) { }
}


/// 控制条图标按钮工厂：图标 + 悬浮提示（控制条按钮一多就拥挤，统一用图标省宽度）
func iconBarButton(_ symbol: String, _ tip: String, _ action: Selector, _ target: AnyObject) -> NSButton {
    let b = NSButton(title: "", target: target, action: action)
    b.bezelStyle = .rounded
    b.image = NSImage(systemSymbolName: symbol, accessibilityDescription: tip)
    b.imagePosition = .imageOnly
    b.toolTip = tip
    b.widthAnchor.constraint(greaterThanOrEqualToConstant: 36).isActive = true   // 图标按钮也要给足宽度，否则被挤成细条
    return b
}
