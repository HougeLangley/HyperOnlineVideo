import AppKit
import CoreImage

/// 音乐播放的"主题色 + 磨砂背景"（对齐 QQ 音乐观感）：
/// 从专辑封面算出主色，并生成 ①主题色渐变 ②高斯模糊放大后的封面（虚化玻璃磨砂底）。
///
/// 为什么自己做而不是用 NSVisualEffectView：播放画面是 mpv 的 GL 上下文（NSOpenGLView），
/// 上面叠 AppKit 视图不可靠（Qt 端 QOpenGLWidget 同理）——所有绘制都在 GL 里，抓帧也能一起取到。
enum MusicTheme {
    struct Theme {
        var tint = NSColor(calibratedRed: 0.16, green: 0.19, blue: 0.24, alpha: 1)       // 主题色（亮）
        var tintDark = NSColor(calibratedRed: 0.07, green: 0.09, blue: 0.12, alpha: 1)   // 主题色（暗，渐变底部）
        var backdrop: NSImage?                                                            // 虚化封面
        var gradient: NSImage?                                                            // 主题色竖向渐变（1×256 拉伸）
    }

    /// 兜底（封面是灰度图或取色失败时用）：中性深蓝灰，保证白字清晰
    private static let neutral = NSColor(calibratedRed: 0.14, green: 0.17, blue: 0.22, alpha: 1)

    static func make(from cover: NSImage) -> Theme {
        guard let cg = cover.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
            return Theme(backdrop: nil, gradient: gradientImage(neutral))
        }
        let tint = dominantColor(of: cg) ?? neutral
        var h: CGFloat = 0, s: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        (tint.usingColorSpace(.deviceRGB) ?? tint).getHue(&h, saturation: &s, brightness: &b, alpha: &a)
        let dark = NSColor(calibratedHue: h, saturation: min(1.0, s * 1.05),
                           brightness: max(0.06, b * 0.42), alpha: 1)
        var t = Theme(tint: tint, tintDark: dark, backdrop: backdrop(from: cg), gradient: nil)
        t.gradient = gradientImage(t.tintDark, t.tint)
        return t
    }

    // MARK: - 取色

    /// 主色：缩到 32×32 → 按色相 12 桶加权投票（权重 = 饱和度² × 明度²，忽略过暗/过灰像素）
    /// → 取最大桶的加权均值 → 观感强化（保证背景够鲜亮，别是一片泥）。
    static func dominantColor(of cg: CGImage) -> NSColor? {
        let w = 32, h = 32
        var px = [UInt8](repeating: 0, count: w * h * 4)
        guard let ctx = CGContext(data: &px, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: w, height: h))

        var buckets: [Int: (r: Double, g: Double, b: Double, w: Double)] = [:]
        var i = 0
        while i < px.count {
            let r = Double(px[i]) / 255, g = Double(px[i + 1]) / 255, b = Double(px[i + 2]) / 255
            i += 4
            let mx = max(r, g, b), mn = min(r, g, b)
            let v = mx
            let s = mx <= 0.0001 ? 0 : (mx - mn) / mx
            if v < 0.12 || s < 0.15 { continue }                       // 过暗 / 过灰：不参与投票
            var hue = 0.0
            let d = mx - mn
            if d > 0.0001 {
                if mx == r { hue = ((g - b) / d).truncatingRemainder(dividingBy: 6) }
                else if mx == g { hue = (b - r) / d + 2 }
                else { hue = (r - g) / d + 4 }
                hue /= 6
                if hue < 0 { hue += 1 }
            }
            let weight = s * s * v * v
            let key = min(11, max(0, Int(hue * 12)))
            var e = buckets[key] ?? (0, 0, 0, 0)
            e.r += r * weight; e.g += g * weight; e.b += b * weight; e.w += weight
            buckets[key] = e
        }
        guard let best = buckets.max(by: { $0.value.w < $1.value.w }), best.value.w > 0.0001 else { return nil }
        let e = best.value
        let col = NSColor(calibratedRed: e.r / e.w, green: e.g / e.w, blue: e.b / e.w, alpha: 1)
        // 观感强化：饱和度不低于 0.45、明度落在 0.34~0.78（背景要"有色但不刺眼"）
        var hh: CGFloat = 0, ss: CGFloat = 0, bb: CGFloat = 0, aa: CGFloat = 0
        col.getHue(&hh, saturation: &ss, brightness: &bb, alpha: &aa)
        return NSColor(calibratedHue: hh, saturation: min(1.0, max(0.45, ss)),
                       brightness: min(0.78, max(0.34, bb)), alpha: 1)
    }

    // MARK: - 磨砂底

    /// 虚化封面：先缩到 ~220px（快），再 CIGaussianBlur 大半径 → 拉伸铺满时就是磨砂玻璃质感
    static func backdrop(from cg: CGImage) -> NSImage? {
        let target = 220.0
        let sc = min(1.0, target / Double(max(cg.width, cg.height)))
        let w = max(8, Int(Double(cg.width) * sc)), h = max(8, Int(Double(cg.height) * sc))
        var px = [UInt8](repeating: 0, count: w * h * 4)
        guard let ctx = CGContext(data: &px, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: w, height: h))
        guard let small = ctx.makeImage() else { return nil }

        let ci = CIImage(cgImage: small)
        let blurred = ci.applyingFilter("CIGaussianBlur", parameters: [kCIInputRadiusKey: 22.0])
            .cropped(to: ci.extent)                                    // 模糊会放大 extent → 裁回原尺寸
            .applyingFilter("CIColorControls", parameters: [kCIInputSaturationKey: 1.15,
                                                            kCIInputBrightnessKey: -0.05])
        let ciCtx = CIContext(options: [.useSoftwareRenderer: false])
        guard let out = ciCtx.createCGImage(blurred, from: ci.extent) else { return nil }
        let img = NSImage(size: NSSize(width: out.width, height: out.height))
        img.addRepresentation(NSBitmapImageRep(cgImage: out))
        return img
    }

    /// 竖向渐变（1×256，底部暗 → 顶部亮）：主题色打底，让画面有纵深而不是一块死色
    static func gradientImage(_ bottom: NSColor, _ top: NSColor? = nil) -> NSImage? {
        let topColor = top ?? bottom
        let w = 1, h = 256
        var px = [UInt8](repeating: 0, count: w * h * 4)
        let cb = bottom.usingColorSpace(.deviceRGB) ?? bottom
        let ct = topColor.usingColorSpace(.deviceRGB) ?? topColor
        for y in 0..<h {
            let t = Double(y) / Double(h - 1)
            let r = Double(cb.redComponent) * (1 - t) + Double(ct.redComponent) * t
            let g = Double(cb.greenComponent) * (1 - t) + Double(ct.greenComponent) * t
            let b = Double(cb.blueComponent) * (1 - t) + Double(ct.blueComponent) * t
            let i = y * 4
            px[i] = UInt8(max(0, min(255, r * 255)))
            px[i + 1] = UInt8(max(0, min(255, g * 255)))
            px[i + 2] = UInt8(max(0, min(255, b * 255)))
            px[i + 3] = 255
        }
        guard let provider = CGDataProvider(data: Data(px) as CFData),
              let cg = CGImage(width: w, height: h, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: w * 4,
                               space: CGColorSpaceCreateDeviceRGB(),
                               bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                               provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)
        else { return nil }
        let img = NSImage(size: NSSize(width: w, height: h))
        img.addRepresentation(NSBitmapImageRep(cgImage: cg))
        return img
    }
}
