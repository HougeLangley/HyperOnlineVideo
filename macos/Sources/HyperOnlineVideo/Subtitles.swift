import Foundation

/// 字幕/歌词解析（与 Qt 端同等职责：SRT/VTT/LRC/JSON/YRC/QRC + 语言优先级 + 同名外挂发现）
struct SubtitleWord { var start = 0.0, end = 0.0, text = "" }
struct SubtitleCue { var start = 0.0, end = 0.0, text = ""; var words: [SubtitleWord] = [] }
struct SubtitleTrack { var label = "", source = ""; var cues: [SubtitleCue] = []; var karaoke = false }

enum Subtitles {
    // MARK: 语言优先级（修复过"英文轨排在中文前"的问题）
    /// 系统语言偏好提示（小写前缀，下标即优先级）——
    /// 例：中文 macOS → ["zh-hans-cn","zh-hans","zh","en"]；德语系统 → ["de-de","de","en"]
    /// 用途：**默认选哪条字幕轨由系统语言决定**（用户明确要求），而不是写死中文优先。
    static var systemLanguageHints: [String] {
        var hints: [String] = []
        for tag in Locale.preferredLanguages {
            let low = tag.lowercased()
            hints.append(low)
            let parts = low.split(separator: "-").map(String.init)
            if parts.count >= 2 { hints.append(parts[0] + "-" + parts[1]) }
            if let p = parts.first { hints.append(p) }
        }
        hints.append("en")
        var seen = Set<String>()
        return hints.filter { !$0.isEmpty && seen.insert($0).inserted }
    }

    static func languageRank(_ lang: String) -> Int {
        let l = lang.trimmingCharacters(in: .whitespaces).lowercased()
        // 字幕标签形如 "zh-Hans · SRT" / "en · SRT" / "中文（中国）· CC" → 取分隔符前部分做语言判定
        let label = l.components(separatedBy: "·").first?.trimmingCharacters(in: .whitespaces) ?? l
        let hints = systemLanguageHints
        let systemIsChinese = hints.contains { $0.hasPrefix("zh") }

        // ① 系统语言驱动：**按 hint 下标打分**（下标越小越优先）。
        //    不能压成 0/1 —— 否则"系统首选语言"和"兜底英文"会同级，英文会顶掉中文（自检抓到过）。
        for (idx, hint) in hints.enumerated() where hint.count >= 2 {
            if label == hint || label.hasPrefix(hint + "-") || label.hasPrefix(hint) { return idx }
        }
        // ② 中文轨可能写成自然语言（简体/繁體/中文（中国））→ 视为命中系统语言
        if systemIsChinese {
            let hans = label.contains("简体") || label.contains("中文（中国）") || label.contains("中文(中国)")
            let hant = label.contains("繁體") || label.contains("中文（台") || label.contains("中文(台")
            if hans || hant {
                let systemHant = (hints.first ?? "").hasPrefix("zh-hant")
                return systemHant == hant ? 0 : 1
            }
        }
        // ③ 都未命中：排在所有 hints 之后（英文优先；中文系统再保留"简体优先"的原有观感）
        let base = hints.count
        let isEn = label == "en" || label.hasPrefix("en-") || label.hasPrefix("english")
        if isEn { return base }
        if systemIsChinese {
            let hans = label.hasPrefix("zh-hans") || label == "zh" || label == "zh-cn" || label == "zh-sg"
            let hant = label.hasPrefix("zh-hant") || label == "zh-tw" || label == "zh-hk" || label == "zh-mo"
            if hans { return base + 1 }
            if hant { return base + 2 }
        }
        return base + 3
    }

    // MARK: 时间戳
    private static func seconds(_ h: String, _ m: String, _ s: String, _ ms: String) -> Double? {
        guard let hh = Double(h), let mm = Double(m), let ss = Double(s) else { return nil }
        var frac = 0.0
        if !ms.isEmpty, let f = Double("0." + ms.trimmingCharacters(in: CharacterSet(charactersIn: "0")) ) { frac = f }
        if !ms.isEmpty, let raw = Double(ms) { frac = raw / pow(10, Double(ms.count)) }
        return hh * 3600 + mm * 60 + ss + frac
    }

    /// "00:00:01,000" / "00:00:01.000" / "01:02.34" 都接受
    static func parseTimestamp(_ raw: String) -> Double? {
        let t = raw.trimmingCharacters(in: .whitespaces)
        let parts = t.replacingOccurrences(of: ",", with: ".").split(separator: ":", omittingEmptySubsequences: false)
        if parts.count == 3 {
            return seconds(String(parts[0]), String(parts[1]), String(parts[2]).split(separator: ".").first.map(String.init) ?? "0",
                           String(parts[2]).split(separator: ".").count > 1 ? String(String(parts[2]).split(separator: ".")[1]) : "")
        }
        if parts.count == 2 {   // LRC: mm:ss.xx
            let secPart = String(parts[1])
            let secs = secPart.split(separator: ".")
            let frac = secs.count > 1 ? Double("0." + secs[1]) ?? 0 : 0
            guard let mm = Double(parts[0]), let ss = Double(secs[0]) else { return nil }
            return mm * 60 + ss + frac
        }
        return nil
    }

    // MARK: SRT / VTT
    static func parseSRT(_ text: String) -> [SubtitleCue] {
        var out: [SubtitleCue] = []
        let blocks = text.replacingOccurrences(of: "\r\n", with: "\n").components(separatedBy: "\n\n")
        for b in blocks {
            let lines = b.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
            guard let ti = lines.firstIndex(where: { $0.contains("-->") }) else { continue }
            let times = lines[ti].components(separatedBy: "-->")
            guard times.count == 2 else { continue }
            guard let s = parseTimestamp(times[0]), let e = parseTimestamp(times[1].split(separator: " ").first.map(String.init) ?? "") else { continue }
            let body = lines[(ti + 1)...].joined(separator: "\n").trimmingCharacters(in: .whitespacesAndNewlines)
            if body.isEmpty { continue }
            out.append(SubtitleCue(start: s, end: e, text: body))
        }
        return out.sorted { $0.start < $1.start }
    }

    static func parseVTT(_ text: String) -> [SubtitleCue] {
        var cleaned: [String] = []
        for line in text.replacingOccurrences(of: "\r\n", with: "\n").components(separatedBy: "\n") {
            let t = line.trimmingCharacters(in: .whitespaces)
            if t.hasPrefix("WEBVTT") || t.hasPrefix("NOTE") || t.hasPrefix("STYLE") { continue }
            cleaned.append(line)
        }
        return parseSRT(cleaned.joined(separator: "\n"))
    }

    // MARK: LRC
    static func parseLRC(_ text: String) -> [SubtitleCue] {
        var tmp: [(Double, String)] = []
        let re = try! NSRegularExpression(pattern: "\\[(\\d{1,2}):(\\d{1,2})(?:[.:](\\d{1,3}))?\\]([^\\[]*)")
        for line in text.replacingOccurrences(of: "\r\n", with: "\n").components(separatedBy: "\n") {
            let range = NSRange(line.startIndex..., in: line)
            for m in re.matches(in: line, range: range) {
                guard let mm = Range(m.range(at: 1), in: line), let ss = Range(m.range(at: 2), in: line) else { continue }
                var t = (Double(line[mm]) ?? 0) * 60 + (Double(line[ss]) ?? 0)
                if let fr = Range(m.range(at: 3), in: line), let v = Double(line[fr]) {
                    t += v / pow(10, Double(line[fr].count))
                }
                let body = Range(m.range(at: 4), in: line).map { String(line[$0]) } ?? ""
                let clean = body.trimmingCharacters(in: .whitespaces)
                if !clean.isEmpty { tmp.append((t, clean)) }
            }
        }
        tmp.sort { $0.0 < $1.0 }
        var out: [SubtitleCue] = []
        for (i, item) in tmp.enumerated() {
            let end = i + 1 < tmp.count ? tmp[i + 1].0 : item.0 + 4.0
            out.append(SubtitleCue(start: item.0, end: end, text: item.1))
        }
        return out
    }

    // MARK: JSON（B站 body/from/to + YouTube json3 events/segs）
    static func parseJSON(_ text: String) -> [SubtitleCue] {
        guard let data = text.data(using: .utf8),
              let root = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else { return [] }
        if let body = root["body"] as? [Any] {          // B站 CC
            var out: [SubtitleCue] = []
            for v in body {
                guard let o = v as? [String: Any] else { continue }
                let s = (o["from"] as? NSNumber)?.doubleValue ?? 0
                let e = (o["to"] as? NSNumber)?.doubleValue ?? 0
                let c = (o["content"] as? String) ?? ""
                if !c.isEmpty { out.append(SubtitleCue(start: s, end: e, text: c)) }
            }
            return out.sorted { $0.start < $1.start }
        }
        if let events = root["events"] as? [Any] {      // YouTube json3
            var out: [SubtitleCue] = []
            for v in events {
                guard let o = v as? [String: Any], let segs = o["segs"] as? [Any] else { continue }
                let s = ((o["tStartMs"] as? NSNumber)?.doubleValue ?? 0) / 1000
                let d = ((o["dDurationMs"] as? NSNumber)?.doubleValue ?? 2500) / 1000
                let text = segs.compactMap { ($0 as? [String: Any])?["utf8"] as? String }.joined()
                    .trimmingCharacters(in: .whitespacesAndNewlines)
                if !text.isEmpty { out.append(SubtitleCue(start: s, end: s + d, text: text)) }
            }
            return out.sorted { $0.start < $1.start }
        }
        return []
    }

    // MARK: YRC / QRC（逐字）
    static func parseYRC(_ text: String) -> [SubtitleCue] {
        var out: [SubtitleCue] = []
        let lineRe = try! NSRegularExpression(pattern: "\\[(\\d+),(\\d+)\\]")
        let wordRe = try! NSRegularExpression(pattern: "\\((\\d+),(\\d+)(?:,\\d+)?\\)([^()\\[\\]]*)")
        for raw in text.components(separatedBy: "\n") {
            let range = NSRange(raw.startIndex..., in: raw)
            guard let lm = lineRe.firstMatch(in: raw, range: range),
                  let r1 = Range(lm.range(at: 1), in: raw), let r2 = Range(lm.range(at: 2), in: raw) else { continue }
            let lineStart = (Double(raw[r1]) ?? 0) / 1000
            let lineDur = (Double(raw[r2]) ?? 0) / 1000
            var cue = SubtitleCue(start: lineStart, end: lineStart + lineDur, text: "")
            let body = String(raw[Range(lm.range, in: raw)!.upperBound...])
            for m in wordRe.matches(in: body, range: NSRange(body.startIndex..., in: body)) {
                guard let a = Range(m.range(at: 1), in: body), let b = Range(m.range(at: 2), in: body),
                      let c = Range(m.range(at: 3), in: body) else { continue }
                let txt = String(body[c])
                if txt.isEmpty { continue }
                let ws = lineStart + (Double(body[a]) ?? 0) / 1000
                cue.words.append(SubtitleWord(start: ws, end: ws + (Double(body[b]) ?? 0) / 1000, text: txt))
                cue.text += txt
            }
            cue.text = cue.text.trimmingCharacters(in: .whitespaces)
            if !cue.text.isEmpty { out.append(cue) }
        }
        return out.sorted { $0.start < $1.start }
    }

    static func parseQRC(_ text: String) -> [SubtitleCue] {
        var body = text
        if let r = text.range(of: "LyricContent=\""), let end = text.range(of: "\"", range: r.upperBound..<text.endIndex) {
            body = String(text[r.upperBound..<end.lowerBound])
        }
        for (a, b) in [("&apos;", "'"), ("&quot;", "\""), ("&lt;", "<"), ("&gt;", ">"), ("&amp;", "&")] {
            body = body.replacingOccurrences(of: a, with: b)
        }
        body = body.replacingOccurrences(of: "\\n", with: "\n")
        // QRC 是"字(起,时长)"，与 YRC 相反 → 转成 YRC 形式再解析
        let lineRe = try! NSRegularExpression(pattern: "\\[(\\d+),(\\d+)\\]([^\\[]*)")
        var yrc = ""
        for line in body.components(separatedBy: "\n") {
            let range = NSRange(line.startIndex..., in: line)
            guard let lm = lineRe.firstMatch(in: line, range: range),
                  let r1 = Range(lm.range(at: 1), in: line), let r2 = Range(lm.range(at: 2), in: line),
                  let r3 = Range(lm.range(at: 3), in: line) else { continue }
            var rebuilt = "[\(line[r1]),\(line[r2])]"
            let rest = String(line[r3])
            let wordRe = try! NSRegularExpression(pattern: "([^()\\[\\]]+)\\((\\d+),(\\d+)\\)")
            for m in wordRe.matches(in: rest, range: NSRange(rest.startIndex..., in: rest)) {
                guard let t = Range(m.range(at: 1), in: rest), let s = Range(m.range(at: 2), in: rest),
                      let d = Range(m.range(at: 3), in: rest) else { continue }
                rebuilt += "(\(rest[s]),\(rest[d]),0)\(rest[t])"
            }
            yrc += rebuilt + "\n"
        }
        return parseYRC(yrc)
    }

    /// 按扩展名选解析器
    static func parse(_ text: String, ext: String) -> [SubtitleCue] {
        switch ext.lowercased() {
        case "srt": return parseSRT(text)
        case "vtt": return parseVTT(text)
        case "lrc": return parseLRC(text)
        case "yrc": return parseYRC(text)
        case "qrc": return parseQRC(text)
        case "json", "json3": return parseJSON(text)
        default:
            if text.contains("WEBVTT") { return parseVTT(text) }
            if text.contains("-->") { return parseSRT(text) }
            if text.contains("[") && text.contains("]") { return parseLRC(text) }
            return []
        }
    }

    /// 双语合并：把翻译轨按"起始时间相近"贴到原文轨上（一行两语）
    static func mergeBilingual(_ main: [SubtitleCue], _ trans: [SubtitleCue], tolerance: Double = 0.6) -> [SubtitleCue] {
        guard !trans.isEmpty else { return main }
        var out = main
        var used = Set<Int>()
        for i in out.indices {
            var best = -1
            var bestDelta = tolerance + 1
            for (j, t) in trans.enumerated() where !used.contains(j) {
                let d = abs(t.start - out[i].start)
                if d < bestDelta { bestDelta = d; best = j }
            }
            if best >= 0, bestDelta <= tolerance {
                used.insert(best)
                out[i].text += "\n" + trans[best].text
            }
        }
        return out
    }

    /// 读文件（UTF-8 失败回退 GB18030）
    static func loadFile(_ path: String) -> [SubtitleCue] {
        guard let data = FileManager.default.contents(atPath: path) else { return [] }
        var text = String(data: data, encoding: .utf8)
        if text == nil {
            let gb = CFStringConvertEncodingToNSStringEncoding(CFStringEncoding(CFStringEncodings.GB_18030_2000.rawValue))
            text = String(data: data, encoding: String.Encoding(rawValue: gb))
        }
        guard let t = text else { return [] }
        return parse(t, ext: (path as NSString).pathExtension)
    }

    /// 同名外挂字幕轨发现：<base>.<lang>.<ext> 与 <base>.<ext>
    /// 排序：语言优先级 → 格式优先级（yrc/qrc > srt > vtt > lrc）→ 路径
    static func findSidecarTracks(_ mediaPath: String) -> [SubtitleTrack] {
        let exts = ["srt", "vtt", "yrc", "qrc", "lrc"]
        let fm = FileManager.default
        let dir = (mediaPath as NSString).deletingLastPathComponent
        let base = ((mediaPath as NSString).lastPathComponent as NSString).deletingPathExtension
        guard let files = try? fm.contentsOfDirectory(atPath: dir) else { return [] }
        func fmtRank(_ p: String) -> Int {
            switch (p as NSString).pathExtension.lowercased() {
            case "yrc", "qrc": return 0
            case "srt": return 1
            case "vtt": return 2
            case "lrc": return 3
            default: return 4
            }
        }
        var found: [SubtitleTrack] = []
        for f in files {
            let ext = (f as NSString).pathExtension.lowercased()
            guard exts.contains(ext) else { continue }
            let stem = (f as NSString).deletingPathExtension
            var lang = ""
            if stem == base { lang = "默认" }
            else if stem.hasPrefix(base + ".") { lang = String(stem.dropFirst(base.count + 1)) }
            else { continue }
            found.append(SubtitleTrack(label: "\(lang) · \(ext.uppercased())", source: dir + "/" + f))
        }
        return found.sorted { a, b in
            let la = languageRank(a.label), lb = languageRank(b.label)
            if la != lb { return la < lb }
            let fa = fmtRank(a.source), fb = fmtRank(b.source)
            if fa != fb { return fa < fb }
            return a.source < b.source
        }
    }
}
