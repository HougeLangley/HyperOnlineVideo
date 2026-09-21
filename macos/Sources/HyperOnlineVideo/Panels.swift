import AppKit

/// B6：设置 / 收藏 / 队列 三个正式面板（把原先的 NSAlert 弹窗替换掉）
///
/// 设计：每个面板是一个独立窗口（非模态，可同时开着播放），
/// 设置面板的所有改动都走 `Settings.apply(key:value:)`（沿用既有校验，不绕过）；
/// 并提供 `dumpText()` 供自动化（`--panel` + `--dump-ui-text`）取证。
enum Panels {
    /// 面板窗口登记（供 UI 抓图定位当前窗口）
    static weak var activeWindow: NSWindow?
    /// 当前面板的文本导出回调（面板自己设置，避免 KVC 访问私有属性）
    static var activeDumpText: (() -> String)?
    static func reset() { activeWindow = nil; activeDumpText = nil }
}

// MARK: - 设置面板

final class SettingsPanel: NSObject {
    private var window: NSWindow!
    private var controls: [(key: String, get: () -> String, set: () -> Void, read: () -> String)] = []
    private var popups: [String: NSPopUpButton] = [:]
    private var savedLabel: NSTextField!
    private var checks: [String: NSButton] = [:]
    private var sliders: [String: NSSlider] = [:]
    private var fields: [String: NSTextField] = [:]
    private let settings = Settings()

    func show() {
        settings.load()
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 560, height: 560),
                          styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false       // 关键：默认 true ⇒ 关掉后窗口被释放，再打开时替换它会对已释放对象减引用 → objc_release 崩溃
        window.title = "设置"
        let root = NSStackView()
        root.orientation = .vertical
        root.alignment = .leading
        root.spacing = 10
        root.edgeInsets = NSEdgeInsets(top: 16, left: 18, bottom: 16, right: 18)

        func row(_ title: String, _ control: NSView, _ width: CGFloat = 0) -> NSView {
            let l = NSTextField(labelWithString: title)
            l.widthAnchor.constraint(equalToConstant: 150).isActive = true
            if width > 0 { control.widthAnchor.constraint(equalToConstant: width).isActive = true }
            let h = NSStackView(views: [l, control])
            h.orientation = .horizontal
            h.spacing = 10
            return h
        }

        /// 下拉行（消除"4 行构造 + 入字典 + 挂行"的重复模式 ✗）
        @discardableResult
        func addPopup(_ key: String, _ title: String, _ items: [String], _ select: Int,
                      _ width: CGFloat = 0) -> NSPopUpButton {
            let p = NSPopUpButton(frame: .zero, pullsDown: false)
            p.addItems(withTitles: items)
            p.selectItem(at: max(0, min(items.count - 1, select)))
            popups[key] = p
            root.addArrangedSubview(row(title, p, width))
            return p
        }

        /// 滑块行（同上 ✓）
        @discardableResult
        func addSlider(_ key: String, _ title: String, value: Double, min lo: Double, max hi: Double,
                       _ width: CGFloat = 260) -> NSSlider {
            let s = NSSlider(value: value, minValue: lo, maxValue: hi, target: nil, action: nil)
            sliders[key] = s
            root.addArrangedSubview(row(title, s, width))
            return s
        }

        /// 文本框行（同上 ✓）
        @discardableResult
        func addField(_ key: String, _ title: String, _ text: String, _ width: CGFloat = 260,
                      extra: NSView? = nil) -> NSTextField {
            let f = NSTextField(string: text)
            f.widthAnchor.constraint(equalToConstant: width).isActive = true
            fields[key] = f
            if let extra {
                let h = NSStackView(views: [f, extra])
                h.orientation = .horizontal
                h.spacing = 8
                root.addArrangedSubview(row(title, h))
            } else {
                root.addArrangedSubview(row(title, f))
            }
            return f
        }

        // 音质上限
        // 面板里用中文标签（保存时按**位置**映射回内部值 ✓ 与 video.maxHeight 同一做法 ✓）
        addPopup("music.qualityCeiling", "音乐音质上限", ["标准 128k", "较高 320k", "无损 FLAC"],
                 ["standard", "exhigh", "lossless"].firstIndex(of: settings.string("music.qualityCeiling", "exhigh")) ?? 1)

        // 视频清晰度上限
        addPopup("video.maxHeight", "视频清晰度上限", AppDelegate.videoQualityTitles,
                 qualityIndex(settings.number("video.maxHeight", 0)))

        // 应用主题（三端同键 ui.theme ✓ auto=跟随系统 / light / dark）
        // ⚠️ 必须用**已加载的** settings ✓ —— 原来是 Settings()（空表 ✗）→ 下拉永远显示"跟随系统" ✗
        //    且保存时会把用户的选择静默改回 auto ✗（与 ThemeController 踩的是同一个坑 ✓）
        addPopup("ui.theme", "应用主题", [ThemeMode.auto, .light, .dark].map { $0.label },
                 [ThemeMode.auto, .light, .dark].firstIndex(of: ThemeMode.parse(settings.string("ui.theme", "auto"))) ?? 0,
                 190)

        // 字幕字号 / 延迟
        addSlider("subtitle.fontScale", "字幕字号倍数", value: settings.number("subtitle.fontScale", 1.0), min: 0.5, max: 2.0)
        addSlider("subtitle.defaultDelay", "默认字幕延迟(秒)", value: settings.number("subtitle.defaultDelay", 0.0), min: -5, max: 5)
        // 列表缩略图大小（与 Android 默认 84 一致；拖完点保存生效）
        addSlider("ui.thumbSize", "列表缩略图大小(pt)", value: settings.number("ui.thumbSize", 84), min: 24, max: 200)

        // 勾选项
        func addCheck(_ key: String, _ title: String, _ def: Bool) {
            let b = NSButton(checkboxWithTitle: title, target: nil, action: nil)
            b.state = settings.bool(key, def) ? .on : .off
            checks[key] = b
            root.addArrangedSubview(b)
        }
        addCheck("video.fillScreen", "全屏时铺满屏幕（裁切左右黑边）", true)
        addCheck("subtitle.karaoke", "歌词用卡拉OK面板（居中 + 逐字高亮）", true)
        addCheck("video.gpuNext", "视频用 gpu-next 渲染（libplacebo，画质更好）", false)
        addCheck("playback.rememberProgress", "记住播放进度（自动续播）", true)
        addCheck("playback.autoNext", "播完自动播下一条（按列表顺序连播）", true)
        addCheck("ui.hoverReveal", "全屏时鼠标贴边缘浮出面板（左侧列表 / 底部控制条）", true)
        addCheck("ui.showNetworkProbe", "启动时检测网络", true)
        addCheck("network.forceDirectDomestic", "国内接口强制直连（绕过环境变量代理）", false)

        // 浏览器 cookie 来源（A0）
        let cookieItems = ["关闭", "safari", "chrome", "chromium", "edge", "firefox", "opera", "brave", "vivaldi", "whale"]
        let cur = settings.string("network.cookiesFromBrowser")
        addPopup("network.cookiesFromBrowser", "从浏览器读 cookie", cookieItems,
                 max(0, cookieItems.firstIndex(of: cur.isEmpty ? "关闭" : cur) ?? 0))

        // 下载目录 + 上限
        // 默认值：设置里为空时显示"生效中的默认目录"，避免用户看到空框不知道默认在哪
        let dirValue = settings.string("download.dir").isEmpty ? Config.downloadDir : settings.string("download.dir")
        let pick = NSButton(title: "选择…", target: self, action: #selector(pickDir))
        addField("download.dir", "下载目录", dirValue, extra: pick)
        addField("download.maxSizeMb", "下载上限 (MB，0=不限)",
                 String(Int(settings.number("download.maxSizeMb", 2048))), 80)

        // 按钮
        let save = NSButton(title: "保存", target: self, action: #selector(save))
        let close = NSButton(title: "关闭", target: self, action: #selector(close))
        save.keyEquivalent = "\r"
        let btns = NSStackView(views: [save, close])
        btns.orientation = .horizontal
        btns.spacing = 10
        root.addArrangedSubview(btns)
        // 保存结果提示：点「保存」后窗口不自动关（方便继续改），所以必须**醒目**告诉用户已经存过了
        savedLabel = NSTextField(labelWithString: "")
        savedLabel.font = .systemFont(ofSize: 13, weight: .semibold)
        savedLabel.textColor = .systemGreen
        root.addArrangedSubview(savedLabel)

        // 与主窗口一致的毛玻璃底（放在最底层，避免盖住控件）
        window.isOpaque = false
        window.backgroundColor = .clear
        window.titlebarAppearsTransparent = false   // 标题栏保持常规（透明会让标题与内容叠在一起）
        let glass = NSVisualEffectView(frame: NSRect(origin: .zero, size: window.contentRect(forFrameRect: window.frame).size))
        glass.autoresizingMask = [.width, .height]
        glass.material = .underWindowBackground
        glass.blendingMode = .behindWindow
        glass.state = .followsWindowActiveState
        window.contentView = root
        root.addSubview(glass, positioned: .below, relativeTo: nil)
        Panels.activeWindow = window
        Panels.activeDumpText = { [weak self] in self?.dumpText() ?? "" }
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        Config.log("[SELFTEST] 设置面板已打开（\(popups.count + checks.count + sliders.count + fields.count) 个控件）")
    }

    /// 高度 → 下拉下标：**必须用共享表**（AppDelegate.videoQualityHeights）。
    /// 这里原来留着一份独立的旧映射（1080→1、480→3…），与共享表（1080→3、480→5…）冲突：
    /// 设置里选 1080p 保存后，面板按旧映射把 1080 显示成第 2 项、控制条按新表读到 480p —— 用户实测的"对应错了"。
    private func qualityIndex(_ h: Double) -> Int {
        if let i = AppDelegate.videoQualityHeights.firstIndex(of: Int(h)) { return i }
        return 0
    }

    @objc private func pickDir() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        if panel.runModal() == .OK { fields["download.dir"]?.stringValue = panel.url?.path ?? "" }
    }

    @objc private func save() {
        var applied: [String] = [], rejected: [String] = []
        let audioLevels = ["standard", "exhigh", "lossless"]
        for (k, p) in popups {
            let v = (k == "video.maxHeight") ? String(heightFor(p.indexOfSelectedItem))
                    : (k == "music.qualityCeiling") ? audioLevels[max(0, min(2, p.indexOfSelectedItem))]
                    // 主题：**按位置**映射回内部值 ✗ 不能写标签文字（"浅色" ✗ 要写 light ✓，三端同键 ✓）
                    : (k == "ui.theme") ? ["auto", "light", "dark"][max(0, min(2, p.indexOfSelectedItem))]
                    : (k == "network.cookiesFromBrowser" ? (p.indexOfSelectedItem == 0 ? "" : p.titleOfSelectedItem ?? "") : p.titleOfSelectedItem ?? "")
            let (ok, _) = settings.apply(key: k, value: v)
            if ok { applied.append(k) } else { rejected.append(k) }
        }
        for (k, c) in checks {
            let (ok, _) = settings.apply(key: k, value: c.state == .on ? "true" : "false")
            if ok { applied.append(k) } else { rejected.append(k) }
        }
        for (k, s) in sliders {
            let (ok, _) = settings.apply(key: k, value: String(format: "%.2f", s.doubleValue))
            if ok { applied.append(k) } else { rejected.append(k) }
        }
        for (k, f) in fields {
            let (ok, _) = settings.apply(key: k, value: f.stringValue)
            if ok { applied.append(k) } else { rejected.append(k) }
        }
        let rejNote = rejected.isEmpty ? "" : "（" + rejected.joined(separator: ",") + "）"
        Config.log("[SELFTEST] 设置面板保存：应用 \(applied.count) 项，拒绝 \(rejected.count) 项" + rejNote)
        NotificationCenter.default.post(name: Notification.Name("hovSettingsChanged"), object: nil)   // 让字号/清晰度等立即生效
        // 醒目提示（带时间戳）：点「保存」后窗口不自动关（方便继续改），所以必须清楚告诉用户"已经存过了"
        let fmt = DateFormatter(); fmt.dateFormat = "HH:mm:ss"
        savedLabel.stringValue = rejected.isEmpty
            ? "✓ 已保存 \(applied.count) 项（\(fmt.string(from: Date()))）—— 设置立即生效，可直接关闭本窗口"
            : "⚠ 已保存 \(applied.count) 项，\(rejected.count) 项被拒：\(rejected.joined(separator: ", "))"
        savedLabel.textColor = rejected.isEmpty ? .systemGreen : .systemOrange
    }

    private func heightFor(_ i: Int) -> Int {
        AppDelegate.videoQualityHeights.indices.contains(i) ? AppDelegate.videoQualityHeights[i] : 0
    }

    @objc private func close() { window.close() }

    /// 供自动化/回归调用：等价于点「保存」（与按钮走同一条链路）
    func triggerSave() { save() }

    /// 供自动化/回归调用：等价于点窗口关闭按钮
    func closeForTest() { window.close() }

    func dumpText() -> String {
        var out = "设置面板（\(popups.count + checks.count + sliders.count + fields.count) 个控件）\n"
        for (k, p) in popups.sorted(by: { $0.key < $1.key }) { out += "  下拉 \(k) = \(p.titleOfSelectedItem ?? "-")\n" }
        for (k, c) in checks.sorted(by: { $0.key < $1.key }) { out += "  勾选 \(k) = \(c.state == .on)\n" }
        for (k, s) in sliders.sorted(by: { $0.key < $1.key }) { out += String(format: "  滑块 %@ = %.2f\n", k, s.doubleValue) }
        for (k, f) in fields.sorted(by: { $0.key < $1.key }) {
            let shown = f.stringValue.isEmpty ? "(空)" : f.stringValue
            out += "  输入 \(k) = " + shown + "\n"
        }
        out += "  提示: \(savedLabel.stringValue.isEmpty ? "(尚未保存)" : savedLabel.stringValue)\n"
        return out
    }
}

// MARK: - 收藏面板

final class FavoritesPanel: NSObject, NSTableViewDataSource, NSTableViewDelegate {
    private var window: NSWindow!
    private var table: NSTableView!
    private let favorites = Favorites()
    private var items: [Favorites.Fav] = []
    weak var app: AppDelegate?

    func show() {
        favorites.load()
        items = favorites.all
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 560, height: 420),
                          styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false       // 同上：避免关掉再打开时过度释放
        window.title = "收藏（\(items.count) 项）"
        let root = NSStackView()
        root.orientation = .vertical
        root.spacing = 10
        root.edgeInsets = NSEdgeInsets(top: 12, left: 12, bottom: 12, right: 12)

        table = NSTableView()
        table.addTableColumn(NSTableColumn(identifier: NSUserInterfaceItemIdentifier("f")))
        table.headerView = nil
        table.dataSource = self
        table.delegate = self
        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.heightAnchor.constraint(equalToConstant: 300).isActive = true
        root.addArrangedSubview(scroll)

        let play = NSButton(title: "播放选中", target: self, action: #selector(playSel))
        let del = NSButton(title: "移除选中", target: self, action: #selector(removeSel))
        let clear = NSButton(title: "清空", target: self, action: #selector(clearAll))
        let close = NSButton(title: "关闭", target: self, action: #selector(close))
        let btns = NSStackView(views: [play, del, clear, close])
        btns.orientation = .horizontal
        btns.spacing = 10
        root.addArrangedSubview(btns)

        // 与主窗口一致的毛玻璃底（放在最底层，避免盖住控件）
        window.isOpaque = false
        window.backgroundColor = .clear
        window.titlebarAppearsTransparent = true
        let glass = NSVisualEffectView(frame: NSRect(origin: .zero, size: window.contentRect(forFrameRect: window.frame).size))
        glass.autoresizingMask = [.width, .height]
        glass.material = .underWindowBackground
        glass.blendingMode = .behindWindow
        glass.state = .followsWindowActiveState
        window.contentView = root
        root.addSubview(glass, positioned: .below, relativeTo: nil)
        Panels.activeWindow = window
        Panels.activeDumpText = { [weak self] in self?.dumpText() ?? "" }
        window.center()
        window.makeKeyAndOrderFront(nil)
        Config.log("[SELFTEST] 收藏面板已打开（\(items.count) 项）")
        Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] t in
            guard let self, self.window.isVisible else { t.invalidate(); return }
            let fresh = self.favorites.all
            if fresh.count != self.items.count {
                self.items = fresh
                self.table.reloadData()
                self.window.title = "收藏（\(fresh.count) 项）"
            }
        }
    }

    func numberOfRows(in tableView: NSTableView) -> Int { items.count }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let l = NSTextField(labelWithString: "\(items[row].title)   [\(items[row].platform)]")
        l.lineBreakMode = .byTruncatingTail
        return l
    }
    @objc private func playSel() {
        let i = table.selectedRow
        guard i >= 0, i < items.count else { return }
        app?.playResult(key: items[i].key, label: items[i].title)
    }
    @objc private func removeSel() {
        let i = table.selectedRow
        guard i >= 0, i < items.count else { return }
        _ = favorites.remove(items[i].id)
        _ = favorites.save()
        items = favorites.all
        table.reloadData()
        window.title = "收藏（\(items.count) 项）"
    }
    @objc private func clearAll() {
        for f in items { _ = favorites.remove(f.id) }
        _ = favorites.save()
        items = []
        table.reloadData()
        window.title = "收藏（0 项）"
    }
    @objc private func close() { window.close() }

    func dumpText() -> String {
        var out = "收藏面板（\(items.count) 项）\n"
        for (i, f) in items.prefix(30).enumerated() { out += "  [\(i)] \(f.title)  [\(f.platform)]\n" }
        return out
    }
}

// MARK: - 队列面板

final class QueuePanel: NSObject, NSTableViewDataSource, NSTableViewDelegate {
    private var window: NSWindow!
    private var table: NSTableView!
    private var mode: NSPopUpButton!
    weak var app: AppDelegate?

    func show() {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 560, height: 420),
                          styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false       // 同上：避免关掉再打开时过度释放
        window.title = "播放队列（\(app?.queue.size ?? 0) 项）"
        let root = NSStackView()
        root.orientation = .vertical
        root.spacing = 10
        root.edgeInsets = NSEdgeInsets(top: 12, left: 12, bottom: 12, right: 12)

        mode = NSPopUpButton()
        mode.addItems(withTitles: ["顺序", "单曲", "随机"])
        mode.selectItem(at: app?.queue.mode.rawValue ?? 0)
        mode.target = self
        mode.action = #selector(modeChanged)
        root.addArrangedSubview(mode)

        table = NSTableView()
        table.addTableColumn(NSTableColumn(identifier: NSUserInterfaceItemIdentifier("q")))
        table.headerView = nil
        table.dataSource = self
        table.delegate = self
        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.heightAnchor.constraint(equalToConstant: 280).isActive = true
        root.addArrangedSubview(scroll)

        let play = NSButton(title: "播放选中", target: self, action: #selector(playSel))
        let up = NSButton(title: "上移", target: self, action: #selector(moveUp))
        let down = NSButton(title: "下移", target: self, action: #selector(moveDown))
        let close = NSButton(title: "关闭", target: self, action: #selector(close))
        let btns = NSStackView(views: [play, up, down, close])
        btns.orientation = .horizontal
        btns.spacing = 10
        root.addArrangedSubview(btns)

        // 与主窗口一致的毛玻璃底（放在最底层，避免盖住控件）
        window.isOpaque = false
        window.backgroundColor = .clear
        window.titlebarAppearsTransparent = true
        let glass = NSVisualEffectView(frame: NSRect(origin: .zero, size: window.contentRect(forFrameRect: window.frame).size))
        glass.autoresizingMask = [.width, .height]
        glass.material = .underWindowBackground
        glass.blendingMode = .behindWindow
        glass.state = .followsWindowActiveState
        window.contentView = root
        root.addSubview(glass, positioned: .below, relativeTo: nil)
        Panels.activeWindow = window
        Panels.activeDumpText = { [weak self] in self?.dumpText() ?? "" }
        window.center()
        window.makeKeyAndOrderFront(nil)
        Config.log("[SELFTEST] 队列面板已打开（\(app?.queue.size ?? 0) 项，模式 \(app?.queue.modeLabel ?? "-")）")
        startLiveRefresh()
    }

    /// 面板开着时实时跟着队列走（搜索灌入/切歌/清空都会反映出来）
    private func startLiveRefresh() {
        Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] t in
            guard let self, self.window.isVisible else { t.invalidate(); return }
            let n = self.app?.queue.size ?? 0
            let title = "播放队列（\(n) 项 · \(self.app?.queue.modeLabel ?? "-")）"
            if self.window.title != title {
                self.window.title = title
                self.mode.selectItem(at: self.app?.queue.mode.rawValue ?? 0)
                self.table.reloadData()
            }
        }
    }

    func numberOfRows(in tableView: NSTableView) -> Int { app?.queue.size ?? 0 }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard let e = app?.queue.at(row) else { return nil }
        let cur = (row == app?.queue.index) ? "▶ " : "   "
        return NSTextField(labelWithString: "\(cur)\(row + 1). \(e.label)")
    }
    @objc private func modeChanged() {
        guard let app else { return }
        let target = PlayQueue.Mode(rawValue: mode.indexOfSelectedItem) ?? .sequential
        while app.queue.mode != target { app.queue.cycleMode() }
        _ = app.queue.save()
        window.title = "播放队列（\(app.queue.size) 项 · \(app.queue.modeLabel)）"
    }
    @objc private func playSel() {
        let i = table.selectedRow
        guard let app, i >= 0, i < app.queue.size, let e = app.queue.at(i) else { return }
        app.queue.jumpTo(i)
        app.playResult(key: e.key, label: e.label)
    }
    @objc private func moveUp() { move(-1) }
    @objc private func moveDown() { move(1) }
    private func move(_ delta: Int) {
        guard let app else { return }
        let i = table.selectedRow
        let j = i + delta
        guard i >= 0, j >= 0, j < app.queue.size,
              app.queue.at(i) != nil, app.queue.at(j) != nil else { return }
        var list = (0..<app.queue.size).compactMap { app.queue.at($0) }
        list.swapAt(i, j)
        app.queue.setList(list, startAt: app.queue.index == i ? j : (app.queue.index == j ? i : app.queue.index))
        _ = app.queue.save()
        table.reloadData()
        table.selectRowIndexes(IndexSet(integer: j), byExtendingSelection: false)
    }
    @objc private func close() { window.close() }

    func dumpText() -> String {
        guard let app else { return "队列面板（无数据）\n" }
        var out = "队列面板（\(app.queue.size) 项 · \(app.queue.modeLabel)）\n"
        for i in 0..<min(app.queue.size, 30) {
                if let e = app.queue.at(i) {
                    let mark = (i == app.queue.index) ? "▶ " : "  "
                    out += "  " + mark + "\(i + 1). " + e.label + "\n"
                }
        }
        return out
    }
}
