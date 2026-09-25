import Foundation

// MARK: - 网易云音乐（旧版公开接口；与 Qt 端、Android 端同一套端点）
final class NetEaseApi {
    struct Song {
        var id = "", name = "", artist = "", album = ""
        var durationMs = 0
    }
    struct StreamInfo { var url = "", br = 0, type = "", level = "", error = "" }

    static func cookieHeader() -> String {
        // Netscape cookie 文件 → Cookie 头（与另两端一致；没有文件则匿名）
        let path = Config.cookiePath("netease")
        guard let text = try? String(contentsOfFile: path, encoding: .utf8) else { return "" }
        var pairs: [String] = []
        for line in text.components(separatedBy: "\n") {
            let t = line.trimmingCharacters(in: .whitespaces)
            if t.isEmpty || t.hasPrefix("#") { continue }
            let f = t.components(separatedBy: "\t")
            if f.count >= 7 { pairs.append("\(f[5])=\(f[6])") }
        }
        return pairs.joined(separator: "; ")
    }

    private static func headers() -> [String: String] {
        var h = ["User-Agent": Http.ua, "Referer": "https://music.163.com/"]
        let ck = cookieHeader()
        if !ck.isEmpty { h["Cookie"] = ck }
        return h
    }

    /// 搜索（旧版接口匿名可用）
    func search(_ keyword: String, limit: Int = 15) -> [Song] {
        let body = "s=\(Self.percent(keyword))&type=1&offset=0&limit=\(limit)"
        let r = Http.post("https://music.163.com/api/search/get/web", body: body.data(using: .utf8)!,
                          headers: Self.headers())
        guard r.ok else {
            Config.log("网易云搜索失败: \(r.error) HTTP \(r.status)")
            return []
        }
        let songs = ((r.json()["result"] as? [String: Any])?["songs"] as? [Any]) ?? []
        var out: [Song] = []
        for v in songs {
            guard let o = v as? [String: Any] else { continue }
            var s = Song()
            s.id = String(Int((o["id"] as? NSNumber)?.doubleValue ?? 0))
            s.name = (o["name"] as? String) ?? ""
            s.durationMs = (o["duration"] as? NSNumber)?.intValue ?? 0
            var artists: [String] = []
            for a in (o["artists"] as? [Any]) ?? [] {
                if let n = (a as? [String: Any])?["name"] as? String { artists.append(n) }
            }
            s.artist = artists.joined(separator: " / ")
            s.album = ((o["album"] as? [String: Any])?["name"] as? String) ?? ""
            if !s.id.isEmpty && !s.name.isEmpty { out.append(s) }
        }
        return out
    }

    /// 取播放地址（level → br 映射；返回的真实码率由服务端决定）
    func songUrl(_ songId: String, level: String) -> StreamInfo {
        var info = StreamInfo()
        // 新版 v1 接口：明确用 level 语义（standard/exhigh/lossless）。
        // 旧接口 br=999000 请求无损时会**静默降级**、响应里也看不出原因（用户实测"切无损没反应/失败"）。
        let enc = (level == "lossless") ? "flac" : "mp3"
        var r = Http.get("https://music.163.com/api/song/enhance/player/url/v1?ids=[\(songId)]&level=\(level)&encodeType=\(enc)",
                         headers: Self.headers())
        if !r.ok || ((r.json()["data"] as? [Any])?.isEmpty ?? true) {          // v1 不可用 → 退回旧接口
            var br = 320_000
            if level == "standard" { br = 128_000 } else if level == "lossless" { br = 999_000 }
            let body = "ids=[\(songId)]&br=\(br)"
            Config.log("网易云取流：v1 接口无数据，退回旧接口（br=\(br)）")
            r = Http.post("https://music.163.com/api/song/enhance/player/url", body: body.data(using: .utf8)!,
                          headers: Self.headers())
        }
        guard r.ok else { info.error = r.error.isEmpty ? "HTTP \(r.status)" : r.error; return info }
        guard let arr = r.json()["data"] as? [Any], let first = arr.first as? [String: Any] else {
            info.error = "返回为空"; return info
        }
        info.url = (first["url"] as? String) ?? ""
        info.br = (first["br"] as? NSNumber)?.intValue ?? 0
        info.type = (first["type"] as? String) ?? ""
        info.level = (first["level"] as? String) ?? ""          // 服务端**实际**给的档位（可能低于请求值）
        if !info.url.isEmpty { info.url = Self.pickWorkingNode(info.url) }   // 节点择优（"随机不播" ✓）
        if info.url.isEmpty {
            info.error = "无可用地址（code=\((first["code"] as? NSNumber)?.intValue ?? 0)，可能需要登录或版权受限）"
        } else {
            Config.log("网易云取流: 请求档位=\(level) 实际档位=\(info.level.isEmpty ? "?" : info.level) 返回码率=\(info.br) 格式=\(info.type)"
                      + (info.level.isEmpty || info.level == level ? "" : "  ← 被服务端降级（该曲/该账号无此音质）"))
        }
        return info
    }

    /// CDN 节点择优（2026-09-25 "随机不播"根因 ✗→✓）：网易云流 URL 随机落在 m701~m804 等节点，
    /// 部分节点被 CDN 返回 403（实测 m704/m804 → 403；m701/m702/m703/m801 → 200；token 不绑定节点），
    /// mpv 一次失败即弃 → 播放停在 0:00。探测替代节点取首个可用，全败回退原 URL。
    private static func pickWorkingNode(_ url: String) -> String {
        let pat = #"^(https?://)(m\d+)\.music\.126\.net(/.*)$"#
        guard let re = try? NSRegularExpression(pattern: pat),
              let m = re.firstMatch(in: url, range: NSRange(url.startIndex..<url.endIndex, in: url)),
              let r1 = Range(m.range(at: 1), in: url),
              let r2 = Range(m.range(at: 2), in: url),
              let r3 = Range(m.range(at: 3), in: url) else { return url }
        let prefix = String(url[r1]), orig = String(url[r2]), suffix = String(url[r3])
        for node in ["m701", "m702", "m703", "m801", "m802", "m803"] where node != orig {
            let cand = prefix + node + ".music.126.net" + suffix
            if Http.probeOk(cand, headers: Self.headers()) {
                Config.log("网易云节点择优: 选用 \(node).music.126.net")
                return cand
            }
        }
        return url
    }

    /// 歌词：原文 + 翻译 + 逐字（yrc 需登录）
    func lyric(_ songId: String) -> (lrc: String, trans: String, yrc: String) {
        let url = "https://music.163.com/api/song/lyric?os=pc&id=\(songId)&lv=-1&kv=-1&tv=-1&yv=-1"
        let r = Http.get(url, headers: Self.headers())
        guard r.ok else { return ("", "", "") }
        let root = r.json()
        return ((root["lrc"] as? [String: Any])?["lyric"] as? String ?? "",
                (root["tlyric"] as? [String: Any])?["lyric"] as? String ?? "",
                (root["yrc"] as? [String: Any])?["lyric"] as? String ?? "")
    }

    /// 批量取封面：一次请求拿多首（搜索接口只给 picId，列表要显示封面就得补这一步）
    func coverUrls(ids: [String]) -> [String: String] {
        guard !ids.isEmpty else { return [:] }
        let body = "ids=[" + ids.joined(separator: ",") + "]"
        let r = Http.post("https://music.163.com/api/song/detail", body: body.data(using: .utf8)!,
                          headers: Self.headers())
        guard r.ok, let songs = r.json()["songs"] as? [[String: Any]] else { return [:] }
        var out: [String: String] = [:]
        for s in songs {
            let id = (s["id"] as? NSNumber)?.stringValue ?? (s["id"] as? String) ?? ""
            let url = (s["album"] as? [String: Any])?["picUrl"] as? String ?? ""
            if !id.isEmpty, !url.isEmpty { out[id] = url }
        }
        return out
    }

    /// 专辑封面（搜索接口只给 picId，必须走 song/detail）
    func coverUrl(_ songId: String) -> String {
        let body = "ids=[\(songId)]"
        let r = Http.post("https://music.163.com/api/song/detail", body: body.data(using: .utf8)!,
                          headers: Self.headers())
        guard r.ok, let songs = r.json()["songs"] as? [Any], let first = songs.first as? [String: Any] else { return "" }
        return ((first["album"] as? [String: Any])?["picUrl"] as? String) ?? ""
    }

    static func percent(_ s: String) -> String {
        s.addingPercentEncoding(withAllowedCharacters: .alphanumerics) ?? s
    }
}

// MARK: - QQ 音乐（网页搜索 + vkey 取流；与 Qt 端同一套端点与档位前缀）
final class QQMusicApi {
    struct Song {
        var mid = "", name = "", artist = "", album = "", albumMid = ""
        var durationSec = 0
    }
    private static let ua = Http.ua
    private static let referer = "https://y.qq.com/"
    private static let guid = "10000"

    private static func cookieHeader() -> String {
        let path = Config.cookiePath("qqmusic")
        guard let text = try? String(contentsOfFile: path, encoding: .utf8) else { return "" }
        var pairs: [String] = []
        for line in text.components(separatedBy: "\n") {
            let t = line.trimmingCharacters(in: .whitespaces)
            if t.isEmpty || t.hasPrefix("#") { continue }
            let f = t.components(separatedBy: "\t")
            if f.count >= 7 { pairs.append("\(f[5])=\(f[6])") }
        }
        return pairs.joined(separator: "; ")
    }

    func search(_ keyword: String, page: Int = 1, limit: Int = 20) -> [Song] {
        // 2026-09-25 修复①：老 soso 接口已失效（实测返回空 ✗）→ 改用 musicu.fcg 现行接口
        // 修复②（当日傍晚 ✗ 用户实测"翻不到下一页"）：musicu **num_per_page 固定只回 15 条** ✗，
        //   "多取后截尾"注定去重归零 ✗ → 必须用 page_num 真翻页（与 Android 对齐 ✓）
        // （music.search.SearchCgiService / DoSearchForQQMusicMobile ✓ 与 Linux/Android 同款 ✓）
        let body: [String: Any] = [
            "comm": ["ct": "19", "cv": "1859", "uin": "0"],
            "req": [
                "module": "music.search.SearchCgiService",
                // 2026-09-25 傍晚：换 **Desktop** 方法（真翻页 ✓ 实测 page1/2 各 50 首不同 ✓）——
                //   grp:1 + num_per_page/page_num 用**数字** ✗（字符串会被服务端忽略 ✗ 曾"翻不到下一页"✗）
                "method": "DoSearchForQQMusicDesktop",
                "param": ["grp": 1, "num_per_page": limit,
                          "page_num": page, "query": keyword, "search_type": 0],
            ],
        ]
        guard let data = try? JSONSerialization.data(withJSONObject: body) else { return [] }
        var h = ["User-Agent": Self.ua, "Referer": Self.referer]
        let ck = Self.cookieHeader()
        if !ck.isEmpty { h["Cookie"] = ck }
        let r = Http.post("https://u.y.qq.com/cgi-bin/musicu.fcg", body: data,
                          contentType: "application/json", headers: h)
        guard r.ok else {
            Config.log("QQ音乐搜索失败: \(r.error) HTTP \(r.status)")
            return []
        }
        var list = Self.searchList(r.json())   // Desktop 响应路径 ✓
          if list.isEmpty && !ck.isEmpty {
              // 登录态失效（旧 cookie → 服务端 req.code=2001 风控 ✗）→ **匿名重试**（搜索不需要登录 ✓）
              var anonBody = body
              anonBody["comm"] = ["ct": "19", "cv": "1859", "uin": "0"]
              if let d2 = try? JSONSerialization.data(withJSONObject: anonBody) {
                  let r2 = Http.post("https://u.y.qq.com/cgi-bin/musicu.fcg", body: d2,
                                     contentType: "application/json",
                                     headers: ["User-Agent": Self.ua, "Referer": Self.referer])
                  if r2.ok {
                      list = Self.searchList(r2.json())
                      Config.log("QQ音乐搜索：带 cookie 无结果（可能登录态失效）→ 匿名重试 \(list.count) 条")
                  }
              }
          }
        var out: [Song] = []
        for v in list {
            guard let o = v as? [String: Any] else { continue }
            var s = Song()
            s.mid = (o["mid"] as? String) ?? ""
            s.name = (o["name"] as? String) ?? ""
            let alb = o["album"] as? [String: Any]
            s.album = (alb?["name"] as? String) ?? ""
            s.albumMid = (alb?["mid"] as? String) ?? ""
            s.durationSec = (o["interval"] as? NSNumber)?.intValue ?? 0
            var singers: [String] = []
            for a in (o["singer"] as? [Any]) ?? [] {
                if let n = (a as? [String: Any])?["name"] as? String { singers.append(n) }
            }
            s.artist = singers.joined(separator: " / ")
            if !s.mid.isEmpty && !s.name.isEmpty { out.append(s) }
        }
        return out
    }

    /// 档位 → filename 前缀（F000 无损 / M800 320k / M500 128k）
    private static func tier(_ t: String) -> (prefix: String, ext: String, label: String) {
        if t == "lossless" { return ("F000", "flac", "无损 FLAC") }
        if t == "standard" { return ("M500", "mp3", "128k") }
        return ("M800", "mp3", "320k")
    }

    /// QQ 搜索响应 → 歌曲数组（Desktop 路径 body.song.list ✓；2026-09-26 去重：两处共用 ✓）
    private static func searchList(_ root: [String: Any]) -> [Any] {
        (((((root["req"] as? [String: Any])?["data"] as? [String: Any])?["body"] as? [String: Any])?["song"] as? [String: Any])?["list"] as? [Any]) ?? []
    }

    /// 取流：先从 cookie 的 uin 试，失败再试数字 uin（与 Qt 端同策略）
    func streamUrl(_ mid: String, tierName: String) -> (url: String, error: String) {
        let t = Self.tier(tierName)
        let filename = "\(t.prefix)\(mid)\(mid).\(t.ext)"
        Config.log("QQ音乐取流：档位=\(t.label) filename 前缀=\(t.prefix)")
        let ck = Self.cookieHeader()
        var h = ["User-Agent": Self.ua, "Referer": Self.referer, "Content-Type": "application/json"]
        if !ck.isEmpty { h["Cookie"] = ck }
        let body: [String: Any] = [
            "comm": ["uin": "0", "format": "json", "ct": 19, "cv": 0],
            "req_0": [
                "module": "vkey.GetVkeyServer", "method": "CgiGetVkey",
                "param": ["guid": Self.guid, "songmid": [mid], "songtype": [0], "uin": "0",
                          "loginflag": 1, "platform": "20", "filename": [filename]],
            ],
        ]
        let data = (try? JSONSerialization.data(withJSONObject: body)) ?? Data()
        let url = "https://u.y.qq.com/cgi-bin/musicu.fcg?-=getplaysongvkey&format=json&platform=yqq.json&needNewCode=0"
        let r = Http.post(url, body: data, contentType: "application/json", headers: h)
        guard r.ok else { return ("", r.error.isEmpty ? "HTTP \(r.status)" : r.error) }
        let data2 = ((r.json()["req_0"] as? [String: Any])?["data"] as? [String: Any]) ?? [:]
        let sip = (data2["sip"] as? [Any])?.first as? String ?? ""
        guard let urls = data2["midurlinfo"] as? [Any], let first = urls.first as? [String: Any] else {
            return ("", "响应结构异常")
        }
        let result = (first["result"] as? NSNumber)?.intValue ?? 0
        let purl = (first["purl"] as? String) ?? ""
        if purl.isEmpty || purl == "null" {
            return ("", result == 104003 ? "无权限（该账号对此歌/此音质无权限）" : "服务端未给出地址（result=\(result)）")
        }
        return (sip + purl, "")
    }

    func lyric(_ mid: String) -> (lrc: String, trans: String) {
        let url = "https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=\(mid)&format=json&nobase64=1"
        var h = ["User-Agent": Self.ua, "Referer": Self.referer]
        let ck = Self.cookieHeader()
        if !ck.isEmpty { h["Cookie"] = ck }
        let r = Http.get(url, headers: h)
        guard r.ok else { return ("", "") }
        let root = r.json()
        return ((root["lyric"] as? String) ?? "", (root["trans"] as? String) ?? "")
    }

    /// 封面（搜索接口已带 albummid，无需额外请求）
    static func coverUrl(albumMid: String) -> String {
        albumMid.isEmpty ? "" : "https://y.gtimg.cn/music/photo_new/T002R300x300M000\(albumMid).jpg"
    }
}
