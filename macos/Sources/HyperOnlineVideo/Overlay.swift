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
        guard let idx = cues.firstIndex(where: { time - delay >= $0.start && time - delay <= $0.end }) else { return [] }
        let cue = cues[idx]
        var out: [NSAttributedString] = []

        if karaoke {
            // 歌词：上一行（暗） / 当前行（逐字高亮） / 下一行（暗）
            if idx > 0 { out.append(NSAttributedString(string: cues[idx - 1].text, attributes: attrs(size: base * 0.8, color: dim))) }
            if !cue.words.isEmpty {
                // 真·逐字：按字级时间切分"已唱/未唱"
                let t = time - delay
                var sung = "", rest = ""
                for w in cue.words {
                    if w.start <= t + 0.001 { sung += w.text } else { rest += w.text }
                }
                let line = NSMutableAttributedString()
                if !sung.isEmpty { line.append(NSAttributedString(string: sung, attributes: attrs(size: base, color: accent, bold: true))) }
                if !rest.isEmpty { line.append(NSAttributedString(string: rest, attributes: attrs(size: base, color: plain, bold: true))) }
                out.append(line)
            } else {
                // 只有行级时间：用行内进度近似（与 Qt 端同策略）
                let span = max(0.01, cue.end - cue.start)
                let ratio = max(0.0, min(1.0, (time - delay - cue.start) / span))
                let chars = Array(cue.text)
                let n = Int(Double(chars.count) * ratio)
                let line = NSMutableAttributedString()
                if n > 0 { line.append(NSAttributedString(string: String(chars[0..<n]), attributes: attrs(size: base, color: accent, bold: true))) }
                if n < chars.count { line.append(NSAttributedString(string: String(chars[n...]), attributes: attrs(size: base, color: plain, bold: true))) }
                out.append(line)
            }
            if idx + 1 < cues.count { out.append(NSAttributedString(string: cues[idx + 1].text, attributes: attrs(size: base * 0.8, color: dim))) }
        } else {
            // 字幕：贴底（可能多行）
            for line in cue.text.components(separatedBy: "\n") where !line.isEmpty {
                out.append(NSAttributedString(string: line, attributes: attrs(size: base, color: plain)))
            }
        }
        return out
    }
}
