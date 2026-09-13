import Foundation

/// 收藏（与 Qt 端共用 favorites.json）
final class Favorites {
    struct Fav {
        var id = "", title = "", uploader = "", platform = "", key = ""
        var addedAt: Int64 = 0
    }
    private var items: [Fav] = []

    func load() {
        items.removeAll()
        let root = Config.readJSON(Config.favoritesPath)
        for v in (root["items"] as? [Any]) ?? [] {
            guard let o = v as? [String: Any], let id = o["id"] as? String, !id.isEmpty else { continue }
            var f = Fav()
            f.id = id
            f.title = (o["title"] as? String) ?? ""
            f.uploader = (o["uploader"] as? String) ?? ""
            f.platform = (o["platform"] as? String) ?? ""
            f.key = (o["key"] as? String) ?? ""
            f.addedAt = Int64((o["addedAt"] as? NSNumber)?.doubleValue ?? 0)
            items.append(f)
        }
        Config.log("收藏已加载: \(items.count) 项")
    }

    @discardableResult
    func save() -> Bool {
        let arr: [[String: Any]] = items.map {
            ["id": $0.id, "title": $0.title, "uploader": $0.uploader, "platform": $0.platform,
             "key": $0.key, "addedAt": Double($0.addedAt)]
        }
        return Config.writeJSON(["items": arr, "updatedAt": Double(ProgressStore.nowMs())], to: Config.favoritesPath)
    }

    func contains(_ id: String) -> Bool { items.contains { $0.id == id } }
    @discardableResult func add(_ f: Fav) -> Bool {
        guard !contains(f.id) else { return false }
        items.append(f)
        return true
    }
    @discardableResult func remove(_ id: String) -> Bool {
        let before = items.count
        items.removeAll { $0.id == id }
        return items.count != before
    }
    var all: [Fav] { items.sorted { $0.addedAt > $1.addedAt } }
    var count: Int { items.count }
}
