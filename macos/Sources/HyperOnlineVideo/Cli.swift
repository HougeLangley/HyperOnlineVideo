import Foundation

/// 命令行模式的判定结果
enum CliDecision {
    case exit(Int32)                 // 纯 CLI：做完就退出
    case play(video: String, audio: String, label: String)   // 网络层已取到直链（含音轨分离）：交给播放器验证整链路
    case gui                          // 走正常界面
}

/// 纯命令行模式（不创建窗口），供自动化与快速验证使用
enum Cli {
    /// 纯 CLI 路径也必须有设置上下文（否则 --resolve 永远用默认档位/匿名 cookie —— 实测踩过）
    private static func loadSettingsContext() {
        let st = Settings.shared
        st.load()
        UrlResolver.cookiesFromBrowser = st.string("network.cookiesFromBrowser")
        UrlResolver.maxHeight = Int(st.number("video.maxHeight", 0))
    }

    static func run(_ args: [String]) -> CliDecision {
        // 自检：--selftest-logic / --queue-selftest（与 Qt 端的参数名保持一致）
        if args.contains("--selftest-logic") || args.contains("--queue-selftest") {
            let r = SelfTest.run()
            for f in r.failures { Config.log("[FAIL] \(f)") }
            print("自检: \(r.pass) / \(r.total) 通过")
            return .exit(r.pass == r.total ? 0 : 1)
        }
        // 设置：--show-settings / --set k=v
        if args.contains("--show-settings") {
            let s = Settings.shared
            s.load()
            print("---- settings (\(Config.settingsPath)) ----")
            print(s.dump())
            return .exit(0)
        }
        if let i = args.firstIndex(of: "--set"), i + 1 < args.count {
            let kv = args[i + 1]
            guard let eq = kv.firstIndex(of: "=") else { return .exit(2) }
            let key = String(kv[..<eq]), value = String(kv[kv.index(after: eq)...])
            let s = Settings.shared
            s.load()
            let (ok, msg) = s.apply(key: key, value: value)
            print(ok ? "[set]    \(key) = \(value)" : "[reject] \(key) = \(value)：\(msg)")
            if ok { print("(设置已写入 \(Config.settingsPath))") }
            return .exit(ok ? 0 : 1)
        }
        // 字幕树解析验证：--bilicc <B站视频页>
        if let i = args.firstIndex(of: "--bilicc"), i + 1 < args.count {
            let tracks = UrlResolver().fetchBiliCc(args[i + 1])
            print("---- B站 CC 字幕 ----")
            print("取回 \(tracks.count) 条轨道")
            for t in tracks { print("  - \(t.label)  \(t.cues.count) 行  首句: \(t.cues.first?.text ?? "")") }
            return .exit(tracks.isEmpty ? 1 : 0)
        }
        // YouTube 字幕：--ytsubs <视频页>
        if let i = args.firstIndex(of: "--ytsubs"), i + 1 < args.count {
            let tracks = UrlResolver().fetchYouTubeSubs(args[i + 1])
            print("---- YouTube 在线字幕 ----")
            print("取回 \(tracks.count) 条轨道")
            for t in tracks { print("  - \(t.label)  \(t.source)") }
            return .exit(0)
        }
        // 直链解析：--resolve <页面地址>（可配 --video-quality auto|audio|360|480|720|1080…）
        if let i = args.firstIndex(of: "--resolve"), i + 1 < args.count {
            loadSettingsContext()
            if let qi = args.firstIndex(of: "--video-quality"), qi + 1 < args.count {
                let v = args[qi + 1].lowercased()
                let h: Int
                switch v {
                case "auto": h = 0
                case "audio", "audio-only": h = -1
                default: h = Int(v.replacingOccurrences(of: "p", with: "")) ?? 0
                }
                UrlResolver.maxHeight = h
                Config.log("[CLI] 清晰度档位 = \(UrlResolver.qualityLabel(h))")
            }
            guard let s = UrlResolver().resolve(args[i + 1]) else {
                print("解析失败"); return .exit(1)
            }
            print("---- 解析结果 ----")
            print("标题: \(s.title)")
            print("视频流: \(s.videoUrl.prefix(90))...")
            print("音频流: \(s.audioUrl.isEmpty ? "（无，单流）" : String(s.audioUrl.prefix(90)) + "...")")
            if args.contains("--play") { return playStream(url: s.videoUrl, audio: s.audioUrl, label: s.title) }
            return .exit(0)
        }
        // 音乐：--music <关键词> / --qqmusic <关键词>
        if let i = args.firstIndex(of: "--music"), i + 1 < args.count {
            return musicTest(keyword: args[i + 1], qq: false, args: args)
        }
        if let i = args.firstIndex(of: "--qqmusic"), i + 1 < args.count {
            return musicTest(keyword: args[i + 1], qq: true, args: args)
        }
        return .gui
    }

    private static func musicTest(keyword: String, qq: Bool, args: [String]) -> CliDecision {
        let settings = Settings.shared
        settings.load()
        var quality = "exhigh"
        if let i = args.firstIndex(of: "--quality"), i + 1 < args.count { quality = args[i + 1] }
        let level = settings.effectiveQuality(quality)
        print("---- \(qq ? "QQ音乐" : "网易云") 搜索：\(keyword)（音质 \(level)）----")

        if qq {
            let api = QQMusicApi()
            let songs = api.search(keyword, limit: 12)
            print("搜索到 \(songs.count) 首")
            for s in songs.prefix(5) { print("  - \(s.name) — \(s.artist)  [\(s.mid)] album=\(s.albumMid)") }
            guard !songs.isEmpty else { return .exit(1) }
            var played = 0
            for s in songs.prefix(5) {
                var (url, err) = api.streamUrl(s.mid, tierName: level)
                if url.isEmpty { (url, err) = api.streamUrl(s.mid, tierName: "standard") }
                if url.isEmpty {
                    print("  跳过「\(s.name)」：\(err)")
                    continue
                }
                print("  取流成功: \(s.name) → \(url.prefix(70))...")
                let (lrc, trans) = api.lyric(s.mid)
                print("  歌词: 原文 \(lrc.count) 字节，翻译 \(trans.count) 字节")
                let cover = QQMusicApi.coverUrl(albumMid: s.albumMid)
                if !cover.isEmpty {
                    let r = Http.get(cover, headers: ["User-Agent": Http.ua, "Referer": "https://y.qq.com/"])
                    print("  封面: HTTP \(r.status)，\(r.data.count) 字节")
                }
                played += 1
                if args.contains("--play") { return playStream(url: url, label: "\(s.name) — \(s.artist)") }
                break
            }
            return .exit(played > 0 ? 0 : 1)
        } else {
            let api = NetEaseApi()
            let songs = api.search(keyword, limit: 12)
            print("搜索到 \(songs.count) 首")
            for s in songs.prefix(5) { print("  - \(s.name) — \(s.artist)  [\(s.id)] album=\(s.album)") }
            guard !songs.isEmpty else { return .exit(1) }
            for s in songs.prefix(5) {
                let info = api.songUrl(s.id, level: level)
                guard !info.url.isEmpty else {
                    print("  跳过「\(s.name)」：\(info.error)")
                    continue
                }
                print("  取流成功: \(s.name) → \(info.url.prefix(70))...")
                let (lrc, trans, yrc) = api.lyric(s.id)
                print("  歌词: 原文 \(lrc.count) 字节，翻译 \(trans.count) 字节，逐字 \(yrc.count) 字节")
                let cover = api.coverUrl(s.id)
                if !cover.isEmpty {
                    let r = Http.get(cover, headers: ["User-Agent": Http.ua])
                    print("  封面: HTTP \(r.status)，\(r.data.count) 字节")
                }
                if args.contains("--play") { return playStream(url: info.url, label: "\(s.name) — \(s.artist)") }
                return .exit(0)
            }
            return .exit(1)
        }
    }

    /// 把取到的直链交给 mpv 播放（验证"网络 → 播放器"整链路）
    private static func playStream(url: String, audio: String = "", label: String) -> CliDecision {
        print("---- 交给播放器验证：\(label) ----")
        return .play(video: url, audio: audio, label: label)
    }
}
