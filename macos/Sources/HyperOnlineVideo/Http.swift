import Foundation

/// 极简同步 HTTP（CLI 自检用：允许阻塞；UI 路径后续换异步）
enum Http {
    struct Reply {
        var status = 0
        var data = Data()
        var error = ""
        var ok: Bool { error.isEmpty && status >= 200 && status < 400 }
        var text: String { String(data: data, encoding: .utf8) ?? "" }
        func json() -> [String: Any] {
            (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] ?? [:]
        }
    }

    /// 共享会话：原来**每个请求都 new 一个 URLSession**（既浪费 TLS 握手，又会随请求数泄漏会话）
    private static let session: URLSession = {
        let cfg = URLSessionConfiguration.ephemeral
        cfg.httpMaximumConnectionsPerHost = 6
        cfg.requestCachePolicy = .reloadIgnoringLocalCacheData
        cfg.waitsForConnectivity = true
        return URLSession(configuration: cfg)
    }()

    private static func run(_ req: URLRequest, timeout: TimeInterval) -> Reply {
        var out = Reply()
        let sem = DispatchSemaphore(value: 0)
        var r = req
        r.timeoutInterval = timeout                    // 超时按请求设（会话是共享的）
        let task = session.dataTask(with: r) { data, resp, err in
            if let e = err { out.error = e.localizedDescription }
            if let h = resp as? HTTPURLResponse { out.status = h.statusCode }
            out.data = data ?? Data()
            sem.signal()
        }
        task.resume()
        _ = sem.wait(timeout: .now() + timeout + 5)
        return out
    }

    static func get(_ url: String, headers: [String: String] = [:], timeout: TimeInterval = 25) -> Reply {
        guard let u = URL(string: url) else { return Reply(error: "URL 非法") }
        var req = URLRequest(url: u)
        for (k, v) in headers { req.setValue(v, forHTTPHeaderField: k) }
        return run(req, timeout: timeout)
    }

    static func post(_ url: String, body: Data, contentType: String = "application/x-www-form-urlencoded",
                     headers: [String: String] = [:], timeout: TimeInterval = 25) -> Reply {
        guard let u = URL(string: url) else { return Reply(error: "URL 非法") }
        var req = URLRequest(url: u)
        req.httpMethod = "POST"
        req.setValue(contentType, forHTTPHeaderField: "Content-Type")
        for (k, v) in headers { req.setValue(v, forHTTPHeaderField: k) }
        req.httpBody = body
        return run(req, timeout: timeout)
    }

    /// Range 探测（网易云 CDN 节点择优用 ✓）：只拉 1 字节，200/206 视为可用
    static func probeOk(_ url: String, headers: [String: String] = [:], timeout: TimeInterval = 3) -> Bool {
        guard let u = URL(string: url) else { return false }
        var req = URLRequest(url: u)
        req.setValue("bytes=0-0", forHTTPHeaderField: "Range")
        for (k, v) in headers { req.setValue(v, forHTTPHeaderField: k) }
        let r = run(req, timeout: timeout)
        return r.status == 200 || r.status == 206
    }

    /// YouTube 直链可流播性预检（Issue #1 ✓ 2026-10-01）：部分出口（VPS/数据中心 IP ✗）只放行 ≤64KB Range ✗，
    /// 真实播放必 403 ✗ → 用 4MB Range 探针判定（正常 200/206 ✓；受限 403 ✗）。
    static func streamRangeOk(_ url: String) -> Int {
        guard url.contains("googlevideo.com"), let u = URL(string: url) else { return 0 }
        var req = URLRequest(url: u)
        req.setValue("bytes=0-4194304", forHTTPHeaderField: "Range")
        return run(req, timeout: 10).status
    }

    static let ua = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36"
}
