import AppKit
import WebKit

/// App 内登录（Phase 5.4）：WKWebView 打开站点登录页，**自动把 cookie 落盘**到
/// ~/.config/hov/cookies/<site>.txt（Netscape 格式，与 Android/Qt 端同一套文件，yt-dlp 可直接用）。
final class LoginWindow: NSObject, WKNavigationDelegate {
    static var shared: LoginWindow?
    private var window: NSWindow!
    private var webView: WKWebView!
    private var timer: Timer?
    private var site = ""
    private var lastCount = -1

    static func url(for site: String) -> String {
        switch site {
        case "bilibili": return "https://passport.bilibili.com/login"
        case "netease": return "https://music.163.com/#/login"
        case "qqmusic": return "https://y.qq.com/"
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
        window.title = "登录 · \(site)（登录后 cookie 会自动保存）"
        window.contentView = webView
        window.makeKeyAndOrderFront(nil)
        if let u = URL(string: Self.url(for: site)) { webView.load(URLRequest(url: u)) }
        // 每 3 秒检查一次 cookie 变化并落盘（与 Qt 端同策略）
        timer = Timer.scheduledTimer(withTimeInterval: 3, repeats: true) { [weak self] _ in self?.dumpCookies() }
        Config.log("登录窗口已打开: \(site) → \(Self.url(for: site))")
    }

    func close() {
        timer?.invalidate()
        timer = nil
        window?.close()
    }

    /// 所有 cookie → Netscape 文件
    func dumpCookies(_ done: ((Int) -> Void)? = nil) {
        webView.configuration.websiteDataStore.httpCookieStore.getAllCookies { [weak self] cookies in
            guard let self else { return }
            let path = Config.cookiePath(self.site)
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
                Config.log("cookie 已保存: \(self.site) \(cookies.count) 条 → \(path)")
            }
            done?(cookies.count)
        }
    }
}
