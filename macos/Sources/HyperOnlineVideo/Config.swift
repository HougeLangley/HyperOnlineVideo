import Foundation

/// 配置位置：与 Linux/Qt 端、Android 端**共用同一套文件**
/// （设置/队列/收藏/进度/cookie 都能跨端复用，这是单仓库三端的一个实际好处）
enum Config {
    static var dir: String { NSHomeDirectory() + "/.config/hov" }
    static var settingsPath: String { dir + "/settings.json" }
    static var queuePath: String { dir + "/queue.json" }
    static var favoritesPath: String { dir + "/favorites.json" }
    static var progressPath: String { dir + "/progress.json" }
    static var downloadDir: String { NSHomeDirectory() + "/Downloads/hov" }
    static func cookiePath(_ site: String) -> String { dir + "/cookies/\(site).txt" }

    static func readJSON(_ path: String) -> [String: Any] {
        guard let data = FileManager.default.contents(atPath: path),
              let obj = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else { return [:] }
        return obj
    }

    @discardableResult
    static func writeJSON(_ obj: [String: Any], to path: String) -> Bool {
        let url = URL(fileURLWithPath: path)
        try? FileManager.default.createDirectory(at: url.deletingLastPathComponent(),
                                                 withIntermediateDirectories: true)
        guard let data = try? JSONSerialization.data(withJSONObject: obj, options: [.prettyPrinted, .sortedKeys])
        else { return false }
        return (try? data.write(to: url)) != nil
    }

    /// 日志：与 Qt 端一致，关键状态写 stderr（自动化可判定）
    static func log(_ s: String) {
        FileHandle.standardError.write((s + "\n").data(using: .utf8)!)
    }
}
