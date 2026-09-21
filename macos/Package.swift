// swift-tools-version: 6.0
import PackageDescription

// macOS 端（Phase 5）：纯 SPM + AppKit，不需要 Xcode 工程。
// libmpv 通过 system library 目标接入（brew install mpv 提供 /opt/homebrew/include/mpv/*.h 与 libmpv.dylib）。
let package = Package(
    name: "HyperOnlineVideo",
    platforms: [.macOS(.v13)],
    targets: [
        .systemLibrary(
            name: "Cmpv",
            pkgConfig: "mpv",
            providers: [.brew(["mpv"])]
        ),
        .executableTarget(
            name: "HyperOnlineVideo",
            dependencies: ["Cmpv"],
            // GL_SILENCE_DEPRECATION：客户端 API 用 mpv 的 GL 渲染器，OpenGL 弃用警告无法避免，
            // 全量屏蔽它，让以后出现的**其它**警告能一眼看见（本次审计就是靠这个把 175 条噪音变 0）。
            swiftSettings: [.swiftLanguageMode(.v5), .unsafeFlags(["-Xcc", "-DGL_SILENCE_DEPRECATION"])]
        ),
    ]
)
