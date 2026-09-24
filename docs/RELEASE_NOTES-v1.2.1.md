# 聚合视频 v1.2.1 —— 全平台正式发布（Android / macOS / Linux 全发行版）

> 本次是**首次全平台发布**：Android、macOS、以及 Linux 的 **Arch / Fedora / openSUSE / NixOS** 全渠道同步上线 ✓
> Linux 端**不提供打包文件下载** —— 全部通过**官方软件源**安装（一键命令、自动更新 ✓），本文第一节即为完整安装指南。

---

## 一、下载与安装（小白也能看懂 · 复制粘贴即可）

### 📱 Android（手机 / 平板）

1. 在本页下方 **"Assets"** 区域点击 **`app-release-1.2.1.apk`** 下载
2. 在手机上打开该文件；若提示「禁止安装未知应用」，按提示允许（安卓装非商店应用的标准流程 ✓）
3. 安装完成后，桌面出现「**聚合视频**」图标即成功 ✓

> 要求 **Android 15（API 35）及以上** ✓ 应用内「设置 → 关于」可确认版本为 **1.2.1** ✓

### 🍎 macOS（Apple 芯片 M 系列）

1. 在 **"Assets"** 区域下载 **`HyperOnlineVideo-1.2.1-arm64.dmg`**
2. 双击打开 → 把「聚合视频」拖进 **应用程序** 文件夹
3. **第一次打开会被 macOS 拦下**（未付费公证的应用都会这样 ✓ 正常现象）：
   - **方法 A**：右键点图标 → 选「打开」→ 再点一次「打开」
   - **方法 B（推荐）**：打开「终端」粘贴：
     ```bash
     sudo xattr -dr com.apple.quarantine /Applications/HyperOnlineVideo.app
     ```

### 🐧 Linux —— 按你的发行版选一条（全部为软件源安装 ✓）

#### Arch Linux / Manjaro / EndeavourOS

```bash
# ① 还没装 AUR 助手的话先装 yay（已有 yay/paru 就跳过）
sudo pacman -S --needed base-devel git
git clone https://aur.archlinux.org/yay.git /tmp/yay && cd /tmp/yay && makepkg -si

# ② 安装（首次自动编译约 2-3 分钟）
yay -S hov-qt
```

#### Fedora 44 / 45

```bash
sudo dnf copr enable houge/hov-qt
sudo dnf install hov-qt
```

#### openSUSE Tumbleweed

```bash
sudo zypper addrepo https://download.opensuse.org/repositories/home:/houge/openSUSE_Tumbleweed/ obs-houge
sudo zypper install hov-qt
```

#### openSUSE Leap 16.0

```bash
sudo zypper addrepo https://download.opensuse.org/repositories/home:/houge/openSUSE_Leap_16.0/ obs-houge-leap
sudo zypper install hov-qt
```

#### NixOS（声明式 · flake）

在 `/etc/nixos/flake.nix` 的 `inputs` 里加：

```nix
hov-qt = {
  url = "github:HougeLangley/HyperOnlineVideo";
  inputs.nixpkgs.follows = "nixpkgs";
};
```

在 `configuration.nix`（或 home-manager 的 `home.nix`）里加：

```nix
environment.systemPackages = [ inputs.hov-qt.packages.x86_64-linux.default ];
```

然后重建：

```bash
sudo nixos-rebuild switch
```

> 只想先试试：`nix run github:HougeLangley/HyperOnlineVideo` ✓

#### Ubuntu / Debian / 其它发行版（源码构建 · 约 5 分钟）

```bash
sudo apt update
sudo apt install -y git cmake ninja-build pkgconf \
  qt6-base-dev qt6-declarative-dev qt6-webengine-dev \
  libmpv-dev mpv ffmpeg yt-dlp

git clone https://github.com/HougeLangley/HyperOnlineVideo.git
cd HyperOnlineVideo/desktop
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DHOV_WEBENGINE=ON
cmake --build build
./build/hov-qt
```

> 安装到系统（带桌面图标）：`sudo cmake --install build --prefix /usr/local` ✓

---

## 二、本版亮点

### 三端下载面板与下载能力（本版主题）

- **Linux（Qt6）**：新增**下载面板**（进度条 / 重试失败 / 逐条取消 / 取消全部 / 打开下载目录 / LRU 清理）与**收藏面板**；
  视频下载修复（起播时记录直链、扩展名与 macOS 对齐）；「清理」现在会真正把**已结束条目从列表移除**。
- **macOS**：新增 **DownloadsPanel**（字节级进度、取消全部、重试失败）、面板可拖拽缩放（关闭按钮一键可用）；
- **Android**：下载卡片新增「**取消**」与「**取消全部**」；完成的卡片 3.5 秒自动淡出，不再堆积。

### 窗口与播放体验（Linux / macOS）

- **窗口几何持久化**：首启 1200×720 居中；退出时保存位置与大小（PiP/全屏态不污染记忆）。
- **画中画（PiP）往返修复**：进出 PiP 后左侧列表宽度、控制条不再错乱（两端同修）。
- **长文本不再撑大窗口**：状态栏/信息栏粘贴超长文本时的窗口跳变已修。
- **视频独立窗口**（macOS）：从主窗弹出视频单独播放。
- 字幕/歌词若干修复；`main.cpp` 拆分为多模块（`MainWindow` / `Playback` / `PlayActions` / `SettingsDialog` …），便于后续维护。

### Linux 打包矩阵（v1.2.1 核心工程）

| 渠道 | 覆盖 | 状态 |
|---|---|---|
| **AUR**（Arch）| x86_64 / aarch64 / riscv64 | ✅ 已发布（`yay -S hov-qt`）|
| **Fedora Copr** | Fedora 44 / 45 × x86_64 / aarch64 / riscv64 | ✅ 六个构建目标全过 |
| **openSUSE OBS** | Tumbleweed × 3 架构 + Leap 16.0 × 2 架构 | ✅ 五目标全过 |
| **NixOS flake** | x86_64 / aarch64 | ✅ 本机 rebuild 即用 |
| **Ubuntu / Debian** | deb 完整套件（本地构建）| ✅ lintian 零告警 |

> 许可元数据全线统一为 **GPL-3.0-only**（与仓库 `LICENSE` 实际授予的版本一致）。

---

## 三、本次产物

| 平台 | 文件 | 大小 |
|---|---|---|
| Android | `app-release-1.2.1.apk`（R8 混淆）| 70.8 MB |
| macOS | `HyperOnlineVideo-1.2.1-arm64.dmg` | 29.3 MB |
| Linux | **无文件下载** —— 见上文各软件源（AUR / Copr / OBS / Nix flake）| — |

---

## 四、已知问题（如实）

| 问题 | 说明 |
|---|---|
| YouTube 字幕偶发获取失败 | YouTube 对部分 IP 的字幕接口限流（429），与应用无关；可稍后重试 |
| B站部分接口 412 | 同上（IP 级限流）|
| QQ 音乐 VIP 曲目匿名不可取流 | 需登录（应用内登录窗口或导入 cookie）|
| Linux riscv64 无 App 内登录窗 | Ubuntu riscv64 端口无 QtWebEngine 包 → 该架构自动降级为「手动放置 cookie」（与 AppImage 一致）|
| Ubuntu 暂无官方软件源 | 目前用源码构建（5 分钟，见上）；后续如开通 OBS Ubuntu 目标会另行说明 |

---

## 五、升级说明

- 三端**配置文件共用**（`~/.config/hov/`），覆盖升级**不丢**设置 / 队列 / 收藏 / 播放进度 / cookie ✓
- **Android**：如果此前安装的是 **debug 版**，与发布版**签名不同**，需要**先卸载再安装**（正式版之间可直接覆盖升级 ✓）
- **Linux**：AUR / Copr / OBS 用户直接 `更新系统` 即可（包管理器自动处理 ✓）

---

## 六、致谢与反馈

- 问题反馈：<https://github.com/HougeLangley/HyperOnlineVideo/issues>
- 项目许可：**GPL-3.0-only**（详见仓库 `LICENSE`）
