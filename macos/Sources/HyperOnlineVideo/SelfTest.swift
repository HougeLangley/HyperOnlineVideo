import Foundation

/// 纯逻辑自检（不触网、不动真实配置）—— 与 Qt 端 `--queue-selftest` 同等职责
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
        do {
            let s = Settings()
            s.load()
            check(s.effectiveQuality("lossless") == "exhigh", "设置：音质上限裁剪（默认 exhigh）")
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
            check(q.next() == false, "队列：单曲循环不前进")
            q.cycleMode()
            check(q.mode == .shuffle, "队列：模式循环到随机")
            q.cycleMode()
            check(q.mode == .sequential, "队列：模式回到顺序")
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

        // ---- 语言优先级 ----
        check(Subtitles.languageRank("zh-Hans · SRT") == 0, "排序：zh-Hans 最优先")
        check(Subtitles.languageRank("中文（中国）· CC") == 0, "排序：B站中文（中国）最优先")
        check(Subtitles.languageRank("zh-Hant") == 2, "排序：繁体次之")
        check(Subtitles.languageRank("en") == 4, "排序：英文靠后")
        check(Subtitles.languageRank("zh-Hans-en") == 1, "排序：再翻译的简体排原生之后")

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
