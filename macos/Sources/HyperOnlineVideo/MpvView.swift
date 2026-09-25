import AppKit
import Cmpv
import OpenGL.GL3  // glReadPixels / GL_* 常量（抓帧取证用）

/// libmpv 渲染视图：NSOpenGLView + mpv 的 OpenGL render API。
///
/// 与 Linux(Qt) 端 `MpvWidget` 是同一套架构：
///   App 层持有 render context → mpv 通知"有新帧"→ 视图标记需要重绘 → draw 里 render。
/// 这样后续的字幕/歌词/封面都在 draw 之后用 App 层绘制（Qt 端已验证该路线可行）。
final class MpvView: NSOpenGLView {
    /// 让播放画面能拿到键盘焦点（点一下画面，单键快捷键就生效；在搜索框里打字时不受影响）
    override var acceptsFirstResponder: Bool { true }
    /// 双击画面回调（进入/退出"纯视频全屏"）
    var onDoubleClick: (() -> Void)?
    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        if event.clickCount == 2 { onDoubleClick?() }
        super.mouseDown(with: event)
    }
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
            UInt32(NSOpenGLPFAAlphaSize), 8,          // 透明表面：圆角要透出窗口背景（毛玻璃）
            UInt32(NSOpenGLPFAOpenGLProfile), UInt32(NSOpenGLProfileVersion3_2Core),
            0,
        ]
        let fmt = NSOpenGLPixelFormat(attributes: attrs) ?? NSOpenGLPixelFormat()
        super.init(frame: frameRect, pixelFormat: fmt)!
        wantsBestResolutionOpenGLSurface = true   // Retina 下按物理像素渲染
        applyCornerRadius()
    }

    // MARK: - 播放区圆角
    /// 播放区四角圆角（用户要求：参考图那样）。
    /// 关键：**必须配合 surfaceOpacity=0 的透明 GL 表面**，layer 的 masksToBounds 才会裁剪 GL 画面。
    /// 实测证据：表面不透明时，边框能圆、画面照旧方角（GL surface 独立合成，不受 layer 裁剪）；
    /// 打开按像素 alpha 合成后，同一份 layer 圆角代码立刻生效（平滑圆角）。
    /// 全屏时置 0：画面铺满整屏，圆角只会让屏幕四角露黑。
    private var playerRadius: CGFloat = 12
    func setPlayerCornerRadius(_ r: CGFloat) {
        playerRadius = r
        applyCornerRadius()
        Config.log("[UI] 播放区圆角 = \(Int(r))pt")
    }
    private func applyCornerRadius() {
        wantsLayer = true
        layer?.cornerRadius = playerRadius
        layer?.masksToBounds = playerRadius > 0.5
        layer?.cornerCurve = .continuous          // 连续曲率（macOS 原生外观，比圆弧更顺眼）
    }
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        applyCornerRadius()                       // 换窗口（进/出全屏、PiP）后 layer 可能重建
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
        // 渲染后端：**macOS 上必须固定用 libmpv** ✓
        //
        // 为什么不能用 gpu-next（2026-09-22 实测 bug ✗ 有窗口枚举铁证 ✓）：
        //   `vo=gpu-next` 是**窗口式 vo** —— mpv 会**自己新建一个窗口** ✗，
        //   于是视频跑到独立窗口、主窗口播放区全黑 ✗（用户复现截图 + CGWindowList 实测：
        //   设 gpu-next 时出现标题为 "<文件> - mpv" 的第二个窗口 ✓；设 libmpv 时只有 1 个窗口 ✓）。
        //   而 `vo=libmpv` 才是"渲染 API / 不建窗口"的那个 vo ✓ —— 只有它能嵌进本视图 ✓。
        // 三端实测更正（2026-09-22 二轮 ✓ 原文关于 Linux 的表述是**错的** ✗ 已改）：
        //   Linux **也没有** --wid（实测 grep 零命中 ✓）→ 其 gpu-next 同样是窗口式 vo ✗，
        //   且实测**偶发 SIGSEGV**（coredumpctl 栈 #2~#12 全在 libmpv.so.2 的 vo 线程 ✓）
        //   → Linux 已同步改为固定 libmpv ✓（键保留 ✓ 见文档 46/47）。
        //   Android 走 mpv-android 的 surface 嵌入 ✓ 是三端里唯一真正支持 gpu-next 的端 ✓（故其固定 gpu-next ✓）。
        // 总规律 ✓：能让视频"嵌进宿主控件"的只有 **libmpv(渲染 API)** 与 **Android surface** 两条路 ✓，
        //   窗口式 vo（gpu/gpu-next）在三端都不可用 ✗（macOS 自建窗口 / Linux 崩溃 / Android 原生崩溃 ✓）。
        // 本端**不再提供** video.gpuNext 设置项（2026-09-22 用户决定 ✓ 全仓删除该键 ✓ 见文档 48）。
        mpv_set_option_string(handle, "vo", "libmpv")
        mpv_set_option_string(handle, "hwdec", "auto-safe")
        mpv_set_option_string(handle, "keep-open", "yes")
        // 缓存策略在**每次加载前**按来源设置（见 prepareCacheOptions）：
        // 网络流要足够大的缓冲抗抖动；**本地文件不要**（大缓存 + 本地 seek 会出现"画面冻住、声音继续/错位"）。
        prepareCacheOptions(isNetwork: false)
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
        if apiOk < 0 || rctx == nil {                    // 渲染上下文创建失败（与 vo 无关 ✓）
            // 注：这里**没有**可回退的 vo ✗ —— macOS 上能嵌入的只有 libmpv ✓，
            // 创建失败意味着"本视图无法显示画面" ✗ → 明确报错，便于排查（不再假装回退 ✗）
            Config.log("[UI] ⚠️ OpenGL 渲染上下文创建失败（apiOk=\(apiOk) rctx=\(rctx == nil ? "nil" : "ok")）→ 本视图将无画面 ✗")
        }
        guard apiOk >= 0, rctx != nil else {
            FileHandle.standardError.write("mpv_render_context_create 失败: \(apiOk)\n".data(using: .utf8)!)
            return
        }

        // mpv 有新帧 → 主线程重绘（跨线程必须回主队列）
        startSnapshotter()          // 后台采集属性快照（主线程从此不再碰 mpv 属性）
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
        // 透明表面：surfaceOpacity=0 → 窗口按**像素 alpha** 合成这张 GL 表面，
        // 于是"把角落像素清零"就能透出窗口背景（毛玻璃），实现圆角卡片。
        // 注意：layer 的 masksToBounds 对 NSOpenGLView 的 surface **无效**（实测：边框能圆、画面照旧方角）。
        var opacity: GLint = 0
        openGLContext?.setValues(&opacity, for: .surfaceOpacity)
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
    /// 流媒体需要的 Referer（B站 的音频 CDN 对 Referer 很敏感：缺了会 403 → "有画面没声音"）
    static func referer(for url: String) -> String {
        let low = url.lowercased()
        if low.contains("bilivideo") || low.contains("bilibili") || low.contains("hdslb") { return "https://www.bilibili.com" }
        if low.contains("googlevideo") || low.contains("youtube") { return "https://www.youtube.com" }
        if low.contains("qq.com") || low.contains("qqmusic") { return "https://y.qq.com" }
        if low.contains("music.163") || low.contains("126.net") { return "https://music.163.com" }
        return ""
    }

    /// 按来源设置 mpv 缓存选项（必须在 loadfile **之前**调用才生效）
    func prepareCacheOptions(isNetwork: Bool) {
        guard let mpv else { return }
        if isNetwork {
            mpv_set_option_string(mpv, "cache", "yes")
            mpv_set_option_string(mpv, "cache-secs", "60")
            mpv_set_option_string(mpv, "demuxer-max-bytes", "64MiB")
            mpv_set_option_string(mpv, "demuxer-max-back-bytes", "16MiB")
        } else {
            // 本地文件：关闭大缓存（seek 走直读，最快也最稳）
            mpv_set_option_string(mpv, "cache", "no")
            mpv_set_option_string(mpv, "demuxer-max-bytes", "8MiB")
            mpv_set_option_string(mpv, "demuxer-max-back-bytes", "4MiB")
        }
    }

    func playResolved(video: String, audio: String) {
        guard mpv != nil else {          // 还没就绪：排队，等 render context 起来再发
            pendingURL = video
            pendingAudio = audio
            return
        }
        // 先设 Referer（对随后的 loadfile 与 audio-add 都生效）
        prepareCacheOptions(isNetwork: (video.isEmpty ? audio : video).hasPrefix("http"))   // 加载前设定缓存策略
        let ref = Self.referer(for: video.isEmpty ? audio : video)
        if let mpv { mpv_set_option_string(mpv, "http-header-fields", ref.isEmpty ? "" : "Referer: " + ref) }
        if !ref.isEmpty { Config.log("[播放] 设置 Referer=\(ref)（媒体流需要）") }
        command(["loadfile", video])
        // keep-open=yes 会让新文件继承"已暂停"状态 → 连播第二条会停在 0:00 不动（Qt 端踩过同一个坑）。
        // 这里显式取消暂停；无条件执行（暂停状态下用户点别的内容也应该开播）。
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) { [weak self] in
            self?.command(["set", "pause", "no"])
        }
        guard !audio.isEmpty else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { [weak self] in
            self?.command(["audio-add", audio, "select"])
            Config.log("[AUDIO] 已向 mpv 添加独立音轨（长度=\(audio.count) 字符）")
            // 音轨看门狗：部分网络/CDN 情况下 audio-add 可能没真正生效（用户实测「有画面没声音」）
            // → 10 秒后若仍检测不到已加载音轨，自动重挂一次（幂等；成功则不动作）
            DispatchQueue.main.asyncAfter(deadline: .now() + 10) { [weak self] in
                guard let self else { return }
                let st = self.audioStatus()
                // 两种失败形态都要救：① 根本没挂上（已加载=无）② 挂上了却没进输出（输出中=无）
                // ② 常见于"连播两条视频/先放音乐再放视频"——旧音轨残留占位（用户实测反馈）。
                guard st.contains("已加载=无") || st.contains("输出中=无") else { return }
                Config.log("[AUDIO] 看门狗：10 秒仍无音轨 → 重新挂载（\(st)）")
                self.command(["audio-add", audio, "select"])
                // 5 秒后再看一次，仍无输出就把旧音轨全部摘掉后重挂（避免残留音轨占着选中位）
                DispatchQueue.main.asyncAfter(deadline: .now() + 5) { [weak self] in
                    guard let self else { return }
                    let st2 = self.audioStatus()
                    guard st2.contains("已加载=无") || st2.contains("输出中=无") else { return }
                    Config.log("[AUDIO] 看门狗二次：仍未输出 → audio-remove 后重挂（\(st2)）")
                    self.command(["audio-remove"])
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in
                        self?.command(["audio-add", audio, "select"])
                    }
                }
            }
        }
    }

    func stop() { command(["stop"]) }
    /// 是否播放到结尾（mpv 的 eof-reached；keep-open=yes 下不会自动卸载，靠这个判断"播完了"）
    func eofReached() -> Bool { snapshot().eof }
    /// 音频输出状态（用于排查"有画面没声音"）：audio-params=已加载的音轨，audio-out-params=正在输出的音频
    func audioStatus() -> String {
        let loaded = stringProperty("audio-params")
        let out = stringProperty("audio-out-params")
        let aid = stringProperty("aid")
        let mute = flagProperty("mute") ? "静音" : "正常"
        return "音轨=\(aid.isEmpty ? "无" : aid) 已加载=\(loaded.isEmpty ? "无" : loaded) 输出中=\(out.isEmpty ? "无" : out) 音量=\(Int(doubleProperty("volume")))%（\(mute)）"
    }

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
        let viewW = Double(bounds.width * scale), viewH = Double(bounds.height * scale)
        let viewSizePx = CGSize(width: bounds.width * scale, height: bounds.height * scale)
        // 音乐模式 = 歌词面板 + 有封面（视频字幕、无封面的歌词仍走原来的排版）
        let musicMode = overlay.karaoke && coverImage != nil
        // ── 音乐背景（**必须在封面之前画**，否则会把封面盖掉 —— 实测踩过）：──
        // 主题色渐变打底 → 虚化放大的封面半透明叠上（磨砂玻璃质感）→ 之后才是清晰的封面/标题/歌词
        if musicMode {
            if let grad = musicTheme?.gradient?.cgImage(forProposedRect: nil, context: nil, hints: nil) {
                glText.drawImage(grad, in: CGRect(x: 0, y: 0, width: viewW, height: viewH), viewSize: viewSizePx)
            }
            if let bd = musicTheme?.backdrop?.cgImage(forProposedRect: nil, context: nil, hints: nil) {
                let side = max(viewW, viewH)                     // 等比放大到"铺满"（多出部分自然裁掉）
                glText.drawImage(bd, in: CGRect(x: (viewW - side) / 2, y: (viewH - side) / 2,
                                                width: side, height: side),
                                 viewSize: viewSizePx, alpha: 0.55)
            }
        }
        // 专辑封面：靠左、**垂直居中**；音乐模式下加圆角（对齐 QQ 音乐封面观感）
        if let cover = coverImage?.cgImage(forProposedRect: nil, context: nil, hints: nil) {
            let side = min(viewH * 0.60, viewW * 0.30)      // 相对比例（不用绝对像素上限：Retina 下会显得很小）
            let rect = CGRect(x: viewW * 0.05, y: (viewH - side) / 2, width: side, height: side)
            glText.drawImage(cover, in: rect, viewSize: viewSizePx, cornerRadius: musicMode ? side * 0.06 : 0)
        }
        if diag, drawCount <= 8 {
            var vp = [GLint](repeating: 0, count: 4)
            glGetIntegerv(GLenum(GL_VIEWPORT), &vp)
            let isFlipped = openGLContext?.view?.isFlipped ?? false
            Config.log("[diag] draw #\(drawCount) bounds=\(Int(bounds.width))x\(Int(bounds.height)) scale=\(scale) viewport=\(vp[0]),\(vp[1]),\(vp[2]),\(vp[3]) flipped=\(isFlipped)")
        }
        let lines = overlay.lines(at: position(), viewSize: viewSizePx)
        if diag, drawCount % 20 == 0 {
            Config.log("[diag] overlay cues=\(overlay.cues.count) hidden=\(overlay.hidden) karaoke=\(overlay.karaoke) "
                       + "pos=\(String(format: "%.1f", position())) lines=\(lines.count) "
                       + "bounds=\(Int(bounds.width))x\(Int(bounds.height)) scale=\(scale)")
        }
        let fontPx = max(14.0, viewH * 0.035) * overlay.fontScale
        let tuples = lines.map { (text: $0, highlightPrefix: 0) }

        if musicMode {
            // ── 音乐播放界面（对齐 QQ 音乐观感）──
            // 左：封面；右：顶部大字标题（歌名 — 歌手）+ 其下歌词（上一行/当前行+译文/下一行），**一律左对齐**
            // 注意：标题**不受"当前没有歌词行"影响**（前奏/间奏时 lines 为空，标题仍要显示）
            // 版面按参考图定：
            //  · 标题与歌词**共用同一个左边界**（参考图红箭头指出：标题左边界=歌词块左边界）
            //  · 标题紧贴歌词块上方 —— 二者是**一个整体**，一起居中，不再各钉一头（之前标题孤零零在顶部）
            //  · 去掉光晕与粗描边，标题/歌词统一"白字 + 柔和投影"，颜色与质感完全一致
            let rx = viewW * 0.44, rw = viewW * 0.52
            let vpad = viewH * 0.07
                          let base0 = max(15.0, viewH * 0.040) * overlay.fontScale         // 与 Overlay 内部同一基准
              let sh = NSShadow()
              sh.shadowBlurRadius = base0 * 0.18
              sh.shadowOffset = NSSize(width: 0, height: -base0 * 0.03)
              sh.shadowColor = NSColor(calibratedWhite: 0, alpha: 0.55)
  
              guard !musicTitle.isEmpty || !lines.isEmpty else { return }      // 前奏且没标题：只留封面
              // 自适应字号（2026-09-25 ✗→✓ 用户实测"长歌词会超出播放窗口"）：
              //   标题+歌词总高超出可用高 → 逐档缩字号（至 0.55×）；Linux 端 SubtitleOverlay 同款策略（三端对齐 ✓）
              var base = base0
              var titleLine: NSAttributedString?
              var titleH = 0.0
              var titleGap = 0.0
              var blockH = 0.0
              let availH = max(80.0, viewH - vpad * 2)
              while true {
                  let g = base * 0.35
                  var tl: NSAttributedString?
                  var th = 0.0
                  if !musicTitle.isEmpty {
                      // 长标题自动缩字号（最多缩到 0.72×），避免被截断成"…"（参考图标题是完整的）
                      var tSize = base * 1.26
                      var line = Self.titleAttr(musicTitle, size: tSize, shadow: sh)
                      var w = line.size().width
                      if w > rw {
                          tSize = max(base * 1.26 * 0.72, tSize * rw / w)
                          line = Self.titleAttr(musicTitle, size: tSize, shadow: sh)
                          w = min(line.size().width, rw)
                      }
                      tl = line
                      th = GlText.measure([line], width: w)
                  }
                  let bh = lines.isEmpty ? 0.0 : GlText.measure(lines, width: rw, lineGap: g, wrap: true)
                  titleLine = tl
                  titleH = th
                  titleGap = (th > 0 && bh > 0) ? th * 0.45 : 0
                  blockH = bh
                  if th + titleGap + bh <= availH || base <= base0 * 0.55 { break }
                  base *= 0.86
              }
              let gap = base * 0.35
              let unitH = titleH + titleGap + blockH
            // 整体（标题+歌词）垂直居中；太高时优先保住顶部不越界
            var unitTop = (viewH + unitH) / 2
            unitTop = min(unitTop, viewH - vpad)
            unitTop = max(unitTop, vpad + unitH)
            if let titleLine, titleH > 0 {
                glText.draw(lines: [(titleLine, 0)],
                            in: CGRect(x: rx, y: unitTop - titleH, width: rw, height: titleH),
                            viewSize: viewSizePx, align: .left, outlineScale: 0, shadow: sh)
            }
            if blockH > 0 {
                let y = unitTop - titleH - titleGap - blockH
                glText.draw(lines: tuples, in: CGRect(x: rx, y: y, width: rw, height: blockH), viewSize: viewSizePx,
                            align: .left, lineGap: gap, wrap: true)
            }
            return
        }
        guard !lines.isEmpty else { return }

        // ── 字幕 / 无封面歌词：保持原排版（字幕贴底；歌词居中，不加光晕）──
        let totalH = fontPx * 1.45 * Double(lines.count)
        let width = viewW * 0.94
        let rect = CGRect(x: (viewW - width) / 2, y: overlay.karaoke ? (viewH - totalH) / 2 : viewH * 0.08,
                          width: width, height: totalH)
        glText.draw(lines: tuples, in: rect, viewSize: viewSizePx, glowColor: nil, glowRadius: 0)
    }

    // MARK: - 属性快照（关键修复：主线程绝不直接读 mpv 属性）
    //
    // 实测（看门狗抓到）：视频起播时主线程连续卡顿 5 秒 × 5 次。
    // 原因：mpv 的属性读取（time-pos/duration/panscan…）会拿核心锁，**流媒体加载期间该锁可能被长时间持有**
    //（网络 demux 阻塞），而我们的 tick 每 0.5 秒就在主线程读一遍 → 整个 UI 冻结。
    // 解法（libmpv 嵌入的标准做法）：后台线程定时采集属性到快照，主线程只读快照（纯内存，永不阻塞）。
    struct Snapshot {
        var duration = 0.0, position = 0.0, paused = false, eof = false, panscan = 0.0
        var volume = 0, speed = 1.0, title = "", audioOut = "", videoSize = ""
    }
    private var snap = Snapshot()
    private let snapLock = NSLock()
    private var snapTimer: DispatchSourceTimer?
    private func store(_ s: Snapshot) { snapLock.lock(); snap = s; snapLock.unlock() }
    /// 主线程读这个：纯内存拷贝，绝不阻塞
    func snapshot() -> Snapshot { snapLock.lock(); defer { snapLock.unlock() }; return snap }

    private func captureSnapshot() {
        guard let mpv else { return }
        var s = Snapshot()
        s.duration = doubleProperty("duration")
        s.position = doubleProperty("time-pos")
        s.paused = flagProperty("pause")
        s.eof = flagProperty("eof-reached")
        s.panscan = doubleProperty("panscan")
        s.volume = Int(doubleProperty("volume"))
        s.speed = doubleProperty("speed")
        s.title = stringProperty("media-title")
        s.audioOut = stringProperty("audio-out-params/format")
        let w = doubleProperty("width"), h = doubleProperty("height")
        s.videoSize = w > 0 ? String(format: "%.0fx%.0f", w, h) : "无视频流"
        store(s)
        _ = mpv
    }

    /// 启动后台采集（0.2 秒一次，开销可忽略）
    private func startSnapshotter() {
        guard snapTimer == nil else { return }
        let q = DispatchQueue(label: "com.hougelangley.hov.mpv.snapshot", qos: .utility)
        let t = DispatchSource.makeTimerSource(queue: q)
        t.schedule(deadline: .now() + 0.2, repeating: 0.2)
        t.setEventHandler { [weak self] in self?.captureSnapshot() }
        t.resume()
        snapTimer = t
    }

    /// 读取并更新状态（0.5 秒一次，供界面显示与自动化判据）
    func refreshStatus() {
        // 同样只读快照（本函数由 ticker 在主线程调用，直接读 mpv 属性会在起播时冻结 UI）
        let snap = snapshot()
        let dur = snap.duration, pos = snap.position, title = snap.title, ao = snap.audioOut
        if dur > 0 {
            lastStatus = String(format: "%@  %.1fs / %.1fs%@", title, pos, dur, ao.isEmpty ? "" : "  AO=\(ao)")
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
    func videoSize() -> String { snapshot().videoSize }

    func position() -> Double { snapshot().position }
    func seek(to sec: Double) { command(["seek", String(format: "%.3f", sec), "absolute"]) }
    func volume() -> Int { snapshot().volume }
    func setVolume(_ v: Int) { command(["set", "volume", String(v)]) }
    func speed() -> Double { snapshot().speed }
    func setSpeed(_ v: Double) { command(["set", "speed", String(format: "%.2f", v)]) }
    // B7 全屏铺满：panscan=1 裁切填满（去掉比例差造成的黑边），0=保持比例
    func setPanscan(_ v: Double) { command(["set", "panscan", String(format: "%.2f", v)]) }
    var panscan: Double { snapshot().panscan }
    /// 可选字幕轨（本地同名外挂 + 在线轨），C 键循环切换
    var tracks: [SubtitleTrack] = []
    func togglePause() { command(["cycle", "pause"]) }
    /// 媒体键用：pause=true 时确保暂停，false 时确保播放
    func togglePauseIfPaused(_ pause: Bool) {
        let isPaused = paused()
        if pause != isPaused { command(["cycle", "pause"]) }
    }
    func paused() -> Bool { snapshot().paused }
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

    /// 标题属性（与歌词同色同质感：白字 + 柔和投影）
    static func titleAttr(_ text: String, size: CGFloat, shadow: NSShadow) -> NSAttributedString {
        NSAttributedString(string: text, attributes: [
            .font: NSFont.systemFont(ofSize: size, weight: .bold),
            .foregroundColor: NSColor(calibratedWhite: 0.99, alpha: 1),
            .shadow: shadow,
        ])
    }

    /// 等比缩到最长边 ≤max（CoverArt 大图降采样；返回 nil 表示无需/无法处理）
    static func downscaled(_ img: NSImage, max maxSide: CGFloat) -> NSImage? {
        let w = img.size.width, h = img.size.height
        guard w > maxSide || h > maxSide, w > 1, h > 1 else { return nil }
        let sc = maxSide / Swift.max(w, h)
        let nw = Int(w * sc), nh = Int(h * sc)
        guard let cg = img.cgImage(forProposedRect: nil, context: nil, hints: nil),
              let ctx = CGContext(data: nil, width: nw, height: nh, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.interpolationQuality = .high
        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: nw, height: nh))
        guard let out = ctx.makeImage() else { return nil }
        let r = NSImage(size: NSSize(width: nw, height: nh))
        r.addRepresentation(NSBitmapImageRep(cgImage: out))
        return r
    }

    /// 音乐标题（"歌名 — 歌手"）：音乐模式下画在右栏顶部（对齐 QQ 音乐观感）
    var musicTitle = ""
    /// 音乐播放背景主题（专辑主色 + 虚化磨砂封面）；取色/模糊在工作线程做，算好用 token 校验是否还是当前封面
    private var musicTheme: MusicTheme.Theme?
    private var coverToken = 0

    /// 专辑封面（音乐场景；Phase 5.3 先记录尺寸用于状态显示，绘制在 5.4 里接上）
    private(set) var coverImage: NSImage?
    func setCoverImage(_ img: NSImage?) {
        // 降采样到 ≤1024px：绘制要每帧上传纹理，原图 1400²+ 太费（音乐界面 10Hz 重绘，实测有感）
        let img = img.map { Self.downscaled($0, max: 1024) ?? $0 }
        coverImage = img
        needsDisplay = true
        coverToken += 1
        guard let img else {
            musicTheme = nil
            Config.log("封面已清空（切到无封面内容）")
            return
        }
        let token = coverToken
        let t0 = Date()
        // 取主色 + 生成虚化底要几十毫秒（CoreImage），放到后台，别卡住播放
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let theme = MusicTheme.make(from: img)
            DispatchQueue.main.async {
                guard let self, token == self.coverToken else { return }   // 已换封面 → 丢弃
                self.musicTheme = theme
                self.needsDisplay = true
                var h: CGFloat = 0, s: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
                theme.tint.getHue(&h, saturation: &s, brightness: &b, alpha: &a)
                Config.log(String(format: "[主题色] 色相 %.0f° 饱和 %.2f 明度 %.2f（虚化底=%@，耗时 %.0f ms）",
                                  h * 360, s, b, theme.backdrop == nil ? "无" : "有",
                                  Date().timeIntervalSince(t0) * 1000))
            }
        }
    }
    func duration() -> Double { snapshot().duration }
    func mediaTitle() -> String { snapshot().title }

    // MARK: - 渲染

    override func draw(_ dirtyRect: NSRect) {
        guard let rctx, let gl = openGLContext else { return }
        gl.makeCurrentContext()
        gl.update()

        let scale = window?.backingScaleFactor ?? 1.0
        var fbo = mpv_opengl_fbo(fbo: 0,
                                 w: Int32(Double(bounds.width) * scale),
                                 h: Int32(Double(bounds.height) * scale),
                                 // 显式声明 RGBA8：加了 alpha 缓冲后，让 mpv 去"猜"格式会猜错
                                 // （实测：不写 = 视频 G/B 通道互换，深蓝画面渲染成绿色）
                                 internal_format: Int32(GL_RGBA8))
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
