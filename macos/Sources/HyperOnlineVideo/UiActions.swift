import AppKit
import AVFoundation

/// 结果行的模型（列表里既显示搜索结果也显示日志/状态，与 Qt 端的做法一致）
struct Row {
    var text = ""
    var key = ""            // 空 = 纯日志行，不可播放
    var isLog = false
    var thumb = ""          // 缩略图地址（B4：列表左侧显示）
    var square = false      // 方图（专辑封面）——决定缩略图框宽度按 1:1 还是 16:9
}

/// 单元格里的图片/文字**不拦截鼠标**：否则双击缩略图时表格收不到事件（B4 放大缩略图后暴露的 bug）
final class PassThroughImageView: NSImageView {
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}
final class PassThroughTextField: NSTextField {
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}

    /// 单击：集合视图的 didSelectItemsAt 已负责播放；这里保留一个可被菜单/脚本调用的入口
extension AppDelegate {
    // MARK: 列表交互（卡片流：集合视图）

    @objc func onRowClick() {
        let i = selectedResultRow
        guard i >= 0, i < rows.count else {
            Config.log("[CLICK] 单击但定位不到卡片（selected=\(selectedResultRow)）")
            return
        }
        let r = rows[i]
        guard !r.key.isEmpty else { setStatus("（日志行）\(r.text)"); return }
        Config.log("[CLICK] 单击卡片 \(i + 1) → 播放：\(r.text.prefix(48))")
        playRow(i)
    }

    /// 行 → 播放（单击/双击共用）
    func playRow(_ i: Int) {
        guard i >= 0, i < rows.count else { return }
        let r = rows[i]
        guard !r.key.isEmpty else { return }
        if let qi = queueIndex(forRow: i) { queue.jumpTo(qi); _ = queue.save() }
        playResult(key: r.key, label: r.text)
    }

    @objc func onRowDoubleClick() {
        let i = selectedResultRow
        guard i >= 0, i < rows.count, !rows[i].key.isEmpty else {
            Config.log("[CLICK] 双击但定位不到可播放卡片（selected=\(selectedResultRow)）")
            return
        }
        Config.log("[CLICK] 双击卡片 \(i + 1) → 播放：\(rows[i].text.prefix(48))")
        playRow(i)
    }


    // MARK: 列表即队列（与 Qt/Android 一致）

    /// 把结果列表同步成播放队列；playIndex 给定时同时把当前项指过去。
    /// 修的是真问题：此前搜索结果根本不进队列，导致播放中按下一首永远提示队列为空。
    func syncQueueFromRows(playIndex: Int? = nil) {
        let items = rows.filter { !$0.key.isEmpty }
        if items.isEmpty {
            queue.setList([])
            _ = queue.save()
            return
        }
        var start = 0
        if let pi = playIndex, pi >= 0, pi < rows.count, !rows[pi].key.isEmpty {
            start = rows[0...pi].filter { !$0.key.isEmpty }.count - 1
        }
        queue.setList(items.map { PlayQueue.Entry(key: $0.key, label: $0.text) }, startAt: max(0, start))
        if playIndex == nil { queue.clearCurrent() }
        _ = queue.save()
    }

    /// 列表行号 → 队列序号（列表里混着日志行，所以不能直接相等）
    func queueIndex(forRow row: Int) -> Int? {
        guard row >= 0, row < rows.count, !rows[row].key.isEmpty else { return nil }
        return rows[0...row].filter { !$0.key.isEmpty }.count - 1
    }

    // MARK: 按钮动作

    @objc func onSourceChanged() {
        let title = sourcePopup.titleOfSelectedItem ?? ""
        setStatus("来源：\(title)")
        if title == "本地库" { showLocalLibrary(); return }
        // 切到非本地来源：必须把"列表 + 分页状态"一起切过去，否则会看到上一个来源的结果，
        // 而分页状态还停留在旧来源（滚动就串源）。用户要求的行为：
        // 每个来源记住自己的搜索词 → 切回去就重新显示该来源的结果 + 可继续瀑布流。
        if searchField.stringValue.trimmingCharacters(in: .whitespaces).isEmpty,
           let last = lastSearchBySource[title] {
            searchField.stringValue = last               // 想起这个来源上次搜的词
        }
        let kw = searchField.stringValue.trimmingCharacters(in: .whitespaces)
        if kw.isEmpty {
            rows.removeAll(); logLines.removeAll(); reloadResults()
            searchQuery = ""; searchSourceLabel = title; searchPage = 1
            searchHasMore = false; searchLoadingMore = false
            searchRequestId += 1                          // 作废在途请求
            appendLog("已切到 \(title)：请输入关键词搜索")
            setStatus("来源：\(title)（请输入关键词）")
            return
        }
        onSearch()                                        // 重新搜该来源 → 结果 + 瀑布流分页
    }

    /// 播放模式按钮：循环切换 顺序 → 单曲循环 → 随机播放 → 列表循环
    @objc func onCycleMode() {
        queue.cycleMode()
        _ = queue.save()
        refreshModeButton()
        setStatus("播放模式：\(queue.modeLabel)")
        appendLog("播放模式切换为：\(queue.modeLabel)")
    }

    /// 同步模式按钮标题（面板/菜单改了模式后也要跟着变）
    func refreshModeButton() {
        modeButton?.title = ""
        // 四态图标：顺序 arrow.forward / 单曲 repeat.1 / 随机 shuffle / 列表循环 repeat
        let sym = queue.mode == .repeatOne ? "repeat.1"
                : (queue.mode == .shuffle ? "shuffle" : (queue.mode == .repeatAll ? "repeat" : "arrow.forward"))
        modeButton?.image = NSImage(systemSymbolName: sym, accessibilityDescription: queue.modeLabel)
        modeButton?.toolTip = "播放模式：\(queue.modeLabel)（点击切换）"
        Config.log("[UI] 播放模式按钮 → \(queue.modeLabel)")
    }

    /// 「更多」菜单：低频功能收进来，避免工具栏拥挤（原来一行 8 个按钮）
    @objc func onMoreMenu(_ sender: NSButton) {
        let m = NSMenu()
        let entries: [(String, String, Selector)] = [
            ("下载面板", "list.bullet.rectangle", #selector(onDownloads)),
            ("打开本地文件", "folder", #selector(onOpenFile)),
            ("收藏", "star", #selector(onFavorites)),
            ("播放队列", "text.badge.plus", #selector(onQueue)),
            ("设置", "gearshape", #selector(onSettings)),
        ]
        for (title, symbol, sel) in entries {
            let i = NSMenuItem(title: title, action: sel, keyEquivalent: "")
            i.target = self
            if let img = NSImage(systemSymbolName: symbol, accessibilityDescription: title) { i.image = img }
            m.addItem(i)
        }
        m.popUp(positioning: nil, at: NSPoint(x: 0, y: sender.bounds.height + 4), in: sender)
    }

    @objc func onSearch() {
        let kw = searchField.stringValue.trimmingCharacters(in: .whitespaces)
        let src = sourcePopup.titleOfSelectedItem ?? "YouTube"
        if src == "本地库" { showLocalLibrary(filter: kw); return }
        guard !kw.isEmpty else { setStatus("请输入关键词"); return }
        lastSearchBySource[src] = kw                      // 记住"来源 → 关键词"，切回来时自动重现
        // 新搜索 = 新列表（与 Android 一致：**替换**而不是追加）。
        // 之前是追加 ⇒ 第二次搜索的结果被塞在旧结果下面，用户以为"没有结果"（实测反馈）。
        rows.removeAll()
        logLines.removeAll()
        reloadResults()
        appendLog("搜索（\(src)）：\(kw)")
        setStatus("搜索中…")

        // 分页状态（瀑布流：滚到底部自动加载下一页，与 Android 的 loadMore 对齐）
        searchQuery = kw
        searchSourceLabel = src
        searchPage = 1
        searchHasMore = true
        searchLoadingMore = false
        // 搜索代次：切来源/进本地库都会 +1。异步结果回来时若代次变了 → 直接丢弃，
        // 否则"启动时恢复的上次搜索"这类慢请求会在用户已经切到本地库之后才落地，
        // 把 20 条 YouTube 结果 append 到本地库列表里（用户实测 bug02 的真凶）。
        searchRequestId += 1
        let reqId = searchRequestId

        DispatchQueue.global().async { [weak self] in
            guard let self else { return }
            let found = self.fetchPage(src, kw, 1)
            DispatchQueue.main.async {
                guard reqId == self.searchRequestId else {
                    Config.log("[搜索] 丢弃过期结果（req=\(reqId) 当前=\(self.searchRequestId)）：\(src) / \(kw)")
                    return
                }
                self.rows.append(contentsOf: found)
                if found.isEmpty { self.appendLog("没有结果（检查网络或换关键词）") }
                else { self.appendLog("共 \(found.count) 条结果（点卡片播放，滚到底自动加载更多）") }
                self.reloadResults()
                self.scrollToResult(0)                 // 新搜索从第一条看起（分页追加时不滚动）
                self.loadThumbs(rows: found)          // B4：异步拉缩略图
                self.syncQueueFromRows()               // B6：搜索结果即播放队列
                self.setStatus("搜索完成：\(found.count) 条（已替换上次结果；滚到底加载更多）")
            }
        }
    }

    // MARK: 分页（瀑布流无限下拉）
    /// 按来源取第 page 页（每页 20 条；各源用"多取后截尾"的方式翻页，简单可靠）
    private func fetchPage(_ src: String, _ kw: String, _ page: Int) -> [Row] {
        // 上限保护：音乐接口的"多取后截尾"依赖 limit，而接口通常上限 100（≈5 页）；
        // YouTube 的 ytsearchN 也设个上限，避免翻页很深时请求过大。
        let maxPage = (src == "YouTube") ? 6 : 5
        if page > maxPage {
            Config.log("[MORE] \(src) 已到分页上限（\(maxPage) 页）")
            return []
        }
        switch src {
        case "YouTube":   return searchYouTube(kw, page: page)
        case "B站":       return searchBilibili(kw, page: page)
        case "网易云音乐": return searchNetease(kw, page: page)
        default:          return searchQQ(kw, page: page)
        }
    }

    private func searchNetease(_ kw: String, page: Int) -> [Row] {
        let api = NetEaseApi()
        let songs = api.search(kw, limit: min(100, 20 * page))
        let slice = songs.count > 20 ? Array(songs.suffix(20)) : songs
        // 一次性把这一页的封面补上（搜索接口只给 picId，不补的话列表里是空白卡片）
        let covers = api.coverUrls(ids: slice.map { String($0.id) })
        var out: [Row] = []
        for s in slice {
            let url = covers[String(s.id)] ?? ""
            var r = Row(text: "\(s.name) — \(s.artist)", key: "netease:\(s.id)", thumb: url)
            r.square = true                            // 专辑封面是方图
            out.append(r)
            musicCovers["netease:\(s.id)"] = url
            musicLabels["netease:\(s.id)"] = r.text
        }
        Config.log("[封面] 网易云：\(covers.count)/\(slice.count) 首取到封面")
        return out
    }

    private func searchQQ(_ kw: String, page: Int) -> [Row] {
        let songs = QQMusicApi().search(kw, limit: min(100, 20 * page))
        let slice = songs.count > 20 ? Array(songs.suffix(20)) : songs
        var out: [Row] = []
        for s in slice {
            var r = Row(text: "\(s.name) — \(s.artist)", key: "qq:\(s.mid)",
                        thumb: QQMusicApi.coverUrl(albumMid: s.albumMid))
            r.square = true                            // 专辑封面是方图
            out.append(r)
            musicCovers["qq:\(s.mid)"] = QQMusicApi.coverUrl(albumMid: s.albumMid)
            musicLabels["qq:\(s.mid)"] = r.text
        }
        return out
    }

    /// 滚到接近底部时拉下一页（由滚动通知触发；同一时间只允许一个请求）
    func loadMoreIfNeeded() {
        // 守卫：连续加载必须"结果列表 = 该来源的搜索"。本地库不参与分页；
        // 换过来源/换过搜索时，stale 的 searchQuery/searchSourceLabel 不能再用
        //（用户实测：在本地库往下滚，居然把之前的 YouTube 搜索结果一页页追加进来了）。
        if sourcePopup.titleOfSelectedItem == "本地库" { return }
        guard !searchLoadingMore, searchHasMore, !searchQuery.isEmpty else { return }
        guard searchSourceLabel == (sourcePopup.titleOfSelectedItem ?? "") else { return }
        let clip = resultsScroll.contentView
        let contentH = resultsGrid.frame.height
        guard clip.bounds.maxY >= contentH - 240 else { return }      // 距底 240pt 内才开始加载
        searchLoadingMore = true
        let next = searchPage + 1
        let src = searchSourceLabel, kw = searchQuery
        let reqId = searchRequestId
        Config.log("[MORE] 加载第 \(next) 页：\(src) / \(kw)")
        setStatus("加载更多（第 \(next) 页）…")
        DispatchQueue.global().async { [weak self] in
            guard let self else { return }
            let more = self.fetchPage(src, kw, next)
            DispatchQueue.main.async {
                guard reqId == self.searchRequestId else {          // 期间换过来源/搜索 → 丢弃这页
                    self.searchLoadingMore = false
                    Config.log("[MORE] 丢弃过期分页（req=\(reqId) 当前=\(self.searchRequestId)）")
                    return
                }
                let existing = Set(self.rows.filter { !$0.key.isEmpty }.map { $0.key })
                let fresh = more.filter { !existing.contains($0.key) }
                self.searchLoadingMore = false
                guard !fresh.isEmpty else {
                    self.searchHasMore = false
                    self.appendLog("没有更多了（第 \(next) 页无新结果）")
                    self.setStatus("已到末页（共 \(self.rows.filter { !$0.key.isEmpty }.count) 条）")
                    return
                }
                self.searchPage = next
                self.rows.append(contentsOf: fresh)
                self.reloadResults()
                self.loadThumbs(rows: fresh)
                self.syncQueueFromRows()
                self.appendLog("已加载第 \(next) 页：+\(fresh.count) 条")
                self.setStatus("第 \(next) 页：+\(fresh.count) 条（共 \(self.rows.filter { !$0.key.isEmpty }.count) 条）")
            }
        }
    }

    private func searchYouTube(_ kw: String, page: Int = 1) -> [Row] {
        guard let ytdlp = UrlResolver.findExecutable("yt-dlp") else {
            Config.log("找不到 yt-dlp")
            return []
        }
        // yt-dlp 的 ytsearchN 不支持起始位置 → 多取后截取最后 20 条当"第 page 页"
        let enc = kw.addingPercentEncoding(withAllowedCharacters: .alphanumerics) ?? kw
        var target = "https://www.youtube.com/results?search_query=\(enc)"
        if let sp = Self.buildSp(sort: searchSort, duration: searchDuration) { target += "&sp=\(sp)" }
        let r = UrlResolver.runProcess(ytdlp, ["--flat-playlist", "-J", "--no-warnings",
                                               "--playlist-end", "\(20 * page)", target])
        guard r.code == 0, let root = (try? JSONSerialization.jsonObject(with: r.out)) as? [String: Any],
              let all = root["entries"] as? [Any] else { return [] }
        let entries: [Any] = all.count > 20 ? Array(all.suffix(20)) : all
        var out: [Row] = []
        for v in entries {
            guard let o = v as? [String: Any], let id = o["id"] as? String else { continue }
            let title = (o["title"] as? String) ?? id
            let up = (o["uploader"] as? String) ?? ""
            let dur = (o["duration"] as? NSNumber)?.doubleValue ?? 0
            var text = title
            if !up.isEmpty { text += "  · \(up)" }
            if dur > 0 { text += "  · \(Self.clock(dur))" }
            // YouTube 缩略图可由 id 直接推出（不必依赖搜索结果里的字段）
            out.append(Row(text: text, key: "https://www.youtube.com/watch?v=\(id)",
                           thumb: "https://i.ytimg.com/vi/\(id)/mqdefault.jpg"))
        }
        return out
    }

    /// B站搜索：用官方 search/type 接口（与 Android 端同一路径，实测可用；yt-dlp 的 bilisearch 会被 412 拦）
    private func searchBilibili(_ kw: String, page: Int = 1) -> [Row] {
        let enc = kw.addingPercentEncoding(withAllowedCharacters: .alphanumerics) ?? kw
        let order = searchSort == 2 ? "pubdate" : (searchSort == 3 ? "click" : "totalrank")
        var url = "https://api.bilibili.com/x/web-interface/search/type?search_type=video&keyword=\(enc)"
                + "&page=\(page)&page_size=20&order=\(order)"
        if searchDuration >= 1 { url += "&duration=\(searchDuration == 3 ? 4 : searchDuration)" }
        var h = ["User-Agent": Http.ua, "Referer": "https://www.bilibili.com/"]
        if let ck = cookieHeaderText(site: "bilibili"), !ck.isEmpty { h["Cookie"] = ck }
        let r = Http.get(url, headers: h)
        guard r.ok else { Config.log("B站搜索请求失败 HTTP \(r.status)"); return [] }
        let root = r.json()
        guard (root["code"] as? NSNumber)?.intValue == 0 else {
            Config.log("B站搜索返回 code=\((root["code"] as? NSNumber)?.intValue ?? -1)")
            return []
        }
        let list = ((root["data"] as? [String: Any])?["result"] as? [Any]) ?? []
        var out: [Row] = []
        for v in list {
            guard let o = v as? [String: Any], let bvid = o["bvid"] as? String else { continue }
            // 标题里带 <em class="keyword"> 高亮标签 → 去掉
            var title = (o["title"] as? String) ?? bvid
            title = title.replacingOccurrences(of: "<[^>]+>", with: "", options: .regularExpression)
            let up = (o["author"] as? String) ?? ""
            var text = title
            if !up.isEmpty { text += "  · \(up)" }
            if let d = o["duration"] as? String, !d.isEmpty { text += "  · \(d)" }
            var pic = (o["pic"] as? String) ?? ""            // B站返回 //i0.hdslb.com/… 形式
            if pic.hasPrefix("//") { pic = "https:" + pic }
            out.append(Row(text: text, key: "https://www.bilibili.com/video/\(bvid)", thumb: pic))
        }
        return out
    }

    /// 从站点 cookie 文件拼 Cookie 头
    private func cookieHeaderText(site: String) -> String? {
        guard let text = try? String(contentsOfFile: Config.cookiePath(site), encoding: .utf8) else { return nil }
        var pairs: [String] = []
        for line in text.components(separatedBy: "\n") {
            let t = line.trimmingCharacters(in: .whitespaces)
            if t.isEmpty || t.hasPrefix("#") { continue }
            let f = t.components(separatedBy: "\t")
            if f.count >= 7 { pairs.append("\(f[5])=\(f[6])") }
        }
        return pairs.isEmpty ? nil : pairs.joined(separator: "; ")
    }

    /// B4：缩略图异步加载（内存缓存 + 用 key 做行守卫，避免错图）
    func loadThumbs(rows: [Row]) {
        for r in rows where !r.thumb.isEmpty {
            if !r.thumb.hasPrefix("http") { continue }          // 本地文件走 loadLocalThumbs（不是 HTTP）
            if thumbCache[r.thumb] != nil { continue }
            thumbCache[r.thumb] = NSImage()                    // 占位，避免并发重复请求
            let url = r.thumb
            let key = r.key
            DispatchQueue.global().async { [weak self] in
                let ref = url.contains("hdslb") ? "https://www.bilibili.com/"
                    : ((url.contains("gtimg") || url.contains("qq.com")) ? "https://y.qq.com/"
                       : (url.contains("163.com") || url.contains("126.net") ? "https://music.163.com/" : ""))
                let rep = Http.get(url, headers: ["User-Agent": Http.ua, "Referer": ref])
                guard rep.ok, let img = NSImage(data: rep.data) else {
                    DispatchQueue.main.async { self?.thumbCache.removeValue(forKey: url) }
                    return
                }
                DispatchQueue.main.async {
                    guard let self else { return }
                    self.thumbCache[url] = img
                    self.thumbLoaded += 1
                    if self.thumbLoaded == 1 || self.thumbLoaded % 10 == 0 {
                        Config.log("缩略图已加载 \(self.thumbLoaded) 张")
                    }
                    // 只刷新对应行（行可能在后续搜索里重排，用 key 定位）
                    if let idx = self.rows.firstIndex(where: { $0.key == key && $0.thumb == url }) {
                        self.resultsGrid.reloadItems(at: Set([IndexPath(item: idx, section: 0)]))
                    }
                }
            }
        }
    }

    /// B3：下载"当前正在播放"的直链（与 Linux 端下载按钮一致）
    @objc func onDownload() {
        guard !lastStreamUrl.isEmpty else {
            setStatus("没有可下载的直链（先播放一首歌或一个视频）")
            return
        }
        // 扩展名：取 URL 路径部分（丢掉 ?vuutv=… 之类的签名串）——无损是 flac、QQ 的 128k 是 m4a/mp3
        let pathOnly = lastStreamUrl.components(separatedBy: "?").first ?? lastStreamUrl
        let urlExt = (pathOnly as NSString).pathExtension.lowercased()
        let ext = ["flac", "m4a", "mp3", "wav", "ape"].contains(urlExt) ? urlExt
                : (lastStreamUrl.contains(".flac") ? "flac" : (lastStreamUrl.contains(".m4a") ? "m4a" : "mp3"))
        var name = playLabel.isEmpty ? "hov-download" : playLabel
        if playPlatform == "在线" || playPlatform == "YouTube" || playPlatform == "B站" { name += ".mp4" } else { name += ".\(ext)" }
        let artist = playLabel.contains(" — ") ? String(playLabel.split(separator: " — ").last ?? "") : ""
        // 关键：把**音轨直链**一起交给下载器（YouTube 视频是分离音轨，只下视频流 = 本地播放没声音）；
        // 音乐还要把**封面**（嵌入标签）与**歌词**（同名 .lrc 侧车文件）一起落盘，本地库播放才有封面和字幕。
        let isMusic = playKey.hasPrefix("netease:") || playKey.hasPrefix("qq:")
        _ = dl.enqueue(url: lastStreamUrl, title: playLabel, fileNameHint: name,
                       artist: artist, album: "",
                       coverUrl: isMusic ? (musicCovers[playKey] ?? "") : "",
                       audioUrl: lastAudioUrl,
                       lyrics: isMusic ? lastLRC : "")
        setStatus("已加入下载队列：\(playLabel)")
    }

    /// B3：下载面板（任务列表 + 清理）
    @objc func onDownloads() {
        let alert = NSAlert()
        alert.messageText = "下载（\(dl.jobs.count) 个任务）"
        let lines = dl.summary()
        alert.informativeText = lines.isEmpty ? "（还没有下载任务；播放内容后点「下载」）" : lines.joined(separator: "\n")
        alert.addButton(withTitle: "LRU 清理")
        alert.addButton(withTitle: "打开下载目录")
        alert.addButton(withTitle: "关闭")
        let r = alert.runModal()
        if r == .alertFirstButtonReturn {
            let mb = Int64(settings.number("download.maxSizeMb", 2048))
            let res = dl.cleanupLru(maxBytes: mb * 1024 * 1024)
            setStatus("清理完成：\(res.beforeBytes/1048576)MB → \(res.afterBytes/1048576)MB，删除 \(res.removed.count) 个")
        } else if r == .alertSecondButtonReturn {
            NSWorkspace.shared.open(URL(fileURLWithPath: dl.dir()))
        }
    }

    @objc func onOpenFile() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.allowedContentTypes = []
        if panel.runModal() == .OK {
            openFiles(panel.urls.map { $0.path })
        }
    }

    /// 打开一组本地文件（列表即队列 → 播完自动下一条）——面板与 `--open a b c` 共用
    func openFiles(_ files: [String]) {
        guard !files.isEmpty else { return }
        rows.append(contentsOf: files.map { Row(text: ($0 as NSString).lastPathComponent, key: $0) })
        queue.setList(files.map { PlayQueue.Entry(key: $0, label: ($0 as NSString).lastPathComponent) }, startAt: 0)
        _ = queue.save()
        Config.log("[列表] 打开 \(files.count) 个本地文件（列表即队列）")
        reloadResults()
        loadThumbs(rows: rows)
        if let f = files.first { playResult(key: f, label: (f as NSString).lastPathComponent) }
    }

    // ── 直链过期自愈（与 Android/Linux 三端同法）────────────────────────────
    // 现象：暂停很久后继续播 → 播一会儿停住 → 手动切清晰度才恢复。
    // 根因：直链有有效期（googlevideo / B站 数小时失效），切档会重新解析 → 与观察吻合。
    // 做法：记录解析时刻 + 按来源 TTL → 过期时**复用 switchVideoQuality(to:)**（它自带重新解析 + 回原位）。
    private func ttlSeconds() -> TimeInterval {
        switch UrlResolver.serviceOf(playKey) {
        case "youtube":  return 4 * 60 * 60
        case "bilibili": return 2 * 60 * 60
        case "netease", "qqmusic": return 30 * 60
        default: return .greatestFiniteMagnitude      // 本地/未知：不过期
        }
    }

    private func healIfExpired() {
        guard !hovHealTried, let t = hovHealLastResolvedAt else { return }
        let age = Date().timeIntervalSince(t)
        let ttl = ttlSeconds()
        guard age > ttl else { return }
        hovHealTried = true
        Config.log("[HEAL] 直链已 \(Int(age))s（> TTL \(Int(ttl))s）→ 重新解析并回原位")
        setStatus("直链已过期，正在重新解析…")
        switchVideoQuality(to: UrlResolver.maxHeight)   // 内含重新解析 + 回到原进度
    }

    @objc func onPlayPause() {
        if player.paused() { healIfExpired() }          // 长时间暂停后按播放：直链可能已过期
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
        lastSeekAt = Date()
        let target = seekSlider.doubleValue
        // 寻址诊断：拖完把"目标/实际起点/时长/解码帧数"记进日志，便于复现"画面冻住、声音错位"时取证
        Config.log(String(format: "[SEEK] 目标=%.1fs 当前=%.1fs 时长=%.1fs 帧数=%d 暂停=%@",
                          target, player.position(), player.duration(), player.renderedFrames,
                          player.paused() ? "是" : "否"))
        player.seek(to: target)
        setStatus("跳转到 \(Self.clock(target))")
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in
            guard let self else { return }
            Config.log(String(format: "[SEEK] 1.5 秒后：位置=%.1fs 帧数=%d 暂停=%@（若位置没跟上或帧数不涨＝mpv 那边卡住了）",
                              self.player.position(), self.player.renderedFrames,
                              self.player.paused() ? "是" : "否"))
        }
    }

    // MARK: 视频清晰度档位（**唯一一份**，控制条下拉 / 设置面板 / V 键循环都引用它）
    // 之前三处各写一份、且都只列到 1080p —— 用户问"为什么只有 1080p，没有更好的画质"就是这个原因
    //（后端其实允许到 4320，只有 UI 没给选项）。
    static let videoQualityHeights = [0, 2160, 1440, 1080, 720, 480, 360, -1]
    static let videoQualityTitles  = ["自动", "2160p 4K", "1440p 2K", "1080p", "720p", "480p", "360p", "仅音频"]

    /// 档位下拉索引 ↔ 高度（-1=仅音频，0=自动）
    func qualityIndex(forHeight h: Double) -> Int {
        if let i = Self.videoQualityHeights.firstIndex(of: Int(h)) { return i }
        switch Int(h) {
        case -1: return Self.videoQualityHeights.count - 1
        case 0: return 0
        case 360: return 6
        case 480: return 5
        case 720: return 4
        case 1080: return 1
        default: return 0
        }
    }
    /// 下拉索引 → 高度（越界给 0=自动）
    func height(forQualityIndex i: Int) -> Int {
        Self.videoQualityHeights.indices.contains(i) ? Self.videoQualityHeights[i] : 0
    }

    // MARK: 质量下拉：视频=清晰度 / 音乐=音质（用户实测：放音乐时还显示 1080p 之类，很违和）
    static let audioLevels = ["standard", "exhigh", "lossless"]
    /// 音质反馈文案：请求与实际不一致时说清原因（用户以为"切换失败"，其实是被服务端降级）
    static func qualityNote(requested: String, actual: String) -> String {
        guard !actual.isEmpty else { return "" }
        if actual == requested { return "音质 \(shortAudioLabel(actual))" }
        return "请求 \(shortAudioLabel(requested))，实际 \(shortAudioLabel(actual))（该曲/该账号无 \(shortAudioLabel(requested))，通常需要会员）"
    }

    static func shortAudioLabel(_ l: String) -> String {
        switch l {
        case "standard": return "标准 128k"
        case "lossless": return "无损 FLAC"
        default: return "较高 320k"
        }
    }
    /// 当前放的是不是"音乐内容"（内容键是 netease:<id> / qq:<mid>）
    var isMusicContent: Bool { playKey.hasPrefix("netease:") || playKey.hasPrefix("qq:") }

    /// 把控制条上的质量下拉切成当前内容该有的形态（音乐→音质三档；其它→视频清晰度六档）
    func refreshQualityPopup() {
        guard let popup = qualityPopup else { return }
        let music = isMusicContent
        // 一律用**共享表**：这里原来还留着一份旧的 6 项字面量，
        // 每次刷新都会把下拉重建成旧列表 → 设置里的 1080 被算成下标 3、落在旧表的 480p 上（用户实测的"对应错了"）
        let titles = music ? Self.audioLevels.map { Self.shortAudioLabel($0) }
                           : Self.videoQualityTitles
        // 项数或标题不一致就重建：只比标题时，若下拉里还是**旧的 6 项列表**，长度不同也不会重建，
        // 于是"按新表算出的下标"落到旧表上 → 设置 1080p 时显示成 480p（用户实测的"对应错了"）。
        if popup.itemTitles != titles || popup.numberOfItems != titles.count {
            popup.removeAllItems()
            popup.addItems(withTitles: titles)
            Config.log("[UI] 质量下拉重建：\(titles.count) 项（原 \(popup.numberOfItems) 项）")
        }
        if music {
            popup.selectItem(at: Self.audioLevels.firstIndex(of: effectiveAudioLevel()) ?? 1)
        } else {
            popup.selectItem(at: qualityIndex(forHeight: Double(UrlResolver.maxHeight)))
        }
        qualityWidth?.constant = music ? 112 : 104   // 再宽一点："2160p 4K" 不被截成 "2160…"          // "较高 320k" / "2160p 4K" 都比 "1080p" 宽（CGFloat）
        popup.toolTip = music ? "音质（Q 键循环切换）" : "清晰度（V 键循环切换）"
        Config.log("[UI] 质量下拉 → \(music ? "音质" : "清晰度")：\(popup.titleOfSelectedItem ?? "-")"
                  + "（共 \(popup.numberOfItems) 项，选中下标 \(popup.indexOfSelectedItem)，设置值 \(music ? effectiveAudioLevel() : "\(UrlResolver.maxHeight)")）")
    }

    @objc func onQuality() {
        let i = qualityPopup.indexOfSelectedItem
        if isMusicContent {                                 // 音乐：下拉里是音质三档
            switchAudioQuality(to: Self.audioLevels[max(0, min(Self.audioLevels.count - 1, i))])
        } else {
            switchVideoQuality(to: height(forQualityIndex: i))
        }
    }

    /// 切清晰度：记住进度 → 用新档位重新解析 → 回到原进度（与"切音质"同一思路）
    func switchVideoQuality(to h: Int) {
        _ = settings.apply(key: "video.maxHeight", value: String(h))
        UrlResolver.maxHeight = h
        refreshQualityPopup()
        setStatus("清晰度：\(UrlResolver.qualityLabel(h))")
        let service = UrlResolver.serviceOf(playKey)      // 内容键是网页地址才算"在线视频"
        guard !service.isEmpty, playKey.hasPrefix("http") else {
            appendLog("清晰度已设为 \(UrlResolver.qualityLabel(h))（当前不是在线视频，下次播放生效）")
            return
        }
        guard let p = player, p.duration() >= 1 else { return }   // 播放器还没建 / 还没在放：只改档位
        let pos = p.position()
        appendLog("按 \(UrlResolver.qualityLabel(h)) 重新解析，完成后回到 \(Self.clock(pos))")
        DispatchQueue.global().async { [weak self] in
            guard let self else { return }
            guard let stream = self.resolver.resolve(self.playKey) else {
                DispatchQueue.main.async { self.setStatus("切换清晰度失败（解析失败）") }
                return
            }
            DispatchQueue.main.async {
                self.hovHealLastResolvedAt = Date()        // 记录解析时刻（供过期自愈判断）
                self.playKey(target: stream.videoUrl,
                             label: stream.title.isEmpty ? self.playLabel : stream.title,
                             audio: stream.audioUrl.isEmpty ? nil : stream.audioUrl,
                             contentKey: self.playKey)     // 内容键不变
                if pos > 0.5 { self.resumeAfterLoad(pos) }
            }
        }
    }

    /// v 键：自动 → 1080 → 720 → 480 → 360 → 仅音频 循环
    func cycleVideoQuality() {
        if isMusicContent { cycleAudioQuality(); return }   // 放音乐时 V 键 = 循环音质（与下拉保持一致）
        let order = [0, 1080, 720, 480, 360, -1]
        let cur = Int(settings.number("video.maxHeight", 0))
        let i = order.firstIndex(of: cur) ?? 0
        switchVideoQuality(to: order[(i + 1) % order.count])
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

    @objc func onPip() { togglePip() }

    /// B5 画中画：小窗 + 置顶 + 隐藏界面（**不重挂 GL 视图** —— 换窗口会让 mpv 渲染上下文失效，
    /// 所以采用"同一视图、改变窗口形态"的方案，播放不中断、不重载）
    func togglePip() {
        pipActive.toggle()
        if pipActive {
            prePipFrame = window.frame
            topBarView?.isHidden = true
            resultsScroll.isHidden = true
            controlsBarView?.isHidden = true
            statusLabel.isHidden = true
            window.level = .floating                                   // 置顶
            window.title = "聚合视频 · 画中画（双击或按 P 退出）"
            let h = 270.0
            let w = h * 16 / 9
            if let screen = window.screen?.visibleFrame {
                let x = screen.maxX - w - 24, y = screen.minY + 24
                window.setFrame(NSRect(x: x, y: y, width: w, height: h), display: true)
            }
            Config.log("[SELFTEST] 已进入画中画（\(Int(w))x\(Int(h)) 置顶小窗）")
        } else {
            topBarView?.isHidden = false
            resultsScroll.isHidden = false
            controlsBarView?.isHidden = false
            statusLabel.isHidden = false
            window.level = .normal
            window.title = "聚合视频 · Hyper Online Video（macOS）"
            if prePipFrame != .zero { window.setFrame(prePipFrame, display: true) }
            Config.log("[SELFTEST] 已退出画中画")
        }
    }

    @objc func onQueue() {
        openQueuePanel()
        return
    }

    @objc func onFavorites() {
        openFavoritesPanel()
        return
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
        openSettingsPanel()
        return
    }

    /// 登录：先选站点再开登录窗口。
    /// 注意：NSAlert 的返回码只能靠**顺序**判断 —— 前三个是 .alertFirst/Second/ThirdButtonReturn，
    /// 之后依次是 rawValue 1000、1001…… 一旦增删按钮就会错位（早前用 rawValue:1003 的写法就是这么坏的）。
    @objc func onLogin() {
        // QQ音乐：腾讯(QQ) 与 微信 的登录入口/账号资产都不互通 ⇒ 做成**两个独立入口**（用户建议，更直观）
        let sites = ["youtube", "bilibili", "netease", "qqmusic", "qqmusic_wx"]
        let titles = ["YouTube", "B站", "网易云音乐", "QQ音乐 · QQ 登录", "QQ音乐 · 微信登录"]
        let alert = NSAlert()
        alert.messageText = "App 内登录"
        var info = "选择站点打开登录窗口，登录完成后 cookie 会自动保存到\n\(Config.dir)/cookies/"
        info += "（与 Linux/Android 端共用同一份文件，yt-dlp 可直接使用）"
        alert.informativeText = info
        for t in titles { alert.addButton(withTitle: t) }
        alert.addButton(withTitle: "用系统浏览器登录并导入")
        alert.addButton(withTitle: "取消")
        // 按钮顺序：站点们 → 用系统浏览器登录并导入 → 取消
        // 注意：返回码就是 1000 + 按钮序号（不要再手工拼，之前撞号导致"点了没反应"）
        let codes: [NSApplication.ModalResponse] = (0..<(titles.count + 2)).map { Self.alertButtonCode($0) }
        let r = alert.runModal()
        handleLoginChoice(r, codes: codes, sites: sites, titles: titles)
    }

    /// 弹窗返回值 → 打开对应登录窗口（抽出来是为了让自动化能用同一个入口验证，见 --login-choice）
    func handleLoginChoice(_ r: NSApplication.ModalResponse, codes: [NSApplication.ModalResponse],
                           sites: [String], titles: [String]) {
        guard let i = codes.firstIndex(of: r) else {
            Config.log("[登录] 返回值 \(r.rawValue) 不在按钮表里（应不会发生）")
            setStatus("登录已取消")
            return
        }
        if i == sites.count {                        // A0：不依赖内置 WebView 的登录路径（站点之后第一个）
            // 原来这里在"浏览器未设置"时只写了一行状态栏文字 —— 用户看不到，表现为"点了没反应"。
            // 现在：没设置就弹选择框（列出本机已装的浏览器），并把结果明确反馈出来。
            let browser: String
            let saved = settings.string("network.cookiesFromBrowser").trimmingCharacters(in: .whitespaces)
            if !saved.isEmpty {
                browser = saved
            } else if let picked = promptForBrowser() {
                browser = picked
                _ = settings.apply(key: "network.cookiesFromBrowser", value: picked)
                appendLog("已记住浏览器来源：\(picked)（下次免选）")
            } else {
                setStatus("已取消导入")
                return
            }
            setStatus("正在从 \(browser) 导入 cookie…（首次可能弹出钥匙串 / 全盘访问授权）")
            appendLog("开始导入 cookie：浏览器=\(browser)")
            DispatchQueue.global().async { [weak self] in
                let (ok, summary) = CookieImport.importFromBrowser(browser, probeUrl: "https://www.bilibili.com/video/BV1GJ411x7h7")
                DispatchQueue.main.async {
                    guard let self else { return }
                    self.appendLog(ok ? "cookie 导入完成：\(summary)" : "cookie 导入失败：\(summary)")
                    self.setStatus(ok ? "cookie 导入完成：\(summary)" : "cookie 导入失败：\(summary)")
                    self.refreshLoginBadge()             // 导入成功立刻把「登录 ✓」点亮，不用重启
                      self.showAlert(ok ? "cookie 导入完成" : "cookie 导入失败", summary)
                    if !ok { self.showAlert("cookie 导入失败", summary) }   // 失败必须显式弹窗，不能只写日志
                }
            }
            return
        }
        guard i < sites.count else {
            setStatus("登录已取消")
            return
        }
        let site = sites[i]
        NSApp.activate(ignoringOtherApps: true)      // 先拉到前台：否则登录窗可能开在主窗后面，看起来像"没反应"
        // 关键：**模态弹窗结束后立刻开窗会被吞掉**（用户实测"点了没有任何窗口"）⇒ 延到下一轮 runloop 再开
        DispatchQueue.main.async { LoginWindow.open(site: site) }
        setStatus("已打开 \(titles[i]) 登录窗口（登录后 cookie 自动保存）")
    }

    /// 搜索过滤器面板（与 Android FilterSheet 对齐）：排序（相关度/最新/播放最多）+ 时长（全部/短视频/中等/长篇）。
    /// 应用后如果搜索框里有词，就立即按新条件重搜当前来源（用户预期"点了就生效"）。
    @objc func onFilters() {
        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 360, height: 330),
                         styleMask: [.titled, .closable], backing: .buffered, defer: false)
        w.title = "搜索过滤器"
        w.isReleasedWhenClosed = false
        filterWindow = w
        let root = NSStackView()
        root.orientation = .vertical
        root.spacing = 10
        root.alignment = .leading
        root.edgeInsets = NSEdgeInsets(top: 18, left: 22, bottom: 18, right: 22)

        func header(_ t: String) -> NSTextField {
            let l = NSTextField(labelWithString: t)
            l.font = .systemFont(ofSize: 13, weight: .semibold)
            return l
        }
        root.addArrangedSubview(header("排序（对 YouTube / B站生效）"))
        filterSortBtns = []
        for (i, t) in ["相关度", "最新", "播放最多"].enumerated() {
            // 关键：AppKit 的单选按钮**按 target/action 分组**（同 action = 同组）——
            // ① 同一组内必须用同一个 action，否则每个按钮自成一组、能同时选中（用户实测：三个排序全亮）
            // ② **不同组必须用不同 action**，否则两组被并成一组 → 选"排序"会把"时长"顶掉
            //    （用户实测反馈的 bug：排序选"最新"后时长全空）
            let b = NSButton(radioButtonWithTitle: t, target: self, action: #selector(onFilterSortRadio))
            b.state = (searchSort == i + 1) ? .on : .off
            filterSortBtns.append(b)
            root.addArrangedSubview(b)
        }
        root.addArrangedSubview(header("时长"))
        filterDurBtns = []
        for (i, t) in ["全部", "短视频（<10 分钟）", "中等（10~30 分钟）", "长篇（>30 分钟）"].enumerated() {
            // 时长组：与排序组**必须用不同的 action**（否则两组并成一组，互相顶掉 —— 本次修复的 bug）
            let b = NSButton(radioButtonWithTitle: t, target: self, action: #selector(onFilterDurRadio))
            b.state = (searchDuration == i) ? .on : .off
            filterDurBtns.append(b)
            root.addArrangedSubview(b)
        }
        let apply = NSButton(title: "应用过滤器", target: self, action: #selector(onFilterApply))
        apply.bezelStyle = .rounded
        apply.keyEquivalent = "\r"
        let cancel = NSButton(title: "取消", target: self, action: #selector(onFilterCancel))
        cancel.bezelStyle = .rounded
        let row = NSStackView(views: [apply, cancel])
        row.orientation = .horizontal
        row.spacing = 10
        root.addArrangedSubview(row)

        w.contentView = root
        if let main = window {           // 放在主窗口上方居中
            let f = main.frame
            w.setFrameOrigin(NSPoint(x: f.midX - 180, y: f.midY - 165))
        }
        NSApp.activate(ignoringOtherApps: true)
        w.makeKeyAndOrderFront(nil)
        w.orderFrontRegardless()
        Config.log("[过滤器] 打开面板（当前 排序=\(searchSort) 时长=\(searchDuration)）")
    }

    /// 排序组单选 action：AppKit 借此把**同一组**的按钮互斥（方法体无需处理任何逻辑）
    @objc func onFilterSortRadio() { }
    /// 时长组单选 action：**必须与排序组不同** —— 同 action 会被 AppKit 并成一组，互顶（本次修复的 bug）
    @objc func onFilterDurRadio() { }

    @objc func onFilterApply() {
        for (i, b) in filterSortBtns.enumerated() where b.state == .on { searchSort = i + 1 }
        for (i, b) in filterDurBtns.enumerated() where b.state == .on { searchDuration = i }
        filterWindow?.close()
        filterWindow = nil
        let sortName = ["", "相关度", "最新", "播放最多"][max(0, min(3, searchSort))]
        let durName = ["全部", "短视频", "中等", "长篇"][max(0, min(3, searchDuration))]
        setStatus("过滤器：排序 \(sortName) / 时长 \(durName)")
        appendLog("搜索过滤器已应用：排序=\(sortName) 时长=\(durName)")
        let kw = searchField.stringValue.trimmingCharacters(in: .whitespaces)
        if !kw.isEmpty, sourcePopup.titleOfSelectedItem != "本地库" { onSearch() }   // 立即按新条件重搜
    }

    @objc func onFilterCancel() {
        filterWindow?.close()
        filterWindow = nil
        setStatus("已取消过滤器")
    }

    /// 让用户挑一个浏览器：优先列出本机已安装的（App 名 → yt-dlp 认的标识）
    private func promptForBrowser() -> String? {
        let known: [(id: String, app: String)] = [
            ("safari", "Safari"), ("chrome", "Google Chrome"), ("chromium", "Chromium"),
            ("edge", "Microsoft Edge"), ("firefox", "Firefox"), ("brave", "Brave Browser"),
            ("vivaldi", "Vivaldi"), ("opera", "Opera"), ("whale", "Whale"),
        ]
        let installed = known.filter {
            FileManager.default.fileExists(atPath: "/Applications/\($0.app).app")
                || FileManager.default.fileExists(atPath: NSHomeDirectory() + "/Applications/\($0.app).app")
        }
        let list = installed.isEmpty ? known : installed     // 一个都没检测到也给全量列表（用户可能装在别处）
        let alert = NSAlert()
        alert.messageText = "选择要读取 cookie 的浏览器"
        alert.informativeText = "请在所选浏览器里登录站点后再导入。\n只会保留这几个站点的 cookie：YouTube / B站 / 网易云 / QQ音乐 → \(Config.dir)/cookies/"
        let popup = NSPopUpButton(frame: NSRect(x: 0, y: 0, width: 260, height: 25))
        popup.addItems(withTitles: list.map { $0.app })
        alert.accessoryView = popup
        alert.addButton(withTitle: "开始导入")
        alert.addButton(withTitle: "取消")
        guard alert.runModal() == .alertFirstButtonReturn else { return nil }
        let idx = max(0, min(popup.indexOfSelectedItem, list.count - 1))
        return list[idx].id
    }

    /// 统一的提示弹窗（失败时用，避免"只写日志、用户看不到"）
    func showAlert(_ title: String, _ text: String) {
        let a = NSAlert()
        a.messageText = title
        a.informativeText = text
        a.addButton(withTitle: "好")
        a.runModal()
    }

    /// 登录状态：看 cookie 文件是否存在（界面按钮据此显示"已登录"）
    func loginBadge() -> String {
        var have: [String] = []
        for (site, name) in [("youtube", "YouTube"), ("bilibili", "B站"), ("netease", "网易云"), ("qqmusic", "QQ")] {
            if FileManager.default.fileExists(atPath: Config.cookiePath(site)) { have.append(name) }
        }
        return have.isEmpty ? "" : "已登录:" + have.joined(separator: "/")
    }

    // MARK: 播放分发

    func playResult(key: String, label: String) {
        if key.hasPrefix("netease:") || key.hasPrefix("qq:") { playMusic(key: key, label: label); return }
        if UrlResolver.isDirectMedia(key) { playKey(target: key, label: label); return }
        resolveAndPlay(key, label)
    }

    /// 让"搜索源"下拉与正在播放的内容一致（用户反馈：源显示 YouTube 却在放 B 站 —— 下拉是搜索源，
    /// 容易被误读成"当前平台"，所以既跟随内容、又在状态栏单独标注平台）
    func followSource(_ service: String, isLocal: Bool = false) {
        let title: String
        switch service {
        case "youtube": title = "YouTube"
        case "bilibili": title = "B站"
        case "netease": title = "网易云音乐"
        case "qqmusic": title = "QQ音乐"
        default: title = isLocal ? "本地库" : ""
        }
        playPlatform = title
        guard !title.isEmpty, sourcePopup.titleOfSelectedItem != title,
              let idx = sourcePopup.itemTitles.firstIndex(of: title) else { return }
        sourcePopup.selectItem(at: idx)
        Config.log("搜索源已跟随内容切到：\(title)")
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

    /// 会话音质档位（空=跟随设置）；受设置里的上限裁剪
    func effectiveAudioLevel() -> String {
        settings.effectiveQuality(sessionAudioLevel.isEmpty ? "lossless" : sessionAudioLevel)
    }
    static func labelOfAudioLevelStatic(_ l: String) -> String { labelOfAudioLevel(l) }
    static func labelOfAudioLevel(_ l: String) -> String {
        switch l {
        case "standard": return "标准 128k"
        case "lossless": return "无损 FLAC"
        default: return "较高 320k"
        }
    }

    /// 切音质：记住进度 → 用新档位重新取流 → 回到原进度（与 Qt 端 Q 键同一条逻辑）
    func switchAudioQuality(to level: String) {
        let ok = ["standard", "exhigh", "lossless"]
        guard ok.contains(level) else {
            setStatus("未知音质档位：\(level)（可用 \(ok.joined(separator: " / "))）")
            return
        }
        // 用户在控制条上**明确**选了高档位 → 把"音质上限"一起抬上去。
        // 否则会被设置里的上限静默裁剪（实测：用户选"无损"却一直停在 320k，看起来就是"切换无损失败"）。
        let ceiling = settings.string("music.qualityCeiling", "exhigh")
        let order = Self.audioLevels
        if (order.firstIndex(of: level) ?? 0) > (order.firstIndex(of: ceiling) ?? 1) {
            _ = settings.apply(key: "music.qualityCeiling", value: level)
            appendLog("音质上限已提升到 \(Self.shortAudioLabel(level))（你在控制条上明确选了它）")
        }
        sessionAudioLevel = level
        refreshQualityPopup()
        let eff = effectiveAudioLevel()
        setStatus("[音质] 目标 \(Self.labelOfAudioLevel(level))，有效档位 \(Self.labelOfAudioLevel(eff))"
                  + (eff == level ? "" : "（受设置上限裁剪）"))
        guard playKey.hasPrefix("netease:") || playKey.hasPrefix("qq:") else {
            appendLog("音质已设为 \(Self.labelOfAudioLevel(level))（当前不是音乐，下次播放生效）")
            return
        }
        guard let p = player, p.duration() >= 1 else { return }   // 播放器还没建 / 还没在放：只改档位
        let pos = p.position()
        appendLog("重新取流（\(Self.labelOfAudioLevel(eff))），完成后回到 \(Self.clock(pos))")
        let key = playKey, label = playLabel
        playMusic(key: key, label: label)                   // 重新走一遍取流
        resumeAfterLoad(pos)                                // 新流就绪后回到原进度
    }

    /// Q 键：标准 → 较高 → 无损 循环（与 Qt 端一致）
    func cycleAudioQuality() {
        let order = ["standard", "exhigh", "lossless"]
        let i = order.firstIndex(of: effectiveAudioLevel()) ?? 1
        switchAudioQuality(to: order[(i + 1) % order.count])
    }

    private func playMusic(key: String, label: String) {
        followSource(key.hasPrefix("netease:") ? "netease" : "qqmusic")
        setStatus("取流中：\(label)")
        DispatchQueue.global().async { [weak self] in
            guard let self else { return }
            let level = self.effectiveAudioLevel()
            var url = "", err = "", cover = self.musicCovers[key] ?? ""
            var lrc = "", trans = "", yrc = ""
            var actualLevel = level                       // 服务端**实际**给的档位（可能低于请求值）
            if key.hasPrefix("netease:") {
                let id = String(key.dropFirst(8))
                let api = NetEaseApi()
                let info = api.songUrl(id, level: level)
                url = info.url; err = info.error
                if !info.level.isEmpty { actualLevel = info.level }
                let ly = api.lyric(id); lrc = ly.lrc; trans = ly.trans; yrc = ly.yrc
                if cover.isEmpty { cover = api.coverUrl(id) }
            } else {
                let mid = String(key.dropFirst(3))
                let api = QQMusicApi()
                var res = api.streamUrl(mid, tierName: level)
                if res.url.isEmpty {
                    res = api.streamUrl(mid, tierName: "standard")
                    if !res.url.isEmpty { actualLevel = "standard" }
                }
                url = res.url; err = res.error
                let ly = api.lyric(mid); lrc = ly.lrc; trans = ly.trans
            }
            let levelNote = Self.qualityNote(requested: level, actual: actualLevel)
            DispatchQueue.main.async {
                if url.isEmpty {
                    self.setStatus("取流失败：\(err)")
                    self.tryPlayNext(after: key)
                    return
                }
                self.playKey(target: url, label: label, contentKey: key)   // 内容键=netease:<id>/qq:<mid>（不是直链）
                if !levelNote.isEmpty { self.appendLog("音质：\(levelNote)") }    // 请求无损却只给 320k 时要说清楚
                self.applyLyrics(lrc: lrc, trans: trans, label: label, yrc: yrc)   // 歌词必须在其后设置
                self.applyCover(cover, label: label, forKey: key)
            }
        }
    }

    private func resolveAndPlay(_ pageUrl: String, _ label: String) {
        hovHealLastResolvedAt = Date(); hovHealTried = false      // 新内容：记录解析时刻并允许一次自愈
        followSource(UrlResolver.serviceOf(pageUrl))
        setStatus("解析中…（\(UrlResolver.serviceOf(pageUrl).isEmpty ? "直链" : UrlResolver.serviceOf(pageUrl))）")
        // 请求序号：用户快速连点两条时，**先发起但后返回**的解析不能把后点的顶掉（用户实测反馈「点了不播」）
        playRequestId += 1
        let reqId = playRequestId
        // 解析可能要十几秒（B站长合集/大会员清晰度），界面给出秒数，避免看起来像"卡死"
        resolveTick?.invalidate()
        let t0 = Date()
        resolveTick = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] t in
            guard let self else { t.invalidate(); return }
            guard reqId == self.playRequestId else { t.invalidate(); return }
            self.setStatus(String(format: "解析中…（%@，已 %.0f 秒）", UrlResolver.serviceOf(pageUrl), Date().timeIntervalSince(t0)))
        }
        DispatchQueue.global().async { [weak self] in
            guard let self else { return }
            guard let stream = self.resolver.resolve(pageUrl) else {
                DispatchQueue.main.async { if reqId == self.playRequestId { self.setStatus("解析失败（检查网络/ cookie）") } }
                return
            }
            guard reqId == self.playRequestId else {
                Config.log("[解析] 丢弃过期结果（req=\(reqId) 当前=\(self.playRequestId)）：\(label.prefix(32))")
                return
            }
            DispatchQueue.main.async { self.resolveTick?.invalidate(); self.resolveTick = nil }
            var subs: [SubtitleTrack] = []
            let svc = UrlResolver.serviceOf(pageUrl)
            if svc == "bilibili" { subs = self.resolver.fetchBiliCc(pageUrl) }
            else if svc == "youtube" { subs = self.resolver.fetchYouTubeSubs(pageUrl) }
            DispatchQueue.main.async {
                guard reqId == self.playRequestId else {          // 解析期间用户又点了别的 → 丢弃
                    Config.log("[解析] 丢弃过期字幕/播放（req=\(reqId) 当前=\(self.playRequestId)）")
                    return
                }
                // ⚠️ 顺序很重要：playKey() 内部会清空 tracks/cues（为新内容做准备），
                // 所以**必须先开播、再挂字幕** —— 反过来会让刚取回的字幕被清掉
                //（用户实测：开关显示"开"却一直没有字幕；日志实证 [SUB] 挂载 285 行 → 随后 轨数=0）。
                self.playKey(target: stream.videoUrl, label: stream.title.isEmpty ? label : stream.title,
                             audio: stream.audioUrl.isEmpty ? nil : stream.audioUrl,
                             contentKey: pageUrl)          // 内容键=页面 URL（直链会过期，不能当键）
                self.player.tracks = subs
                if !subs.isEmpty {
                    self.trackIndex = 0
                    self.applyTrack(0)                    // 内部会：取消隐藏 + 同步图标 + 打 [SUB] 诊断
                    self.updateSubtitleButton()
                    self.appendLog("在线字幕：\(subs.count) 条轨道（C 键切换）")
                    Config.log("[SUB] 在线字幕已挂载：\(subs.count) 轨，当前 \(self.player.overlay.cues.count) 行")
                } else {
                    // 取不到就要"说出来"：以前静默 → 用户看到开关是"开"却没字幕，无从判断
                    self.setStatus("该视频没有可用在线字幕")
                    Config.log("[SUB] 在线字幕：0 轨（该视频无字幕或取回失败）")
                }
            }
        }
    }

    private func applyLyrics(lrc: String, trans: String, label: String, yrc: String = "") {
        // 优先 YRC（网易云登录后返回，带**字级时间戳**）：真·逐字高亮；没有就退回 LRC + 行内进度近似
        var cues = yrc.isEmpty ? Subtitles.parseLRC(lrc) : Subtitles.parseYRC(yrc)
        if cues.isEmpty { cues = Subtitles.parseLRC(lrc) }
        let t = Subtitles.parseLRC(trans)
        if !t.isEmpty { cues = Subtitles.mergeBilingual(cues, t) }
        player.overlay.cues = cues
        player.overlay.sourceName = "歌词：\(label)"
        player.musicTitle = label          // 音乐界面右栏顶部的大字标题
        // 记住歌词原文：下载音乐时写成同名 .lrc（本地库播放即可显示歌词）
        if !lrc.isEmpty {
            var text = lrc.trimmingCharacters(in: .whitespacesAndNewlines)
            for line in trans.components(separatedBy: "\n") where !line.trimmingCharacters(in: .whitespaces).isEmpty {
                text += "\n" + line                       // 译文行自带时间戳，本地播放器会按时间轴合并显示
            }
            lastLRC = text
        }
        refreshKaraokeFlag()      // 播放新内容后按当前轨重算（此前会无条件置 true → SRT 被当歌词渲染）
        player.updateOverlayTimer()
        if !cues.isEmpty {
            let wordCount = cues.reduce(0) { $0 + $1.words.count }
            appendLog("已加载歌词 \(cues.count) 行" + (wordCount > 0 ? "（逐字 \(wordCount) 个时间戳）" : ""))
            if CommandLine.arguments.contains("--diag") {
                for c in cues.prefix(4) {
                    Config.log(String(format: "[diag] cue %.2f~%.2f %@", c.start, c.end, c.text))
                }
            }
        }
    }

    /// 取封面并显示。
    /// **必须带换歌守卫**：异步下载期间用户可能已经换歌，先发出的请求后返回就会把封面盖成上一首的
    /// （Qt 端 CoverArt 有这行守卫，Swift 移植时漏了 —— 用户实测看到"封面不是这首歌的"）。
    private func applyCover(_ url: String, label: String, forKey key: String) {
        guard !url.isEmpty else { return }
        if CommandLine.arguments.contains("--diag") {
            Config.log("[diag] 封面请求: key=\(key) url=\(url.prefix(60))")
        }
        DispatchQueue.global().async { [weak self] in
            let r = Http.get(url, headers: ["User-Agent": Http.ua])
            guard r.ok, let img = NSImage(data: r.data) else { return }
            DispatchQueue.main.async {
                guard let self else { return }
                guard key == self.playKey else {          // 已换歌 → 丢弃这份封面
                    Config.log("封面丢弃（已换歌）: 请求key=\(key) 当前key=\(self.playKey)")
                    return
                }
                self.player.setCoverImage(img)
                self.appendLog("封面已应用: \(Int(img.size.width))x\(Int(img.size.height)) key=\(key)")
            }
        }
    }

    /// 本地库缩略图：视频抽第 1 秒的帧、音频读内嵌封面，都没有就用"音符/影片"占位图标。
    /// 全部在后台线程生成（AVFoundation 抽帧是 IO+解码），完成后回主线程填缓存并只刷新对应行。
    func loadLocalThumbs(rows: [Row]) {
        for r in rows where !r.thumb.isEmpty && !r.thumb.hasPrefix("http") {
            if thumbCache[r.thumb] != nil { continue }
            thumbCache[r.thumb] = NSImage()                 // 占位，避免重复生成
            let path = r.thumb, key = r.key
            DispatchQueue.global(qos: .utility).async { [weak self] in
                let img = Self.localThumbImage(path: path)
                DispatchQueue.main.async {
                    guard let self, let img else {
                        self?.thumbCache.removeValue(forKey: path)
                        return
                    }
                    self.thumbCache[path] = img
                    self.thumbLoaded += 1
                    if let idx = self.rows.firstIndex(where: { $0.key == key && $0.thumb == path }) {
                        self.resultsGrid.reloadItems(at: Set([IndexPath(item: idx, section: 0)]))
                    }
                }
            }
        }
    }

    /// YouTube 搜索 sp 参数（protobuf 手工编码 + URL-Safe Base64 去填充）——与 Android buildSp 逐字节一致
    static func buildSp(sort: Int, duration: Int) -> String? {
        var buf = Data()
        if sort == 2 { buf.append(contentsOf: [0x08, 0x02]) }
        if sort == 3 { buf.append(contentsOf: [0x08, 0x03]) }
        if (1...3).contains(duration) { buf.append(contentsOf: [0x12, 0x02, 0x18, UInt8(duration)]) }
        guard !buf.isEmpty else { return nil }
        return buf.base64EncodedString().replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "")
    }

    /// 生成一张本地媒体缩略图（失败返回 nil 会回退到占位图标）
    static func localThumbImage(path: String) -> NSImage? {
        let ext = (path as NSString).pathExtension.lowercased()
        let videoExts = ["mp4", "mkv", "webm", "mov", "avi", "ts", "m4v", "flv"]
        let url = URL(fileURLWithPath: path)
        guard FileManager.default.fileExists(atPath: path) else { return nil }
        let asset = AVURLAsset(url: url)
        if videoExts.contains(ext) {
            let gen = AVAssetImageGenerator(asset: asset)
            gen.appliesPreferredTrackTransform = true
            gen.maximumSize = CGSize(width: 640, height: 640)
            gen.requestedTimeToleranceBefore = CMTime(seconds: 1, preferredTimescale: 600)
            gen.requestedTimeToleranceAfter = CMTime(seconds: 1, preferredTimescale: 600)
            if let cg = try? gen.copyCGImage(at: CMTime(seconds: 1, preferredTimescale: 600), actualTime: nil) {
                let img = NSImage(size: NSSize(width: cg.width, height: cg.height))
                img.addRepresentation(NSBitmapImageRep(cgImage: cg))
                return img
            }
            return Self.placeholderThumb(video: true)
        }
        // 音频：内嵌封面
        if let meta = asset.commonMetadata.first(where: { $0.commonKey == .commonKeyArtwork }),
           let data = meta.value as? Data, let img = NSImage(data: data) {
            return img
        }
        return Self.placeholderThumb(video: false)
    }

    /// 占位图标（音符 / 影片），用 SF Symbol 画一张 256×256 的图
    static func placeholderThumb(video: Bool) -> NSImage? {
        let side: CGFloat = 256
        let img = NSImage(size: NSSize(width: side, height: side))
        img.lockFocus()
        NSColor(calibratedWhite: 0.20, alpha: 1).setFill()
        NSBezierPath(rect: NSRect(x: 0, y: 0, width: side, height: side)).fill()
        let cfg = NSImage.SymbolConfiguration(pointSize: side * 0.42, weight: .regular)
        if let sym = NSImage(systemSymbolName: video ? "film" : "music.note", accessibilityDescription: nil)?
            .withSymbolConfiguration(cfg) {
            let s = sym.size
            let tinted = NSImage(size: s)
            tinted.lockFocus()
            NSColor(calibratedWhite: 0.75, alpha: 1).set()
            sym.draw(at: .zero, from: .zero, operation: .sourceOver, fraction: 1)
            NSRect(x: 0, y: 0, width: s.width, height: s.height).fill(using: .sourceAtop)
            tinted.unlockFocus()
            tinted.draw(in: NSRect(x: (side - s.width) / 2, y: (side - s.height) / 2,
                                   width: s.width, height: s.height))
        }
        img.unlockFocus()
        return img
    }

    // MARK: 本地库

    /// 切到"本地库"：**列出目录下所有音视频**（递归、跳过隐藏/临时文件）；带关键词时只列匹配项。
    /// 用户实测反馈：①原来只显示顶层目录 → 子目录里的内容看不到；②`rows.append` 只追加不替换 →
    /// 每次切换来源/再搜索都会把旧结果和新结果叠在一起（搜索那次已修过，本地库这里漏了）。
    func showLocalLibrary(filter: String = "") {
        let dir = settings.string("download.dir").isEmpty ? Config.downloadDir : settings.string("download.dir")
        let fm = FileManager.default
        var isDir: ObjCBool = false
        guard fm.fileExists(atPath: dir, isDirectory: &isDir), isDir.boolValue else {
            setStatus("本地库目录不存在：\(dir)")
            appendLog("本地库目录不存在：\(dir)")
            return
        }
        // 与"新搜索"一致：**替换**而不是追加
        rows.removeAll()
        logLines.removeAll()
        reloadResults()
        // 关键：进入本地库要把分页状态清干净 —— 否则滚到底会拿旧搜索的关键词继续拉页
        //（用户实测 bug：本地库列表里混进一堆 YouTube 搜索结果）
        searchQuery = ""
        searchSourceLabel = "本地库"
        searchPage = 1
        searchHasMore = false
        searchLoadingMore = false
        searchRequestId += 1          // 让"在途的搜索/翻页"全部作废（它们的异步结果不能再落到本地库列表上）
        let scanId = searchRequestId
        setStatus("本地库扫描中…（\(dir)）")
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self else { return }
            let found = Self.scanMedia(root: dir, dir: dir, filter: filter)
            DispatchQueue.main.async {
                guard scanId == self.searchRequestId else {          // 扫描期间又换了来源 → 丢弃
                    Config.log("[本地库] 丢弃过期扫描结果（req=\(scanId) 当前=\(self.searchRequestId)）")
                    return
                }
                self.rows = found
                self.syncQueueFromRows()           // B6：本地库同样入队
                self.loadLocalThumbs(rows: found)  // 本地缩略图（视频抽帧/音频内嵌封面/占位）
                self.reloadResults()
                self.scrollToResult(0)
                let tail = filter.isEmpty ? "" : "（筛选：\(filter)）"
                self.appendLog("本地库 \(dir)：\(found.count) 个文件\(tail)")
                self.setStatus("本地库：\(found.count) 个文件\(tail)")
            }
        }
    }

    /// 递归扫音视频文件（后台线程执行）。上限保护：最多 2000 个文件、目录深度 4 层，
    /// 跳过隐藏项与下载中间产物（.tag/.bak/.part），避免大目录把内存/时间吃光。
    static func scanMedia(root: String, dir: String, filter: String, depth: Int = 0, cap: Int = 2000) -> [Row] {
        let fm = FileManager.default
        let media = ["mp3", "flac", "m4a", "aac", "wav", "ape", "ogg",
                     "mp4", "mkv", "webm", "mov", "avi", "ts", "m4v", "flv"]
        var out: [Row] = []
        guard depth <= 4, let items = try? fm.contentsOfDirectory(atPath: dir) else { return out }
        for f in items.sorted() {
            if out.count >= cap { break }
            if f.hasPrefix(".") { continue }                       // 隐藏项 / 中间产物
            let lower = f.lowercased()
            if lower.hasSuffix(".tag") || lower.hasSuffix(".bak") || lower.hasSuffix(".part") { continue }
            let path = dir + "/" + f
            var isD: ObjCBool = false
            guard fm.fileExists(atPath: path, isDirectory: &isD) else { continue }
            if isD.boolValue {
                out.append(contentsOf: scanMedia(root: root, dir: path, filter: filter,
                                                 depth: depth + 1, cap: cap - out.count))
                continue
            }
            let ext = (f as NSString).pathExtension.lowercased()
            guard media.contains(ext) else { continue }
            if !filter.isEmpty, !lower.contains(filter.lowercased()) { continue }
            let size = (try? fm.attributesOfItem(atPath: path)[.size] as? Int64) ?? 0
            // 子目录里的文件带上**相对 root** 的路径，避免同名文件看起来一模一样
            let shown = path.replacingOccurrences(of: root + "/", with: "")
            // thumb 用文件路径当键：本地缩略图由 loadLocalThumbs 现场生成（视频抽帧 / 音频内嵌封面 / 占位图标）
            let squareThumb = !["mp4", "mkv", "webm", "mov", "avi", "ts", "m4v", "flv"].contains(ext)
            out.append(Row(text: "\(shown)   \(LocalLibraryFmt.size(size))", key: path,
                           thumb: path, square: squareThumb))
        }
        return out
    }
}

enum LocalLibraryFmt {
    static func size(_ b: Int64) -> String {
        if b >= 1_048_576 { return String(format: "%.1f MB", Double(b) / 1_048_576) }
        if b >= 1024 { return String(format: "%.0f KB", Double(b) / 1024) }
        return "\(b) B"
    }
}
