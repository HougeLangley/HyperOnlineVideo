import AppKit

/// 纯视频全屏：只留画面。
/// - 鼠标移到**左侧**：滑出播放列表浮层（点其他视频即切换）
/// - 鼠标移到**底部**：滑出控制条浮层（按钮 + 进度）
/// - 退出：双击画面 / 按 Esc / 点浮层里的「退出全屏」
/// 实现要点：只把结果区、控制条**摘出来当浮层**，播放器视图始终不动（GL 视图一挪就会丢上下文）。
extension AppDelegate {
    /// 进入纯视频全屏
    func enterVideoFullscreen() {
        guard !videoFullscreen else { return }
        videoFullscreen = true
        player.setPlayerCornerRadius(0)      // 全屏铺满：圆角会让屏幕四角露黑
        fsTransitioning = true      // 过渡期间的重排不算用户拖动（退出时会瞬时变成全宽，实测踩到）
        listPaneWidth = resultsScroll.frame.width          // 记住结果区宽度，退出时原样恢复
        if listPaneWidth < 100 { listPaneWidth = 380 }
        controlsParent = controlsBarView?.superview as? NSStackView
        Config.log("[FS] 进入全屏，记住结果区宽度 = \(Int(listPaneWidth))pt")
        guard let content = window.contentView else { return }

        // 结果区 → 左侧「毛玻璃」浮层（先打开 translatesAutoresizingMask，否则 frame 会被 Auto Layout 覆盖）
        resultsScroll.translatesAutoresizingMaskIntoConstraints = true
        controlsBarView?.translatesAutoresizingMaskIntoConstraints = true
        resultsScroll.removeFromSuperview()
        // 玻璃面板：macOS 原生 NSVisualEffectView（.hudWindow + .withinWindow = 模糊窗口内内容）
        let panel = NSVisualEffectView(frame: NSRect(x: 12, y: 12, width: 448, height: content.bounds.height - 24))
        panel.material = .hudWindow
        panel.blendingMode = .withinWindow
        panel.state = .active
        panel.wantsLayer = true
        panel.layer?.cornerRadius = 16
        panel.layer?.masksToBounds = true
        panel.layer?.borderWidth = 1
        panel.layer?.borderColor = NSColor.white.withAlphaComponent(0.14).cgColor
        panel.autoresizingMask = [.height]
        panel.isHidden = true
        resultsScroll.frame = panel.bounds
        resultsScroll.autoresizingMask = [.width, .height]
        resultsScroll.drawsBackground = false          // 让玻璃透出来
        panel.addSubview(resultsScroll)
        glassPanel = panel
        content.addSubview(panel, positioned: .above, relativeTo: nil)

        // 控制条 → 底部浮层（额外加一个「退出全屏」按钮，退出入口要显眼）
        controlsBarView?.removeFromSuperview()
        if let bar = controlsBarView {
            let h = max(40, bar.fittingSize.height)
            // 居中 + 半透明 + 离底 26pt（避开 macOS Dock），两侧留白
            let w = min(content.bounds.width - 80, 980)
            bar.frame = NSRect(x: (content.bounds.width - w) / 2, y: fsBarBottomInset, width: w, height: h)
            bar.autoresizingMask = [.minXMargin, .maxXMargin, .maxYMargin]
            bar.wantsLayer = true
            bar.layer?.backgroundColor = NSColor.black.withAlphaComponent(0.55).cgColor   // 半透明
            bar.layer?.cornerRadius = 12
            bar.layer?.masksToBounds = true
            bar.isHidden = true
            content.addSubview(bar, positioned: .above, relativeTo: nil)
        }
        topBarView?.isHidden = true
        statusLabel?.isHidden = true

        if !window.styleMask.contains(.fullScreen) { window.toggleFullScreen(nil) }
        startHoverWatch()
        showFsHint("双击画面 / 按 Esc 退出全屏 · 鼠标移到左边看列表、移到底部看控制条")
        endFsTransition(after: 1.2)
        Config.log("[FS] 进入纯视频全屏（⌘⌃F 或双击画面进入；Esc/双击退出）")
    }

    /// 退出纯视频全屏：把浮层放回原来的位置
    func exitVideoFullscreen() {
        guard videoFullscreen else { return }
        videoFullscreen = false
        player.setPlayerCornerRadius(12)     // 回到窗口：恢复圆角
        fsTransitioning = true
        hoverTimer?.invalidate()
        hoverTimer = nil
        topBarView?.isHidden = false
        statusLabel?.isHidden = false
        resultsScroll.removeFromSuperview()          // 从玻璃面板里摘出来
        glassPanel?.removeFromSuperview()
        glassPanel = nil
        resultsScroll.drawsBackground = false       // 整窗毛玻璃：列表背景也保持透明
        resultsScroll.translatesAutoresizingMaskIntoConstraints = false   // 交回给分隔视图管理
        controlsBarView?.translatesAutoresizingMaskIntoConstraints = false
        resultsScroll.isHidden = false
        resultsScroll.autoresizingMask = [.width, .height]
        resultsScroll.layer?.backgroundColor = nil
        // 放回分隔视图，并按进全屏前的宽度恢复（否则会缩到最小宽 —— 用户实测反馈）
        let restoreWidth = listPaneWidth        // 先存局部：重排回调会把 listPaneWidth 覆盖成全宽（实测踩到）
        if let sv = middleSplit {
            // 必须插在播放器栏**之前**：直接 addSubview 会追加到末尾 → 列表跑右、视频跑左（用户实测反馈）
            if let pb = playerBoxView {
                sv.addSubview(resultsScroll, positioned: .below, relativeTo: pb)
            } else {
                sv.addSubview(resultsScroll)
            }
            sv.adjustSubviews()
            sv.setPosition(restoreWidth, ofDividerAt: 0)
            // 退出全屏时窗口尺寸要过一会儿才恢复，期间 setPosition 会被夹小 → 多落几次兜底
            for delay in [0.3, 0.8, 1.5, 2.2] {
                DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
                    guard let self, !self.videoFullscreen, let sv2 = self.middleSplit else { return }
                    sv2.setPosition(restoreWidth, ofDividerAt: 0)
                    self.listPaneWidth = restoreWidth
                }
            }
            Config.log("[FS] 退出全屏，恢复结果区宽度 = \(Int(restoreWidth))pt")
        } else {
            window.contentView?.addSubview(resultsScroll)
        }
        controlsBarView?.removeFromSuperview()
        controlsBarView?.isHidden = false
        controlsBarView?.layer?.backgroundColor = nil
        if let p = controlsParent { p.addArrangedSubview(controlsBarView) } else { window.contentView?.addSubview(controlsBarView!) }
        if window.styleMask.contains(.fullScreen) { window.toggleFullScreen(nil) }
        fsHintLabel?.removeFromSuperview()
        fsHintLabel = nil
        setStatus("已退出全屏")
        Config.log("[FS] 已退出纯视频全屏")
        endFsTransition(after: 1.2)
    }

    func toggleVideoFullscreen() { videoFullscreen ? exitVideoFullscreen() : enterVideoFullscreen() }

    /// 过渡窗口结束后才允许把宽度当成用户拖动记录
    private func endFsTransition(after seconds: Double) {
        DispatchQueue.main.asyncAfter(deadline: .now() + seconds) { [weak self] in
            self?.fsTransitioning = false
            // 注意：这里**不要**用 frame 回写 listPaneWidth ——
            // 全屏期间结果区是浮层（固定 460pt 宽），会把用户设定的宽度覆盖掉（实测踩到）
        }
    }

    /// 鼠标靠近边缘时显示浮层（0.15 秒轮询；只在全屏期间运行）
    private func startHoverWatch() {
        hoverTimer?.invalidate()
        hoverTimer = Timer.scheduledTimer(withTimeInterval: 0.15, repeats: true) { [weak self] _ in
            self?.updateHoverVisibility()
        }
    }

    /// 依据鼠标位置显示/隐藏两个浮层（带**迟滞**：靠近边缘即弹出，离开一段距离才收起）
    /// 之前的写法是"离开 70pt 就立刻收起"，结果鼠标刚移到列表上、列表就没了（用户实测反馈）。
    func updateHoverVisibility(forceMouse: NSPoint? = nil) {
        guard videoFullscreen else { return }
        // 用户可关：ui.hoverReveal=false 时全屏不再浮出面板（与 Linux 同键 ✓）
        if !settings.bool("ui.hoverReveal", true) {
            glassPanel?.isHidden = true
            controlsBarView?.isHidden = true
            return
        }
        let m = forceMouse ?? NSEvent.mouseLocation          // 屏幕坐标
        let f = window.frame

        // —— 左侧列表（玻璃面板）——
        let listW = glassPanel?.frame.width ?? resultsScroll.frame.width
        let listShowAt = f.minX + 70                          // 进入这个范围就弹出
        let listKeepUntil = f.minX + listW + 140              // 还在面板右侧 140pt 内就保持
        if m.x <= listShowAt { listOverlayVisible = true }
        else if m.x > listKeepUntil { listOverlayVisible = false }

        // —— 底部控制条 ——
        let barH = (controlsBarView?.frame.height ?? 40) + fsBarBottomInset
        let barShowAt = f.minY + barH + 40                    // 靠近底部即弹出
        let barKeepUntil = f.minY + barH + 160                // 离开一段距离才收起
        if m.y <= barShowAt { barOverlayVisible = true }
        else if m.y > barKeepUntil { barOverlayVisible = false }

        // 全屏后窗口尺寸会变：把控制条浮层按当前宽度重新居中（幂等，代价极小）
        if let bar = controlsBarView, let content = window.contentView {
            let w = min(content.bounds.width - 80, 980)
            let x = (content.bounds.width - w) / 2
            let h = max(40, bar.fittingSize.height)
            if abs(bar.frame.width - w) > 1 || abs(bar.frame.origin.x - x) > 1 || abs(bar.frame.height - h) > 1
                || abs(bar.frame.origin.y - fsBarBottomInset) > 1 {
                bar.frame = NSRect(x: x, y: fsBarBottomInset, width: w, height: h)
                Config.log("[FS] 控制条浮层重新居中：x=\(Int(x)) 宽=\(Int(w)) 高=\(Int(h))（离底 \(Int(fsBarBottomInset))pt）")
            }
        }
        let listView: NSView? = glassPanel ?? resultsScroll
        if let lv = listView, lv.isHidden == listOverlayVisible { lv.isHidden = !listOverlayVisible }
        if let bar = controlsBarView, bar.isHidden == barOverlayVisible { bar.isHidden = !barOverlayVisible }
    }

    /// 进入全屏后显示的提示（几秒后自动消失）
    private func showFsHint(_ text: String) {
        fsHintLabel?.removeFromSuperview()
        guard let content = window.contentView else { return }
        let label = NSTextField(labelWithString: text)
        label.font = .systemFont(ofSize: 13)
        label.textColor = .white
        label.alignment = .center
        label.wantsLayer = true
        label.layer?.backgroundColor = NSColor.black.withAlphaComponent(0.65).cgColor
        label.layer?.cornerRadius = 8
        label.frame = NSRect(x: (content.bounds.width - 560) / 2, y: content.bounds.height - 90, width: 560, height: 30)
        label.autoresizingMask = [.minYMargin, .minXMargin, .maxXMargin]
        content.addSubview(label, positioned: .above, relativeTo: nil)
        fsHintLabel = label
        DispatchQueue.main.asyncAfter(deadline: .now() + 4.0) { [weak self] in
            guard let self, self.fsHintLabel === label else { return }
            label.removeFromSuperview()
            self.fsHintLabel = nil
        }
    }
}


// MARK: - 分隔条（左结果区宽度：可拖动 + 记住）
extension AppDelegate: NSSplitViewDelegate {
    /// 拖动分隔条后：记住宽度（进全屏恢复用）并写回设置（跨启动记住）
    func splitViewDidResizeSubviews(_ notification: Notification) {
        // 启动初期布局也会触发这个回调：必须等【初始宽度应用完成】后才当成用户拖动，
        // 否则会把旧的半宽值写回设置、冲掉用户的选择（实测踩到）
        // 只认**真正的鼠标拖动**：布局自动重排（窗口尺寸变化、控制条最小宽度变化、进出全屏/画中画）
        // 也会走到这里，误存下来就会把用户的分隔宽度冲掉 —— 实测把 ui.listWidth 写成了 683pt，
        // 结果列表占掉 58% 宽、播放区被挤扁、控制条溢出（用户看到"界面完全乱了"）。
        guard let sv = notification.object as? NSSplitView, sv === middleSplit,
              !videoFullscreen, !pipActive, !restoringPaneWidth, paneWidthInitialized, !fsTransitioning,
              NSEvent.pressedMouseButtons & 0x1 != 0 else { return }
        let w = resultsScroll.frame.width
        guard w > 100 else { return }
        listPaneWidth = w
        paneWidthSaveWork?.cancel()
        paneWidthSaveWork = DispatchWorkItem { [weak self] in          // 拖动过程少写盘
            self?.settings.apply(key: "ui.listWidth", value: String(Int(w)))
            Config.log("[LAYOUT] 结果区宽度已记住 = \(Int(w))pt")
        }
        if let work = paneWidthSaveWork { DispatchQueue.main.asyncAfter(deadline: .now() + 0.6, execute: work) }
    }

    /// 限制两栏的最小宽度（拖动时不会把某一栏挤没）
    func splitView(_ splitView: NSSplitView, constrainMinCoordinate proposedMinimumPosition: CGFloat,
                   ofSubviewAt dividerIndex: Int) -> CGFloat {
        max(proposedMinimumPosition, 260)
    }
    func splitView(_ splitView: NSSplitView, constrainMaxCoordinate proposedMaximumPosition: CGFloat,
                   ofSubviewAt dividerIndex: Int) -> CGFloat {
        // 播放区至少要放得下整条控制条（按钮「不允许被压扁」，见 Ui.swift）：约 580pt。
        // 之前只留 340pt：能拖到播放区仅 497pt → 控制条溢出到第二行（画中画跑下去 = 用户看到的"界面乱掉"）。
        min(proposedMaximumPosition, max(300, splitView.bounds.width - 580))
    }

    /// 应用结果区宽度：设置里有就用设置的；否则给"不大不小"的默认（约 38%，上限 460pt）
    func applySavedPaneWidth(attempt: Int = 0) {
        guard let sv = middleSplit, !videoFullscreen else { return }
        window.contentView?.layoutSubtreeIfNeeded()
        // 窗口布局未完成时 sv 宽度还是暂态值，setPosition 会被"最大坐标"夹小（实测 420 → 241）→ 稍后重试
        guard sv.bounds.width >= 600 || attempt >= 12 else {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [weak self] in
                self?.applySavedPaneWidth(attempt: attempt + 1)
            }
            return
        }
        let saved = settings.number("ui.listWidth", 0)
        let limit = max(300, sv.bounds.width - 580)      // 与拖动上限一致：保证控制条放得下
        // 默认宽度：**按卡片尺寸算「横向刚好 2 张」**（与 Linux 的 defaultPanelWidth() 同一套定义）。
        // 原实现是"窗口 38%、上限 460"—— 那是偶然值，窄窗口下放不下 2 列（用户实测反馈，要求两端对齐）。
        let card = gridItemSize()
        let defW = 2.0 * card.width + 3.0 * 10.0 + 20.0  // 2 张卡片 + 三处间距 + 左右留白
        let w = saved > 100 ? min(CGFloat(saved), limit)
                            : min(limit, max(300, defW))
        restoringPaneWidth = true
        sv.setPosition(w, ofDividerAt: 0)
        listPaneWidth = w
        DispatchQueue.main.async {
            self.restoringPaneWidth = false
            self.paneWidthInitialized = true      // 此后的重排才算用户拖动
        }
        Config.log("[LAYOUT] 结果区宽度 = \(Int(w))pt（\(saved > 100 ? "来自设置" : "默认：卡片×2 = \(Int(defW))pt")；拖动分隔条可调，会自动记住）")
        if saved <= 100, let win = window {          // 首次运行：把窗口高度调到"纵向 4 张"（与 Linux 一致）
            let need = 4.0 * card.height + 5.0 * 10.0 + 200.0
            if win.frame.height < need {
                Config.log("[LAYOUT] 首次运行：窗口高度 \(Int(win.frame.height)) → \(Int(need))pt（纵向 4 张）")
                win.setContentSize(NSSize(width: win.frame.width, height: need))
            }
        }
    }
}
