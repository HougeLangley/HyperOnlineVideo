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
        return (try? data.write(to: url, options: .atomic)) != nil      // 原子写：崩在半路也不会留下半个 JSON
    }

    /// 日志：与 Qt 端一致，关键状态写 stderr（自动化可判定）
    static func log(_ s: String) {
        FileHandle.standardError.write((s + "\n").data(using: .utf8)!)
        appendToLogFile(s)      // 同时落盘：从 Finder 启动时 stderr 看不到，用户复现后可直接把日志给我
    }

    /// 日志落盘：串行队列 + 常驻句柄。
    /// 修掉三个问题：①原来多线程并发追加同一文件（下载/网络/GL 都在写）→ 行会交错损坏，
    /// 而自检还要解析这些行；②原来每行都开/关一次文件句柄（无用 syscall）；
    /// ③原来超限时 `truncate(atOffset: 0)` 后不回位 → 下一行写在旧偏移处，产生稀疏大文件，
    /// 且与注释"保留最后 1MB"不符。现在按注释保留后半并重开句柄。
    private static let logQueue = DispatchQueue(label: "com.hougelangley.hov.log")
    private static var logHandle: FileHandle?
    private static let logLimit = 2 * 1024 * 1024

    private static func appendToLogFile(_ s: String) {
        let line = String(format: "%.1f ", Date().timeIntervalSince1970) + s + "\n"
        logQueue.sync {                                     // sync：保证顺序、且退出前不会丢行
            let path = dir + "/app.log"
            if logHandle == nil {
                try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
                if !FileManager.default.fileExists(atPath: path) {
                    FileManager.default.createFile(atPath: path, contents: nil)
                }
                logHandle = try? FileHandle(forWritingTo: URL(fileURLWithPath: path))
                _ = try? logHandle?.seekToEnd()
            }
            guard let h = logHandle, let data = line.data(using: .utf8) else { return }
            try? h.write(contentsOf: data)   // write(contentsOf:) 会抛（offsetInFile/seekToEnd 才是不抛的那个）
            let size = h.offsetInFile        // 不抛：加 try? 会被警告
            if size > logLimit, let all = try? Data(contentsOf: URL(fileURLWithPath: path)) {
                let keep = all.suffix(logLimit / 2)         // 保留最后 1MB
                _ = try? h.close()
                try? Data(keep).write(to: URL(fileURLWithPath: path))
                logHandle = try? FileHandle(forWritingTo: URL(fileURLWithPath: path))
                _ = try? logHandle?.seekToEnd()
            }
        }
    }
}
