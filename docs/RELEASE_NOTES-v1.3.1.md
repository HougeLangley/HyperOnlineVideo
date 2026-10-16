# 聚合视频 v1.3.1 —— 稳定性修复版（全平台）

> Android · macOS · Linux（Arch / Fedora / openSUSE / Debian / Ubuntu / NixOS）
> 发布日期：2026-10-17 · GPL-3.0-only

## 🐛 重点修复

### Android（用户实测反馈 ✓）
- **字幕「时有时无」根治**：逐字排查出四个根因并全修 ✓
  ① 切歌/连播从不刷新在线字幕轨（首条有字幕、连播后消失 ✗）
  ② 字幕二次解析总带失效 cookie → 改为**匿名优先、cookie 重试** ✓
  ③ 多语言视频中文轨被无序截断丢弃 → 按**系统语言排序**收集 ✓
  ④ B站字幕退避重试 2 → 3 次 ✓
- **「点下一个视频出错」根治**：YouTube 搜索结果混入**频道/播放列表**条目 ✗
  → 搜索侧过滤非视频形态 ✓ + 切歌失败**自动跳过**（上限 5 ✓ 方向感知 ✓）

## 🛡 稳健性（三端对齐）
- **macOS / Linux 同类潜患一并消除**：
  - macOS：视频解析失败自动 `tryPlayNext`（前进最多 6 条 ✓）
  - Linux：新增解析失败回调 → 队列驱动失败**自动跳过**（上限 5 ✓ 超时不回调防风暴 ✓）
- 三端行为统一：**队列中任何不可播放条目都会自动跳过并在界面说明** ✓

## ⚙️ 工程与依赖
- **Kotlin/Compose 插件 2.2.20 → 2.4.21**（编译 0 告警 ✓）
- Gradle 9.8.1 · AGP 9.4.1 · coroutines 1.11.0 · org.json 20260814
- CI：GHCR（GitHub Packages）产物发布 ✓ · 自托管 runner ✓ · 发布人工闸门 ✓

## 📦 安装

| 平台 | 方式 |
|---|---|
| Android 15+ | Releases 下载 `hov-1.3.1-arm64.apk`（arm64-v8a ✓）|
| macOS（Apple 芯片）| Releases 下载 `HyperOnlineVideo-1.3.1-arm64.dmg`（首次打开需右键→打开 ✓）|
| Arch / Manjaro | AUR：`yay -S hov-qt`（hov-qt 1.3.1-1 ✓）|
| Fedora 44/45 | `sudo dnf copr enable houge/hov-qt && sudo dnf install hov-qt` |
| openSUSE（TW / Leap）| OBS `home:houge` |
| Debian / Ubuntu | Releases 下载 `hov-qt_1.3.1-1_amd64.deb` |
| 任意发行版 | Releases 下载 `HyperOnlineVideo-x86_64.AppImage` |
| NixOS | flake：`github:HougeLangley/HyperOnlineVideo` |

## 🔎 验证
- Android：真机（小米 2509FPN0BC）字幕链 580/477 条实测 ✓ 连点 ⏭ ×3 全通过 ✓
- macOS：自检 119/119 ✓ 用户实测通过 ✓
- Arch Linux ARM VM：全新构建 ✓ GUI 截图验收 ✓ 自测零失败 ✓
- CI：三端门禁全绿 ✓

**完整更新日志**：见仓库 `desktop/packaging/deb/debian/changelog` · `git log`
