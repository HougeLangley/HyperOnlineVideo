import Foundation

/// 设置（键名与 Qt 端完全一致，共享 ~/.config/hov/settings.json）
final class Settings {
    static let defaults: [String: Any] = [
        "music.qualityCeiling": "exhigh",
        "subtitle.fontScale": 1.0,
        "subtitle.defaultDelay": 0.0,
        "subtitle.karaoke": true,
        "download.dir": "",
        "download.maxSizeMb": 2048,
        "network.forceDirectDomestic": false,
        "ui.showNetworkProbe": true,
        "playback.rememberProgress": true,
    ]
    private var values: [String: Any] = Settings.defaults

    func load() {
        values = Settings.defaults
        let raw = Config.readJSON(Config.settingsPath)
        for (k, v) in raw { values[k] = v }        // 未知键保留（向前兼容）
        Config.log("设置已加载: \(values.count) 项（\(Config.settingsPath)）")
    }

    @discardableResult
    func save() -> Bool { Config.writeJSON(values, to: Config.settingsPath) }

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
        case "subtitle.karaoke", "network.forceDirectDomestic", "ui.showNetworkProbe", "playback.rememberProgress":
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
        if key == "subtitle.fontScale" || key == "subtitle.defaultDelay" || key == "download.maxSizeMb" {
            values[key] = Double(value) ?? 0
        } else if ["subtitle.karaoke", "network.forceDirectDomestic", "ui.showNetworkProbe", "playback.rememberProgress"].contains(key) {
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
