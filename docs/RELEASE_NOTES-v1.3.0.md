# 聚合视频 v1.3.0 —— 社区协作里程碑（fork 整合 + YouTube 全链修复 + 字幕三端统一）

> **这是一个由社区协作促成的大版本**：全部平台（Android / macOS / Linux）的 YouTube 播放链、字幕链完成系统性修复；
> 三端行为全面对齐；代码经四维审核（重复/硬编码/重复块/未启用代码）达标。
> Linux 端全部通过**官方软件源**安装（一键命令、自动更新），第一节即为完整安装指南。

---

## 🙏 特别鸣谢：lishoujun 的 fork 贡献（本版整合）

本版**直接整合并完整保留署名**了 [@lishoujun](https://github.com/lishoujun) 在其 fork 中的 14 个高质量提交，
其中多项是**关键问题的根治方案**：

| 贡献 | 解决的问题 |
|---|---|
| **YtRangeProxy（分块 Range 本机中继）** | 受限网络出口（VPS/数据中心 IP）下 googlevideo 直链 403 → 彻底修复 |
| **`player_client=web_embedded` 客户端策略** | android_vr 客户端"10MB 墙"（播十几秒断流）|
| **DASH 音频 mpv NODE 数组挂载 + 路径转义** | 分离音轨挂载竞态（并附 143 行 Python 回归测试）|
| **点击立即取消上一次解析 + 字幕后台抓取** | 连续点击卡顿、解析叠加 |
| **CLI `--help` / `--version`** | 命令行可用性 |
| **GitHub Actions CI 工作流** | 自动化构建基础 |

> 感谢这份贡献 —— **协作让这个项目变得更好**。上游将持续跟进并欢迎更多 PR。

---

## 一、下载与安装（完整指南 · 复制粘贴即可）

### 📱 Android（手机 / 平板）
1. 下载本页 **Assets → `app-release-1.3.0.apk`**
2. 手机打开安装；若提示"禁止安装未知应用"，按提示允许（标准流程）
3. 需 **Android 15（API 35）+**；应用内「设置 → 关于」确认版本 **1.3.0**

### 🍎 macOS（Apple 芯片 M 系列）
1. 下载 **Assets → `HyperOnlineVideo-1.3.0-arm64.dmg`**
2. 打开 DMG → 拖入「应用程序」
3. 首次打开被拦（未公证）：右键→打开；或终端：
   ```bash
   sudo xattr -dr com.apple.quarantine /Applications/HyperOnlineVideo.app
   ```

### 🐧 Linux（按发行版选择）

**Arch Linux / Manjaro / EndeavourOS**（AUR · clang 构建）
```bash
sudo pacman -S --needed base-devel git
git clone https://aur.archlinux.org/yay.git /tmp/yay && cd /tmp/yay && makepkg -si   # 已装 yay/paru 可跳过
yay -S hov-qt
```

**Fedora 44 / 45**（COPR）
```bash
sudo dnf copr enable houge/hov-qt
sudo dnf install hov-qt
```

**openSUSE Tumbleweed**
```bash
sudo zypper addrepo https://download.opensuse.org/repositories/home:/houge/openSUSE_Tumbleweed/ obs-houge
sudo zypper install hov-qt
```

**openSUSE Leap 16.0**
```bash
sudo zypper addrepo https://download.opensuse.org/repositories/home:/houge/openSUSE_Leap_16.0/ obs-houge-leap
sudo zypper install hov-qt
```

**Ubuntu 26.04**（OBS 仓库 · x86_64/aarch64）
```bash
echo "deb https://download.opensuse.org/repositories/home:/houge/Ubuntu_26.04/ ./" | sudo tee /etc/apt/sources.list.d/hov-qt.list
curl -fsSL https://download.opensuse.org/repositories/home:/houge/Ubuntu_26.04/Release.key | sudo gpg --dearmor -o /etc/apt/trusted.gpg.d/hov-qt.gpg
sudo apt update && sudo apt install hov-qt
```

**Debian 13**（OBS 仓库 · x86_64/aarch64）
```bash
echo "deb https://download.opensuse.org/repositories/home:/houge/Debian_13/ ./" | sudo tee /etc/apt/sources.list.d/hov-qt.list
curl -fsSL https://download.opensuse.org/repositories/home:/houge/Debian_13/Release.key | sudo gpg --dearmor -o /etc/apt/trusted.gpg.d/hov-qt.gpg
sudo apt update && sudo apt install hov-qt
```

**任意发行版（便携）**：下载 **`HyperOnlineVideo-aarch64.AppImage`** → `chmod +x` → 直接运行

**NixOS（flake）**
```nix
# flake.nix inputs 中：
hov-qt = { url = "github:HougeLangley/HyperOnlineVideo"; inputs.nixpkgs.follows = "nixpkgs"; };
# configuration.nix：
environment.systemPackages = [ inputs.hov-qt.packages.x86_64-linux.default ];
```
```bash
sudo nixos-rebuild switch
```

---

## 二、本版亮点

### 🎬 YouTube 冷启动播放修复（三端）
- **症状**：冷启动后点第一个视频不播/无声/无画面；后续正常
- **根因**（Android 日志实锤）：音频挂载是固定延时命令，与首帧加载竞态；且选流优先 HLS 主清单（**常无音轨**）
- **修复**：音频改**事件驱动**（file-loaded ✓ 幂等 ✓）；macOS 采用 **`audio-files` 预挂载**（Linux 同款）；**优先 DASH 分轨**，HLS 仅兜底

### 💬 字幕链三端统一（含中文自动翻译）
- **语言判断**：严格取**系统语言**（`Locale`，**与网络出口无关**）；按系统语言优先级排序 → **默认显示**
- **中文**：支持 YouTube **自动翻译轨**（语言码实为 `zh-Hans-en`）；轨名规范显示为 `zh-Hans（翻译）`
- **抗限流**：匿名优先 → cookie 重试 → **429 退避重试**；macOS 两步法（先取可用语言再下载 ≤2 条）
- **根治"多语言齐发被拒"**：改 yt-dlp 正则匹配（只请求**真实存在**的轨）

### ⚡ 画质与解析对齐
- 撤销强制客户端；匿名优先解析（失效 cookie 会触发 YouTube 降级）；实测 **1080p/1440p 正常选中**

### 🧹 工程质量
- 日志时间前缀（`hh:mm:ss`）；解析超时 75s；四维代码审核（重复/硬编码/重复块/未启用代码）达标；三端告警清零（平台性残留有据记录）

---

## 三、完整更新日志（自 v1.2.5 起）
- 整合 fork 14 提交（YtRangeProxy / web_embedded / DASH NODE / cancel-inflight / CLI / CI）
- YouTube 冷启动影音修复（Android 事件驱动 · mac 预挂载 · DASH 优先）
- 字幕三端统一（系统语言 · 中文翻译轨 · 匿名优先 · 429 退避 · 两步法）
- 画质对齐（撤销客户端强制 · 匿名优先）
- 代码审核与告警清理（三端）

---

## 四、已知问题
- **受限出口**（VPS/数据中心 IP）：仅影响 YouTube 直链 → 已由 YT-PROXY 分块中继缓解；极端情况请更换线路
- **Ubuntu 24.04 / Debian 12**：Qt < 6.5 暂不支持（请用 26.04 / 13 或 AppImage）
- **Apple 公证**：macOS 应用未付费公证，首次打开按第一节处理

---

## 五、致谢
@lishoujun（fork 关键贡献 · 本版核心修复来源）· 所有提交 Issue 与实测反馈的用户 —— 这个版本属于社区。
