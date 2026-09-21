import Foundation

/// 播放队列（JSON 结构与 Qt 端一致：entries/index/mode，可跨端读写 queue.json）
final class PlayQueue {
    enum Mode: Int { case sequential = 0, repeatOne = 1, shuffle = 2, repeatAll = 3 }
    struct Entry { var key = "", label = "" }

    private(set) var entries: [Entry] = []
    private(set) var index = -1
    private(set) var mode: Mode = .sequential

    init(path: String = Config.queuePath) { self.path = path }
    private let path: String

    func setList(_ list: [Entry], startAt: Int = 0) {
        entries = list
        index = list.isEmpty ? -1 : max(0, min(startAt, list.count - 1))
    }
    var isEmpty: Bool { entries.isEmpty }
    var size: Int { entries.count }
    func current() -> Entry? { (index >= 0 && index < entries.count) ? entries[index] : nil }
    func at(_ i: Int) -> Entry? { (i >= 0 && i < entries.count) ? entries[i] : nil }
    func jumpTo(_ i: Int) { if i >= 0 && i < entries.count { index = i } }
    /// 清掉当前项标记（列表已就绪但还没开始播）
    func clearCurrent() { index = -1 }

    /// 下一首；返回 false 表示"没动"（单曲循环或列表空）
    @discardableResult
    func next() -> Bool {
        guard !entries.isEmpty else { return false }
        switch mode {
        case .repeatOne, .repeatAll:      // manual next: advance in order (auto path handles repeat-one)
            if index + 1 >= entries.count { index = 0 } else { index += 1 }
            return true
        case .shuffle: index = Int.random(in: 0..<entries.count); return true
        case .sequential:
            if index + 1 >= entries.count { index = 0 } else { index += 1 }
            return true
        }
    }
    @discardableResult
    func prev() -> Bool {
        guard !entries.isEmpty else { return false }
        if index - 1 < 0 { index = entries.count - 1 } else { index -= 1 }
        return true
    }
    func cycleMode() { mode = Mode(rawValue: (mode.rawValue + 1) % 4) ?? .sequential }   // 顺序→单曲→随机→列表循环
    var modeLabel: String {
        switch mode {
        case .repeatOne: return "单曲循环"
        case .shuffle: return "随机播放"
        case .repeatAll: return "列表循环"
        default: return "顺序播放"
        }
    }
    func label() -> String {
        guard let e = current() else { return "" }
        return "第 \(index + 1)/\(entries.count) 首 · \(modeLabel) · \(e.label)"
    }

    @discardableResult
    func save() -> Bool { saveToPath(path) }
    @discardableResult
    func saveToPath(_ p: String) -> Bool {
        let arr: [[String: Any]] = entries.map { ["key": $0.key, "label": $0.label] }
        return Config.writeJSON(["entries": arr, "index": index, "mode": mode.rawValue], to: p)
    }
    @discardableResult
    func load() -> Bool { loadFromPath(path) }
    @discardableResult
    func loadFromPath(_ p: String) -> Bool {
        let root = Config.readJSON(p)
        guard let arr = root["entries"] as? [Any] else { return false }
        var list: [Entry] = []
        for v in arr {
            guard let o = v as? [String: Any], let k = o["key"] as? String else { continue }
            list.append(Entry(key: k, label: (o["label"] as? String) ?? ""))
        }
        entries = list
        index = (root["index"] as? NSNumber)?.intValue ?? (list.isEmpty ? -1 : 0)
        mode = Mode(rawValue: (root["mode"] as? NSNumber)?.intValue ?? 0) ?? .sequential
        if index >= entries.count { index = entries.isEmpty ? -1 : 0 }
        return true
    }
}
