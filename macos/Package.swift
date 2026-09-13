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
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
    ]
)
