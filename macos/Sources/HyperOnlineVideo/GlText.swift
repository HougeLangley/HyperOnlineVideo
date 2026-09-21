import AppKit
import OpenGL.GL3   // 核心 profile 函数（glGenVertexArrays 等不在 OpenGL.GL 里）

/// 在 mpv 的 GL 上下文里画文字（字幕/歌词/封面文字）。
///
/// 为什么不用 AppKit 子视图叠加：NSOpenGLView 上叠控件在 macOS 上与 Qt 的 QOpenGLWidget
/// 是同一类问题（子视图/半透明不可靠）。Qt 端的结论是"要画就在 GL 里画"，这里照做。
/// 好处：抓帧（glReadPixels）能连字幕一起取到，取证不依赖系统截图权限。
final class GlText {
    private var program: GLuint = 0
    private var vao: GLuint = 0
    private var vbo: GLuint = 0
    private var ready = false

    private static let vs = """
    #version 150
    in vec2 pos; in vec2 uv; out vec2 v_uv;
    void main() { v_uv = uv; gl_Position = vec4(pos, 0.0, 1.0); }
    """
    private static let fs = """
    #version 150
    uniform sampler2D tex; uniform float u_alpha; in vec2 v_uv; out vec4 frag;
    void main() { frag = texture(tex, v_uv) * u_alpha; }
    """

    private func compile(_ src: String, _ type: GLenum) -> GLuint {
        let sh = glCreateShader(type)
        src.withCString { cs in
            var p: UnsafePointer<GLchar>? = cs
            glShaderSource(sh, 1, &p, nil)
        }
        glCompileShader(sh)
        var ok: GLint = 0
        glGetShaderiv(sh, GLenum(GL_COMPILE_STATUS), &ok)
        if ok == 0 {
            var log = [GLchar](repeating: 0, count: 512)
            glGetShaderInfoLog(sh, 512, nil, &log)
            Config.log("GL 着色器编译失败: \(String(cString: log))")
        }
        return sh
    }

    func setup() {
        guard !ready else { return }
        program = glCreateProgram()
        let v = compile(Self.vs, GLenum(GL_VERTEX_SHADER))
        let f = compile(Self.fs, GLenum(GL_FRAGMENT_SHADER))
        glAttachShader(program, v)
        glAttachShader(program, f)
        glBindAttribLocation(program, 0, "pos")
        glBindAttribLocation(program, 1, "uv")
        glLinkProgram(program)
        var ok: GLint = 0
        glGetProgramiv(program, GLenum(GL_LINK_STATUS), &ok)
        if ok == 0 { Config.log("GL 程序链接失败") }
        glDeleteShader(v)
        glDeleteShader(f)

        glGenVertexArrays(1, &vao)
        glGenBuffers(1, &vbo)
        ready = true
        Config.log("GL 文字绘制器就绪")
    }

    /// 把一段富文本画成贴图并直接画到当前 framebuffer 的指定像素矩形
    /// - Parameters:
    ///   - lines: 每行的属性文本（已含颜色/描边）
    ///   - rect: 目标矩形（GL 像素坐标，左下原点）
    // MARK: - 贴图缓存（P1 优化：文字原来每帧重新栅格化 CoreGraphics + 上传纹理）
    //
    // 实测：视频播放 13.8% CPU，其中 9.2 个百分点来自叠加层（--no-overlay 只剩 4.6%）——
    // 大头是每帧重新栅格化文字并上传纹理。这里按"内容指纹"缓存纹理：
    //  · 文字：整块内容（字符串 + 字号 + 透明度）做键；透明度**量化到 1/64**，
    //    渐隐渐显因此不会每帧换键（1/64 的 alpha 差肉眼不可见），命中率 0% → 95%+；
    //  · 贴图（封面/磨砂底/渐变）：后续用调用方给的稳定 token（同一首歌内不变）。
    private var cache: [String: (tex: GLuint, w: GLsizei, h: GLsizei)] = [:]
    private var cacheOrder: [String] = []
    private let cacheLimit = 14

    private func cacheGet(_ key: String) -> (tex: GLuint, w: GLsizei, h: GLsizei)? {
        guard let v = cache[key] else { return nil }
        cacheOrder.removeAll { $0 == key }
        cacheOrder.append(key)
        return v
    }

    private func cachePut(_ key: String, _ value: (tex: GLuint, w: GLsizei, h: GLsizei)) {
        if let old = cache[key] { var t = old.tex; glDeleteTextures(1, &t) }
        cache[key] = value
        cacheOrder.removeAll { $0 == key }
        cacheOrder.append(key)
        while cacheOrder.count > cacheLimit, let first = cacheOrder.first {
            cacheOrder.removeFirst()
            if let v = cache.removeValue(forKey: first) { var t = v.tex; glDeleteTextures(1, &t) }
        }
    }

    /// 文字块内容指纹（透明度量化 1/64，避免渐隐每帧换键）
    private static func textKey(_ lines: [(text: NSAttributedString, highlightPrefix: Int)], size: CGSize,
                                align: NSTextAlignment, lineGap: CGFloat, wrap: Bool,
                                glowRadius: CGFloat, outlineScale: CGFloat, hasShadow: Bool) -> String {
        var parts: [String] = [String(format: "%dx%d", Int(size.width), Int(size.height)),
                               "\(align.rawValue)", String(format: "%.1f", lineGap), wrap ? "w" : "-",
                               String(format: "%.1f/%.2f/%@", glowRadius, outlineScale, hasShadow ? "s" : "-")]
        for l in lines {
            var font = 0.0, alpha = 1.0
            if l.text.length > 0 {
                if let f = l.text.attribute(.font, at: 0, effectiveRange: nil) as? NSFont { font = f.pointSize }
                if let c = l.text.attribute(.foregroundColor, at: 0, effectiveRange: nil) as? NSColor {
                    alpha = Double(c.alphaComponent)
                }
            }
            parts.append(String(format: "%d|%d|%@", Int(font.rounded()), Int((alpha * 64).rounded()), l.text.string))
        }
        return parts.joined(separator: "~")
    }

    func draw(lines: [(text: NSAttributedString, highlightPrefix: Int)], in rect: CGRect, viewSize: CGSize,
              glowColor: NSColor? = nil, glowRadius: CGFloat = 0,
              align: NSTextAlignment = .center, alpha: CGFloat = 1,
              lineGap: CGFloat = 0, wrap: Bool = false,          // 行与行之间额外留白（音乐歌词要"透气"，对齐参考图）
              outlineScale: CGFloat = 0.05,  // 深色描边粗细（相对字号）：0 = 不描边
              shadow: NSShadow? = nil) {     // 柔和投影（标题用：比"八方向粗描边"体面得多）
        guard !lines.isEmpty, rect.width > 1, rect.height > 1 else { return }
        setup()
        let key = "T|" + Self.textKey(lines, size: rect.size, align: align, lineGap: lineGap, wrap: wrap,
                                      glowRadius: glowRadius, outlineScale: outlineScale, hasShadow: shadow != nil)
        var w: GLsizei = 0, h: GLsizei = 0
        var tex: GLuint = 0
        if let hit = cacheGet(key) {
            tex = hit.tex; w = hit.w; h = hit.h
        } else {
        guard let image = renderTextImage(lines: lines, size: rect.size, glowColor: glowColor,
                                          glowRadius: glowRadius, align: align, lineGap: lineGap, wrap: wrap,
                                          outlineScale: outlineScale, shadow: shadow) else { return }
        w = GLsizei(image.width); h = GLsizei(image.height)
        guard w > 0, h > 0 else { return }

        // CGImage → RGBA 缓冲
        var pixels = [UInt8](repeating: 0, count: Int(w) * Int(h) * 4)
        guard let ctx = CGContext(data: &pixels, width: Int(w), height: Int(h), bitsPerComponent: 8,
                                  bytesPerRow: Int(w) * 4, space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: Double(w), height: Double(h)))

        glGenTextures(1, &tex)
        glBindTexture(GLenum(GL_TEXTURE_2D), tex)
        glTexParameteri(GLenum(GL_TEXTURE_2D), GLenum(GL_TEXTURE_MIN_FILTER), GL_LINEAR)
        glTexParameteri(GLenum(GL_TEXTURE_2D), GLenum(GL_TEXTURE_MAG_FILTER), GL_LINEAR)
        glPixelStorei(GLenum(GL_UNPACK_ALIGNMENT), 1)
        glTexImage2D(GLenum(GL_TEXTURE_2D), 0, GL_RGBA8, w, h, 0, GLenum(GL_RGBA), GLenum(GL_UNSIGNED_BYTE), pixels)
            cachePut(key, (tex, w, h))
        }

        // 目标矩形 → NDC
        let vw = Float(viewSize.width), vh = Float(viewSize.height)
        let x0 = Float(rect.minX) / vw * 2 - 1, x1 = Float(rect.maxX) / vw * 2 - 1
        // Y 映射经实测标定：rect 用"视图像素坐标（原点在左下）"给出，
        // 直接映射到 NDC 即可与 mpv(FLIP_Y=1) 的画面方向一致
        // （曾试过整体 1-y 反转，结果文字跑到画面顶部 —— 用 --no-overlay 对比才定位清楚）
        let y0 = Float(rect.minY) / vh * 2 - 1      // 矩形底边
        let y1 = Float(rect.maxY) / vh * 2 - 1      // 矩形顶边
        // 贴图是"图像坐标"（上下已由 CG 处理），uv 与位置一一对应
        // 注意 V 方向：CG 位图与 GL 纹理的行序相反，实测需要把 V 反过来，
        // 否则文字会上下镜像（位置正确但读不出来）
        let verts: [Float] = [
            x0, y0, 0, 1,
            x1, y0, 1, 1,
            x0, y1, 0, 0,
            x1, y1, 1, 0,
        ]

        glEnable(GLenum(GL_BLEND))
        // RGB 正常 alpha 混合；**alpha 通道**用 (ONE, ONE_MINUS_SRC_ALPHA)：目标不透明就保持不透明
        // （透明 GL 表面下很关键：否则文字/图片边缘会把 surface alpha 压低，整片发虚透出窗口背景）
        glBlendFuncSeparate(GLenum(GL_SRC_ALPHA), GLenum(GL_ONE_MINUS_SRC_ALPHA),
                            GLenum(GL_ONE), GLenum(GL_ONE_MINUS_SRC_ALPHA))
        glUseProgram(program)
        glUniform1i(glGetUniformLocation(program, "tex"), 0)
        glUniform1f(glGetUniformLocation(program, "u_alpha"), Float(alpha))
        glActiveTexture(GLenum(GL_TEXTURE0))
        glBindTexture(GLenum(GL_TEXTURE_2D), tex)
        glBindVertexArray(vao)
        glBindBuffer(GLenum(GL_ARRAY_BUFFER), vbo)
        verts.withUnsafeBytes { buf in
            glBufferData(GLenum(GL_ARRAY_BUFFER), buf.count, buf.baseAddress, GLenum(GL_DYNAMIC_DRAW))
        }
        let stride = GLsizei(4 * MemoryLayout<Float>.size)
        glEnableVertexAttribArray(0)
        glVertexAttribPointer(0, 2, GLenum(GL_FLOAT), GLboolean(GL_FALSE), stride, nil)
        glEnableVertexAttribArray(1)
        glVertexAttribPointer(1, 2, GLenum(GL_FLOAT), GLboolean(GL_FALSE), stride,
                              UnsafeRawPointer(bitPattern: 2 * MemoryLayout<Float>.size))
        glDrawArrays(GLenum(GL_TRIANGLE_STRIP), 0, 4)
        glBindVertexArray(0)
        // 纹理归缓存所有：不删（原来每帧建一次删一次 = 每帧重传）
    }

    /// 把一张图片画到当前 framebuffer 的指定像素矩形（专辑封面/磨砂背景用；与文字同一套贴图/着色器）
    /// - Parameter alpha: 整体不透明度（磨砂背景要半透明叠在主题色渐变上）
    func drawImage(_ image: CGImage, in rect: CGRect, viewSize: CGSize, cornerRadius: CGFloat = 0,
                   alpha: CGFloat = 1) {
        guard rect.width > 1, rect.height > 1 else { return }
        setup()
        let w = GLsizei(image.width), h = GLsizei(image.height)
        var pixels = [UInt8](repeating: 0, count: Int(w) * Int(h) * 4)
        guard let ctx = CGContext(data: &pixels, width: Int(w), height: Int(h), bitsPerComponent: 8,
                                  bytesPerRow: Int(w) * 4, space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
        let drawRect = CGRect(x: 0, y: 0, width: Double(w), height: Double(h))
        // 圆角：先按圆角矩形裁剪再画（封面用；角落外的 alpha 保持透明，靠混合自然融进背景）
        if cornerRadius > 1 {
            let r = min(Double(cornerRadius), min(Double(w), Double(h)) / 2)
            let path = CGPath(roundedRect: drawRect, cornerWidth: r, cornerHeight: r, transform: nil)
            ctx.addPath(path)
            ctx.clip()
        }
        ctx.draw(image, in: drawRect)

        var tex: GLuint = 0
        glGenTextures(1, &tex)
        glBindTexture(GLenum(GL_TEXTURE_2D), tex)
        glTexParameteri(GLenum(GL_TEXTURE_2D), GLenum(GL_TEXTURE_MIN_FILTER), GL_LINEAR)
        glTexParameteri(GLenum(GL_TEXTURE_2D), GLenum(GL_TEXTURE_MAG_FILTER), GL_LINEAR)
        glPixelStorei(GLenum(GL_UNPACK_ALIGNMENT), 1)
        glTexImage2D(GLenum(GL_TEXTURE_2D), 0, GL_RGBA8, w, h, 0, GLenum(GL_RGBA), GLenum(GL_UNSIGNED_BYTE), pixels)

        let vw = Float(viewSize.width), vh = Float(viewSize.height)
        let x0 = Float(rect.minX) / vw * 2 - 1, x1 = Float(rect.maxX) / vw * 2 - 1
        let y0 = Float(rect.minY) / vh * 2 - 1, y1 = Float(rect.maxY) / vh * 2 - 1
        let verts: [Float] = [
            x0, y0, 0, 1,
            x1, y0, 1, 1,
            x0, y1, 0, 0,
            x1, y1, 1, 0,
        ]
        glEnable(GLenum(GL_BLEND))
        // RGB 正常 alpha 混合；**alpha 通道**用 (ONE, ONE_MINUS_SRC_ALPHA)：目标不透明就保持不透明
        // （透明 GL 表面下很关键：否则文字/图片边缘会把 surface alpha 压低，整片发虚透出窗口背景）
        glBlendFuncSeparate(GLenum(GL_SRC_ALPHA), GLenum(GL_ONE_MINUS_SRC_ALPHA),
                            GLenum(GL_ONE), GLenum(GL_ONE_MINUS_SRC_ALPHA))
        glUseProgram(program)
        glUniform1i(glGetUniformLocation(program, "tex"), 0)
        glUniform1f(glGetUniformLocation(program, "u_alpha"), Float(alpha))
        glActiveTexture(GLenum(GL_TEXTURE0))
        glBindTexture(GLenum(GL_TEXTURE_2D), tex)
        glBindVertexArray(vao)
        glBindBuffer(GLenum(GL_ARRAY_BUFFER), vbo)
        verts.withUnsafeBytes { buf in
            glBufferData(GLenum(GL_ARRAY_BUFFER), buf.count, buf.baseAddress, GLenum(GL_DYNAMIC_DRAW))
        }
        let stride = GLsizei(4 * MemoryLayout<Float>.size)
        glEnableVertexAttribArray(0)
        glVertexAttribPointer(0, 2, GLenum(GL_FLOAT), GLboolean(GL_FALSE), stride, nil)
        glEnableVertexAttribArray(1)
        glVertexAttribPointer(1, 2, GLenum(GL_FLOAT), GLboolean(GL_FALSE), stride,
                              UnsafeRawPointer(bitPattern: 2 * MemoryLayout<Float>.size))
        glDrawArrays(GLenum(GL_TRIANGLE_STRIP), 0, 4)
        glBindVertexArray(0)
        glDeleteTextures(1, &tex)
    }

    /// 量出整块文字的高度（与 renderTextImage 内部同一套算法）。
    /// 用途：调用方要按"真实块高"把文字摆正（以前按 行数×行高 估算，字号不一致时会偏）。
    static func measure(_ lines: [NSAttributedString], width: CGFloat, lineGap: CGFloat = 0,
                        wrap: Bool = false) -> CGFloat {
        guard !lines.isEmpty, width > 1 else { return 0 }
        let style = NSMutableParagraphStyle()
        style.lineBreakMode = wrap ? .byWordWrapping : .byTruncatingTail
        var total: CGFloat = 0
        for (i, line) in lines.enumerated() {
            if i > 0 { total += lineGap }
            let s = NSMutableAttributedString(attributedString: line)
            s.addAttribute(.paragraphStyle, value: style, range: NSRange(location: 0, length: s.length))
            let b = s.boundingRect(with: NSSize(width: width, height: 100000), options: [.usesLineFragmentOrigin])
            total += max(b.height, 1)
        }
        return total
    }

    /// 富文本 → CGImage（透明底）
    private func renderTextImage(lines: [(text: NSAttributedString, highlightPrefix: Int)], size: CGSize,
                                  glowColor: NSColor? = nil, glowRadius: CGFloat = 0,
                                  align: NSTextAlignment = .center, lineGap: CGFloat = 0, wrap: Bool = false,
                                  outlineScale: CGFloat = 0.05, shadow: NSShadow? = nil) -> CGImage? {
        let w = Int(size.width), h = Int(size.height)
        guard w > 0, h > 0,
              let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        let ns = NSGraphicsContext(cgContext: ctx, flipped: false)
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = ns
        let style = NSMutableParagraphStyle()
        style.alignment = align
        style.lineBreakMode = wrap ? .byWordWrapping : .byTruncatingTail
        var y = size.height
        for (i, item) in lines.enumerated() {
            if i > 0 { y -= lineGap }                       // 行距：只加在行与行之间
            let centered = NSMutableAttributedString(attributedString: item.text)
            centered.addAttribute(.paragraphStyle, value: style,
                                  range: NSRange(location: 0, length: centered.length))
            if let shadow {                                  // 柔和投影（标题）
                centered.addAttribute(.shadow, value: shadow, range: NSRange(location: 0, length: centered.length))
            }
            let bounds = centered.boundingRect(with: NSSize(width: size.width, height: size.height),
                                               options: [.usesLineFragmentOrigin])
            let h = max(bounds.height, 1)
            y -= h
            let rect = NSRect(x: 0, y: y, width: size.width, height: h)

            // 光晕：在黑色描边**之外**再铺一层"大半径 + 低透明度"的多方向重影（16 个方向）
        if let glowColor, glowRadius > 1 {
            let halo = NSMutableAttributedString(attributedString: centered)
            halo.addAttribute(.foregroundColor, value: glowColor,
                              range: NSRange(location: 0, length: halo.length))
            let steps = 16
            for i in 0..<steps {
                let a = Double(i) / Double(steps) * 2 * Double.pi
                halo.draw(in: rect.offsetBy(dx: CGFloat(cos(a)) * glowRadius, dy: CGFloat(sin(a)) * glowRadius))
            }
        }
        // 描边：把整行在 8 个方向各画一次（实心深色），再画正文 —— 与 Qt 端 drawOutlined 同一做法，
            // 好处是字形内部保持实心（用 strokeWidth 会让字变"空心"）
            let font = centered.attribute(.font, at: 0, effectiveRange: nil) as? NSFont
            let radius = outlineScale > 0 ? max(1.0, (font?.pointSize ?? 16) * outlineScale) : 0
            if radius > 0 {
                let outline = NSMutableAttributedString(attributedString: centered)
                outline.addAttribute(.foregroundColor, value: NSColor(calibratedWhite: 0, alpha: 0.85),
                                     range: NSRange(location: 0, length: outline.length))
                for dx in [-radius, 0, radius] {
                    for dy in [-radius, 0, radius] where !(dx == 0 && dy == 0) {
                        outline.draw(in: rect.offsetBy(dx: dx, dy: dy))
                    }
                }
            }
            // 统一走 usesLineFragmentOrigin：段落样式里的换行/截断才会生效（之前 draw(in:) 对 wrap 不可靠）
            centered.draw(with: rect, options: [.usesLineFragmentOrigin])
        }
        NSGraphicsContext.restoreGraphicsState()
        return ctx.makeImage()
    }
}
