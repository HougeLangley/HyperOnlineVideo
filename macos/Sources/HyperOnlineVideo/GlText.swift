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
    uniform sampler2D tex; in vec2 v_uv; out vec4 frag;
    void main() { frag = texture(tex, v_uv); }
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
    func draw(lines: [(text: NSAttributedString, highlightPrefix: Int)], in rect: CGRect, viewSize: CGSize) {
        guard !lines.isEmpty, rect.width > 1, rect.height > 1 else { return }
        setup()
        guard let image = renderTextImage(lines: lines, size: rect.size) else { return }
        let w = GLsizei(image.width), h = GLsizei(image.height)
        guard w > 0, h > 0 else { return }

        // CGImage → RGBA 缓冲
        var pixels = [UInt8](repeating: 0, count: Int(w) * Int(h) * 4)
        guard let ctx = CGContext(data: &pixels, width: Int(w), height: Int(h), bitsPerComponent: 8,
                                  bytesPerRow: Int(w) * 4, space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: Double(w), height: Double(h)))

        var tex: GLuint = 0
        glGenTextures(1, &tex)
        glBindTexture(GLenum(GL_TEXTURE_2D), tex)
        glTexParameteri(GLenum(GL_TEXTURE_2D), GLenum(GL_TEXTURE_MIN_FILTER), GL_LINEAR)
        glTexParameteri(GLenum(GL_TEXTURE_2D), GLenum(GL_TEXTURE_MAG_FILTER), GL_LINEAR)
        glPixelStorei(GLenum(GL_UNPACK_ALIGNMENT), 1)
        glTexImage2D(GLenum(GL_TEXTURE_2D), 0, GL_RGBA8, w, h, 0, GLenum(GL_RGBA), GLenum(GL_UNSIGNED_BYTE), pixels)

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
        glBlendFunc(GLenum(GL_SRC_ALPHA), GLenum(GL_ONE_MINUS_SRC_ALPHA))
        glUseProgram(program)
        glUniform1i(glGetUniformLocation(program, "tex"), 0)
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

    /// 把一张图片画到当前 framebuffer 的指定像素矩形（专辑封面用；与文字同一套贴图/着色器）
    func drawImage(_ image: CGImage, in rect: CGRect, viewSize: CGSize) {
        guard rect.width > 1, rect.height > 1 else { return }
        setup()
        let w = GLsizei(image.width), h = GLsizei(image.height)
        var pixels = [UInt8](repeating: 0, count: Int(w) * Int(h) * 4)
        guard let ctx = CGContext(data: &pixels, width: Int(w), height: Int(h), bitsPerComponent: 8,
                                  bytesPerRow: Int(w) * 4, space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: Double(w), height: Double(h)))

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
        glBlendFunc(GLenum(GL_SRC_ALPHA), GLenum(GL_ONE_MINUS_SRC_ALPHA))
        glUseProgram(program)
        glUniform1i(glGetUniformLocation(program, "tex"), 0)
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

    /// 富文本 → CGImage（透明底）
    private func renderTextImage(lines: [(text: NSAttributedString, highlightPrefix: Int)], size: CGSize) -> CGImage? {
        let w = Int(size.width), h = Int(size.height)
        guard w > 0, h > 0,
              let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        let ns = NSGraphicsContext(cgContext: ctx, flipped: false)
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = ns
        let style = NSMutableParagraphStyle()
        style.alignment = .center
        style.lineBreakMode = .byTruncatingTail
        var y = size.height
        for (line, _) in lines {
            let centered = NSMutableAttributedString(attributedString: line)
            centered.addAttribute(.paragraphStyle, value: style,
                                  range: NSRange(location: 0, length: centered.length))
            let bounds = centered.boundingRect(with: NSSize(width: size.width, height: size.height),
                                               options: [.usesLineFragmentOrigin])
            let h = max(bounds.height, 1)
            y -= h
            let rect = NSRect(x: 0, y: y, width: size.width, height: h)

            // 描边：把整行在 8 个方向各画一次（实心深色），再画正文 —— 与 Qt 端 drawOutlined 同一做法，
            // 好处是字形内部保持实心（用 strokeWidth 会让字变"空心"）
            let font = centered.attribute(.font, at: 0, effectiveRange: nil) as? NSFont
            let radius = max(1.5, (font?.pointSize ?? 16) * 0.05)
            let outline = NSMutableAttributedString(attributedString: centered)
            outline.addAttribute(.foregroundColor, value: NSColor(calibratedWhite: 0, alpha: 0.85),
                                 range: NSRange(location: 0, length: outline.length))
            for dx in [-radius, 0, radius] {
                for dy in [-radius, 0, radius] where !(dx == 0 && dy == 0) {
                    outline.draw(in: rect.offsetBy(dx: dx, dy: dy))
                }
            }
            centered.draw(in: rect)
        }
        NSGraphicsContext.restoreGraphicsState()
        return ctx.makeImage()
    }
}
