# 聚合视频 v1.2.4 —— QQ 音乐歌词修复 + clang 全平台构建（Android / macOS / Linux 全平台）

> 本次为**修复性更新**：修复 QQ 音乐在部分环境下"歌词全黑不显示"的问题；代码经 clang 全面净化；
> Linux 全渠道改用 **clang/llvm** 构建，并新增 **Ubuntu / Debian** 官方构建仓库 ✓。
> Linux 端全部通过**官方软件源**安装（一键命令、自动更新 ✓），第一节即为完整安装指南。

---

## 一、下载与安装（小白也能看懂 · 复制粘贴即可）

### 📱 Android（手机 / 平板）

1. 在本页下方 **"Assets"** 区域点击 **`app-release-1.2.4.apk`** 下载
2. 在手机上打开该文件；若提示「禁止安装未知应用」，按提示允许（安卓装非商店应用的标准流程 ✓）
3. 安装完成后，桌面出现「**聚合视频**」图标即成功 ✓

> 要求 **Android 15（API 35）及以上** ✓ 应用内「设置 → 关于」可确认版本为 **1.2.4** ✓

### 🍎 macOS（Apple 芯片 M 系列）

1. 在 **"Assets"** 区域下载 **`HyperOnlineVideo-1.2.4-arm64.dmg`**
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

# ② 安装（首次自动编译约 2-3 分钟；用 clang/llvm 构建 ✓）
yay -S hov-qt
```

#### Fedora 44 / 45

```bash
sudo dnf copr enable houge/hov-qt
sudo dnf install hov-qt
```

#### Ubuntu 26.04

```bash
sudo add-apt-repository -y ppa:houge/hov-qt 2>/dev/null || true   # 若提示无此 PPA 请看下行
echo "deb https://download.opensuse.org/repositories/home:/houge/Ubuntu_26.04/ ./" | sudo tee /etc/apt/sources.list.d/hov-qt.list
curl -fsSL https://download.opensuse.org/repositories/home:/houge/Ubuntu_26.04/Release.key | sudo gpg --dearmor -o /etc/apt/trusted.gpg.d/hov-qt.gpg
sudo apt update && sudo apt install hov-qt
```

#### Debian 13

```bash
echo "deb https://download.opensuse.org/repositories/home:/houge/Debian_13/ ./" | sudo tee /etc/apt/sources.list.d/hov-qt.list
curl -fsSL https://download.opensuse.org/repositories/home:/houge/Debian_13/Release.key | sudo gpg --dearmor -o /etc/apt/trusted.gpg.d/hov-qt.gpg
sudo apt update && sudo apt install hov-qt
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

> 首次编译约 5 分钟 ✓ WebEngine 登录窗在 NixOS 下默认不链接（登录请手动放置 cookie 文件，与应用内说明一致 ✓）

---

## 二、本版亮点（v1.2.4 更新内容）

### 🎵 QQ 音乐歌词修复（核心修复 ✓）

- **问题**：部分用户播放 QQ 音乐时右栏全黑、无标题无歌词（网易云正常 ✗）
- **根因**：QQ 登录 cookie 文件随时间累积到 15KB 后，请求头超限，歌词接口（`c.y.qq.com`）**直接拒绝连接**（HTTP 000）
- **修复**：歌词请求改为**匿名优先**；为空再用"按完整条目截断到 3KB"的 Cookie 重试一次 ✓
- 覆盖：**macOS + Linux**（Android 走新版接口，不受影响 ✓）

### 🧹 代码净化（clang 全净）

- 移除死字段 1 处 + 未使用的 lambda 捕获 4 处；**clang `-Wextra` 告警数 5 → 0 ✓**
- 全平台等价重构，行为零变化 ✓

### 🐧 Linux 构建体系升级

- **全渠道改用 clang/llvm 构建**（clang + lld ✓）：Arch / Fedora(COPR) / openSUSE(OBS) / Ubuntu / Debian ✓
- **新增 Ubuntu 26.04 与 Debian 13 官方构建仓库**（x86_64 + aarch64 ✓ 一键安装 ✓）
- 实测：openSUSE Tumbleweed x86_64/aarch64/riscv64、Leap 16.0、Ubuntu 26.04、Debian 13 全目标构建成功 ✓

---

## 三、完整更新日志（自 v1.2.3 起）

- **QQ 音乐**：歌词全黑修复（匿名优先 + 截断 Cookie 重试；macOS/Linux 同修）
- **代码**：清理死字段 + 未用捕获（clang -Wextra 零告警）
- **构建**：全 Linux 渠道 clang/llvm 化；新增 Ubuntu/Debian 构建仓库
- **工程**：新增 riscv64 deb 构建；全平台复验（Android 真机 / macOS 119/119 自测 / Linux E2E）

---

## 四、已知问题与说明

- **B站登录**：部分功能（1080P+/字幕等）需登录；若搜索异常，请在应用内重新登录 B 站 ✓
- **Apple 公证**：macOS 应用未做付费公证，首次打开需按第一节方法绕过（正常现象 ✓）
- **QQ 音乐**：若提示"需重新登录"，在应用内「登录」页重新登录即可 ✓
- **Ubuntu 24.04 / Debian 12**：Qt 版本低于 6.5，暂不支持（请升级系统或使用 AppImage ✓）

---

## 五、致谢

感谢真实使用场景中的每一条反馈 —— 本版的 QQ 歌词修复正是来自用户的对比测试与截图 ✓
发现问题欢迎开 [Issue](https://github.com/HougeLangley/HyperOnlineVideo/issues) ✓
