import Foundation

/// 在线播放解析（与 Qt 端同一套做法：App 层调 yt-dlp 解析出真实直链，再交给 mpv）
///   - 视频/音频分流（ADR-002）：-f "bv*+ba/b"，合并格式的地址在 requested_formats 里
///   - B站 CC 字幕走官方 CC API（x/web-interface/view → x/player/v2 → subtitle_url）
///   - YouTube 字幕走 yt-dlp 落地文件（json3/srt）
///   - cookie 复用 ~/.config/hov/cookies/<站点>.txt（与另两端一致）
final class UrlResolver {
    struct Stream {
        var videoUrl = "", audioUrl = "", title = ""
        var subtitles: [SubtitleTrack] = []
    }

    static func serviceOf(_ url: String) -> String {
        let u = url.lowercased()
        if u.contains("youtube.com") || u.contains("youtu.be") { return "youtube" }
        if u.contains("bilibili.com") || u.contains("b23.tv") { return "bilibili" }
        if u.contains("music.163.com") { return "netease" }
        if u.contains("y.qq.com") || u.contains("qqmusic") { return "qqmusic" }
        return ""
    }

    static func cookieFileFor(_ url: String) -> String {
        let s = serviceOf(url)
        guard !s.isEmpty else { return "" }
        let p = Config.cookiePath(s)
        return FileManager.default.fileExists(atPath: p) ? p : ""
    }

    static func isDirectMedia(_ url: String) -> Bool {
        if url.hasPrefix("/") || url.hasPrefix("file://") { return true }
        if url.hasPrefix("http://") || url.hasPrefix("https://") {
            for ext in [".mp4", ".m4a", ".mp3", ".webm", ".mkv", ".flac", ".aac", ".mov", ".ts", ".m3u8"] where url.contains(ext) {
                return true
            }
        }
        return false
    }

    // MARK: 子进程（注意：管道必须在后台读取，否则大输出会死锁）
    struct ProcResult { var code: Int32 = -1; var out = Data(); var err = "" }

    /// 在 PATH（含 Homebrew 常见位置）里找可执行文件
    /// 注意：Process.executableURL **不会**查 PATH，必须给绝对路径
    static func findExecutable(_ name: String) -> String? {
        var dirs = ["/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin"]
        if let path = ProcessInfo.processInfo.environment["PATH"] {
            dirs = path.components(separatedBy: ":") + dirs
        }
        for d in dirs where !d.isEmpty {
            let p = d + "/" + name
            if FileManager.default.isExecutableFile(atPath: p) { return p }
        }
        return nil
    }

    @discardableResult
    static func runProcess(_ launch: String, _ args: [String], timeout: TimeInterval = 120) -> ProcResult {
        var res = ProcResult()
        let p = Process()
        p.executableURL = URL(fileURLWithPath: launch)
        p.arguments = args
        let outPipe = Pipe(), errPipe = Pipe()
        p.standardOutput = outPipe
        p.standardError = errPipe
        do { try p.run() } catch {
            res.err = "无法启动 \(launch)：\(error.localizedDescription)"
            return res
        }
        let sem = DispatchSemaphore(value: 0)
        var outData = Data(), errData = Data()
        DispatchQueue.global().async {
            outData = outPipe.fileHandleForReading.readDataToEndOfFile()
            errData = errPipe.fileHandleForReading.readDataToEndOfFile()
            sem.signal()
        }
        p.waitUntilExit()
        _ = sem.wait(timeout: .now() + timeout)
        res.code = p.terminationStatus
        res.out = outData
        res.err = String(data: errData, encoding: .utf8) ?? ""
        return res
    }

    // MARK: 解析直链
    func resolve(_ pageUrl: String) -> Stream? {
        var args = ["-J", "--no-warnings", "--no-playlist", "-f", "bv*+ba/b"]
        let cookies = Self.cookieFileFor(pageUrl)
        if !cookies.isEmpty { args.append(contentsOf: ["--cookies", cookies]) }
        args.append(pageUrl)
        guard let ytdlp = Self.findExecutable("yt-dlp") else {
            Config.log("找不到 yt-dlp（brew install yt-dlp）")
            return nil
        }
        let r = Self.runProcess(ytdlp, args)
        guard r.code == 0 else {
            Config.log("解析失败（yt-dlp 退出码 \(r.code)）")
            let tail = r.err.components(separatedBy: "\n").suffix(3).joined(separator: "\n")
            if !tail.isEmpty { Config.log(tail) }
            return nil
        }
        guard let root = (try? JSONSerialization.jsonObject(with: r.out)) as? [String: Any] else {
            Config.log("解析结果不是合法 JSON")
            return nil
        }
        var s = Stream()
        s.title = (root["title"] as? String) ?? ""
        if let req = root["requested_downloads"] as? [Any], let first = req.first as? [String: Any] {
            let direct = (first["url"] as? String) ?? ""
            if !direct.isEmpty {
                s.videoUrl = direct
                s.audioUrl = ""
            } else if let fmts = first["requested_formats"] as? [Any] {
                for f in fmts {
                    guard let o = f as? [String: Any], let u = o["url"] as? String else { continue }
                    if (o["vcodec"] as? String) == "none" { s.audioUrl = u } else { s.videoUrl = u }
                }
            }
        }
        if s.videoUrl.isEmpty { Config.log("解析结果里没有可用地址"); return nil }
        Config.log("解析成功: \(s.title.isEmpty ? pageUrl : s.title)\(s.audioUrl.isEmpty ? "" : "（视频+音频分流）")")
        return s
    }

    // MARK: B站 CC（官方 API；无字幕清单 = 该视频确实没有 CC）
    func fetchBiliCc(_ pageUrl: String) -> [SubtitleTrack] {
        let bvRe = try! NSRegularExpression(pattern: "(BV[0-9A-Za-z]{10})")
        let avRe = try! NSRegularExpression(pattern: "av(\\d+)", options: [.caseInsensitive])
        func firstMatch(_ re: NSRegularExpression, _ s: String, _ g: Int) -> String {
            let r = NSRange(s.startIndex..., in: s)
            guard let m = re.firstMatch(in: s, range: r), let gr = Range(m.range(at: g), in: s) else { return "" }
            return String(s[gr])
        }
        let bv = firstMatch(bvRe, pageUrl, 1), av = firstMatch(avRe, pageUrl, 1)
        guard !bv.isEmpty || !av.isEmpty else {
            Config.log("B站字幕：URL 里没有 BV/av 号")
            return []
        }
        let headers = ["User-Agent": Http.ua, "Referer": "https://www.bilibili.com"]
        let viewUrl = bv.isEmpty ? "https://api.bilibili.com/x/web-interface/view?aid=\(av)"
                                 : "https://api.bilibili.com/x/web-interface/view?bvid=\(bv)"
        let v = Http.get(viewUrl, headers: headers)
        guard v.ok else { Config.log("B站字幕：view 请求失败 HTTP \(v.status)"); return [] }
        let vo = v.json()
        let data = (vo["data"] as? [String: Any]) ?? [:]
        let aid = Int((data["aid"] as? NSNumber)?.doubleValue ?? 0)
        let cid = Int((data["cid"] as? NSNumber)?.doubleValue ?? 0)
        Config.log("B站字幕：view 接口 code=\((vo["code"] as? NSNumber)?.intValue ?? -1) aid=\(aid) cid=\(cid)")
        guard (vo["code"] as? NSNumber)?.intValue == 0, aid > 0, cid > 0 else { return [] }

        let p = Http.get("https://api.bilibili.com/x/player/v2?aid=\(aid)&cid=\(cid)", headers: headers)
        guard p.ok else { Config.log("B站字幕：player/v2 请求失败"); return [] }
        let po = p.json()
        let subs = (((po["data"] as? [String: Any])?["subtitle"] as? [String: Any])?["subtitles"] as? [Any]) ?? []
        Config.log("B站字幕：player/v2 code=\((po["code"] as? NSNumber)?.intValue ?? -1) 轨数=\(subs.count)")
        var tracks: [SubtitleTrack] = []
        for v in subs {
            guard let o = v as? [String: Any] else { continue }
            var url = (o["subtitle_url"] as? String) ?? ""
            if url.hasPrefix("//") { url = "https:" + url }
            let doc = (o["lan_doc"] as? String) ?? (o["lan"] as? String) ?? "字幕"
            guard !url.isEmpty else { continue }
            let body = Http.get(url, headers: headers)
            guard body.ok else { continue }
            let cues = Subtitles.parseJSON(body.text)
            if !cues.isEmpty {
                tracks.append(SubtitleTrack(label: "\(doc) · CC", source: "", cues: cues, karaoke: false))
                Config.log("B站字幕轨已取回: \(doc) \(cues.count) 行")
            }
        }
        return tracks.sorted { Subtitles.languageRank($0.label) < Subtitles.languageRank($1.label) }
    }

    // MARK: YouTube 字幕（yt-dlp 落地文件）
    func fetchYouTubeSubs(_ pageUrl: String) -> [SubtitleTrack] {
        let dir = NSTemporaryDirectory() + "/hov-subs-" + String(abs(pageUrl.hashValue))
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        var args = ["--skip-download", "--no-warnings", "--no-playlist",
                    "--write-subs", "--write-auto-subs",
                    "--sub-langs", "zh-Hans,zh-CN,zh,zh-Hant,zh-TW,en",
                    "--sub-format", "srt/vtt/best",
                    "-o", dir + "/%(id)s.%(ext)s"]
        let cookies = Self.cookieFileFor(pageUrl)
        if !cookies.isEmpty { args.append(contentsOf: ["--cookies", cookies]) }
        args.append(pageUrl)
        guard let ytdlp = Self.findExecutable("yt-dlp") else {
            Config.log("找不到 yt-dlp（brew install yt-dlp）")
            return []
        }
        let r = Self.runProcess(ytdlp, args)
        guard let files = try? FileManager.default.contentsOfDirectory(atPath: dir) else { return [] }
        var tracks: [SubtitleTrack] = []
        for f in files.sorted() {
            let ext = (f as NSString).pathExtension.lowercased()
            guard ["srt", "vtt", "json3", "json"].contains(ext) else { continue }
            let path = dir + "/" + f
            let cues = Subtitles.loadFile(path)
            guard !cues.isEmpty else { continue }
            var lang = ((f as NSString).deletingPathExtension as NSString).pathExtension
            if lang.isEmpty { lang = "默认" }
            tracks.append(SubtitleTrack(label: "\(lang) · \(ext.uppercased())", source: path, cues: [], karaoke: false))
        }
        if tracks.isEmpty {
            let tail = r.err.components(separatedBy: "\n").filter { !$0.isEmpty }.suffix(2).joined(separator: " ")
            Config.log("在线字幕：yt-dlp 未取到字幕 exit=\(r.code) \(tail)")
        } else {
            Config.log("在线字幕：yt-dlp 取回 \(tracks.count) 条")
        }
        return tracks.sorted { Subtitles.languageRank($0.label) < Subtitles.languageRank($1.label) }
    }
}
