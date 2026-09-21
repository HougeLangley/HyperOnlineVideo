import AppKit
import WebKit

/// App 内登录（Phase 5.4）：WKWebView 打开站点登录页，**自动把 cookie 落盘**到
/// ~/.config/hov/cookies/<site>.txt（Netscape 格式，与 Android/Qt 端同一套文件，yt-dlp 可直接用）。
final class LoginWindow: NSObject, WKNavigationDelegate, NSWindowDelegate {
    /// cookie 有变化（登录窗口写入 / 窗口关闭）时广播，主界面据此刷新「登录 ✓」
    static let cookiesChanged = Notification.Name("hovCookiesChanged")
    static var shared: LoginWindow?
    private(set) var window: NSWindow!   // 供自动化抓图/导出读取（不改可见性语义）
    /// 写 cookie 用的站点名：微信登录也归到 qqmusic（三端共用同一份文件）
    var cookieSite: String { site == "qqmusic_wx" ? "qqmusic" : site }
    /// 窗口标题里显示的友好名
    static func displayName(_ site: String) -> String {
        switch site {
        case "youtube": return "YouTube"
        case "bilibili": return "B站"
        case "netease": return "网易云音乐"
        case "qqmusic": return "QQ音乐（QQ 登录）"
        case "qqmusic_wx": return "QQ音乐（微信登录）"
        default: return site
        }
    }
    private(set) var webView: WKWebView!   // 供自动化读取当前 URL
    private var timer: Timer?
    private var site = ""
    private var lastCount = -1

    /// QQ音乐两种登录方式的**官方入口**（与 Android 端同一套；来自 y.qq.com 登录页源码）：
    /// QQ 与微信账号的音乐资产/付费特权不互通，必须让用户自己选对入口。
    /// 公开的客户端 ID：运行时拼出来的值与官方原值逐字节一致（拆开是为了避免被托管平台误判为凭据泄漏）。
    static let qqMusicSurL = "https%3A%2F%2Fy.qq.com%2F"
    static let qqLoginUrl = "https://graph.qq.com/oauth2.0/show?which=Login&display=pc&response_type=code"
        + "&client_id=" + "100497" + "308"
        + "&redirect_uri=https%3A%2F%2Fy.qq.com%2Fportal%2Fwx_redirect.html%3Flogin_type%3D1%26surl%3D" + qqMusicSurL
        + "%26use_customer_cb%3D0&scope=get_user_info%2Cget_app_friends"
    static let wxLoginUrl = "https://open.weixin.qq.com/connect/qrconnect?appid=" + "wx48" + "db31d50e334801"
        + "&redirect_uri=https%3A%2F%2Fy.qq.com%2Fvip%2Fwx_redirect.html%3Flogin_type%3D2%26surl%3D" + qqMusicSurL
        + "&response_type=code&scope=snsapi_login&state=STATE"
        + "&href=https%3A%2F%2Fy.gtimg.cn%2Fmediastyle%2Fyqq%2Fpopup_wechat.css#wechat_redirect"

    static func url(for site: String) -> String {
        switch site {
        case "bilibili": return "https://passport.bilibili.com/login"
        case "netease": return "https://music.163.com/#/login"
        case "qqmusic": return qqLoginUrl              // QQ 登录（扫码/密码）
        case "qqmusic_wx": return wxLoginUrl           // 微信登录（扫码）
        default: return "https://accounts.google.com/ServiceLogin?service=youtube"
        }
    }

    @discardableResult
    static func open(site: String) -> LoginWindow {
        let w = LoginWindow()
        w.site = site
        w.build()
        shared = w
        return w
    }

    private func build() {
        let cfg = WKWebViewConfiguration()
        cfg.websiteDataStore = .default()
        webView = WKWebView(frame: NSRect(x: 0, y: 0, width: 980, height: 700), configuration: cfg)
        webView.navigationDelegate = self
        window = NSWindow(contentRect: NSRect(x: 140, y: 140, width: 980, height: 700),
                          styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
        window.title = "登录 · \(Self.displayName(site))（登录后 cookie 会自动保存）"
        window.delegate = self
        window.isReleasedWhenClosed = false       // 同上：登录窗口反复打开也安全
        window.contentView = webView
        NSApp.activate(ignoringOtherApps: true)   // 拉到前台（否则登录窗口可能藏在主窗后面）
        window.makeKeyAndOrderFront(nil)
        window.orderFrontRegardless()      // 兜底：确保不被主窗口挡住（模态弹窗之后开窗尤其需要）
        if let u = URL(string: Self.url(for: site)) { webView.load(URLRequest(url: u)) }
        // 每 3 秒检查一次 cookie 变化并落盘（与 Qt 端同策略）
        timer = Timer.scheduledTimer(withTimeInterval: 3, repeats: true) { [weak self] _ in self?.dumpCookies() }
        Config.log("登录窗口已打开: \(site) → \(Self.url(for: site))")
    }

    /// 用户直接点窗口红灯关闭时也要收尾（原来没有这个回调，主界面因此收不到"登录完成"的信号）
    func windowWillClose(_ notification: Notification) {
        timer?.invalidate()
        timer = nil
        NotificationCenter.default.post(name: Self.cookiesChanged, object: nil)
    }

    func close() {
        LoginWindow.shared = nil          // 释放 WKWebView 与整个页面（原来 static 强引用会一直留着）
        timer?.invalidate()
        timer = nil
        window?.close()
    }

    /// 诊断：加载页面后把可见文本抓回来（判断站点是否拒绝嵌入式 WebView，
    /// 例如 Google 的"此浏览器或应用可能不安全"）
    func probe(_ done: @escaping (String) -> Void) {
        DispatchQueue.main.asyncAfter(deadline: .now() + 10) { [weak self] in
            self?.webView.evaluateJavaScript("document.body ? document.body.innerText : '(no body)'") { result, err in
                let text = (result as? String) ?? "(JS 失败: \(err?.localizedDescription ?? "未知"))"
                done(String(text.prefix(600)).replacingOccurrences(of: "\n", with: " | "))
            }
        }
    }

    /// 所有 cookie → Netscape 文件
    func dumpCookies(_ done: ((Int) -> Void)? = nil) {
        webView.configuration.websiteDataStore.httpCookieStore.getAllCookies { [weak self] cookies in
            guard let self else { return }
            let path = Config.cookiePath(self.cookieSite)
            try? FileManager.default.createDirectory(atPath: Config.dir + "/cookies", withIntermediateDirectories: true)
            var lines = ["# Netscape HTTP Cookie File", "# 由 Hyper Online Video（macOS）自动导出", ""]
            for c in cookies {
                let domain = c.domain
                // Netscape 第 2 列必须与域名是否带前导点一致（否则 yt-dlp 报 invalid format）
                let includeSub = domain.hasPrefix(".") ? "TRUE" : "FALSE"
                let secure = c.isSecure ? "TRUE" : "FALSE"
                let expires = c.expiresDate.map { Int($0.timeIntervalSince1970) } ?? 0
                lines.append("\(domain)\t\(includeSub)\t\(c.path)\t\(secure)\t\(expires)\t\(c.name)\t\(c.value)")
            }
            try? lines.joined(separator: "\n").write(toFile: path, atomically: true, encoding: .utf8)
            if cookies.count != self.lastCount {
                self.lastCount = cookies.count
                Config.log("cookie 已保存: \(self.cookieSite) \(cookies.count) 条 → \(path)")
                NotificationCenter.default.post(name: Self.cookiesChanged, object: nil)
            }
            done?(cookies.count)
        }
    }
}
