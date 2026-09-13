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

    private static func run(_ req: URLRequest, timeout: TimeInterval) -> Reply {
        var out = Reply()
        let sem = DispatchSemaphore(value: 0)
        let cfg = URLSessionConfiguration.ephemeral
        cfg.timeoutIntervalForRequest = timeout
        cfg.requestCachePolicy = .reloadIgnoringLocalCacheData
        let task = URLSession(configuration: cfg).dataTask(with: req) { data, resp, err in
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

    static let ua = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36"
}
