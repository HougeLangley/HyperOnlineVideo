import AppKit

/// 字幕/歌词覆盖层状态（对应 Qt 端的 SubtitleOverlay）
final class Overlay {
    var cues: [SubtitleCue] = []
    var sourceName = ""
    var karaoke = false          // true = 歌词面板（居中 + 逐字高亮）
    var hidden = false
    var delay = 0.0
    var fontScale = 1.0

    private let accent = NSColor(calibratedRed: 0.31, green: 0.86, blue: 1.0, alpha: 1)   // 已唱：青蓝
    private let plain = NSColor.white
    private let dim = NSColor(calibratedWhite: 1.0, alpha: 0.55)

    func cueAt(_ t: Double) -> SubtitleCue? {
        let tt = t - delay
        return cues.first { tt >= $0.start && tt <= $0.end }
    }

    /// 只给字体与颜色；描边交给 GlText 用"八方向偏移"实现
    /// （曾经用 strokeWidth: -3 的描边，结果描边吃掉字形内部，观感"空洞"）
    private func attrs(size: CGFloat, color: NSColor, bold: Bool = false) -> [NSAttributedString.Key: Any] {
        let font = NSFont.systemFont(ofSize: size, weight: bold ? .semibold : .medium)
        return [.font: font, .foregroundColor: color]
    }

    /// 生成要绘制的行（每行一个属性文本）。返回空数组 = 什么都不画。
    func lines(at time: Double, viewSize: CGSize) -> [NSAttributedString] {
        guard !hidden, !cues.isEmpty else { return [] }
        let base = max(15.0, viewSize.height * 0.040) * fontScale   // 略放大：观感更实
        var hit = cues.firstIndex(where: { time - delay >= $0.start && time - delay <= $0.end })
        if hit == nil, karaoke {
            // 间奏/长音空隙：保留**最后一句**（QQ 音乐等播放器都这么做，观感连贯）。
            // 前奏（还没到第一句）仍然什么都不显示。只影响歌词模式，字幕行为不变。
            let tt = time - delay
            hit = cues.lastIndex(where: { $0.start <= tt })
        }
        guard let idx = hit else { return [] }
        let cue = cues[idx]
        var out: [NSAttributedString] = []

        if karaoke {
            // 歌词：多行 + 渐隐渐显（对齐 QQ 音乐观感）
            //  · 当前行：大字加粗 + 逐字高亮（青蓝）
            //  · 已唱过的行往上淡出；即将唱到的行往下淡入（离当前行越远越透明）
            //  · 紧邻的上下两行还随"当前行进度"平滑过渡：当前行快唱完时下一行逐渐亮起，刚开口时上一行还亮着
            // 双语歌词由 Subtitles.mergeBilingual 把译文并到 text 第二行起 → 这里拆成独立的小字行
            func mainText(_ t: String) -> String { String(t.components(separatedBy: "\n").first ?? "") }
            func transLines(_ t: String) -> [String] {
                t.components(separatedBy: "\n").dropFirst().filter { !$0.trimmingCharacters(in: .whitespaces).isEmpty }
            }
            // 当前行进度（0~1）：有字级时间戳就用它，否则按行内时间比例
            let tt = time - delay
            let prog: Double = cue.words.isEmpty
                ? max(0.0, min(1.0, (tt - cue.start) / max(0.01, cue.end - cue.start)))
                : Double(cue.words.filter { $0.start <= tt + 0.001 }.count) / Double(max(1, cue.words.count))

            // 行数：按画面高度自适应（至少上下各 2 行，长屏上下各 3 行 → 共 5~7 句）
            let span = max(2, min(3, Int((viewSize.height * 0.60) / (base * 2.4))))
            // "虚拟位置"模型：当前位置 = 当前行序号 + 行内进度。用它算**连续**距离，
            // 于是整摞歌词会随时间**平滑上移式**淡出/淡入（而不是到点突跳）——
            // 上一行随当前行推进越来越淡，下一行则逐渐亮起来。
            let virtualPos = Double(idx) + prog
            let lo = max(0, idx - span), hi = min(cues.count - 1, idx + span)
            for i in lo...hi {
                let c = cues[i]
                let dist = abs(Double(i) - virtualPos)
                // 1/(1+0.85·d^1.6)：d=0→1.00，d=1→0.54，d=2→0.28，d=3→0.17（平滑且不会消失）
                let a = CGFloat(1.0 / (1.0 + 0.85 * pow(dist, 1.6)))
                let level = min(3, Int(dist.rounded()))          // 字号/字重按离散档位（避免文字一直缩放）
                let size = base * (level == 0 ? 1.0 : level == 1 ? 0.84 : level == 2 ? 0.72 : 0.64)
                if i == idx {
                    // 当前行：满亮度 + 逐字高亮（青蓝）
                    let line = NSMutableAttributedString()
                    let main = mainText(c.text)
                    if !c.words.isEmpty {
                        var sung = "", rest = ""
                        for w in c.words { if w.start <= tt + 0.001 { sung += w.text } else { rest += w.text } }
                        if !sung.isEmpty { line.append(NSAttributedString(string: sung, attributes: attrs(size: size, color: accent, bold: true))) }
                        if !rest.isEmpty { line.append(NSAttributedString(string: rest, attributes: attrs(size: size, color: plain, bold: true))) }
                    } else {
                        let chars = Array(main)
                        let n = Int(Double(chars.count) * prog)
                        if n > 0 { line.append(NSAttributedString(string: String(chars[0..<n]), attributes: attrs(size: size, color: accent, bold: true))) }
                        if n < chars.count { line.append(NSAttributedString(string: String(chars[n...]), attributes: attrs(size: size, color: plain, bold: true))) }
                    }
                    out.append(line)
                    for tr in transLines(c.text) {
                        out.append(NSAttributedString(string: tr,
                            attributes: attrs(size: base * 0.60, color: plain.withAlphaComponent(0.62))))
                    }
                } else {
                    out.append(NSAttributedString(string: mainText(c.text),
                        attributes: attrs(size: size, color: plain.withAlphaComponent(a), bold: false)))
                    // 译文：相邻行才带，且整体压暗（之前太亮，抢了当前行的戏）
                    if Double(abs(i - idx)) <= 1.0 + prog {
                        for tr in transLines(c.text) {
                            out.append(NSAttributedString(string: tr,
                                attributes: attrs(size: base * 0.54, color: plain.withAlphaComponent(a * 0.62))))
                        }
                    }
                }
            }
        } else {
            // 字幕：贴底（可能多行）
            for line in cue.text.components(separatedBy: "\n") where !line.isEmpty {
                out.append(NSAttributedString(string: line, attributes: attrs(size: base, color: plain)))
            }
        }
        return out
    }
}
