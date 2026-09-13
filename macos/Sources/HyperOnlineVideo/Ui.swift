import AppKit

/// 主界面（Phase 5.3）—— 与 Qt 端同布局：顶部来源/搜索栏 + 左侧结果列表 + 右侧播放器 + 底部控制条
extension AppDelegate {
    func buildUi() {
        window.title = "聚合视频 · Hyper Online Video（macOS）"
        window.setContentSize(NSSize(width: 1180, height: 720))
        window.minSize = NSSize(width: 900, height: 560)

        let root = NSView(frame: NSRect(x: 0, y: 0, width: 1180, height: 720))
        root.autoresizingMask = [.width, .height]

        // ---- 顶部栏 ----
        sourcePopup.addItems(withTitles: ["YouTube", "网易云音乐", "QQ音乐", "本地库"])
        sourcePopup.target = self
        sourcePopup.action = #selector(onSourceChanged)
        searchField.placeholderString = "输入关键词后回车（来源见左侧下拉：YouTube / 网易云 / QQ音乐 / 本地库）"
        searchField.target = self
        searchField.action = #selector(onSearch)
        searchField.frame = NSRect(x: 0, y: 0, width: 380, height: 24)

        let top = NSStackView()
        top.orientation = .horizontal
        top.spacing = 8
        top.edgeInsets = NSEdgeInsets(top: 8, left: 10, bottom: 8, right: 10)
        top.addArrangedSubview(sourcePopup)
        top.addArrangedSubview(searchField)
        for (title, sel) in [("搜索", #selector(onSearch)), ("打开本地文件", #selector(onOpenFile)),
                             ("设置", #selector(onSettings)), ("收藏", #selector(onFavorites)),
                             ("队列", #selector(onQueue)), ("登录", #selector(onLogin))] {
            let b = NSButton(title: title, target: self, action: sel)
            b.bezelStyle = .rounded
            top.addArrangedSubview(b)
        }

        // ---- 中部：左结果列表 / 右播放器 ----
        resultsTable.addTableColumn(NSTableColumn(identifier: NSUserInterfaceItemIdentifier("main")))
        resultsTable.headerView = nil
        resultsTable.rowHeight = 22
        resultsTable.dataSource = self
        resultsTable.delegate = self
        resultsTable.target = self
        resultsTable.doubleAction = #selector(onRowDoubleClick)
        resultsScroll.documentView = resultsTable
        resultsScroll.hasVerticalScroller = true
        resultsScroll.borderType = .noBorder

        player = MpvView(frame: NSRect(x: 0, y: 0, width: 800, height: 560))
        player.autoresizingMask = [.width, .height]
        if CommandLine.arguments.contains("--diag") { player.diag = true }
        if CommandLine.arguments.contains("--no-overlay") { player.overlay.hidden = true }

        // ---- 控制条 ----
        prevButton = NSButton(title: "⏮", target: self, action: #selector(onPrev))
        playButton = NSButton(title: "⏯", target: self, action: #selector(onPlayPause))
        nextButton = NSButton(title: "⏭", target: self, action: #selector(onNext))
        for b in [prevButton!, playButton!, nextButton!] {
            b.bezelStyle = .rounded
            b.widthAnchor.constraint(equalToConstant: 46).isActive = true
        }
        seekSlider = NSSlider(value: 0, minValue: 0, maxValue: 100, target: self, action: #selector(onSeek))
        seekSlider.isContinuous = false
        seekSlider.widthAnchor.constraint(greaterThanOrEqualToConstant: 260).isActive = true
        timeLabel = NSTextField(labelWithString: "0:00 / 0:00")
        timeLabel.font = .monospacedDigitSystemFont(ofSize: 12, weight: .regular)
        speedPopup = NSPopUpButton()      // IUO 必须先建实例（曾漏掉 → 启动即 nil trap）
        speedPopup.addItems(withTitles: ["0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "1.75x", "2.0x"])
        speedPopup.selectItem(withTitle: "1.0x")
        speedPopup.target = self
        speedPopup.action = #selector(onSpeed)
        volumeSlider = NSSlider(value: 100, minValue: 0, maxValue: 100, target: self, action: #selector(onVolume))
        volumeSlider.widthAnchor.constraint(equalToConstant: 110).isActive = true
        let fullBtn = NSButton(title: "全屏", target: self, action: #selector(onFullscreen))
        fullBtn.bezelStyle = .rounded

        let controls = NSStackView()
        controls.orientation = .horizontal
        controls.spacing = 8
        controls.edgeInsets = NSEdgeInsets(top: 6, left: 10, bottom: 6, right: 10)
        for v in [prevButton, playButton, nextButton, seekSlider, timeLabel, speedPopup, volumeSlider, fullBtn] {
            controls.addArrangedSubview(v!)
        }

        let playerBox = NSStackView()
        playerBox.orientation = .vertical
        playerBox.spacing = 0
        playerBox.addArrangedSubview(player)
        playerBox.addArrangedSubview(controls)

        let middle = NSStackView()
        middle.orientation = .horizontal
        middle.spacing = 0
        middle.distribution = .fill
        middle.addArrangedSubview(resultsScroll)
        middle.addArrangedSubview(playerBox)
        resultsScroll.widthAnchor.constraint(equalToConstant: 360).isActive = true
        playerBox.setContentHuggingPriority(.defaultLow, for: .horizontal)
        middle.setContentHuggingPriority(.defaultLow, for: .vertical)

        // ---- 状态栏（关键状态写进界面，便于抓图取证）----
        statusLabel = NSTextField(labelWithString: "就绪")
        statusLabel.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
        statusLabel.lineBreakMode = .byTruncatingTail

        let body = NSStackView()
        body.orientation = .vertical
        body.spacing = 0
        body.addArrangedSubview(top)
        body.addArrangedSubview(middle)
        body.addArrangedSubview(statusLabel)
        body.frame = root.bounds
        body.autoresizingMask = [.width, .height]
        root.addSubview(body)
        window.contentView = root

        startTicker()      // 0.5 秒刷新控制条与状态
        appendLog("就绪（来源：\(sourcePopup.titleOfSelectedItem ?? "-")）")
        NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] ev in
            self?.handleKey(ev) == true ? nil : ev
        }
    }

    // MARK: 状态与日志

    func setStatus(_ s: String) {
        statusLabel.stringValue = s
        Config.log("[状态] \(s)")
    }

    func appendLog(_ s: String) {
        logLines.append(s)
        Config.log("[列表] \(s)")        // 同时也进 stderr：自动化只看日志也能判定
        if logLines.count > 400 { logLines.removeFirst() }
        resultsTable.reloadData()
        let n = resultsTable.numberOfRows
        if n > 0 { resultsTable.scrollRowToVisible(n - 1) }
    }

    // MARK: 定时刷新

    private func startTicker() {
        let t = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            guard let self else { return }
            self.tickCount += 1
            if CommandLine.arguments.contains("--diag"), self.tickCount % 6 == 1 {
                Config.log("[diag] tick #\(self.tickCount) playerNil=\(self.player == nil) mediaKeysNil=\(self.mediaKeys == nil) labelLen=\(self.playLabel.count)")
            }
            self.player?.refreshStatus()
            if let p = self.player {
                let dur = p.duration(), pos = p.position()
                self.seekSlider.maxValue = max(dur, 1)
                if !self.seekSlider.isHighlighted { self.seekSlider.doubleValue = pos }
                self.timeLabel.stringValue = String(format: "%@ / %@", Self.clock(pos), Self.clock(dur))
                // 状态栏显示"我们自己的标题"而不是 mpv 的 media-title
                // （流媒体的 media-title 是一长串 URL，没法看）
                let extra = p.overlay.cues.isEmpty ? "" : "   字幕:\(p.overlay.sourceName)(\(p.overlay.cues.count))"
                // 正在播放信息（控制中心/锁屏读它）
                let artist = self.playLabel.contains(" — ") ? String(self.playLabel.split(separator: " — ").last ?? "") : ""
                self.mediaKeys?.updateNowPlaying(title: self.playLabel, artist: artist,
                                                 duration: dur, paused: p.paused(), position: pos)
                let head = self.playLabel.isEmpty ? p.lastStatus : "\(self.playLabel)   \(Self.clock(pos)) / \(Self.clock(dur))\(p.paused() ? "  [暂停]" : "")"
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

    func handleKey(_ ev: NSEvent) -> Bool {
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
        case "c": cycleSubtitleTrack(); return true
        case "s": player.overlay.hidden.toggle(); setStatus(player.overlay.hidden ? "字幕：关" : "字幕：开"); return true
        case "a": toggleFavorite(); return true
        case "m": queue.cycleMode(); setStatus("队列模式：\(queue.modeLabel)"); return true
        default: return false
        }
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
            setStatus("字幕：关")
            return
        }
        let t = player.tracks[idx]
        player.overlay.cues = t.cues.isEmpty ? Subtitles.loadFile(t.source) : t.cues
        player.overlay.sourceName = t.label
        player.overlay.karaoke = ["lrc", "yrc", "qrc"].contains((t.source as NSString).pathExtension.lowercased())
        setStatus("字幕轨：\(t.label)（\(player.overlay.cues.count) 行）")
    }
}
