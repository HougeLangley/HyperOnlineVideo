# 聚合视频 v1.1.0 —— 三端统一发布

> Android · Linux(Qt6/Wayland) · macOS(AppKit) —— 同一套配置文件、同一套快捷键、同一套自检。

## 本版亮点

### 三端都有的核心能力
- **在线播放零广告**：YouTube / B站 解析直链后交给 libmpv，App 层不插播、不裁剪
- **音乐**：网易云音乐 + QQ音乐 搜索、播放、歌词（含**逐字高亮**）、封面、下载
- **在线字幕**：B站 CC（官方 API）与 YouTube 字幕（yt-dlp），App 层统一渲染
- **本地库**：下载目录浏览、排序、重命名、直接播放
- **播放队列**：顺序 / 单曲 / 随机 + **持久化**（跨端共用 `queue.json`）
- **进度记忆**：自动续播（≥15 秒且未接近结尾才续播）
- **收藏**：跨端共用 `favorites.json`
- **App 内登录**：内置浏览器登录，cookie 自动落盘为 Netscape 格式（yt-dlp 可直接复用）
- **媒体控制**：Android MediaSession（通知栏/锁屏）、Linux MPRIS（媒体键/playerctl）、macOS 控制中心/键盘媒体键

### 各端要点
| | 本版新增 |
|---|---|
| Android | 版本号 1.1.0（versionCode 2）；与 v1.0.0 功能一致，含此前全部能力 |
| Linux | 进度记忆 / MPRIS 媒体键 / 在线字幕（B站官方 CC API）/ 音乐封面与**播放中切音质** / 下载 LRU 清理 / 本地库 |
| macOS | **首次发布**：完整界面（搜索·播放·队列·歌词字幕·本地库·设置·收藏）、媒体键、WKWebView 登录、自包含 `.app` + DMG |

## 产物

| 平台 | 文件 | 大小 |
|---|---|---|
| Android | `app-release.apk`（已签名，versionName 1.1.0） | 72.6 MB |
| Linux | `HyperOnlineVideo-aarch64.AppImage` | 88.7 MB |
| Linux | `hov-qt-1.1.0-1-aarch64.pkg.tar.xz`（Arch） | 497 KB |
| Linux | `HyperOnlineVideo.flatpak` | 40.5 MB |
| macOS | `HyperOnlineVideo-1.1.0-arm64.dmg`（自包含，含 48 个动态库） | 29 MB |

## 安装提示

- **Android**：安装 APK 需允许"未知来源"；首次使用建议在 App 内登录（YouTube/B站/网易云/QQ音乐）
- **Linux**：AppImage 需 `chmod +x`；Arch 包用 `pacman -U`；Flatpak 用 `flatpak install <文件>`
  - AppImage / Flatpak **不含 QtWebEngine**（体积考虑），登录请把 cookie 放到 `~/.config/hov/cookies/`
  - 需要系统提供 `yt-dlp`（`pacman -S yt-dlp` / `apt install yt-dlp`）
- **macOS**：App 未做 Apple 公证，首次打开若提示"无法验证开发者"，请右键 → 打开，或执行
  `xattr -dr com.apple.quarantine /Applications/HyperOnlineVideo.app`
  - App **自带 libmpv/ffmpeg**，无需额外安装；在线解析仍需 `brew install yt-dlp`

## 已知限制（如实说明）

- 三端均**不绕过**版权与平台规则：VIP/版权受限曲目在未登录（或非会员）时无法播放，会明确提示并自动跳到下一首
- B站 CC 字幕覆盖率取决于视频本身；YouTube 字幕在部分网络环境下会受限流影响
- 直播、番剧、付费课程等未做专门适配
- macOS 端暂无下载管理器；Android 的"仅 WiFi 下载"为移动端专属能力
- 流量直连：Android 可在 App 内彻底绕过 VPN；桌面端仅能绕过"环境变量代理"（TUN 类代理需在代理工具里配规则）

## 自检结果（发布前跑通）

| 平台 | 项目 | 结果 |
|---|---|---|
| Android | 单元测试 | 16 / 16 通过 |
| Linux | `--queue-selftest` | 52 / 52 通过 |
| macOS | `--selftest-logic` | 41 / 41 通过 |

## 许可

GPL-3.0-only。本项目仅用于个人学习与研究，请遵守各内容平台的用户协议。
