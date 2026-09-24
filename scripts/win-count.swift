// 用途：枚举某进程的所有窗口（判定"视频是否跑到独立窗口"类 bug ✓——见文档 44）
// 用法：swift scripts/win-count.swift <pid>
import CoreGraphics
import Foundation
// 枚举指定 PID 的所有窗口（判定"视频是否跑到独立窗口" ✓）
let pid = Int32(CommandLine.arguments[1])!
guard let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]] else { exit(1) }
var n = 0
for w in list {
    guard let owner = w[kCGWindowOwnerPID as String] as? Int32, owner == pid else { continue }
    let name = w[kCGWindowName as String] as? String ?? "（无标题）"
    let layer = w[kCGWindowLayer as String] as? Int ?? 0
    let b = w[kCGWindowBounds as String] as? [String: Any] ?? [:]
    let width = (b["Width"] as? Double) ?? 0, height = (b["Height"] as? Double) ?? 0
    n += 1
    print(String(format: "    #%d layer=%d  %.0fx%.0f  %@", n, layer, width, height, name))
}
print("  ⇒ 窗口总数: \(n)")
