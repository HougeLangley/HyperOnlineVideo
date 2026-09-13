import AppKit

/// 结果行的模型（列表里既显示搜索结果也显示日志/状态，与 Qt 端的做法一致）
struct Row {
    var text = ""
    var key = ""            // 空 = 纯日志行，不可播放
    var isLog = false
}

extension AppDelegate: NSTableViewDataSource, NSTableViewDelegate {
    // MARK: 表格

    func numberOfRows(in tableView: NSTableView) -> Int { rows.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let id = NSUserInterfaceItemIdentifier("cell")
        let label: NSTextField
        if let reused = tableView.makeView(withIdentifier: id, owner: self) as? NSTextField {
            label = reused
        } else {
            label = NSTextField(labelWithString: "")
            label.identifier = id
            label.lineBreakMode = .byTruncatingTail
            label.font = .systemFont(ofSize: 12.5)
        }
        let r = rows[row]
        label.stringValue = r.text
        label.textColor = r.isLog || r.key.isEmpty ? .secondaryLabelColor : .labelColor
        return label
    }

    @objc func onRowDoubleClick() {
        let i = resultsTable.clickedRow
        guard i >= 0, i < rows.count, !rows[i].key.isEmpty else { return }
        let r = rows[i]
        playResult(key: r.key, label: r.text)
    }

    // MARK: 按钮动作

    @objc func onSourceChanged() {
        let title = sourcePopup.titleOfSelectedItem ?? ""
        setStatus("来源：\(title)")
        if title == "本地库" { showLocalLibrary() }
    }

    @objc func onSearch() {
        let kw = searchField.stringValue.trimmingCharacters(in: .whitespaces)
        let src = sourcePopup.titleOfSelectedItem ?? "YouTube"
        if src == "本地库" { showLocalLibrary(filter: kw); return }
        guard !kw.isEmpty else { setStatus("请输入关键词"); return }
        appendLog("搜索（\(src)）：\(kw)")
        setStatus("搜索中…")

        DispatchQueue.global().async { [weak self] in
            guard let self else { return }
            var found: [Row] = []
            if src == "YouTube" {
                found = self.searchYouTube(kw)
            } else if src == "网易云音乐" {
                let songs = NetEaseApi().search(kw, limit: 20)
                for s in songs {
                    var r = Row(text: "\(s.name) — \(s.artist)", key: "netease:\(s.id)")
                    r.isLog = false
                    found.append(r)
                    self.musicCovers["netease:\(s.id)"] = ""      // 封面播放时再取（搜索接口不带）
                    self.musicLabels["netease:\(s.id)"] = r.text
                }
            } else {
                let songs = QQMusicApi().search(kw, limit: 20)
                for s in songs {
                    let r = Row(text: "\(s.name) — \(s.artist)", key: "qq:\(s.mid)")
                    found.append(r)
                    self.musicCovers["qq:\(s.mid)"] = QQMusicApi.coverUrl(albumMid: s.albumMid)
                    self.musicLabels["qq:\(s.mid)"] = r.text
                }
            }
            DispatchQueue.main.async {
                self.rows.append(contentsOf: found)
                if found.isEmpty { self.appendLog("没有结果（检查网络或换关键词）") }
                else { self.appendLog("共 \(found.count) 条结果（双击播放）") }
                self.resultsTable.reloadData()
                self.setStatus("搜索完成：\(found.count) 条")
            }
        }
    }

    private func searchYouTube(_ kw: String) -> [Row] {
        guard let ytdlp = UrlResolver.findExecutable("yt-dlp") else {
            Config.log("找不到 yt-dlp")
            return []
        }
        let r = UrlResolver.runProcess(ytdlp, ["--flat-playlist", "-J", "--no-warnings", "ytsearch20:\(kw)"])
        guard r.code == 0, let root = (try? JSONSerialization.jsonObject(with: r.out)) as? [String: Any],
              let entries = root["entries"] as? [Any] else { return [] }
        var out: [Row] = []
        for v in entries {
            guard let o = v as? [String: Any], let id = o["id"] as? String else { continue }
            let title = (o["title"] as? String) ?? id
            let up = (o["uploader"] as? String) ?? ""
            let dur = (o["duration"] as? NSNumber)?.doubleValue ?? 0
            var text = title
            if !up.isEmpty { text += "  · \(up)" }
            if dur > 0 { text += "  · \(Self.clock(dur))" }
            out.append(Row(text: text, key: "https://www.youtube.com/watch?v=\(id)"))
        }
        return out
    }

    @objc func onOpenFile() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.allowedContentTypes = []
        if panel.runModal() == .OK {
            let files = panel.urls.map { $0.path }
            guard !files.isEmpty else { return }
            for (i, f) in files.enumerated() {
                let name = (f as NSString).lastPathComponent
                rows.append(Row(text: name, key: f))
                queue.setList(files.enumerated().map { Row(text: ($0.element as NSString).lastPathComponent, key: $0.element) }
                    .map { PlayQueue.Entry(key: $0.key, label: $0.text) }, startAt: 0)
                if i == 0 { playResult(key: f, label: name) }
            }
            resultsTable.reloadData()
        }
    }

    @objc func onPlayPause() {
        player.togglePause()
        setStatus(player.paused() ? "已暂停" : "播放中")
    }
    @objc func onPrev() { playQueueStep(forward: false) }
    @objc func onNext() { playQueueStep(forward: true) }

    private func playQueueStep(forward: Bool) {
        guard !queue.isEmpty else { setStatus("队列为空"); return }
        _ = forward ? queue.next() : queue.prev()
        if let e = queue.current() { playResult(key: e.key, label: e.label) }
    }

    @objc func onSeek() {
        player.seek(to: seekSlider.doubleValue)
        setStatus("跳转到 \(Self.clock(seekSlider.doubleValue))")
    }

    @objc func onSpeed() {
        let v = Double((speedPopup.titleOfSelectedItem ?? "1.0x").replacingOccurrences(of: "x", with: "")) ?? 1.0
        player.setSpeed(v)
        setStatus(String(format: "倍速 %.2fx", v))
    }

    @objc func onVolume() {
        player.setVolume(Int(volumeSlider.doubleValue))
        setStatus("音量 \(Int(volumeSlider.doubleValue))")
    }

    @objc func onFullscreen() { window.toggleFullScreen(nil) }

    @objc func onQueue() {
        let alert = NSAlert()
        alert.messageText = "播放队列（\(queue.size) 项）"
        var lines: [String] = ["模式：\(queue.modeLabel)", ""]
        for i in 0..<queue.size {
            if let e = queue.at(i) { lines.append("\(i + 1). \(e.label)") }
        }
        alert.informativeText = lines.joined(separator: "\n")
        alert.addButton(withTitle: "下一首")
        alert.addButton(withTitle: "切换模式")
        alert.addButton(withTitle: "关闭")
        let r = alert.runModal()
        if r == .alertFirstButtonReturn { onNext() }
        if r == .alertSecondButtonReturn { queue.cycleMode(); setStatus("队列模式：\(queue.modeLabel)") }
    }

    @objc func onFavorites() {
        let alert = NSAlert()
        alert.messageText = "收藏（\(favorites.count) 项）"
        let list = favorites.all
        alert.informativeText = list.isEmpty ? "（还没有收藏；播放时按 A 或点击收藏按钮）"
                                             : list.map { "• \($0.title)" }.joined(separator: "\n")
        alert.addButton(withTitle: "播放第一项")
        alert.addButton(withTitle: "清空收藏")
        alert.addButton(withTitle: "关闭")
        let r = alert.runModal()
        if r == .alertFirstButtonReturn, let f = list.first { playResult(key: f.key, label: f.title) }
        if r == .alertSecondButtonReturn {
            for f in list { _ = favorites.remove(f.id) }
            _ = favorites.save()
            setStatus("收藏已清空")
        }
    }

    func toggleFavorite() {
        guard !playKey.isEmpty else { setStatus("没有正在播放的项"); return }
        if favorites.contains(playKey) {
            _ = favorites.remove(playKey)
            _ = favorites.save()
            setStatus("已取消收藏（现 \(favorites.count) 项）")
        } else {
            var f = Favorites.Fav()
            f.id = playKey
            f.key = playKey
            f.title = playLabel
            f.platform = playKey.contains(":") ? String(playKey.split(separator: ":")[0]) : "local"
            f.addedAt = ProgressStore.nowMs()
            _ = favorites.add(f)
            _ = favorites.save()
            setStatus("已收藏：\(playLabel)（现 \(favorites.count) 项）")
        }
    }

    @objc func onSettings() {
        let alert = NSAlert()
        alert.messageText = "设置"
        alert.informativeText = settings.dump()
        alert.addButton(withTitle: "音质：标准")
        alert.addButton(withTitle: "音质：较高")
        alert.addButton(withTitle: "音质：无损")
        alert.addButton(withTitle: "关闭")
        let r = alert.runModal()
        let map: [NSApplication.ModalResponse: String] = [.alertFirstButtonReturn: "standard",
                                                          .alertSecondButtonReturn: "exhigh",
                                                          .alertThirdButtonReturn: "lossless"]
        if let q = map[r] {
            _ = settings.apply(key: "music.qualityCeiling", value: q)
            setStatus("音质上限：\(q)")
        }
    }

    @objc func onLogin() {
        setStatus("登录窗口将在 Phase 5.4 提供（当前可把 cookie 放到 \(Config.dir)/cookies/）")
    }

    // MARK: 播放分发

    func playResult(key: String, label: String) {
        if key.hasPrefix("netease:") || key.hasPrefix("qq:") { playMusic(key: key, label: label); return }
        if UrlResolver.isDirectMedia(key) { playKey(target: key, label: label); return }
        resolveAndPlay(key, label)
    }

    /// 取流失败自动换下一条（网易云/QQ 都有版权受限曲目，与另两端"逐条尝试"一致）
    func tryPlayNext(after key: String) {
        let prefix = key.contains(":") ? String(key.split(separator: ":")[0]) + ":" : ""
        guard let idx = rows.firstIndex(where: { $0.key == key }) else { return }
        let rest = rows[(idx + 1)...].prefix(6)
        for r in rest where r.key.hasPrefix(prefix) && !r.key.isEmpty {
            appendLog("换下一条试播：\(r.text)")
            playResult(key: r.key, label: r.text)
            return
        }
        setStatus("连续几条都取不到播放地址（可能需要登录）")
    }

    private func playMusic(key: String, label: String) {
        setStatus("取流中：\(label)")
        DispatchQueue.global().async { [weak self] in
            guard let self else { return }
            let level = self.settings.effectiveQuality("lossless")
            var url = "", err = "", cover = self.musicCovers[key] ?? ""
            var lrc = "", trans = ""
            if key.hasPrefix("netease:") {
                let id = String(key.dropFirst(8))
                let api = NetEaseApi()
                let info = api.songUrl(id, level: level)
                url = info.url; err = info.error
                let ly = api.lyric(id); lrc = ly.lrc; trans = ly.trans
                if cover.isEmpty { cover = api.coverUrl(id) }
            } else {
                let mid = String(key.dropFirst(3))
                let api = QQMusicApi()
                var res = api.streamUrl(mid, tierName: level)
                if res.url.isEmpty { res = api.streamUrl(mid, tierName: "standard") }
                url = res.url; err = res.error
                let ly = api.lyric(mid); lrc = ly.lrc; trans = ly.trans
            }
            DispatchQueue.main.async {
                if url.isEmpty {
                    self.setStatus("取流失败：\(err)")
                    self.tryPlayNext(after: key)
                    return
                }
                self.playKey(target: url, label: label)     // 顺序很关键：playKey 会清空 overlay，
                self.applyLyrics(lrc: lrc, trans: trans, label: label)   // 歌词必须在其后设置
                self.applyCover(cover, label: label)
            }
        }
    }

    private func resolveAndPlay(_ pageUrl: String, _ label: String) {
        setStatus("解析中…（\(UrlResolver.serviceOf(pageUrl).isEmpty ? "直链" : UrlResolver.serviceOf(pageUrl))）")
        DispatchQueue.global().async { [weak self] in
            guard let self else { return }
            guard let stream = self.resolver.resolve(pageUrl) else {
                DispatchQueue.main.async { self.setStatus("解析失败（检查网络/ cookie）") }
                return
            }
            var subs: [SubtitleTrack] = []
            let svc = UrlResolver.serviceOf(pageUrl)
            if svc == "bilibili" { subs = self.resolver.fetchBiliCc(pageUrl) }
            else if svc == "youtube" { subs = self.resolver.fetchYouTubeSubs(pageUrl) }
            DispatchQueue.main.async {
                self.player.tracks = subs
                if let first = subs.first {
                    self.trackIndex = 0
                    self.applyTrack(0)
                    self.appendLog("在线字幕：\(subs.count) 条轨道（C 键切换）")
                }
                self.playKey(target: stream.videoUrl, label: stream.title.isEmpty ? label : stream.title,
                             audio: stream.audioUrl.isEmpty ? nil : stream.audioUrl)
            }
        }
    }

    private func applyLyrics(lrc: String, trans: String, label: String) {
        var cues = Subtitles.parseLRC(lrc)
        let t = Subtitles.parseLRC(trans)
        if !t.isEmpty { cues = Subtitles.mergeBilingual(cues, t) }
        player.overlay.cues = cues
        player.overlay.sourceName = "歌词：\(label)"
        player.overlay.karaoke = settings.bool("subtitle.karaoke", true)
        player.updateOverlayTimer()
        if !cues.isEmpty {
            appendLog("已加载歌词 \(cues.count) 行")
            if CommandLine.arguments.contains("--diag") {
                for c in cues.prefix(4) {
                    Config.log(String(format: "[diag] cue %.2f~%.2f %@", c.start, c.end, c.text))
                }
            }
        }
    }

    private func applyCover(_ url: String, label: String) {
        guard !url.isEmpty else { return }
        DispatchQueue.global().async {
            let r = Http.get(url, headers: ["User-Agent": Http.ua])
            guard r.ok, let img = NSImage(data: r.data) else { return }
            DispatchQueue.main.async {
                self.player.setCoverImage(img)
                self.appendLog("封面已加载 \(Int(img.size.width))x\(Int(img.size.height))")
            }
        }
    }

    // MARK: 本地库

    func showLocalLibrary(filter: String = "") {
        let dir = settings.string("download.dir").isEmpty ? Config.downloadDir : settings.string("download.dir")
        let fm = FileManager.default
        guard let files = try? fm.contentsOfDirectory(atPath: dir) else {
            setStatus("本地库目录不存在：\(dir)")
            return
        }
        let media = ["mp3", "flac", "m4a", "wav", "mp4", "mkv", "webm", "mov"]
        var found: [Row] = []
        for f in files.sorted() where media.contains((f as NSString).pathExtension.lowercased()) {
            if !filter.isEmpty, !f.lowercased().contains(filter.lowercased()) { continue }
            let path = dir + "/" + f
            let size = (try? fm.attributesOfItem(atPath: path)[.size] as? Int64) ?? 0
            found.append(Row(text: "\(f)   \(LocalLibraryFmt.size(size))", key: path))
        }
        rows.append(contentsOf: found)
        appendLog("本地库 \(dir)：\(found.count) 个文件")
        resultsTable.reloadData()
        setStatus("本地库：\(found.count) 个文件")
    }
}

enum LocalLibraryFmt {
    static func size(_ b: Int64) -> String {
        if b >= 1_048_576 { return String(format: "%.1f MB", Double(b) / 1_048_576) }
        if b >= 1024 { return String(format: "%.0f KB", Double(b) / 1024) }
        return "\(b) B"
    }
}
