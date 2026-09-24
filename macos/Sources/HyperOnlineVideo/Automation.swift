import AppKit

/// 自动化探针（**纯 CLI 取证代码** ✓ 与业务逻辑无关 ✓ 从 `applicationDidFinishLaunching` 拆出）
///
/// 为什么拆：启动函数曾达 **509 行** ✗（审计量化 #150 同批），其中绝大多数是 `--xxx` 探针分支 ✓。
/// 拆出后启动流程只剩"真正的启动逻辑" ✓；探针集中在这里便于查找与维护 ✓。
/// 注意：本扩展与 AppDelegate 同模块 ✓ 可直接访问其 internal 成员 ✓（无需改访问级别 ✓）。
extension AppDelegate {

    /// 安装全部自动化探针（在启动流程末尾调用 ✓ 只解析参数并安排定时器 ✓ 不做实际播放 ✗）
    func installAutomationProbes(_ args: [String]) {
// B6：面板（自动化取证用）--panel settings|favorites|queue
if let i = args.firstIndex(of: "--panel"), i + 1 < args.count {
    let which = args[i + 1]
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) { [weak self] in
        guard let self else { return }
        switch which {
        case "settings":
            self.openSettingsPanel()
            if args.contains("--panel-save") {     // 自动化：等价于点一次「保存」（便于回归取证）
                DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { self.settingsPanel?.triggerSave() }
            }
        case "favorites": self.openFavoritesPanel()
        case "queue": self.openQueuePanel()
        case "downloads": self.openDownloadsPanel()      // W2 ✓ 与 Linux `--panel downloads` 同旗标 ✓
        default: Config.log("未知面板：\(which)")
        }
    }
}
// 设置面板保存后：把影响运行时的项立刻应用（字号/档位/cookie 来源等）
NotificationCenter.default.addObserver(forName: Notification.Name("hovSettingsChanged"),
                                       object: nil, queue: .main) { [weak self] _ in
    self?.applySettingsRuntime()
}
// B7 自动化：N 秒后进全屏；--fill-screen/--no-fill-screen 预置设置
if args.contains("--fill-screen") { _ = settings.apply(key: "video.fillScreen", value: "true") }
if args.contains("--no-fill-screen") { _ = settings.apply(key: "video.fillScreen", value: "false") }
if let i = args.firstIndex(of: "--fullscreen-after"), i + 1 < args.count,
   let secs = Double(args[i + 1]) {
    DispatchQueue.main.asyncAfter(deadline: .now() + secs) { [weak self] in
        self?.window.toggleFullScreen(nil)
    }
}
// 自动化探针：--key-probe 打印一组按键的归属判定（⌘Q 归菜单 / 输入中归输入框 / 单键归播放器）
if args.contains("--key-probe") {
    typealias P = PlayerKeyPolicy
    let cases: [(String, UInt16, String, NSEvent.ModifierFlags, Bool)] = [
        ("⌘Q", 12, "q", [.command], false),
        ("⌘,", 43, ",", [.command], false),
        ("⌘O", 31, "o", [.command], false),
        ("单键 p（播放器焦点）", 35, "p", [], false),
        ("单键 p（输入框中）", 35, "p", [], true),
        ("空格（输入框中）", 49, " ", [], true),
        ("未定义键 x", 7, "x", [], false),
        ("空格（弹窗在前）", 49, " ", [], false),
    ]
    for (name, code, chars, mods, editing) in cases {
        let mainKey = !(name.contains("弹窗在前"))
        let who = P.shouldHandle(keyCode: code, characters: chars, modifiers: mods, isEditing: editing,
                                 isMainWindowKey: mainKey)
            ? "播放器接管" : (mods.contains(.command) ? "交给菜单" : (editing ? "交给输入框" : "忽略"))
        Config.log("[KEYPROBE] \(name) → \(who)")
    }
    // 再验证"聚焦搜索框时能否正确识别为输入中"（这是 bug 的关键判定条件）
    DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in
        guard let self else { return }
        self.window.makeFirstResponder(self.searchField)      // 等价于用户点了一下搜索框
        let editing = (NSApp.keyWindow?.firstResponder as? NSTextView) != nil
        Config.log("[KEYPROBE] 聚焦搜索框 → 判定为输入中=\(editing)（期望 true）")
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
            self.window.makeFirstResponder(self.player)        // 等价于点了一下画面
            let editing2 = (NSApp.keyWindow?.firstResponder as? NSTextView) != nil
            Config.log("[KEYPROBE] 聚焦播放器 → 判定为输入中=\(editing2)（期望 false）")
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                Config.log("[KEYPROBE] 探测完成，自动退出（这是纯 CLI 探针）")
                exit(0)
            }
        }
    }
}
// 诊断：--dump-viewtree 打印表格相关视图的 frame 链（查点击/坐标问题）
if args.contains("--dump-viewtree") {
    DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { [weak self] in
        guard let self else { return }
        let t = self.resultsGrid as NSView
        var chain = "表格链: "
        var v: NSView? = t
        while let cur = v {
            chain += "\(type(of: cur))[f=\(Int(cur.frame.origin.x)),\(Int(cur.frame.origin.y)) "
                  + "\(Int(cur.frame.width))x\(Int(cur.frame.height)) flipped=\(cur.isFlipped) b=\(Int(cur.bounds.origin.y))] "
            v = cur.superview
        }
        Config.log("[VIEWTREE] \(chain)")
        let l = self.resultsGrid.collectionViewLayout
        let content = l?.collectionViewContentSize ?? .zero
        let order = self.middleSplit?.subviews.map { $0 === self.resultsScroll ? "列表" : ($0 === self.playerBoxView ? "播放器" : "?") }.joined(separator: "|") ?? "-"
        Config.log("[VIEWTREE] 音频：\(self.player.audioStatus())")
        Config.log("[VIEWTREE] 分栏顺序=\(order) 结果区宽=\(Int(self.resultsScroll.frame.width))pt 全屏=\(self.videoFullscreen)")
        Config.log("[VIEWTREE] 窗口=\(Int(self.window.frame.width))x\(Int(self.window.frame.height)) "
                   + "卡片数=\(self.rows.count) 卡片尺寸=\(self.gridItemSize()) "
                   + "网格frame=\(Int(self.resultsGrid.frame.width))x\(Int(self.resultsGrid.frame.height)) "
                   + "布局内容高=\(Int(content.height)) "
                   + "可视高=\(Int(self.resultsScroll.contentView.bounds.height)) "
                   + "可滚动=\(self.resultsGrid.frame.height > self.resultsScroll.contentView.bounds.height + 1)")
    }
}
// 自动化：--click-row <n> 向第 n 行发**合成鼠标点击**（单击+双击各一次），验证事件真的能到表格。
// 注意：必须先把该行滚入可见 —— 列表会自动滚到底部，直接换算坐标会落在窗口外（实测踩到）。
// 自动化：--query2 <关键词> --query2-source <序号> --query2-after <秒>
// 复现"先 A 源搜索、切换来源、再搜"的序列（用户实测反馈的顺序问题）
if let i = args.firstIndex(of: "--query2"), i + 1 < args.count {
    let kw2 = args[i + 1]
    var src2 = 0
    if let j = args.firstIndex(of: "--query2-source"), j + 1 < args.count, let n = Int(args[j + 1]) { src2 = n }
    var after = 8.0
    if let j = args.firstIndex(of: "--query2-after"), j + 1 < args.count, let sec = Double(args[j + 1]) { after = sec }
    DispatchQueue.main.asyncAfter(deadline: .now() + after) { [weak self] in
        guard let self else { return }
        Config.log("[SEQ] 第二次搜索：切换来源 index=\(src2)（\(self.sourcePopup.itemTitle(at: src2))）关键词=\(kw2)")
        self.sourcePopup.selectItem(at: src2)
        self.onSourceChanged()
        self.searchField.stringValue = kw2
        self.onSearch()
    }
}
// 自动化：--subtitle-off-at <秒> 到点调用"字幕开关"（验证控制条按钮/S 键同一条链路）
if let i = args.firstIndex(of: "--subtitle-off-at"), i + 1 < args.count, let sec = Double(args[i + 1]) {
    DispatchQueue.main.asyncAfter(deadline: .now() + sec) { [weak self] in
        guard let self else { return }
        Config.log("[SUB] 到点触发字幕开关（当前 hidden=\(self.player.overlay.hidden)）")
        self.onSubtitleToggle()
        Config.log("[SUB] 开关后 hidden=\(self.player.overlay.hidden) 按钮标题=\(self.subtitleBtn?.title ?? "-")")
    }
}
// 自动化：--set-pane-width <n> 模拟用户拖动分隔条（验证"宽度会被记住"）
if let i = args.firstIndex(of: "--set-pane-width"), i + 1 < args.count, let w = Double(args[i + 1]) {
    DispatchQueue.main.asyncAfter(deadline: .now() + 2.5) { [weak self] in
        guard let self, let sv = self.middleSplit else { return }
        Config.log("[PANEW] 模拟拖动到 \(Int(w))pt")
        sv.setPosition(CGFloat(w), ofDividerAt: 0)
    }
}
// 自动化：--fs-probe 进入纯视频全屏，并用"强制鼠标位置"验证左右/底部浮层的显隐逻辑
if args.contains("--fs-probe") {
    DispatchQueue.main.asyncAfter(deadline: .now() + 4.0) { [weak self] in
        guard let self else { return }
        _ = self.dumpUi(to: "/tmp/fs_before.png")      // 进全屏前（确定性抓帧）
        Config.log("[FS-PROBE] 已抓 /tmp/fs_before.png（进全屏前）")
        self.enterVideoFullscreen()
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
            self.hoverTimer?.invalidate()      // 探针期间停掉轮询，否则强制位置会被真实鼠标位置覆盖（实测踩到）
            self.hoverTimer = nil
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
            let f = self.window.frame
            func dump(_ tag: String) {
                Config.log("[FS-PROBE] \(tag) 列表浮层=\((self.glassPanel ?? self.resultsScroll).isHidden ? "隐藏" : "显示") "
                           + "控制条浮层=\((self.controlsBarView?.isHidden ?? true) ? "隐藏" : "显示") "
                           + "顶栏=\((self.topBarView?.isHidden ?? false) ? "隐藏" : "显示") 全屏中=\(self.videoFullscreen)")
            }
            let listW = self.resultsScroll.frame.width
            self.updateHoverVisibility(forceMouse: NSPoint(x: f.midX, y: f.midY)); dump("① 鼠标在中间（远处）")
            self.updateHoverVisibility(forceMouse: NSPoint(x: f.minX + 20, y: f.midY)); dump("② 触及左边缘")
            self.updateHoverVisibility(forceMouse: NSPoint(x: f.minX + 200, y: f.midY)); dump("③ 移到列表上（应保持）")
            // 抓一张"玻璃面板可见"的整屏图（等一帧，否则拍不到刚显示的浮层）
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                defer {   // 拍完再做"移开"用例，否则抓图时已经隐藏了（实测踩到顺序问题）
                    self.updateHoverVisibility(forceMouse: NSPoint(x: f.minX + listW + 220, y: f.midY)); dump("④ 移开列表 220pt（应收起）")
                }
                if let gp = self.glassPanel {
                    Config.log("[FS-PROBE] 玻璃面板：frame=\(Int(gp.frame.origin.x)),\(Int(gp.frame.origin.y)) "
                               + "\(Int(gp.frame.width))x\(Int(gp.frame.height)) hidden=\(gp.isHidden) "
                               + "结果区=\(Int(self.resultsScroll.frame.width))x\(Int(self.resultsScroll.frame.height)) "
                               + "卡片数=\(self.rows.filter { !$0.key.isEmpty }.count) 缩略图=\(self.thumbCache.count)")
                }
                _ = self.dumpUi(to: "/tmp/fs_glass.png")
                Config.log("[FS-PROBE] 已抓 /tmp/fs_glass.png（毛玻璃列表面板可见时）")
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                self.updateHoverVisibility(forceMouse: NSPoint(x: f.midX, y: f.minY + 30)); dump("⑤ 触及底部")
            }
            // 抓图要等界面刷新一帧，否则拍不到刚显示的浮层（实测踩到）
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                if let bar = self.controlsBarView {
                    Config.log("[FS-PROBE] 控制条浮层：frame=\(Int(bar.frame.origin.x)),\(Int(bar.frame.origin.y)) "
                               + "\(Int(bar.frame.width))x\(Int(bar.frame.height)) hidden=\(bar.isHidden) "
                               + "子视图=\(bar.arrangedSubviews.count) content=\(Int(self.window.contentView?.bounds.width ?? 0))x\(Int(self.window.contentView?.bounds.height ?? 0))")
                }
                _ = self.dumpUi(to: "/tmp/fs_bar.png")
                Config.log("[FS-PROBE] 已抓 /tmp/fs_bar.png（控制条浮层可见时）")
                self.updateHoverVisibility(forceMouse: NSPoint(x: f.midX, y: f.minY + 400)); dump("⑥ 移开底部 400pt（应收起）")
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
                self.exitVideoFullscreen()
                let order = self.middleSplit?.subviews.map { $0 === self.resultsScroll ? "列表" : ($0 === self.playerBoxView ? "播放器" : "?") }.joined(separator: "|") ?? "-"
                Config.log("[FS-PROBE] 已退出：全屏中=\(self.videoFullscreen) 顶栏=\((self.topBarView?.isHidden ?? false) ? "隐藏" : "显示") 分栏顺序=\(order) 结果区宽=\(Int(self.resultsScroll.frame.width))pt")
                DispatchQueue.main.asyncAfter(deadline: .now() + 2.5) {   // 等窗口从全屏恢复
                    _ = self.dumpUi(to: "/tmp/fs_after.png")
                    let o2 = self.middleSplit?.subviews.map { $0 === self.resultsScroll ? "列表" : ($0 === self.playerBoxView ? "播放器" : "?") }.joined(separator: "|") ?? "-"
                    Config.log("[FS-PROBE] 已抓 /tmp/fs_after.png（退出后）：分栏顺序=\(o2) 结果区宽=\(Int(self.resultsScroll.frame.width))pt")
                }
            }
        }
    }
}
// 自动化：--panel-cycle <次数>：反复"打开设置面板 → 关闭"（验证窗口过度释放崩溃已修）
if let i = args.firstIndex(of: "--panel-cycle"), i + 1 < args.count, let n = Int(args[i + 1]) {
    for k in 0..<n {
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0 + Double(k) * 0.6) { [weak self] in
            guard let self else { return }
            self.openSettingsPanel()
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
                self.settingsPanel?.closeForTest()
                Config.log("[PROBE] 面板第 \(k + 1)/\(n) 次开关完成")
            }
        }
    }
}
// 自动化：--open-files-twice <秒>：连续两次调用"打开本地文件"的核心逻辑（验证不再崩溃）
if let i = args.firstIndex(of: "--open-files-twice"), i + 1 < args.count, let sec = Double(args[i + 1]) {
    DispatchQueue.main.asyncAfter(deadline: .now() + sec) { [weak self] in
        guard let self else { return }
        Config.log("[PROBE] 第一次 openFiles")
        self.openFiles(["/tmp/auto1.mp4", "/tmp/auto2.mp4"])
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
            Config.log("[PROBE] 第二次 openFiles")
            self.openFiles(["/tmp/auto3.mp4"])
            Config.log("[PROBE] 两次 openFiles 完成，未崩溃")
        }
    }
}
// 自动化：--login-choice <按钮序号>：等价于"在登录弹窗里点了第 N 个按钮"
// （弹窗无法被脚本点击，这里直接喂返回值，走与真实点击**完全相同**的处理链路）
if let i = args.firstIndex(of: "--login-choice"), i + 1 < args.count, let idx = Int(args[i + 1]) {
    let sites = ["youtube", "bilibili", "netease", "qqmusic", "qqmusic_wx"]
    let titles = ["YouTube", "B站", "网易云音乐", "QQ音乐 · QQ 登录", "QQ音乐 · 微信登录"]
    let codes = (0..<(titles.count + 2)).map { Self.alertButtonCode($0) }
    DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in
        Config.log("[登录探针] 模拟点击第 \(idx) 个按钮（返回码 \(Self.alertButtonCode(idx).rawValue)）")
        self?.handleLoginChoice(Self.alertButtonCode(idx), codes: codes, sites: sites, titles: titles)
    }
}
// 自动化：--audio-probe <秒>（+3 秒各报一次音频输出状态，排查"有画面没声音"）
if let i = args.firstIndex(of: "--audio-probe"), i + 1 < args.count, let sec = Double(args[i + 1]) {
    // 采样窗口拉长到 60 秒：短窗口（0/3/6）抓不到「播一会儿音频才掉」（用户实测）
    for offset in [0.0, 3.0, 6.0, 15.0, 30.0, 45.0, 60.0] {
        DispatchQueue.main.asyncAfter(deadline: .now() + sec + offset) { [weak self] in
            guard let self else { return }
            Config.log("[AUDIO] +\(Int(sec + offset))秒：\(self.player.audioStatus())")
        }
    }
}
// 自动化：--play-row2 <n> --play-row2-at <秒> 第二次点击（复现"快速连点两条"的竞态）
if let i = args.firstIndex(of: "--play-row2"), i + 1 < args.count, let row = Int(args[i + 1]) {
    var at = 3.5
    if let j = args.firstIndex(of: "--play-row2-at"), j + 1 < args.count, let sec = Double(args[j + 1]) { at = sec }
    DispatchQueue.main.asyncAfter(deadline: .now() + at) { [weak self] in
        guard let self, row >= 0, row < self.rows.count else { return }
        Config.log("[DBCLICK] 第二次点击第 \(row + 1) 行：\(self.rows[row].text.prefix(36))")
        self.resultsGrid.layoutSubtreeIfNeeded()          // 同 scrollToResult：先布局再选中，避免越界
        guard self.resultsGrid.numberOfSections > 0, self.resultsGrid.numberOfItems(inSection: 0) > row else {
            Config.log("[PLAYROW] 越界（当前 \(self.rows.count) 条），跳过")
            return
        }
        self.resultsGrid.selectItems(at: Set([IndexPath(item: row, section: 0)]), scrollPosition: .nearestHorizontalEdge)
        self.onRowClick()
    }
}
// 自动化：--scroll-bottom 滚到底部（验证"触底自动加载下一页"）
if args.contains("--scroll-bottom") {
    for round in 0..<4 {      // 连续触底 4 次，验证分页链与末页停止
    DispatchQueue.main.asyncAfter(deadline: .now() + 6.0 + Double(round) * 6.0) { [weak self] in
        guard let self else { return }
        Config.log("[SCROLL] 滚到底部：内容高=\(Int(self.resultsGrid.frame.height)) 当前卡片=\(self.rows.filter { !$0.key.isEmpty }.count)")
        let y = max(0, self.resultsGrid.frame.height - self.resultsScroll.contentView.bounds.height)
        self.resultsScroll.contentView.scroll(to: NSPoint(x: 0, y: y))
        self.resultsScroll.reflectScrolledClipView(self.resultsScroll.contentView)
    }
    }
}
// 自动化：--click-row <n> 选中第 n 张卡片（等价于点一下卡片；播放由 didSelectItemsAt 负责）
if let i = args.firstIndex(of: "--click-row"), i + 1 < args.count, let row = Int(args[i + 1]) {
    DispatchQueue.main.asyncAfter(deadline: .now() + 3.0) { [weak self] in
        guard let self, row >= 0, row < self.rows.count else { return }
        Config.log("[SYNCLICK] 选中卡片 \(row + 1)：\(self.rows[row].text.prefix(40))")
        self.resultsGrid.selectItems(at: Set([IndexPath(item: row, section: 0)]), scrollPosition: .nearestHorizontalEdge)
        self.resultsGrid.delegate?.collectionView?(self.resultsGrid, didSelectItemsAt: Set([IndexPath(item: row, section: 0)]))
    }
}

// 自动化：--play-row <n> 选中第 n 行并走"双击"同一条处理链（验证列表能播放）。
// 说明：合成鼠标事件在滚动视图里坐标换算不可靠（document view 比窗口高），
// 所以这里直接复用 onRowDoubleClick 的兜底路径（selectedRow），行为与真双击一致。
if let i = args.firstIndex(of: "--play-row"), i + 1 < args.count, let row = Int(args[i + 1]) {
    var playRowAt = 3.0
    if let j = args.firstIndex(of: "--play-row-at"), j + 1 < args.count, let sec = Double(args[j + 1]) { playRowAt = sec }
    DispatchQueue.main.asyncAfter(deadline: .now() + playRowAt) { [weak self] in
        guard let self else { return }
        self.resultsGrid.selectItems(at: Set([IndexPath(item: row, section: 0)]), scrollPosition: .nearestHorizontalEdge)
        Config.log("[PLAYROW] 选中第 \(row) 行：\(self.rows.indices.contains(row) ? self.rows[row].text.prefix(40) : "越界")")
        self.onRowDoubleClick()
    }
}
if let i = args.firstIndex(of: "--pip-after"), i + 1 < args.count,
   let sec = Double(args[i + 1]) {
    DispatchQueue.main.asyncAfter(deadline: .now() + sec) { [weak self] in
        self?.togglePip()
    }
}
if let i = args.firstIndex(of: "--query"), i + 1 < args.count,
   args.contains("--pip-auto") {
    DispatchQueue.main.asyncAfter(deadline: .now() + 22) { [weak self] in
        if self?.pipActive == false { self?.togglePip() }
    }
}
// 自动化：--play-first <秒> —— N 秒后播放第一条搜索结果（与 **Linux `--autoplay` 对称** ✓）
//   为什么需要：`--download-current` 必须有“正在播放的直链”才有意义 ✗ →
//   少了这步就没法端到端验证「视频下载（混流 + 大小/进度）」✓（本轮回归用它 ✓）
if let i = args.firstIndex(of: "--play-first"), i + 1 < args.count,
   let sec = Double(args[i + 1]) {
    DispatchQueue.main.asyncAfter(deadline: .now() + sec) { [weak self] in
        guard let self, let r = self.rows.first(where: { !$0.key.isEmpty }) else { return }
        Config.log("[PROBE] 自动播放第一条：\(r.text.prefix(40))")
        self.playResult(key: r.key, label: r.text)
    }
}

// 自动化：--cancel-all <秒> —— N 秒后取消全部进行中的下载（面板「取消全部」按钮走的是同一条内核路径 ✓）
//   为什么用探针：macOS 点按钮需要「辅助功能」权限 ✗ 无法脚本化 → 探针覆盖 `DownloadManager.cancelAll`
//   （而它是逐个调用 `cancel(_:)` ✓ 与行内「取消下载」按钮同一函数 ✓）
if let i = args.firstIndex(of: "--cancel-all"), i + 1 < args.count,
   let sec = Double(args[i + 1]) {
    DispatchQueue.main.asyncAfter(deadline: .now() + sec) { [weak self] in
        Config.log("[PROBE] 取消全部下载")
        self?.dl.cancelAll()
        self?.downloadsPanel?.reloadFromManager()
    }
}

// 自动化：--clean-list <秒> —— N 秒后等价于点面板「LRU 清理」按钮（**同一函数** ✓ Bug 回归用）
//   为什么用探针：macOS 点按钮需要「辅助功能」权限 ✗ 无法脚本化 ✓
if let i = args.firstIndex(of: "--clean-list"), i + 1 < args.count,
   let sec = Double(args[i + 1]) {
    DispatchQueue.main.asyncAfter(deadline: .now() + sec) { [weak self] in
        Config.log("[PROBE] 点「LRU 清理」（含列表清理 ✓）")
        self?.openDownloadsPanel()
        self?.downloadsPanel?.cleanLru()
        self?.downloadsPanel?.reloadFromManager()
    }
}

// 自动化：--menu-downloads <秒> —— **走菜单那条路**打开下载面板（验证入口接线 ✓ Bug1 回归用）
//   （`--panel downloads` 是直接开面板 ✓ 覆盖不了“菜单 selector 还指向旧实现”这类问题 ✗）
if let i = args.firstIndex(of: "--menu-downloads"), i + 1 < args.count,
   let sec = Double(args[i + 1]) {
    DispatchQueue.main.asyncAfter(deadline: .now() + sec) { [weak self] in
        Config.log("[PROBE] 菜单路径：打开下载面板")
        self?.onDownloads()
    }
}
// 自动化：--pip-after <sec>[,<sec>...] —— 与 **Linux 同名同语义**（定时 toggle 画中画 ✓）
//   逗号多时刻 = 一次跑完"进 PiP + 退 PiP"往返 ✓（复现"退出后左侧列表宽度变化"类问题的精确手段 ✓）
if let i = args.firstIndex(of: "--pip-after"), i + 1 < args.count {
    for part in args[i + 1].split(separator: ",") {
        if let sec = Double(part.trimmingCharacters(in: .whitespaces)), sec > 0 {
            DispatchQueue.main.asyncAfter(deadline: .now() + sec) { [weak self] in
                self?.togglePip()
            }
        }
    }
}
if let i = args.firstIndex(of: "--download-current"), i + 1 < args.count,
   let sec = Double(args[i + 1]) {                 // 自动化：N 秒后点一次「下载」（等价用户操作）
    DispatchQueue.main.asyncAfter(deadline: .now() + sec) { [weak self] in
        Config.log("[PROBE] 触发下载当前内容")
        self?.onDownload()
    }
}
if args.contains("--filters") {                          // 自动化：打开搜索过滤器面板
    DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in self?.onFilters() }
}
if let i = args.firstIndex(of: "--quality"), i + 1 < args.count {
    wSwitchAudioQuality(level: args[i + 1])          // 同步应用（避免晚于播放生效）
}
if let i = args.firstIndex(of: "--cycle-quality-after"), i + 1 < args.count,
   let sec = Double(args[i + 1]) {
    DispatchQueue.main.asyncAfter(deadline: .now() + sec) { [weak self] in
        self?.cycleAudioQuality()
    }
}
if let i = args.firstIndex(of: "--video-quality"), i + 1 < args.count {
    let v = args[i + 1].lowercased()
    let h: Int
    switch v {
    case "auto": h = 0
    case "audio", "audio-only": h = -1
    default: h = Int(v.replacingOccurrences(of: "p", with: "")) ?? 0
    }
    // 只改**会话档位**，不写设置：CLI 探针曾把用户的 video.maxHeight 从 1080 改成 2160（实测踩到）。
    // 要持久化请显式用 --set video.maxHeight=2160。
    UrlResolver.maxHeight = h
    Config.log("[CLI] 清晰度档位 = \(UrlResolver.qualityLabel(h))（仅本次会话，不改设置）")
}
if let i = args.firstIndex(of: "--cycle-video-quality-after"), i + 1 < args.count,
   let sec = Double(args[i + 1]) {
    DispatchQueue.main.asyncAfter(deadline: .now() + sec) { [weak self] in
        self?.cycleVideoQuality()
    }
}
if let i = args.firstIndex(of: "--import-cookies"), i + 1 < args.count {
    let browser = args[i + 1]
    DispatchQueue.global().async { [weak self] in
        let (ok, summary) = CookieImport.importFromBrowser(browser, probeUrl: "https://www.bilibili.com/video/BV1GJ411x7h7")
        Config.log("[SELFTEST] cookie 导入(\(browser)): \(ok ? "成功" : "失败") — \(summary)")
        DispatchQueue.main.async { self?.refreshLoginBadge() }   // 导入后立刻反映到界面（不用重启）
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
        Config.log("[SELFTEST] 媒体键探测完成，自动退出（纯 CLI 探针）")
        exit(0)
    }
}
selfTest = args.contains("--selfcheck")

ThemeController.shared.applyFromSettings(settings)     // ui.theme=auto/dark/light（三端同键 ✓ 阶段一：AppKit 外观跟随）
settings.load()
UrlResolver.cookiesFromBrowser = settings.string("network.cookiesFromBrowser")
UrlResolver.maxHeight = Int(settings.number("video.maxHeight", 0))
progress.load()
favorites.load()
_ = queue.load()
refreshModeButton()          // 启动即显示已保存的播放模式

window = NSWindow(contentRect: NSRect(x: 100, y: 100, width: 1180, height: 720),
                  styleMask: [.titled, .closable, .resizable, .miniaturizable],
                  backing: .buffered, defer: false)
window.isReleasedWhenClosed = false       // 自建窗口统一：关闭不释放（避免过度释放崩溃）
// 外观统一由 ThemeController 决定（不要把 darkAqua 写死 ✗ 否则用户选浅色会被覆盖）
window.appearance = NSAppearance(named: ThemeController.shared.mode.isLight ? .aqua : .darkAqua)
    }
}
