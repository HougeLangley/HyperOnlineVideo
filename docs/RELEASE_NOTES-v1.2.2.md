# 聚合视频 v1.2.2 —— 音乐体验大版本（Android / macOS / Linux 全平台 · 全发行版）

> 本次更新聚焦**音乐体验与稳定性**：QQ 音乐搜索实现**真·无限下滑翻页**（Desktop 协议，20 首/页）；
> 网易云与 QQ 的**翻页封面**、**下载内嵌封面**全面修复；新增 **CDN 节点择优**与 **mpv 日志泵**，
> 根治"随机不播"；字幕自动选轨、画质上限、长歌词自适应三端对齐；macOS 若干崩溃修复。
>
> Linux 端同样**不提供打包文件下载** —— 全部通过**官方软件源**安装（一键命令、自动更新 ✓），第一节即为完整安装指南。

---

## 一、下载与安装（小白也能看懂 · 复制粘贴即可）

### 📱 Android（手机 / 平板）

1. 在本页下方 **"Assets"** 区域点击 **`app-release-1.2.2.apk`** 下载
2. 在手机上打开该文件；若提示「禁止安装未知应用」，按提示允许（安卓装非商店应用的标准流程 ✓）
3. 安装完成后，桌面出现「**聚合视频**」图标即成功 ✓

> 要求 **Android 15（API 35）及以上** ✓ 应用内「设置 → 关于」可确认版本为 **1.2.2** ✓

### 🍎 macOS（Apple 芯片 M 系列）

1. 在 **"Assets"** 区域下载 **`HyperOnlineVideo-1.2.2-arm64.dmg`**
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

## 二、本版亮点（v1.2.2 更新内容）

### 🎵 音乐 —— QQ 真·无限下滑翻页

- **QQ 音乐搜索现在支持真·无限下滑翻页**（每页 20 首，滑到底自动加载下一页并无限延续 ✓）
  - 采用 QQ 现行 **Desktop 搜索协议**（`DoSearchForQQMusicDesktop` + 数字型分页参数），
    匿名可用、登录后更稳；三端（Android / macOS / Linux）同款实现 ✓
- **翻页封面修复**：搜索结果滑到后面不再"灰块" —— 修复了"同专辑多曲只显示第一张封面"的缩略图缓存缺陷 ✓
- **下载内嵌封面**：下载的音乐现在会把专辑封面**写进文件标签**（mp3 的 `attached_pic`），
  文件管理器 / 播放器都能看到封面 ✓（`ffprobe` 实测验证 ✓）

### 🎧 音乐 —— "随机不播"根因修复

- **网易云 CDN 节点择优**：网易云返回的流地址会随机落在 m701~m804 等节点，个别节点会 403；
  现在会自动探测并选择可用节点（失败自动回退），根治"点了没声音、进度条不动" ✓
- **mpv 日志泵**：新增播放器事件/日志通道（此前播放器错误被静默吞掉），排障能力大幅提升 ✓
- **登录态降级链**：请求失败自动尝试备用通道；QQ 登录态过期时会**明确提示重新登录**（不再静默 0 条 ✓）

### 💬 字幕 —— 自动选轨修复（三端）

- 修复"AI 字幕被标成（自动翻译）导致评分错位、字幕不自动选"的问题；新增裸「中文 / Chinese」兜底匹配 ✓
- B 站字幕改走直连 + 429 退避重试，VPN 环境下不再间歇性拉不到字幕 ✓

### 🎬 画质 —— "视频清晰度上限"设置真正生效

- 修复"设置了 1080p 但实际仍按默认档位播放"的问题（加载顺序 + UI 回填）；Linux/Android 补齐设置项 ✓

### ✍️ 歌词 —— 长句自适应（三端）

- 长歌词不再溢出播放窗口：总高超出时自动逐档缩小字号（下限 0.55×）✓
- macOS 修复"音乐模式背景变黑、歌词变成视频字幕样式"的问题 ✓

### 🛡️ 稳定性 / 其他

- macOS：修复搜索后崩溃（共享缓存改为主线程单一写入口 ✓）
- macOS：修复关闭登录窗崩溃（WebEngine 析构竞态 ✓）
- Linux：修复虚拟机环境登录窗"桌面穿透"（GL 兼容 ✓）
- 播放器：修复"上一曲播完点下一首停在 0:00"的竞态 ✓
- 音乐模式：封面 + 玻璃背景 + 滚动歌词 + 逐字高亮全面对齐三端 ✓

---

## 三、完整更新日志（自 v1.2.1 起）

### 音乐

- QQ 搜索换用现行接口并支持**真分页无限下滑**（三端）
- 网易云 / QQ **翻页封面**预热与回填修复
- 下载音乐**内嵌封面**（Linux / macOS / Android 三端）
- 网易云 **CDN 节点择优**（403 节点自动规避）
- **mpv 事件/日志泵**（播放失败不再静默）
- 登录态失效自动降级 + 明确提示
- 音乐模式：封面/玻璃背景/滚动歌词/逐字高亮对齐（Linux 修复歌词滚动 ✓）
- 音质上限（无损/320k/128k）三端对齐；左下角"音质档位"分流 ✓

### 字幕 / 歌词

- 自动选轨：AI 字幕误标修复 + 裸中文兜底 + B 站直连退避（三端）
- 长歌词自适应缩字（三端）；macOS 音乐模式歌词修复
- 本地 sidecar 歌词（.lrc）显示与滚动修复

### 视频 / 画质

- 清晰度上限设置修复（Linux / macOS / Android 三端生效）
- 视频清晰度 UI 回填与切换修复

### 稳定性

- macOS：搜索崩溃、登录窗崩溃、音乐模式黑屏修复
- Linux：虚拟机 GL 登录窗穿透修复、关窗崩溃修复、播放器竞态修复
- Android：多 buffer 日志、输入法兼容等工程改进

### 打包 / 工程

- Linux 全渠道：Arch（pkgrel=12）· Ubuntu deb · Fedora rpm（Copr）· openSUSE（OBS）· AppImage · NixOS flake
- 三端代码审计清理（重复代码消除、缩进规范化）
- 文档：踩坑百科（#270-#288）、构建矩阵更新

---

## 四、已知问题与说明

- **QQ 音乐搜索**：接口侧当前对无效登录态/匿名访问有风控（返回 code=2001）。应用已内置降级与提示；
  若提示"需重新登录"，请在应用内「登录」页重新登录 QQ 音乐即可恢复（搜索结果与翻页均不受影响 ✓）。
- **Apple 公证**：macOS 应用未做付费公证，首次打开需按第一节方法绕过（正常现象 ✓）。
- **NixOS**：WebEngine 登录窗默认不链接；登录请按应用内说明手动放置 cookie 文件 ✓。

---

## 五、致谢

感谢所有测试与反馈的朋友 —— 1.2.2 的"QQ 无限下滑翻页"与"随机不播"根治均来自真实使用场景的反馈 ✓
发现问题欢迎开 [Issue](https://github.com/HougeLangley/HyperOnlineVideo/issues) ✓
