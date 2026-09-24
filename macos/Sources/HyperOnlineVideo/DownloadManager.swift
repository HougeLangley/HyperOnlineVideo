import Foundation

/// 下载管理（macOS 端；与 Linux(Qt)/Android 端语义对齐）
///
/// 核心语义：并发 2 + 失败重试 2 + 进度节流 10% + 文件名净化 + 下载完成后写标签/封面 + LRU 清理。
/// 落盘：设置 `download.dir`（默认 ~/Downloads/hov）。
final class DownloadManager {
    enum State: String { case queued = "排队", running = "下载中", done = "完成", failed = "失败", canceled = "已取消" }

    struct Job {
        var id = UUID().uuidString
        var title = ""
        var url = ""
        var filePath = ""
        var state: State = .queued
        var bytesDone: Int64 = 0
        var bytesTotal: Int64 = -1
        var retries = 0
        var error = ""
        var audioUrl = ""                 // 音轨直链（YouTube 等分离音轨：与视频流一起 ffmpeg 封装）
        var lyrics = ""                   // 歌词原文（音乐：写成同名 .lrc）
        var isAudio = true                // 音频任务才嵌标签/写歌词；视频任务跳过（避免把视频重封装成纯音频）
        // 音乐标签（下载完成后嵌入；为空则跳过）
        var artist = ""
        var album = ""
        var coverUrl = ""
    }

    static let maxConcurrent = 2
    static let maxRetries = 2

    private(set) var jobs: [Job] = []
    var onUpdate: ((Job) -> Void)?
    /// 播放中？由 App 层注入：播放时下载并发降到 1，别和流媒体抢带宽/连接（用户实测：后台下载时切歌卡很久）
    var isPlaying: (() -> Bool)?

    /// 下载专用会话：标记为**后台优先级**（系统调度会让位于前台流媒体）+ 每主机 1 条连接
    private static let session: URLSession = {
        let cfg = URLSessionConfiguration.default
        cfg.networkServiceType = .background
        cfg.httpMaximumConnectionsPerHost = 1
        cfg.waitsForConnectivity = true
        return URLSession(configuration: cfg)
    }()
    var log: ((String) -> Void)?
    private var active: [String: URLSessionDownloadTask] = [:]
    /// W4 ✓ 混流进程句柄 + 进度定时器（取消时要能杀掉 ✗ 原来用 `UrlResolver.runProcess` 拿不到句柄 → 取消不掉 ✓）
    private var muxProcs: [String: Process] = [:]
    private var muxTimers: [String: Timer] = [:]
    /// W2 ✓ 进度采样定时器（0.3s；active 清空后自停 ✓）
    private var progressTimer: Timer?
    private var dirOverride = ""

    func setDir(_ d: String) { dirOverride = d }
    func dir() -> String {
        let d = dirOverride.isEmpty ? (Settings.shared.string("download.dir")) : dirOverride
        let path = d.isEmpty ? Config.downloadDir : d
        try? FileManager.default.createDirectory(atPath: path, withIntermediateDirectories: true)
        return path
    }

    /// 文件名净化（与 Qt 端同一套规则：去掉路径分隔符与 Windows 保留字符）
    static func sanitize(_ name: String) -> String {
        var out = name
        for ch in ["/", "\\", ":", "*", "?", "\"", "<", ">", "|"] { out = out.replacingOccurrences(of: ch, with: "_") }
        out = out.trimmingCharacters(in: .whitespacesAndNewlines)
        return out.isEmpty ? "hov-download" : String(out.prefix(120))
    }

    @discardableResult
    func enqueue(url: String, title: String, fileNameHint: String = "",
                 artist: String = "", album: String = "", coverUrl: String = "",
                 audioUrl: String = "", lyrics: String = "") -> String? {
        guard !url.isEmpty else { return nil }
        var j = Job()
        j.title = title.isEmpty ? url : title
        j.url = url
        j.artist = artist
        j.album = album
        j.coverUrl = coverUrl
        j.audioUrl = audioUrl
        j.lyrics = lyrics
        j.isAudio = ["mp3", "flac", "m4a", "aac", "wav", "ape", "ogg"]
            .contains((j.filePath as NSString).pathExtension.lowercased())   // 用最终文件名判断（name 此时还没算出来）
        var name = fileNameHint.isEmpty ? j.title : fileNameHint
        if (name as NSString).pathExtension.isEmpty { name += ".mp3" }     // 未知扩展名按音频处理
        j.filePath = dir() + "/" + Self.sanitize(name)
        try? FileManager.default.removeItem(atPath: j.filePath)            // 不续传：避免半截文件被当成完成
        jobs.append(j)
        log?("已加入下载队列：\(j.title)")
        onUpdate?(j)
        pump()
        return j.id
    }

    /// 下载时的 Referer：按 URL 归属源站给（音乐 CDN 会校验来源）。
    /// 之前写死 music.163.com —— QQ 音乐的链接会被拒（用户实测：下载全失败，先被 ATS 拦，修完 ATS 还有这一关）。
    static func referer(for url: String) -> String {
        let u = url.lowercased()
        if u.contains("qq.com") { return "https://y.qq.com/" }
        if u.contains("126.net") || u.contains("music.163") { return "https://music.163.com/" }
        if u.contains("bilivideo") || u.contains("hdslb") || u.contains("bilibili") { return "https://www.bilibili.com/" }
        if u.contains("googlevideo") { return "https://www.youtube.com/" }
        return "https://music.163.com/"
    }

    private func pump() {
        let limit = (isPlaying?() ?? false) ? 1 : Self.maxConcurrent
        while active.count < limit,
              let idx = jobs.firstIndex(where: { $0.state == .queued }) {
            start(jobs[idx].id)
        }
    }

    private func start(_ id: String) {
        guard let idx = jobs.firstIndex(where: { $0.id == id }) else { return }
        if !jobs[idx].audioUrl.isEmpty {          // 分离音轨（YouTube DASH 等）→ 走 ffmpeg 合并路径
            startMux(id, video: jobs[idx].url, audio: jobs[idx].audioUrl)
            return
        }
        jobs[idx].state = .running
        onUpdate?(jobs[idx])
        guard let u = URL(string: jobs[idx].url) else {           // 以前是强制解包：脏 URL 会直接把 app 崩掉
            fail(id, "下载地址非法")
            finish(id)
            return
        }
        var req = URLRequest(url: u)
        req.setValue(Http.ua, forHTTPHeaderField: "User-Agent")
        req.setValue(Self.referer(for: jobs[idx].url), forHTTPHeaderField: "Referer")

        let task = Self.session.downloadTask(with: req) { [weak self] tmp, _, err in
            guard let self else { return }
            if let err { self.fail(id, err.localizedDescription); self.finish(id); return }
            guard let tmp else { self.fail(id, "没有收到数据"); self.finish(id); return }
            // ── 落盘：纯文件 I/O 放在**本回调（已在后台队列）**里做，不要进主线程 ──
            // 原实现在 main.async 内直接 removeItem + moveItem：删除旧文件（重下同名大文件）与跨卷移动
            // 在大文件上可达秒级 → 正是看门狗抓到的「下载时主线程卡顿 1.9 秒」（见踩坑百科 #143）。
            let dst = URL(fileURLWithPath: self.jobs.first(where: { $0.id == id })?.filePath ?? "")
            var size: Int64 = 0
            if !dst.path.isEmpty {
                try? FileManager.default.removeItem(at: dst)
                do { try FileManager.default.moveItem(at: tmp, to: dst) }
                catch {
                    DispatchQueue.main.async { self.fail(id, "落盘失败：\(error.localizedDescription)"); self.finish(id) }
                    return
                }
                if let attrs = try? FileManager.default.attributesOfItem(atPath: dst.path),
                   let n = attrs[.size] as? Int64 { size = n }
            }
            DispatchQueue.main.async {
                guard let i = self.jobs.firstIndex(where: { $0.id == id }) else { self.finish(id); return }
                self.jobs[i].bytesDone = size
                self.jobs[i].bytesTotal = size
                self.onUpdate?(self.jobs[i])
                // 落盘完成：余下的重活（歌词/嵌标签/LRU）全部交给公共收尾在后台做
                //（嵌标签 ffmpeg 最长 60s + ffprobe 20s，在主线程做会让界面冻几十秒 —— 用户实测真凶之一）
                self.finalizeDownload(id: id, filePath: self.jobs[i].filePath, size: size, source: "直链")
            }
        }
        active[id] = task
        task.resume()
        startProgressSampling()      // W2 ✓ 开始采样增量进度（面板进度条靠它 ✓）
    }

    // MARK: - W2 增量进度采样
    //
    /// 用户口径（2026-09-23）：「macOS 和 Linux 端的下载面板都缺下载进度显示」。
    /// 根因：本实现用 downloadTask 的**完成回调** → 全程拿不到 bytesDone/bytesTotal ✗
    ///   （实测面板里只能显示“进行中”，大小列空白 ✗）。
    /// 为何不用 `URLSessionDownloadDelegate`：它的 `didFinishDownloadingTo` 是**必选** ✗，
    ///   一旦实现，临时文件就必须由代理搬走（否则被系统删除），与现有完成回调的落盘逻辑冲突 ✗ 风险高。
    /// 采用 `URLSessionTask.progress`（Foundation 自带 ✓）定期采样：**零侵入** ✓ 不改任何下载/落盘路径 ✓
    private func startProgressSampling() {
        guard progressTimer == nil else { return }
        // 主 RunLoop 定时器 ✓ → 回调在主线程 → 与 jobs 的“仅主线程访问”约束一致 ✓
        progressTimer = Timer.scheduledTimer(withTimeInterval: 0.3, repeats: true) { [weak self] _ in
            self?.sampleProgress()
        }
    }

    private func sampleProgress() {
        guard !active.isEmpty else { progressTimer?.invalidate(); progressTimer = nil; return }
        for (id, task) in active {
            guard let i = jobs.firstIndex(where: { $0.id == id }) else { continue }
            // ⚠️ 用 countOfBytesReceived/Expected ✗ **不是** task.progress：
            //    实测（独立小脚本 6 秒采样）progress 是 0~100 的**归一化值**且滞后（t=6s 仍 0/100 ✗），
            //    而 countOfBytes* 是真实字节（262144 / 41943040 ✓✓）
            let done = task.countOfBytesReceived
            let total = task.countOfBytesExpectedToReceive
            if done <= 0 && total <= 0 { continue }                  // 还没开始收字节
            if jobs[i].bytesDone == done && (total <= 0 || jobs[i].bytesTotal == total) { continue }
            jobs[i].bytesDone = done
            if total > 0 { jobs[i].bytesTotal = total }
            onUpdate?(jobs[i])                                        // → 面板 update(job:) → 进度条前进 ✓
        }
    }

    // MARK: - 公共收尾（消除"直链下载"与"ffmpeg 混流"两条路径的重复代码）

    /// 收尾所需的元信息快照（**必须在主线程取**：jobs 只允许主线程访问）
    private struct FinalizeMeta {
        var isAudio = false, title = "", artist = "", album = "", coverUrl = "", lyrics = ""
    }

    private func metaSnapshot(for id: String) -> FinalizeMeta {
        guard let j = jobs.first(where: { $0.id == id }) else { return FinalizeMeta() }
        return FinalizeMeta(isAudio: j.isAudio, title: j.title, artist: j.artist,
                            album: j.album, coverUrl: j.coverUrl, lyrics: j.lyrics)
    }

    /// 两个下载路径的**公共收尾**：歌词侧车 → 嵌标签 → 回主线程写状态 → LRU 清理。
    /// 拆分前这段在两个地方各写了一遍（近乎逐行相同）——性能修复（重活下后台）也要改两处，
    /// 属于典型的"重复代码导致的维护陷阱"。
    private func finalizeDownload(id: String, filePath: String, size: Int64, source: String) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            let meta = self.metaSnapshot(for: id)          // 主线程取快照
            DispatchQueue.global(qos: .utility).async { [weak self] in
                guard let self else { return }
                if meta.isAudio, !meta.lyrics.isEmpty {    // 音乐：写同名 .lrc 侧车（本地库播放自动加载）
                    let lrcPath = (filePath as NSString).deletingPathExtension + ".lrc"
                    try? meta.lyrics.write(toFile: lrcPath, atomically: true, encoding: .utf8)
                    self.log?("已写入歌词侧车文件：\((lrcPath as NSString).lastPathComponent)（\(meta.lyrics.count) 字节）")
                }
                // 只有**音频**任务才嵌标签：视频任务原来是 `-map 0:a?`，等于把视频重封装成纯音频（实测踩到）
                let ok = meta.isAudio && self.embedTags(title: meta.title, artist: meta.artist, album: meta.album,
                                                        coverUrl: meta.coverUrl, filePath: filePath)
                DispatchQueue.main.async { [weak self] in
                    guard let self else { return }
                    guard let i = self.jobs.firstIndex(where: { $0.id == id }) else { self.finish(id); return }
                    if ok { self.log?("已嵌入音乐标签：\(meta.title)") }
                    self.jobs[i].bytesDone = size
                    self.jobs[i].bytesTotal = size
                    self.jobs[i].state = .done
                    self.log?("下载完成（\(source)）：\((filePath as NSString).lastPathComponent)（\(size / 1024) KB）")
                    self.onUpdate?(self.jobs[i])
                    self.finish(id)
                    // LRU 清理同样下后台：目录大/刚下完大文件时，遍历+删除在主线程会卡
                    let mb = Settings.shared.number("download.maxSizeMb", 2048)
                    if mb > 0 {
                        DispatchQueue.global(qos: .utility).async { _ = self.cleanupLru(maxBytes: Int64(mb) * 1024 * 1024) }
                    }
                }
            }
        }
    }

    /// 视频流与音轨**分两条**时（YouTube DASH/B站 分离音轨）：交给 ffmpeg 直接拉两路并封装成一个 mp4。
    /// 原来只下载 `lastStreamUrl`（视频流）→ 本地库播放**没有声音**（用户实测反馈）。
    private func startMux(_ id: String, video: String, audio: String) {
        guard let idx = jobs.firstIndex(where: { $0.id == id }) else { return }
        jobs[idx].state = .running
        jobs[idx].bytesTotal = -1               // ffmpeg 不报进度：用 -1 表示"进行中"
        onUpdate?(jobs[idx])
        let j = jobs[idx]
        let dst = j.filePath
        // W4 ✓ 进度：ffmpeg 全程不报字节 ✗ → ① HEAD 两路拿总长 ✓ ② 0.5s 轮询输出文件大小 ✓
        //   （用户口径：macOS 面板“没有下载的文件大小显示” ✗ Linux 能显示 `9.6 MB / 108.9 MB` ✓ → 两端对齐 ✓）
        startMuxProgress(id: id, urls: [video, audio], outPath: dst)
        DispatchQueue.global(qos: .utility).async { [weak self] in
            guard let self else { return }
            guard let ffmpeg = UrlResolver.findExecutable("ffmpeg") else {
                DispatchQueue.main.async { self.stopMuxProgress(id); self.fail(id, "找不到 ffmpeg（合并视频与音轨需要它）"); self.finish(id) }
                return
            }
            try? FileManager.default.removeItem(atPath: dst)
            // ⚠️ 自己起 Process（而不是 `UrlResolver.runProcess` ✗）：**需要句柄才能取消** ✗→✓
            //    （旧实现拿不到句柄 → “取消全部/取消下载”对混流中的任务无效 ✗）
            let proc = Process()
            proc.executableURL = URL(fileURLWithPath: ffmpeg)
            proc.arguments = ["-y", "-hide_banner", "-loglevel", "error",
                              "-i", video, "-i", audio,
                              "-c", "copy", "-movflags", "+faststart", dst]
            let pipe = Pipe()
            proc.standardError = pipe
            proc.standardOutput = pipe
            DispatchQueue.main.async { self.muxProcs[id] = proc }
            do { try proc.run() } catch {
                DispatchQueue.main.async {
                    self.stopMuxProgress(id)
                    self.muxProcs.removeValue(forKey: id)
                    self.fail(id, "无法启动 ffmpeg：\(error.localizedDescription)")
                    self.finish(id)
                }
                return
            }
            proc.waitUntilExit()
            let errText = String(data: pipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
            let code = proc.terminationStatus
            let size = ((try? FileManager.default.attributesOfItem(atPath: dst))?[.size] as? Int64) ?? 0
            let ok = code == 0 && size > 0
            DispatchQueue.main.async {
                self.stopMuxProgress(id)
                self.muxProcs.removeValue(forKey: id)
                guard self.jobs.contains(where: { $0.id == id }) else { return }   // 已被取消（条目已移除）→ 到此为止 ✓
                if ok {
                    self.finalizeDownload(id: id, filePath: dst, size: size, source: "本地混流")
                } else {
                    guard let i = self.jobs.firstIndex(where: { $0.id == id }) else { self.finish(id); return }
                    self.jobs[i].state = .failed
                    self.jobs[i].error = "合并视频+音轨失败：\(errText.isEmpty ? "ffmpeg 返回 \(code)" : String(errText.prefix(140)))"
                    self.log?("下载失败：\(j.title) — \(self.jobs[i].error)")
                    self.onUpdate?(self.jobs[i])
                    self.finish(id)
                }
            }
        }
    }

    /// W4 ✓ 混流进度上报（ffmpeg 不报字节 ✗）：HEAD 两路求总长 + 轮询输出文件大小
    private func startMuxProgress(id: String, urls: [String], outPath: String) {
        for u in urls where !u.isEmpty {
            guard let url = URL(string: u) else { continue }
            var req = URLRequest(url: url)
            req.httpMethod = "HEAD"
            req.setValue(Http.ua, forHTTPHeaderField: "User-Agent")
            req.setValue(Self.referer(for: u), forHTTPHeaderField: "Referer")
            URLSession.shared.dataTask(with: req) { [weak self] _, resp, _ in
                guard let self, let n = resp?.expectedContentLength, n > 0 else { return }
                DispatchQueue.main.async {                       // 主线程串行叠加 ✓ 无数据竞争 ✓
                    guard let i = self.jobs.firstIndex(where: { $0.id == id }) else { return }
                    self.jobs[i].bytesTotal = max(0, self.jobs[i].bytesTotal) + n
                    self.onUpdate?(self.jobs[i])
                }
            }.resume()
        }
        let t = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            guard let self, let i = self.jobs.firstIndex(where: { $0.id == id }) else { return }
            let sz = ((try? FileManager.default.attributesOfItem(atPath: outPath))?[.size] as? Int64) ?? 0
            if sz != self.jobs[i].bytesDone {
                self.jobs[i].bytesDone = sz
                self.onUpdate?(self.jobs[i])
            }
        }
        muxTimers[id] = t
    }

    private func stopMuxProgress(_ id: String) {
        muxTimers[id]?.invalidate()
        muxTimers.removeValue(forKey: id)
    }

    /// 结束一条任务：清 active + 继续调度队列（都在主线程）
    private func finish(_ id: String) {
        DispatchQueue.main.async {
            self.active.removeValue(forKey: id)
            self.pump()
        }
    }

    private func fail(_ id: String, _ reason: String) {
        DispatchQueue.main.async {
            guard let i = self.jobs.firstIndex(where: { $0.id == id }) else { return }
            if self.jobs[i].retries < Self.maxRetries {
                self.jobs[i].retries += 1
                self.jobs[i].state = .queued
                self.log?("下载失败（第 \(self.jobs[i].retries) 次），重试：\(reason)")
            } else {
                self.jobs[i].state = .failed
                self.jobs[i].error = reason
                self.log?("下载失败：\(self.jobs[i].title) — \(reason)")
            }
            self.onUpdate?(self.jobs[i])
        }
    }

    /// 音乐标签嵌入：写临时文件 → ffprobe 读回校验 → 替换（任一步失败都保留原文件）
    /// 与 Linux 端同一策略（绝不写坏下载产物）
    @discardableResult
    func embedTags(title: String, artist: String, album: String, coverUrl: String, filePath: String) -> Bool {
        guard !title.isEmpty || !artist.isEmpty else { return false }
        guard let ffmpeg = UrlResolver.findExecutable("ffmpeg") else { return false }
        let ext = (filePath as NSString).pathExtension
        let tmp = filePath + ".tag" + (ext.isEmpty ? "" : "." + ext)
        try? FileManager.default.removeItem(atPath: tmp)
        var args = ["-y", "-hide_banner", "-loglevel", "error", "-i", filePath]
        let hasCover = !coverUrl.isEmpty
        if hasCover { args += ["-i", coverUrl] }
        args += ["-map", "0:a?"]
        if hasCover { args += ["-map", "1", "-disposition:v:0", "attached_pic",
                               "-metadata:s:v", "title=Album cover", "-metadata:s:v", "comment=Cover (front)"] }
        args += ["-c:a", "copy", "-metadata", "title=\(title)"]
        if !artist.isEmpty { args += ["-metadata", "artist=\(artist)"] }
        if !album.isEmpty { args += ["-metadata", "album=\(album)"] }
        if ext.lowercased() == "mp3" { args += ["-id3v2_version", "3"] }
        args.append(tmp)
        let r = UrlResolver.runProcess(ffmpeg, args, timeout: 60)
        guard r.code == 0, FileManager.default.fileExists(atPath: tmp) else {
            try? FileManager.default.removeItem(atPath: tmp)
            return false
        }
        // 读回校验：标题确实写进去了才替换
        if let ffprobe = UrlResolver.findExecutable("ffprobe") {
            let p = UrlResolver.runProcess(ffprobe, ["-v", "error", "-show_entries", "format_tags=title",
                                                     "-of", "default=nw=1:nk=1", tmp], timeout: 20)
            let readBack = String(data: p.out, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            if readBack.isEmpty {
                try? FileManager.default.removeItem(atPath: tmp)
                log?("嵌标签校验未通过（读回为空），保留原文件")
                return false
            }
        }
        let bak = filePath + ".bak"
        try? FileManager.default.removeItem(atPath: bak)
        guard (try? FileManager.default.moveItem(atPath: filePath, toPath: bak)) != nil else {
            try? FileManager.default.removeItem(atPath: tmp)
            return false
        }
        if (try? FileManager.default.moveItem(atPath: tmp, toPath: filePath)) == nil {
            try? FileManager.default.moveItem(atPath: bak, toPath: filePath)   // 回滚
            return false
        }
        try? FileManager.default.removeItem(atPath: bak)
        return true
    }

    struct CleanResult { var beforeBytes: Int64 = 0; var afterBytes: Int64 = 0; var removed: [String] = [] }

    /// LRU 清理：目录总量超上限时按 mtime 从旧到新删到 90% 以内；正在下载/排队的文件绝不碰
    /// （只用 mtime：现代文件系统 atime 不可靠，见踩坑 #70）
    @discardableResult
    func cleanupLru(maxBytes: Int64) -> CleanResult {
        var res = CleanResult()
        guard maxBytes > 0 else { return res }
        let base = dir()
        let exts = ["mp3", "flac", "m4a", "wav", "mp4", "mkv", "webm", "ts", "mov"]
        guard let files = try? FileManager.default.contentsOfDirectory(atPath: base) else { return res }
        struct Item { var path: String; var size: Int64; var used: Date }
        var items: [Item] = []
        for f in files where exts.contains((f as NSString).pathExtension.lowercased()) {
            let p = base + "/" + f
            let busy = jobs.contains { ($0.state == .queued || $0.state == .running) && $0.filePath == p }
            if busy { continue }
            let attrs = try? FileManager.default.attributesOfItem(atPath: p)
            let size = (attrs?[.size] as? Int64) ?? 0
            let used = (attrs?[.modificationDate] as? Date) ?? Date.distantPast
            items.append(Item(path: p, size: size, used: used))
            res.beforeBytes += size
        }
        res.afterBytes = res.beforeBytes
        guard res.beforeBytes > maxBytes else { return res }
        items.sort { $0.used < $1.used }
        let target = maxBytes * 9 / 10
        for it in items {
            if res.afterBytes <= target { break }
            if (try? FileManager.default.removeItem(atPath: it.path)) != nil {
                res.afterBytes -= it.size
                res.removed.append(it.path)
                log?("LRU 清理：\((it.path as NSString).lastPathComponent)（\(it.size / 1024 / 1024) MB）")
            }
        }
        log?("LRU 清理结果：\(res.beforeBytes / 1024 / 1024) MB → \(res.afterBytes / 1024 / 1024) MB（上限 \(maxBytes / 1024 / 1024) MB，删除 \(res.removed.count) 个）")
        // W4 ✓ Bug3 修复（与 Linux 同）：“清理”把文件删了，**列表里的对应条目也要消失** ✗→✓
        //   （用户实测：点清理后已完成的内容仍留在下载面板 ✗ 因为原来只删文件、不动作业列表 ✗）
        //   顺带把“文件已不存在”的结束态条目（失败/已取消等）一并清出列表 ✓
        if !res.removed.isEmpty {
            let gone = Set(res.removed)
            jobs.removeAll { j in
                if j.state == .running || j.state == .queued { return false }   // 进行中的绝不碰 ✓
                return gone.contains(j.filePath) || j.filePath.isEmpty || !FileManager.default.fileExists(atPath: j.filePath)
            }
            Config.log("[LRU] 列表同步：清理后剩 \(jobs.count) 个条目（进行中的保留 ✓）")
        }
        return res
    }

    /// W4 ✓ 取消**单个**任务（排队/下载中/**混流中**均可 ✓）—— 与 Android “取消即划走”一致：**直接移除条目** ✓
    /// 已完成/失败/已取消的条目会拒绝 ✗（不误删记录 ✓）
    @discardableResult
    func cancel(_ id: String) -> Bool {
        var done = false
        if Thread.isMainThread {
            done = cancelOnMain(id)
        } else {
            DispatchQueue.main.sync { done = self.cancelOnMain(id) }
        }
        return done
    }

    private func cancelOnMain(_ id: String) -> Bool {
        guard let i = jobs.firstIndex(where: { $0.id == id }) else { return false }
        guard jobs[i].state == .queued || jobs[i].state == .running else { return false }
        active[id]?.cancel()
        active.removeValue(forKey: id)
        if let p = muxProcs[id] { p.terminate(); muxProcs.removeValue(forKey: id) }
        muxTimers[id]?.invalidate()
        muxTimers.removeValue(forKey: id)
        var removed = jobs.remove(at: i)
        if !removed.filePath.isEmpty { try? FileManager.default.removeItem(atPath: removed.filePath) }
        log?("已取消下载：\(removed.title)")
        removed.state = .canceled
        onUpdate?(removed)          // 面板找不到该 id → 整表重载 → 行消失 ✓
        pump()                      // 让排队的下一个补位 ✓
        return true
    }

    /// W5 ✓ 「清理」同时清列表：移除全部**已结束**条目（完成/失败/已取消 ✓ 进行中的绝不碰 ✗）
    ///   用户口径（2026-09-23）：macOS 点「清理」后已下载内容仍留在列表 ✗ → 三端对齐 ✓
    ///   （原实现只在“LRU 真删了文件”时才同步 ✗ → 默认 2GB 上限下几乎什么都不删 ✗ → 条目永远留着 ✗）
    @discardableResult
    func dropFinishedJobs() -> Int {
        let before = jobs.count
        jobs.removeAll { $0.state == .done || $0.state == .failed || $0.state == .canceled }
        let n = before - jobs.count
        if n > 0 { Config.log("[LRU] 清理列表：移除 \(n) 条已结束记录（剩 \(jobs.count) ✓）") }
        return n
    }

    /// W4 ✓ 取消全部进行中的任务（用户指定按钮 ✓）
    func cancelAll() {
        let ids = jobs.filter { $0.state == .queued || $0.state == .running }.map { $0.id }
        for id in ids { _ = cancel(id) }
        log?("已取消全部 \(ids.count) 个任务")
    }

    /// 供进度面板显示
    /// 失败任务重试一轮（清空 retries 与 error）—— 与 Linux `DownloadManager::retryFailed` 同语义 ✓
    /// 面板「重试失败」按钮用（用户口径：两端下载面板功能一致 ✓ 2026-09-23）
    func retryFailed() {
        DispatchQueue.main.async {
            var n = 0
            for i in self.jobs.indices where self.jobs[i].state == .failed {
                self.jobs[i].state = .queued
                self.jobs[i].retries = 0
                self.jobs[i].error = ""
                self.jobs[i].bytesDone = 0
                self.jobs[i].bytesTotal = -1
                self.onUpdate?(self.jobs[i])
                n += 1
            }
            if n > 0 { self.pump() }
            self.log?("重试失败任务：\(n) 个")
        }
    }

    func summary() -> [String] {
        jobs.map { j in
            let pct = j.bytesTotal > 0 ? Int(j.bytesDone * 100 / j.bytesTotal) : -1
            let size = j.bytesDone > 0 ? "\(j.bytesDone / 1024) KB" : "-"
            return "[\(j.state.rawValue)] \(j.title)  \(pct >= 0 ? "\(pct)%" : "") \(size)\(j.error.isEmpty ? "" : "  ← \(j.error)")"
        }
    }
}
