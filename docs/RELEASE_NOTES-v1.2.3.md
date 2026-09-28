# 聚合视频 v1.2.3 —— B站搜索升级（wbi 签名）+ 稳定性维护（Android / macOS / Linux 全平台）

> 本次为**维护性更新**：B 站搜索升级为**官方现行 wbi 签名协议**（解析更稳、抗风控 ✓）；
> Android 端"播完自动连播"加固；并完成 yt-dlp 版本与四平台 API 的全面核查 ✓。
> Linux 端全部通过**官方软件源**安装（一键命令、自动更新 ✓），第一节即为完整安装指南。

---

## 一、下载与安装（小白也能看懂 · 复制粘贴即可）

### 📱 Android（手机 / 平板）

1. 在本页下方 **"Assets"** 区域点击 **`app-release-1.2.3.apk`** 下载
2. 在手机上打开该文件；若提示「禁止安装未知应用」，按提示允许（安卓装非商店应用的标准流程 ✓）
3. 安装完成后，桌面出现「**聚合视频**」图标即成功 ✓

> 要求 **Android 15（API 35）及以上** ✓ 应用内「设置 → 关于」可确认版本为 **1.2.3** ✓

### 🍎 macOS（Apple 芯片 M 系列）

1. 在 **"Assets"** 区域下载 **`HyperOnlineVideo-1.2.3-arm64.dmg`**
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

> 首次编译约 5 分钟 ✓ WebEngine 登录窗在 NixOS 下默认不链接（登录请手动放置 cookie 文件，与应用内说明一致 ✓）

---

## 二、本版亮点（v1.2.3 更新内容）

### 📺 B 站搜索升级：wbi 签名（三端）

- B 站已对旧版搜索接口的风控收紧（冷请求会被拦截）；本版三端**统一升级为官方现行 wbi 签名协议**：
  - `nav` 获取密钥 → mixin 表生成签名 → `w_rid = md5(query + mixin)`，密钥缓存 6 小时（自动跟进每日轮换 ✓）
  - 策略：**wbi 优先，失败自动回退旧接口** ✓（双保险，用户无感 ✓）
- 实测：三端均返回 20 条搜索结果 ✓（Android 真机 / Linux 装机版 / macOS ✓）

### 📱 Android：播完自动连播加固

- 修复"某些环境播完不切下一首"的边界：mpv 结束后 `time-pos/duration` 会被卸载，
  个别环境结束标志不翻转 → 新增**稳健兜底判据**（曾正常播放过 + 双属性失效 = 已结束 ✓）
- 新增诊断日志（播放结束/切歌/守卫值 ✓）——同类问题一行定位 ✓
- 说明：若您此前关闭过「设置 → 播放 → 播完自动连播（队列）」，播放器播完会按设计退出 ✓ 请确认该开关为开启状态 ✓

### 🔧 维护核查

- yt-dlp：三端均为**最新稳定版 2026.08.19** ✓（Android 启动自更新 ✓ / macOS 跟随 Homebrew ✓ / Linux 跟随发行版 ✓）
- 四平台 API 审计：YouTube（yt-dlp ✓）/ 网易云 / QQ 音乐 均为**现行最优协议** ✓ 无需变更 ✓

---

## 三、完整更新日志（自 v1.2.2 起）

- **B站**：搜索升级 wbi 签名（三端同款 · 旧接口自动回退 · 密钥 6h 缓存）
- **Android**：播完自动连播稳健兜底 + 三处诊断日志
- **维护**：yt-dlp 版本核查（2026.08.19 = 最新稳定 ✓）；四平台 API 审计与存档
- **工程**：B站搜索解析逻辑三端抽公共函数（去重）；文档补充（踩坑 #289/#290）

---

## 四、已知问题与说明

- **B站登录**：部分功能（1080P+/字幕等）需登录；升级后若搜索异常，请在应用内重新登录 B 站 ✓
- **Apple 公证**：macOS 应用未做付费公证，首次打开需按第一节方法绕过（正常现象 ✓）
- **QQ 音乐**：若提示"需重新登录"，在应用内「登录」页重新登录即可 ✓

---

## 五、致谢

感谢真实使用场景中的每一条反馈 —— 本版的 B站 wbi 升级与 Android 连播加固均源于日常使用观察 ✓
发现问题欢迎开 [Issue](https://github.com/HougeLangley/HyperOnlineVideo/issues) ✓
