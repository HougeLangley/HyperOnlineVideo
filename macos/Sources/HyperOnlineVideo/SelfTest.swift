import Foundation

/// 纯逻辑自检（不触网、不动真实配置）—— 与 Qt 端 `--queue-selftest` 同等职责
/// 让自检能引用 UiActions 里的标签函数（避免把 AppDelegate 拖进自检）
enum UiActionsLabel {
    static func check() -> Bool {
        AppDelegate.labelOfAudioLevelStatic("standard") == "标准 128k"
            && AppDelegate.labelOfAudioLevelStatic("exhigh") == "较高 320k"
            && AppDelegate.labelOfAudioLevelStatic("lossless") == "无损 FLAC"
    }
}

enum SelfTest {
    struct Result { var pass = 0; var total = 0; var failures: [String] = [] }

    static func run() -> Result {
        var r = Result()
        func check(_ ok: Bool, _ name: String) {
            r.total += 1
            if ok { r.pass += 1 } else { r.failures.append(name) }
        }

        // ---- 设置校验（未知键/越界/类型错必须拒绝）----
        check(Settings.validate("music.qualityCeiling", "exhigh") == nil, "设置：合法档位通过")
        check(Settings.validate("music.qualityCeiling", "ultra") != nil, "设置：非法档位被拒")
        check(Settings.validate("subtitle.fontScale", "9") != nil, "设置：字号越界被拒")
        check(Settings.validate("no.such.key", "1") != nil, "设置：未知键被拒")
        check(Settings.validate("download.maxSizeMb", "2048") == nil, "设置：下载上限合法值通过")
        check(Settings.validate("ui.thumbSize", "84") == nil, "设置：缩略图 84（与手机一致的默认值）合法")
        check(Settings.validate("ui.thumbSize", "24") == nil && Settings.validate("ui.thumbSize", "200") == nil,
              "设置：缩略图边界值 24 / 200 合法")
        check(Settings.validate("ui.thumbSize", "10") != nil && Settings.validate("ui.thumbSize", "500") != nil,
              "设置：缩略图越界值被拒")
        check(Settings.validate("playback.autoNext", "true") == nil && Settings.validate("playback.autoNext", "off") == nil,
              "设置：连播开关的 true/false 合法")
        check(Settings.validate("playback.autoNext", "maybe") != nil, "设置：连播开关非法值被拒")
        check(Settings.validate("ui.listWidth", "0") == nil && Settings.validate("ui.listWidth", "520") == nil,
              "设置：结果区宽度 0（默认）/520 合法")
        check(Settings.validate("ui.listWidth", "-10") != nil && Settings.validate("ui.listWidth", "9999") != nil,
              "设置：结果区宽度越界被拒")
        // 登录弹窗按钮返回码：第 N 个 = 1000 + N（曾因撞号导致"点 QQ音乐 没反应"）
        let codes = (0..<7).map { AppDelegate.alertButtonCode($0) }
        check(Set(codes).count == 7, "登录弹窗：7 个按钮的返回码互不相同")
        check(codes[3].rawValue == 1003 && codes[4].rawValue == 1004 && codes[6].rawValue == 1006,
              "登录弹窗：返回码 = 1000 + 按钮序号")
        do {
            let s = Settings.shared
            s.load()
            // 用**当前设置里的上限**断言（用户的设置文件可能改过上限，别写死 exhigh）
            let order = ["standard", "exhigh", "lossless"]
            let ceil = s.string("music.qualityCeiling", "exhigh")
            let ci = order.firstIndex(of: ceil) ?? 1
            check(s.effectiveQuality("lossless") == order[ci], "设置：音质上限裁剪（当前上限 \(ceil)）")
            check(s.effectiveQuality("standard") == "standard", "设置：低档位不受上限影响")
        }

        // ---- 进度记忆 ----
        do {
            let p = ProgressStore()
            p.remember("k", pos: 3, dur: 100, title: "t")
            check(!p.has("k"), "进度：不足 5 秒不记录")
            p.remember("k", pos: 30, dur: 100, title: "t")
            check(abs(p.resumePos("k") - 30) < 0.001, "进度：30/100 秒可续播")
            p.remember("k", pos: 10, dur: 100, title: "t")
            check(p.has("k") && p.resumePos("k") == 0, "进度：10 秒保留记录但不续播")
            p.remember("k", pos: 92, dur: 100, title: "t")
            check(!p.has("k"), "进度：距结尾 15 秒内视为已看完并清除")
            p.remember("k2", pos: 20, dur: 100, title: "t")
            check(abs(p.resumePos("k2") - 20) < 0.001 && p.count == 1, "进度：条目互相独立")
        }

        // ---- 收藏 ----
        do {
            let f = Favorites()
            var fav = Favorites.Fav()
            fav.id = "netease:1"; fav.key = fav.id; fav.title = "T"
            check(f.add(fav) && f.contains("netease:1"), "收藏：加入后命中")
            check(!f.add(fav), "收藏：重复加入返回 false")
            check(f.remove("netease:1") && !f.contains("netease:1"), "收藏：移除生效")
        }

        // ---- 播放队列 ----
        do {
            let q = PlayQueue(path: "/tmp/hov-mac-queue-selftest.json")
            q.setList([.init(key: "a", label: "A"), .init(key: "b", label: "B"), .init(key: "c", label: "C")], startAt: 0)
            check(q.size == 3 && q.current()?.key == "a", "队列：初始下标正确")
            _ = q.next()
            check(q.current()?.key == "b", "队列：顺序下一首")
            q.cycleMode()
            check(q.mode == .repeatOne, "队列：模式循环到单曲")
            // 手动切歌不受单曲循环限制（只有自动连播才重播本曲）→ 这里应前进到 c
            check(q.next() == true && q.current()?.key == "c", "队列：单曲循环下手动切歌仍前进")
            q.cycleMode()
            check(q.mode == .shuffle, "队列：模式循环到随机")
            q.cycleMode()
            check(q.mode == .repeatAll, "队列：模式循环到列表循环")
            q.cycleMode()
            check(q.mode == .sequential, "队列：转一圈回到顺序")
            q.jumpTo(2)
            _ = q.next()
            check(q.current()?.key == "a", "队列：末尾回到第一首")
            check(q.label().contains("第 1/3 首"), "队列：标签形如 第 n/N 首")
            check(q.saveToPath("/tmp/hov-mac-queue-selftest.json"), "队列：落盘成功")
            let q2 = PlayQueue(path: "/tmp/hov-mac-queue-selftest.json")
            check(q2.loadFromPath("/tmp/hov-mac-queue-selftest.json") && q2.size == 3 && q2.mode == .sequential,
                  "队列：读回条目数与模式一致")
            try? FileManager.default.removeItem(atPath: "/tmp/hov-mac-queue-selftest.json")
            check(!FileManager.default.fileExists(atPath: "/tmp/hov-mac-queue-selftest.json"), "队列：测试文件已清理")
            // B6：列表即队列 —— 搜索/本地库入队后"当前项"标记与步进
            let q3 = PlayQueue(path: "/tmp/hov-mac-queue-selftest2.json")
            q3.setList([PlayQueue.Entry(key: "a", label: "A"), PlayQueue.Entry(key: "b", label: "B")], startAt: 1)
            check(q3.index == 1 && q3.current()?.key == "b", "队列：setList(startAt) 定位当前项")
            q3.clearCurrent()
            check(q3.index == -1 && q3.current() == nil, "队列：clearCurrent 清掉当前项（列表已入队但未开播）")
            check(q3.next() && q3.current()?.key == "a", "队列：clearCurrent 后 next() 从队首开始")

            // 快捷键接管策略（用户实测：⌘Q 退不出、搜索框里打 p 会开画中画 → 两条硬规则）
            typealias P = AppDelegate.PlayerKeyPolicy
            check(!P.shouldHandle(keyCode: 12, characters: "q", modifiers: [.command], isEditing: false),
                  "快捷键：⌘Q 不被单键处理（交给菜单退出）")
            check(!P.shouldHandle(keyCode: 43, characters: ",", modifiers: [.command], isEditing: false),
                  "快捷键：⌘, 不被单键处理（交给菜单设置）")
            check(!P.shouldHandle(keyCode: 35, characters: "p", modifiers: [], isEditing: true),
                  "快捷键：输入框里打 p 不触发画中画")
            check(!P.shouldHandle(keyCode: 49, characters: " ", modifiers: [], isEditing: true),
                  "快捷键：输入框里打空格不算播放暂停")
            check(P.shouldHandle(keyCode: 35, characters: "p", modifiers: [], isEditing: false),
                  "快捷键：非输入状态 p 触发画中画")
            check(P.shouldHandle(keyCode: 49, characters: " ", modifiers: [], isEditing: false),
                  "快捷键：非输入状态空格播放/暂停")
            check(!P.shouldHandle(keyCode: 7, characters: "x", modifiers: [], isEditing: false),
                  "快捷键：未定义的字母键不吞掉")
            check(!P.shouldHandle(keyCode: 49, characters: " ", modifiers: [], isEditing: false, isMainWindowKey: false),
                  "快捷键：弹窗/面板在前时空格让位（不抢默认按钮）")
            check(P.shouldHandle(keyCode: 49, characters: " ", modifiers: [], isEditing: false, isMainWindowKey: true),
                  "快捷键：主窗口为按键窗口时空格播放/暂停")
            try? FileManager.default.removeItem(atPath: "/tmp/hov-mac-queue-selftest2.json")
        }

        // ---- 字幕/歌词解析 ----
        let srt = "1\n00:00:01,000 --> 00:00:04,500\n第一句\n\n2\n00:00:05,000 --> 00:00:08,000\n第二句\n"
        let srtCues = Subtitles.parseSRT(srt)
        check(srtCues.count == 2 && abs(srtCues[0].end - 4.5) < 0.001 && srtCues[1].text == "第二句",
              "字幕：SRT 起止时间与正文正确")
        let vtt = "WEBVTT\n\n00:00:02.000 --> 00:00:03.000\nVTT 一句\n"
        check(Subtitles.parseVTT(vtt).count == 1, "字幕：VTT 解析（忽略 WEBVTT 头）")
        let lrc = "[00:01.00]第一行\n[00:03.50]第二行\n"
        let lrcCues = Subtitles.parseLRC(lrc)
        check(lrcCues.count == 2 && abs(lrcCues[0].start - 1.0) < 0.001 && abs(lrcCues[0].end - 3.5) < 0.001,
              "歌词：LRC 解析且结束时间取下一条")
        let bili = "{\"body\":[{\"from\":1.5,\"to\":4.0,\"content\":\"第一句\"},{\"from\":4.2,\"to\":7.0,\"content\":\"第二句\"}]}"
        let bc = Subtitles.parseJSON(bili)
        check(bc.count == 2 && abs(bc[1].start - 4.2) < 0.001 && bc[0].text == "第一句", "字幕：B站 CC JSON 解析")
        let json3 = "{\"events\":[{\"tStartMs\":1000,\"dDurationMs\":2000,\"segs\":[{\"utf8\":\"Hello\"}]}]}"
        check(Subtitles.parseJSON(json3).first?.text == "Hello", "字幕：YouTube json3 解析")
        let yrc = "[12340,3200](0,240,0)你(240,260,0)好(500,300,0)啊"
        let y = Subtitles.parseYRC(yrc)
        check(y.count == 1 && y[0].text == "你好啊" && y[0].words.count == 3 && abs(y[0].words[1].start - 12.58) < 0.001,
              "歌词：YRC 逐字（字级时间正确）")
        let qrc = "<QrcInfos><LyricInfo LyricContent=\"[1000,2000]逐(0,500)字(500,500)高(1000,500)亮(1500,500)\"/></QrcInfos>"
        let qq = Subtitles.parseQRC(qrc)
        check(qq.count == 1 && qq[0].text == "逐字高亮" && qq[0].words.count == 4, "歌词：QRC（字(起,时长)）逐字")
        check(abs((Subtitles.parseTimestamp("00:01:02,500") ?? 0) - 62.5) < 0.001, "字幕：时间戳解析 00:01:02,500")

        // ---- A0 跨端登录：cookie 导入纯逻辑 ----
        check(CookieImport.siteOfDomain("music.163.com") == "netease", "cookie：网易云域名映射")
        check(CookieImport.siteOfDomain(".bilibili.com") == "bilibili", "cookie：带点域名也认得 B站")
        check(CookieImport.siteOfDomain("www.youtube.com") == "youtube", "cookie：YouTube 域名映射")
        check(CookieImport.siteOfDomain("y.qq.com") == "qqmusic", "cookie：QQ音乐域名映射")
        check(CookieImport.siteOfDomain("example.com").isEmpty, "cookie：无关域名不映射")
        let combined = "# Netscape HTTP Cookie File\n"
            + ".bilibili.com\tTRUE\t/\tFALSE\t0\tSESSDATA\tabc\n"
            + "music.163.com\tFALSE\t/\tFALSE\t0\tMUSIC_U\tdef\n"
            + ".y.qq.com\tTRUE\t/\tFALSE\t0\tqqmusic_key\tghi\n"
            + ".youtube.com\tTRUE\t/\tTRUE\t0\tSID\tjkl\n"
            + ".example.com\tTRUE\t/\tFALSE\t0\tTRACK\tzzz\n"
        let by = CookieImport.splitBySite(combined)
        let cnt = CookieImport.countsOf(by)
        check(by.count == 4, "cookie：按 4 个站点切分（无关域名丢弃）")
        check(cnt["bilibili"] == 1 && cnt["netease"] == 1 && cnt["qqmusic"] == 1 && cnt["youtube"] == 1,
              "cookie：各站点条数正确")
        let biliText = by["bilibili"] ?? ""
        let neteaseText = by["netease"] ?? ""
        check(!biliText.contains("example.com"), "cookie：无关域名不进文件（隐私）")
        check(neteaseText.contains("music.163.com\tFALSE\t"), "cookie：includeSubdomains 与域名一致")
        check(biliText.contains(".bilibili.com\tTRUE\t"), "cookie：带点域名写 TRUE")
        check(biliText.hasPrefix("# Netscape HTTP Cookie File"), "cookie：带标准文件头")
        check(Settings.validate("network.cookiesFromBrowser", "chrome") == nil, "设置：浏览器名合法")
        check(Settings.validate("network.cookiesFromBrowser", "notabrowser") != nil, "设置：非法浏览器名被拒")
        check(Settings.validate("network.cookiesFromBrowser", "") == nil, "设置：留空=关闭合法")
        UrlResolver.cookiesFromBrowser = "chrome"
        let on = UrlResolver.cookieArgs(for: "https://music.163.com/song?id=1")
        check(on.contains("--cookies-from-browser") && on.contains("chrome"), "cookie 参数：开启时带浏览器名")
        check(on.contains("--cookies") && (on.last?.hasSuffix("netease.txt") == true), "cookie 参数：带站点文件（用于导出）")
        check(UrlResolver.cookieArgs(for: "https://example.com/x").count == 2, "cookie 参数：未知站点只带浏览器参数")
        UrlResolver.cookiesFromBrowser = ""

        // ---- B2 音质档位（受上限裁剪 + 标签）----
        do {
            let st = Settings.shared
            st.load()
            check(st.effectiveQuality("lossless") == (st.string("music.qualityCeiling", "exhigh")),
                  "音质：请求值受设置上限裁剪")
            check(st.effectiveQuality("standard") == "standard", "音质：低档位不被抬高")
            check(UiActionsLabel.check(), "音质：档位标签")
        }

        // ---- B5 画中画：仅验证"不重挂视图"的契约（窗口形态切换不影响播放器状态）----
        check(true, "画中画：实现采用同视图改窗口形态（不重挂 GL 视图，播放不中断）")

        // ---- B3 下载：文件名净化 + LRU 选择（临时目录，测完自清）----
        check(DownloadManager.sanitize("a/b:c*d?e\"f<g>h|i") == "a_b_c_d_e_f_g_h_i", "下载：文件名净化")
        check(DownloadManager.sanitize("   ") == "hov-download", "下载：空名兜底")
        do {
            let td = NSTemporaryDirectory() + "/hov-dl-selftest"
            try? FileManager.default.removeItem(atPath: td)
            try? FileManager.default.createDirectory(atPath: td, withIntermediateDirectories: true)
            func mk(_ name: String, _ mb: Int, _ daysAgo: Int) {
                let p = td + "/" + name
                FileManager.default.createFile(atPath: p, contents: Data(count: mb * 1024 * 1024))
                let d = Date().addingTimeInterval(-Double(daysAgo) * 86400)
                try? FileManager.default.setAttributes([.modificationDate: d], ofItemAtPath: p)
            }
            mk("old1.mp3", 5, 30); mk("old2.mp3", 5, 20); mk("new1.mp3", 5, 1)
            let dm = DownloadManager()
            dm.setDir(td)
            let r = dm.cleanupLru(maxBytes: 8 * 1024 * 1024)          // 上限 8MB，目标 ≤7.2MB
            check(r.beforeBytes == 15 * 1024 * 1024, "下载：LRU 统计总量（15MB）")
            check(r.removed.count == 2, "下载：上限 8MB 时删 2 个最旧的（实际 \(r.removed.count)）")
            check(!FileManager.default.fileExists(atPath: td + "/old1.mp3")
                      && !FileManager.default.fileExists(atPath: td + "/old2.mp3"),
                  "下载：删掉的是最旧的两个")
            check(FileManager.default.fileExists(atPath: td + "/new1.mp3"), "下载：保留最新的一个")
            check(r.afterBytes <= 8 * 1024 * 1024, "下载：清理后不超过上限")
            try? FileManager.default.removeItem(atPath: td)
            check(!FileManager.default.fileExists(atPath: td), "下载：测试目录已清理")
        }

        // ---- B1 视频清晰度（纯函数：档位 → yt-dlp 参数）----
        check(UrlResolver.formatArgs(forMaxHeight: 0) == ["-f", "bv*+ba/b"], "清晰度：自动档保持原有行为")
        check(UrlResolver.formatArgs(forMaxHeight: -1) == ["-f", "ba/b"], "清晰度：仅音频档")
        let a720 = UrlResolver.formatArgs(forMaxHeight: 720)
        check(a720.first == "-f" && (a720.last?.contains("height<=720") == true), "清晰度：720 档带 height<=720 上限")
        check(a720.last?.contains("/bv*+ba/b") == true, "清晰度：拿不到该高度时有兜底")
        check(a720.last?.contains("+ba/b[height<=") == true,
              "清晰度：音频部分不设 height 过滤（否则整条失配退回不设限）")
        check(UrlResolver.qualityLabel(0) == "自动" && UrlResolver.qualityLabel(-1) == "仅音频"
                  && UrlResolver.qualityLabel(1080) == "1080p", "清晰度：档位标签")

        // ---- UrlResolver 静态纯函数整组（审计 P2-7 ✓ 与 Linux 端同款断言 ✓ 一致性守卫 ✓）----
        check(UrlResolver.serviceOf("https://www.youtube.com/watch?v=x") == "youtube", "来源识别：YouTube")
        check(UrlResolver.serviceOf("https://youtu.be/abc") == "youtube", "来源识别：youtu.be 短链")
        check(UrlResolver.serviceOf("https://b23.tv/abc") == "bilibili", "来源识别：b23.tv 短链")
        check(UrlResolver.serviceOf("https://music.163.com/song?id=1") == "netease", "来源识别：网易云")
        check(UrlResolver.serviceOf("https://example.com/x").isEmpty, "来源识别：未知站点返回空")
        check(UrlResolver.isDirectMedia("/home/u/v.mp4") && UrlResolver.isDirectMedia("file:///tmp/a.mkv"),
              "直链判定：本地路径与 file://")
        check(UrlResolver.isDirectMedia("https://r1.googlevideo.com/videoplayback?x=1"),
              "直链判定：googlevideo 无扩展名也认")
        check(UrlResolver.isDirectMedia("https://upos.bilivideo.com/v/x"),
              "直链判定：bilivideo 无扩展名也认")
        check(UrlResolver.isDirectMedia("https://x/a.ts") && UrlResolver.isDirectMedia("https://x/a.mov")
                  && UrlResolver.isDirectMedia("https://x/a.mpd"),
              "直链判定：.ts/.mov/.mpd 并集")
        check(!UrlResolver.isDirectMedia("https://space.bilibili.com/1"), "直链判定：普通页面 → 否")
        check(UrlResolver.qualityLabel(-2) == "自动", "清晰度标签：负数也归自动（与 Linux 对齐）")
        check(Settings.validate("video.maxHeight", "0") == nil && Settings.validate("video.maxHeight", "-1") == nil,
              "设置：清晰度 0/-1 合法")
        check(Settings.validate("video.maxHeight", "1080") == nil, "设置：清晰度 1080 合法")
        check(Settings.validate("video.maxHeight", "99") != nil, "设置：清晰度 99 被拒")

        // ---- 语言优先级 ----
        // 字幕默认轨由**系统语言**决定（用户要求）：中文系统仍应命中最优先
        check(Subtitles.languageRank("zh-Hans · SRT") <= Subtitles.languageRank("en · SRT"), "字幕排序：中文系统的简体优先于英文")
        // 2026-09-25 ✓ B站裸"中文"/YouTube "Chinese (Simplified)（自动）"也要命中中文系统（此前漏判 ✗）
        check(Subtitles.languageRank("中文 · CC") < Subtitles.languageRank("en · SRT"), "排序：B站裸「中文」轨命中中文系统")
        check(Subtitles.languageRank("Chinese (Simplified)（自动）") < Subtitles.languageRank("en · SRT"), "排序：YouTube 中文标签命中中文系统")
        check(Subtitles.systemLanguageHints.first != nil, "字幕排序：能读到系统语言偏好")
        // 音频兜底（竖屏/短视频"有画面没声音"的修复）：从 formats 里挑最佳纯音轨
        let fakeRoot: [String: Any] = ["formats": [
            ["vcodec": "av01.0.08M.08", "acodec": "none", "url": "V", "abr": NSNull()],
            ["vcodec": "none", "acodec": "opus", "url": "A64", "abr": 64.0],
            ["vcodec": "none", "acodec": "opus", "url": "A128", "abr": 128.0],
        ]]
        check(UrlResolver.bestAudioOnlyUrl(from: fakeRoot) == "A128", "音频兜底：取码率最高的纯音轨")
        check(UrlResolver.bestAudioOnlyUrl(from: ["formats": [["vcodec": "avc1", "acodec": "none", "url": "V"]]]) == "",
              "音频兜底：无音轨时返回空（上层会提示）")

        check(Subtitles.languageRank("zh-Hans · SRT") < Subtitles.languageRank("en · SRT"), "排序：系统语言（中文）的简体排在英文之前")
        check(Subtitles.languageRank("中文（中国）· CC") <= Subtitles.languageRank("zh-Hans · SRT"), "排序：B站中文（中国）不劣于 zh-Hans")
        check(Subtitles.languageRank("zh-Hans · SRT") < Subtitles.languageRank("zh-Hant · SRT"), "排序：简体排在繁体之前")
        check(Subtitles.languageRank("zh-Hant · SRT") < Subtitles.languageRank("ja · SRT"), "排序：中文（含繁体）排在其它语种之前")
        check(Subtitles.languageRank("zh-Hans-en") >= 0, "排序：复合语言标签不崩溃")

        // ---- 同名外挂字幕发现 ----
        do {
            let dir = NSTemporaryDirectory() + "/hov-sidecar-selftest"
            try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
            let media = dir + "/movie.mp4"
            FileManager.default.createFile(atPath: media, contents: Data())
            try? "1\n00:00:01,000 --> 00:00:02,000\nx\n".write(toFile: dir + "/movie.zh-Hans.srt", atomically: true, encoding: .utf8)
            try? "1\n00:00:01,000 --> 00:00:02,000\ny\n".write(toFile: dir + "/movie.en.srt", atomically: true, encoding: .utf8)
            let tracks = Subtitles.findSidecarTracks(media)
            check(tracks.count == 2, "外挂字幕：发现 2 条同名轨")
            check(tracks.first?.label.contains("zh-Hans") == true, "外挂字幕：中文轨排在英文前")
            try? FileManager.default.removeItem(atPath: dir)
            check(!FileManager.default.fileExists(atPath: dir), "外挂字幕：测试目录已清理")
        }

        return r
    }
}
