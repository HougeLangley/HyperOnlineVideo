import Foundation

/// 从系统浏览器导入 cookie（A0 跨端统一登录；macOS 侧）
///
/// 原理与 Linux 端一致：yt-dlp 的 `--cookies FILE` 会**读入并把 cookie jar 写回**该文件，
/// 配合 `--cookies-from-browser` 就把浏览器 cookie 落成了 Netscape 文件（音乐 API 与 yt-dlp 都吃）。
/// 好处：不依赖内置 WebView（AppImage/Flatpak 同样可用），且绕开 Google 对嵌入式登录的限制。
enum CookieImport {
    /// 域名 → 站点
    static func siteOfDomain(_ domainIn: String) -> String {
        let d = domainIn.hasPrefix(".") ? String(domainIn.dropFirst()) : domainIn
        let low = d.lowercased()
        let rules: [(String, [String])] = [
            ("youtube", ["youtube.com", "google.com", "googlevideo.com", "youtu.be", "googleapis.com", "gstatic.com"]),
            ("bilibili", ["bilibili.com", "hdslb.com", "b23.tv", "bilivideo.com"]),
            ("netease", ["163.com", "126.com", "126.net"]),
            ("qqmusic", ["qq.com", "gtimg.cn"]),
        ]
        for (site, suffixes) in rules where suffixes.contains(where: { low.hasSuffix($0) }) { return site }
        return ""
    }

    /// 合并的 Netscape 内容 → 站点 → 该站点 Netscape 文本（含文件头；无关域名丢弃）
    static func splitBySite(_ combined: String) -> [String: String] {
        var buckets: [String: [String]] = [:]
        for line in combined.components(separatedBy: CharacterSet.newlines) {
            let t = line.trimmingCharacters(in: .whitespaces)
            if t.isEmpty || t.hasPrefix("#") { continue }
            let f = t.components(separatedBy: "\t")
            guard f.count >= 7 else { continue }
            let site = siteOfDomain(f[0])
            if site.isEmpty { continue }                       // 隐私：与本应用无关的域名直接丢弃
            // Netscape 第 2 列必须与域名是否带前导点一致（否则 yt-dlp 报 invalid format）
            let includeSub = f[0].hasPrefix(".") ? "TRUE" : "FALSE"
            buckets[site, default: []].append([f[0], includeSub, f[2], f[3], f[4], f[5], f[6]].joined(separator: "\t"))
        }
        var out: [String: String] = [:]
        for (site, lines) in buckets {
            out[site] = "# Netscape HTTP Cookie File\n# 由 Hyper Online Video 从系统浏览器导入\n\n" + lines.joined(separator: "\n") + "\n"
        }
        return out
    }

    static func countsOf(_ bySite: [String: String]) -> [String: Int] {
        var out: [String: Int] = [:]
        for (site, text) in bySite {
            out[site] = text.components(separatedBy: "\n").filter { !$0.trimmingCharacters(in: .whitespaces).isEmpty && !$0.hasPrefix("#") }.count
        }
        return out
    }

    /// 跑一次 yt-dlp（用浏览器 cookie）→ 合并 jar 落到临时文件 → 按站点切分写入 cookies/ 目录
    static func importFromBrowser(_ browser: String, probeUrl: String) -> (ok: Bool, summary: String) {
        guard !browser.isEmpty else { return (false, "未指定浏览器（设置项 network.cookiesFromBrowser）") }
        guard let ytdlp = UrlResolver.findExecutable("yt-dlp") else { return (false, "找不到 yt-dlp（brew install yt-dlp）") }
        let dir = Config.dir + "/cookies"
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        let tmp = dir + "/.import-combined.txt"
        try? FileManager.default.removeItem(atPath: tmp)

        let r = UrlResolver.runProcess(ytdlp, ["--cookies-from-browser", browser, "--cookies", tmp,
                                              "--simulate", "--skip-download", "--no-warnings", probeUrl], timeout: 120)
        guard let text = try? String(contentsOfFile: tmp, encoding: .utf8) else {
            let tail = r.err.components(separatedBy: "\n").filter { !$0.isEmpty }.suffix(2).joined(separator: " ")
            return (false, "未能导出 cookie（yt-dlp 退出码 \(r.code)）：\(tail)")
        }
        let bySite = splitBySite(text)
        guard !bySite.isEmpty else {
            try? FileManager.default.removeItem(atPath: tmp)
            return (false, "浏览器里没有与本站点相关的 cookie（请先在浏览器登录对应站点）")
        }
        let counts = countsOf(bySite)
        var parts: [String] = []
        for (site, body) in bySite {
            if (try? body.write(toFile: dir + "/\(site).txt", atomically: true, encoding: .utf8)) != nil {
                parts.append("\(site)=\(counts[site] ?? 0) 条")
            }
        }
        try? FileManager.default.removeItem(atPath: tmp)      // 合并文件含全部浏览器 cookie → 立刻删
        return (parts.isEmpty ? false : true, parts.isEmpty ? "写入失败（目录不可写？）" : parts.sorted().joined(separator: "，"))
    }
}
