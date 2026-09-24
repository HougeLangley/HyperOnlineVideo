import Foundation

/// 设置（键名与 Qt 端完全一致，共享 ~/.config/hov/settings.json）
final class Settings {
    /// 全局唯一实例（**单一真相** ✓）
    ///
    /// 为什么必须是单例（2026-09-21 实测 bug ✗）：
    ///   此前 AppDelegate / SettingsPanel / DownloadManager / MpvView 各自新建 `Settings()` 实例 ✗，
    ///   每个实例各持一份 `values` 字典 ✗ → **任何一方 save() 都会用自己那份快照覆盖整个文件** ✗
    ///   → 表现为："保存设置 → 完全退出 → 再启动，设置丢失" ✗✓
    ///   （已用 --fill-screen 探针复现：5 个用户键被整体重写为 18 个默认值键 ✗）
    /// 现在：构造即 load ✓ 且 init 私有 ✗ → "未加载的实例"在类型层面不可能出现 ✓✓
    static let shared = Settings()

    static let defaults: [String: Any] = [
        "music.qualityCeiling": "exhigh",
        "subtitle.fontScale": 1.0,
        "subtitle.defaultDelay": 0.0,
        "subtitle.karaoke": true,
        "download.dir": "",
        "download.maxSizeMb": 2048,
        "network.forceDirectDomestic": false,
        "ui.showNetworkProbe": true,
        "ui.thumbSize": 84,
        "ui.listWidth": 0,                  // 左侧结果区宽度（pt；0=未设置过 → 用默认值 38%，上限 460）
        "playback.rememberProgress": true,
        "playback.autoNext": true,           // 播放完成后自动播放下一条（按列表顺序）
        "network.cookiesFromBrowser": "",   // 从系统浏览器读 cookie（A0 登录方案）
        "video.fillScreen": false,   // 默认**不**裁切：上下黑边可接受，裁掉左右反而丢失画面（用户实测反馈）
        "video.maxHeight": 0,               // 视频清晰度上限（0=自动不限，-1=仅音频，否则 360/480/720/1080…）
"ui.theme": "auto",                        // auto=跟随系统 light/dark（与 Linux/Android 同键 ✓）
        "ui.hoverReveal": true,             // 全屏时鼠标贴边缘浮出面板（与 Linux 同键 ✓）
        "ui.windowFrame": "",               // W1 ✓ 窗口几何 "x,y,w,h"（空=首启 → 1180x720 并居中 ✓；退出时保存，画中画/全屏态除外 ✓）
    ]
    private var values: [String: Any] = Settings.defaults

    /// 私有构造：**立即从磁盘加载** ✓（不允许存在"只有默认值"的实例 ✗）
    private init() { load() }

    /// 显式重载（一般无需调用；构造时已自动加载 ✓）
    func load() {
        values = Settings.defaults
        let raw = Config.readJSON(Config.settingsPath)
        for (k, v) in raw { values[k] = v }        // 未知键保留（向前兼容）
        Config.log("设置已加载: \(values.count) 项（\(Config.settingsPath)）")
    }

    @discardableResult
    func save() -> Bool {
        // 保留一行写盘日志（设置丢失类问题的定位关键 ✓ 代价极小 ✓）
        Config.log("[SETTINGS-SAVE] keys=\(values.count) thumbSize=\(values["ui.thumbSize"] ?? "nil") theme=\(values["ui.theme"] ?? "nil")")
        return Config.writeJSON(values, to: Config.settingsPath)
    }

    func string(_ key: String, _ def: String = "") -> String { (values[key] as? String) ?? def }
    func bool(_ key: String, _ def: Bool) -> Bool {
        if let b = values[key] as? Bool { return b }
        if let n = values[key] as? NSNumber { return n.boolValue }
        if let s = values[key] as? String { return ["true", "1", "yes", "on"].contains(s.lowercased()) }
        return def
    }
    func number(_ key: String, _ def: Double) -> Double {
        if let n = values[key] as? NSNumber { return n.doubleValue }
        if let s = values[key] as? String, let d = Double(s) { return d }
        return def
    }

    /// 音质档位：请求值再受设置里的上限裁剪（standard < exhigh < lossless）
    func effectiveQuality(_ requested: String) -> String {
        let order = ["standard", "exhigh", "lossless"]
        let ceiling = string("music.qualityCeiling", "exhigh")
        let ci = order.firstIndex(of: ceiling) ?? 1
        let ri = order.firstIndex(of: requested) ?? 1
        return order[min(ci, ri)]
    }

    /// 单项校验（与 Qt 端同一套规则；未知键/越界/类型错都拒绝）
    static func validate(_ key: String, _ value: String) -> String? {
        guard defaults.keys.contains(key) else { return "未知设置项（可用：\(defaults.keys.sorted().joined(separator: " / "))）" }
        let v = value.trimmingCharacters(in: .whitespaces)
        switch key {
        case "music.qualityCeiling":
            return ["standard", "exhigh", "lossless"].contains(v) ? nil : "取值必须是 standard / exhigh / lossless"
        case "subtitle.fontScale":
            guard let d = Double(v), d >= 0.5, d <= 2.0 else { return "取值必须是 0.5 ~ 2.0 之间的数字" }
        case "subtitle.defaultDelay":
            guard let d = Double(v), d >= -5.0, d <= 5.0 else { return "取值必须是 -5.0 ~ 5.0 之间的秒数" }
        case "download.maxSizeMb":
            guard let d = Double(v), d >= 0, d <= 1_048_576 else { return "取值必须是 0 ~ 1048576 之间的数字（MB，0=不限制）" }
        case "video.maxHeight":
            guard let d = Double(v) else { return "取值必须是数字（0=自动，-1=仅音频，或 360/480/720/1080…）" }
            if d == 0 || d == -1 { return nil }
            return (d >= 144 && d <= 4320) ? nil : "清晰度上限应在 144 ~ 4320 之间（或 0=自动 / -1=仅音频）"
        case "ui.listWidth":
            guard let d = Double(v), d >= 0, d <= 5000 else { return "结果区宽度必须是 0 ~ 5000 之间的数字（pt，0=用默认）" }
            return nil
        case "ui.thumbSize":
            guard let d = Double(v), d >= 24, d <= 200 else { return "缩略图高度必须是 24 ~ 200 之间的数字（pt）" }
            return nil
        case "network.cookiesFromBrowser":
            if v.isEmpty { return nil }
            let head = v.split(separator: "+").first?.split(separator: ":").first.map(String.init)?.lowercased() ?? ""
            let ok = ["brave", "chrome", "chromium", "edge", "firefox", "opera", "safari", "vivaldi", "whale"]
            return ok.contains(head) ? nil : "浏览器名必须是 \(ok.joined(separator: " / ")) 之一（或留空关闭）"
        case "subtitle.karaoke", "network.forceDirectDomestic", "ui.showNetworkProbe", "playback.rememberProgress",
             "ui.hoverReveal",
             "playback.autoNext",
             "video.fillScreen":
            let ok = ["true", "false", "1", "0", "yes", "no", "on", "off"]
            return ok.contains(v.lowercased()) ? nil : "取值必须是 true / false"
        default:
            break   // download.dir 等自由文本
        }
        return nil
    }

    /// 应用一项（先校验）
    @discardableResult
    func apply(key: String, value: String) -> (ok: Bool, message: String) {
        if let err = Settings.validate(key, value) { return (false, err) }
        if key == "subtitle.fontScale" || key == "subtitle.defaultDelay" || key == "download.maxSizeMb"
            || key == "ui.thumbSize" || key == "ui.listWidth" {
            values[key] = Double(value) ?? 0
        } else if ["subtitle.karaoke", "network.forceDirectDomestic", "ui.showNetworkProbe", "playback.rememberProgress",
                    "playback.autoNext", "ui.hoverReveal",
                    "video.fillScreen"].contains(key) {
            values[key] = ["true", "1", "yes", "on"].contains(value.lowercased())
        } else {
            values[key] = value
        }
        save()
        return (true, "")
    }

    func dump() -> String {
        Settings.defaults.keys.sorted().map { k in
            let cur = "\(values[k] ?? "")"
            let def = "\(Settings.defaults[k] ?? "")"
            return "\(k) = \(cur)\(cur == def ? "   (默认)" : "   (已改，默认 \(def))")"
        }.joined(separator: "\n")
    }
}
