import Foundation

/// 播放进度记忆（规则与 Qt 端一致，共享 progress.json）
final class ProgressStore {
    struct Entry { var pos: Double = 0; var dur: Double = 0; var title: String = ""; var updatedAt: Int64 = 0 }

    static let recordMin = 5.0, resumeMin = 15.0, endGuard = 15.0
    static let maxEntries = 500
    static let maxAgeMs: Int64 = 120 * 24 * 3600 * 1000

    private var items: [String: Entry] = [:]

    func load() {
        items.removeAll()
        let root = Config.readJSON(Config.progressPath)
        for (k, v) in (root["items"] as? [String: Any]) ?? [:] {
            guard let o = v as? [String: Any] else { continue }
            var e = Entry()
            e.pos = (o["pos"] as? NSNumber)?.doubleValue ?? 0
            e.dur = (o["dur"] as? NSNumber)?.doubleValue ?? 0
            e.title = (o["title"] as? String) ?? ""
            e.updatedAt = Int64((o["updatedAt"] as? NSNumber)?.doubleValue ?? 0)
            items[k] = e
        }
        prune()
        Config.log("进度记忆已加载: \(items.count) 条")
    }

    @discardableResult
    func save() -> Bool {
        var arr: [String: Any] = [:]
        for (k, e) in items {
            arr[k] = ["pos": e.pos, "dur": e.dur, "title": e.title, "updatedAt": Double(e.updatedAt)]
        }
        return Config.writeJSON(["items": arr, "updatedAt": Double(Self.nowMs())], to: Config.progressPath)
    }

    static func nowMs() -> Int64 { Int64(Date().timeIntervalSince1970 * 1000) }

    /// 记录进度；"已看完"会删除并返回 false
    @discardableResult
    func remember(_ key: String, pos: Double, dur: Double, title: String = "") -> Bool {
        guard !key.isEmpty, dur >= 1, pos >= Self.recordMin else { return false }
        if pos > dur - Self.endGuard {
            if items.removeValue(forKey: key) != nil { Config.log("进度：已看完，清除记录 \(key)") }
            return false
        }
        var e = Entry()
        e.pos = pos
        e.dur = dur
        e.title = title.isEmpty ? (items[key]?.title ?? "") : title
        e.updatedAt = Self.nowMs()
        items[key] = e
        return true
    }

    func resumePos(_ key: String) -> Double {
        guard let e = items[key], e.dur >= 1, e.pos >= Self.resumeMin, e.pos <= e.dur - Self.endGuard else { return 0 }
        return e.pos
    }

    func has(_ key: String) -> Bool { items[key] != nil }
    @discardableResult func remove(_ key: String) -> Bool { items.removeValue(forKey: key) != nil }
    func clear() { items.removeAll() }
    var count: Int { items.count }

    /// 过期清理 + 超量丢最旧
    func prune() {
        let now = Self.nowMs()
        for (k, e) in items where e.updatedAt > 0 && now - e.updatedAt > Self.maxAgeMs {
            items.removeValue(forKey: k)
        }
        guard items.count > Self.maxEntries else { return }
        let sorted = items.sorted { $0.value.updatedAt < $1.value.updatedAt }
        for (k, _) in sorted.prefix(items.count - Self.maxEntries) { items.removeValue(forKey: k) }
        Config.log("进度：超出上限，已丢弃最旧若干条")
    }

    func list() -> [(String, Entry)] { items.map { ($0.key, $0.value) }.sorted { $0.1.updatedAt > $1.1.updatedAt } }
}
