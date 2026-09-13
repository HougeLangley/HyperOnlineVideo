import AppKit
import Cmpv
import OpenGL.GL3  // glReadPixels / GL_* 常量（抓帧取证用）

/// libmpv 渲染视图：NSOpenGLView + mpv 的 OpenGL render API。
///
/// 与 Linux(Qt) 端 `MpvWidget` 是同一套架构：
///   App 层持有 render context → mpv 通知"有新帧"→ 视图标记需要重绘 → draw 里 render。
/// 这样后续的字幕/歌词/封面都在 draw 之后用 App 层绘制（Qt 端已验证该路线可行）。
final class MpvView: NSOpenGLView {
    private var mpv: OpaquePointer?
    private var rctx: OpaquePointer?
    /// 供 UI 显示的最近状态（自动化判据也读它）
    private(set) var lastStatus: String = "(未开始)"
    /// mpv 尚未就绪时先记下要播的地址（prepareOpenGL 之后才建 mpv）
    private var pendingURL: String?
    /// 字幕/歌词覆盖层（App 层绘制，与 Qt 端同一架构）
    let overlay = Overlay()
    private let glText = GlText()
    private var pendingAudio: String?
    /// 累计渲染帧数：证明"画面真的在画"，不依赖截图权限
    private(set) var renderedFrames = 0

    override init(frame frameRect: NSRect) {
        let attrs: [NSOpenGLPixelFormatAttribute] = [
            UInt32(NSOpenGLPFAAccelerated),
            UInt32(NSOpenGLPFADoubleBuffer),
            UInt32(NSOpenGLPFAColorSize), 24,
            UInt32(NSOpenGLPFAOpenGLProfile), UInt32(NSOpenGLProfileVersion3_2Core),
            0,
        ]
        let fmt = NSOpenGLPixelFormat(attributes: attrs) ?? NSOpenGLPixelFormat()
        super.init(frame: frameRect, pixelFormat: fmt)!
        wantsBestResolutionOpenGLSurface = true   // Retina 下按物理像素渲染
    }

    required init?(coder: NSCoder) { fatalError("不支持 storyboard") }

    // MARK: - mpv 初始化

    /// 必须在 GL 上下文就绪后调用（prepareOpenGL）
    private func setupMpv() {
        guard let ctx = openGLContext else {
            FileHandle.standardError.write("MpvView: 没有 OpenGL 上下文\n".data(using: .utf8)!)
            return
        }
        ctx.makeCurrentContext()

        guard let handle = mpv_create() else {
            FileHandle.standardError.write("mpv_create 失败\n".data(using: .utf8)!)
            return
        }
        mpv = handle
        // 与 Qt 端保持一致：禁用 mpv 自带的 ytdl 钩子（交给 App 层解析），字体也用 App 层画
        mpv_set_option_string(handle, "ytdl", "no")
        mpv_set_option_string(handle, "vo", "libmpv")
        mpv_set_option_string(handle, "hwdec", "auto-safe")
        mpv_set_option_string(handle, "keep-open", "yes")
        mpv_set_option_string(handle, "terminal", "no")
        mpv_set_option_string(handle, "audio-display", "no")
        // 单一渲染来源：禁止 mpv 自己加载/渲染外挂字幕（否则与 App 层叠加层重复，
        // 而且会掩盖叠加层的真实位置 —— Qt 端踩过同一个坑）
        mpv_set_option_string(handle, "sub-auto", "no")
        mpv_set_option_string(handle, "sid", "no")

        let initRc = mpv_initialize(handle)
        guard initRc >= 0 else {
            FileHandle.standardError.write("mpv_initialize 失败: \(initRc)\n".data(using: .utf8)!)
            return
        }

        // OpenGL render API（Swift 看不到 C 宏，按文档写字符串 "opengl"）
        var glInit = mpv_opengl_init_params(
            get_proc_address: { _, name in
                guard let name else { return nil }
                // RTLD_DEFAULT = (void *)-2
                return dlsym(UnsafeMutableRawPointer(bitPattern: -2), name)
            },
            get_proc_address_ctx: nil
        )

        let apiType = strdup("opengl")!   // Swift 看不到 C 宏，按文档写字符串
        let apiOk: Int32 = withUnsafeMutablePointer(to: &glInit) { glPtr in
            var params: [mpv_render_param] = [
                mpv_render_param(type: MPV_RENDER_PARAM_API_TYPE, data: UnsafeMutableRawPointer(apiType)),
                mpv_render_param(type: MPV_RENDER_PARAM_OPENGL_INIT_PARAMS,
                                 data: UnsafeMutableRawPointer(glPtr)),
                mpv_render_param(type: MPV_RENDER_PARAM_INVALID, data: nil),
            ]
            return mpv_render_context_create(&rctx, handle, &params)
        }
        free(apiType)
        guard apiOk >= 0, rctx != nil else {
            FileHandle.standardError.write("mpv_render_context_create 失败: \(apiOk)\n".data(using: .utf8)!)
            return
        }

        // mpv 有新帧 → 主线程重绘（跨线程必须回主队列）
        mpv_render_context_set_update_callback(rctx, { ctxPtr in
            guard let ctxPtr else { return }
            let view = Unmanaged<MpvView>.fromOpaque(ctxPtr).takeUnretainedValue()
            DispatchQueue.main.async { view.needsDisplay = true }
        }, Unmanaged.passUnretained(self).toOpaque())

        FileHandle.standardError.write("mpv render context ready（client API 版本 \(mpv_client_api_version())）\n".data(using: .utf8)!)
        if let pendingURL {
            self.pendingURL = nil
            let audio = pendingAudio ?? ""
            pendingAudio = nil
            playResolved(video: pendingURL, audio: audio)
            FileHandle.standardError.write("开始播放（就绪后补发）: \(pendingURL)\(audio.isEmpty ? "" : " + 音轨")\n".data(using: .utf8)!)
        }
    }

    override func prepareOpenGL() {
        super.prepareOpenGL()
        if mpv == nil { setupMpv() }
    }

    // MARK: - 播放控制

    /// 执行 mpv 命令（数组必须以 nil 结尾：Qt 端曾因漏终止符出现段错误）
    private func command(_ parts: [String]) {
        guard let mpv else { return }
        var ptrs: [UnsafePointer<CChar>?] = parts.map { UnsafePointer(strdup($0)!) }
        ptrs.append(nil)
        defer { for p in ptrs { if let p { free(UnsafeMutablePointer(mutating: p)) } } }
        mpv_command(mpv, &ptrs)
    }

    func play(_ url: String) {
        playResolved(video: url, audio: "")
    }

    /// 视频流 + 音轨分离挂载（ADR-002）：先 loadfile 视频，再 audio-add 音轨。
    /// 与 Qt 端一致：命令数组必须以 nil 结尾（否则段错误）。
    func playResolved(video: String, audio: String) {
        guard mpv != nil else {          // 还没就绪：排队，等 render context 起来再发
            pendingURL = video
            pendingAudio = audio
            return
        }
        command(["loadfile", video])
        guard !audio.isEmpty else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { [weak self] in
            self?.command(["audio-add", audio, "select"])
        }
    }

    func stop() { command(["stop"]) }

    private func stringProperty(_ name: String) -> String {
        guard let mpv else { return "" }
        guard let raw = mpv_get_property_string(mpv, name) else { return "" }
        defer { mpv_free(raw) }
        return String(cString: raw)
    }

    private func doubleProperty(_ name: String) -> Double {
        guard let mpv else { return 0 }
        var v: Double = 0
        withUnsafeMutablePointer(to: &v) { ptr in
            _ = mpv_get_property(mpv, name, MPV_FORMAT_DOUBLE, UnsafeMutableRawPointer(ptr))
        }
        return v
    }

    private func flagProperty(_ name: String) -> Bool {
        guard let mpv else { return false }
        var v: Int32 = 0
        withUnsafeMutablePointer(to: &v) { ptr in
            _ = mpv_get_property(mpv, name, MPV_FORMAT_FLAG, UnsafeMutableRawPointer(ptr))
        }
        return v != 0
    }

    /// 在 mpv 画面之上画字幕/歌词（App 层渲染；画完就能被抓帧取到）
    private var drawCount = 0
    var diag = false                             // --diag 时打印前几帧的绘制参数（排查用）

    private func drawOverlay(scale: CGFloat) {
        drawCount += 1
        // 专辑封面：画在左上角（与 Qt 端同位置）；只有音乐场景才会设置它
        if let cover = coverImage?.cgImage(forProposedRect: nil, context: nil, hints: nil) {
            let side = min(Double(bounds.height * scale) / 3.0, 200.0)
            let rect = CGRect(x: 18 * scale, y: Double(bounds.height * scale) - side - 18 * scale,
                              width: side, height: side)
            glText.drawImage(cover, in: rect, viewSize: CGSize(width: bounds.width * scale,
                                                               height: bounds.height * scale))
        }
        if diag, drawCount <= 8 {
            var vp = [GLint](repeating: 0, count: 4)
            glGetIntegerv(GLenum(GL_VIEWPORT), &vp)
            let isFlipped = openGLContext?.view?.isFlipped ?? false
            Config.log("[diag] draw #\(drawCount) bounds=\(Int(bounds.width))x\(Int(bounds.height)) scale=\(scale) viewport=\(vp[0]),\(vp[1]),\(vp[2]),\(vp[3]) flipped=\(isFlipped)")
        }
        let lines = overlay.lines(at: position(), viewSize: CGSize(width: bounds.width * scale,
                                                                  height: bounds.height * scale))
        if diag, drawCount % 20 == 0 {
            Config.log("[diag] overlay cues=\(overlay.cues.count) hidden=\(overlay.hidden) karaoke=\(overlay.karaoke) "
                       + "pos=\(String(format: "%.1f", position())) lines=\(lines.count) "
                       + "bounds=\(Int(bounds.width))x\(Int(bounds.height)) scale=\(scale)")
        }
        guard !lines.isEmpty else { return }
        let fontPx = max(14.0, Double(bounds.height * scale) * 0.035) * overlay.fontScale
        let lineH = fontPx * 1.45
        let totalH = lineH * Double(lines.count)
        let width = Double(bounds.width * scale) * 0.94
        let x = (Double(bounds.width * scale) - width) / 2
        // 歌词居中面板；字幕贴底（与 Qt 端一致）
        let y = overlay.karaoke ? (Double(bounds.height * scale) - totalH) * 0.45
                                : Double(bounds.height * scale) * 0.08
        let rect = CGRect(x: x, y: y, width: width, height: totalH)
        let tuples = lines.map { (text: $0, highlightPrefix: 0) }
        glText.draw(lines: tuples, in: rect, viewSize: CGSize(width: bounds.width * scale,
                                                              height: bounds.height * scale))
    }

    /// 读取并更新状态（0.5 秒一次，供界面显示与自动化判据）
    func refreshStatus() {
        let dur = doubleProperty("duration")
        let pos = doubleProperty("time-pos")
        let idle = flagProperty("core-idle")
        let title = stringProperty("media-title")
        let ao = stringProperty("audio-out-params/format")
        if dur > 0 {
            lastStatus = String(format: "%@  %.1fs / %.1fs%@%@", title,
                                pos, dur, idle ? "  [idle]" : "", ao.isEmpty ? "" : "  AO=\(ao)")
        } else {
            lastStatus = title.isEmpty ? "(空)" : "\(title)  时长未知"
        }
    }

    /// 把当前 GL 帧读回成 NSImage（UI 抓图与取证共用）
    func grabImage() -> NSImage? {
        guard let gl = openGLContext else { return nil }
        gl.makeCurrentContext()
        let scale = window?.backingScaleFactor ?? 1.0
        let w = Int(Double(bounds.width) * scale), h = Int(Double(bounds.height) * scale)
        guard w > 1, h > 1 else { return nil }
        var buf = [UInt8](repeating: 0, count: w * h * 4)
        glReadPixels(0, 0, GLsizei(w), GLsizei(h), GLenum(GL_RGBA), GLenum(GL_UNSIGNED_BYTE), &buf)
        // GL 原点在左下 → 上下翻转后再写 PNG
        var flipped = [UInt8](repeating: 0, count: w * h * 4)
        for y in 0..<h {
            let src = (h - 1 - y) * w * 4
            let dst = y * w * 4
            flipped[dst..<(dst + w * 4)] = buf[src..<(src + w * 4)]
        }
        guard let provider = CGDataProvider(data: Data(flipped) as CFData),
              let cg = CGImage(width: w, height: h, bitsPerComponent: 8, bitsPerPixel: 32,
                               bytesPerRow: w * 4, space: CGColorSpaceCreateDeviceRGB(),
                               bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                               provider: provider, decode: nil, shouldInterpolate: false,
                               intent: .defaultIntent) else { return nil }
        let img = NSImage(size: NSSize(width: w, height: h))
        img.addRepresentation(NSBitmapImageRep(cgImage: cg))
        return img
    }

    /// 把当前 GL 帧读回并存成 PNG（验证"画面上真有内容"，不依赖系统截图权限）
    @discardableResult
    func dumpFrame(to path: String) -> Bool {
        guard let img = grabImage(), let tiff = img.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:]) else { return false }
        return (try? png.write(to: URL(fileURLWithPath: path))) != nil
    }

    /// 音频输出参数（证明"出声"：格式/采样率/声道）
    func audioOut() -> String {
        let fmt = stringProperty("audio-out-params/format")
        if fmt.isEmpty { return "" }
        let sr = doubleProperty("audio-out-params/samplerate")
        let ch = doubleProperty("audio-out-params/channel-count")
        return String(format: "%@ %.0fHz %.0fch", fmt, sr, ch)
    }

    /// 视频分辨率（证明"出画"）
    func videoSize() -> String {
        let w = doubleProperty("width"), h = doubleProperty("height")
        return w > 0 ? String(format: "%.0fx%.0f", w, h) : "无视频流"
    }

    func position() -> Double { doubleProperty("time-pos") }
    func seek(to sec: Double) { command(["seek", String(format: "%.3f", sec), "absolute"]) }
    func volume() -> Int { Int(doubleProperty("volume")) }
    func setVolume(_ v: Int) { command(["set", "volume", String(v)]) }
    func speed() -> Double { doubleProperty("speed") }
    func setSpeed(_ v: Double) { command(["set", "speed", String(format: "%.2f", v)]) }
    /// 可选字幕轨（本地同名外挂 + 在线轨），C 键循环切换
    var tracks: [SubtitleTrack] = []
    func togglePause() { command(["cycle", "pause"]) }
    /// 媒体键用：pause=true 时确保暂停，false 时确保播放
    func togglePauseIfPaused(_ pause: Bool) {
        let isPaused = paused()
        if pause != isPaused { command(["cycle", "pause"]) }
    }
    func paused() -> Bool { flagProperty("pause") }
    /// 叠加层心跳：mpv 只在"有新视频帧"时通知重绘，**纯音频播放时几乎没有通知**，
    /// 于是歌词/字幕不会更新（实测：帧数停在 3）。有字幕时用 10Hz 定时器自己驱动重绘。
    private var overlayTimer: Timer?
    func updateOverlayTimer() {
        let need = !overlay.cues.isEmpty
        if need, overlayTimer == nil {
            overlayTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
                guard let self else { return }
                if self.overlay.cues.isEmpty { self.updateOverlayTimer(); return }
                self.needsDisplay = true
            }
        } else if !need, overlayTimer != nil {
            overlayTimer?.invalidate()
            overlayTimer = nil
        }
    }

    /// 专辑封面（音乐场景；Phase 5.3 先记录尺寸用于状态显示，绘制在 5.4 里接上）
    private(set) var coverImage: NSImage?
    func setCoverImage(_ img: NSImage) { coverImage = img; needsDisplay = true }
    func duration() -> Double { doubleProperty("duration") }
    func mediaTitle() -> String { stringProperty("media-title") }

    // MARK: - 渲染

    override func draw(_ dirtyRect: NSRect) {
        guard let rctx, let gl = openGLContext else { return }
        gl.makeCurrentContext()
        gl.update()

        let scale = window?.backingScaleFactor ?? 1.0
        var fbo = mpv_opengl_fbo(fbo: 0,
                                 w: Int32(Double(bounds.width) * scale),
                                 h: Int32(Double(bounds.height) * scale),
                                 internal_format: 0)
        // 实测：NSOpenGLView 与 Qt 的 QOpenGLWidget 一样是"翻转"目标（左上原点）
        // → 必须 FLIP_Y=1，否则画面上下颠倒（用"上半红/下半蓝"的素材判定出来的）
        var flip: Int32 = 1
        withUnsafeMutablePointer(to: &fbo) { fboPtr in
            withUnsafeMutablePointer(to: &flip) { flipPtr in
                var params: [mpv_render_param] = [
                    mpv_render_param(type: MPV_RENDER_PARAM_OPENGL_FBO,
                                     data: UnsafeMutableRawPointer(fboPtr)),
                    mpv_render_param(type: MPV_RENDER_PARAM_FLIP_Y,
                                     data: UnsafeMutableRawPointer(flipPtr)),
                    mpv_render_param(type: MPV_RENDER_PARAM_INVALID, data: nil),
                ]
                mpv_render_context_render(rctx, &params)
                renderedFrames += 1
            }
        }
        drawOverlay(scale: scale)
        gl.flushBuffer()
    }
}
