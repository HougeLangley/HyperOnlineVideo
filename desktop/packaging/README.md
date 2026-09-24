# Linux 打包说明（六种分发渠道 · 2026-09-24 全面验证）

本目录是**所有 Linux 分发形态的唯一入口**。每个渠道一个子目录/文件，互不干扰；
**各渠道的 spec/control 必须按发行版分开维护**（依赖命名、WebEngine 可用性、路径依赖行为都不同 ✗ 不可强行合一）。

## 一、渠道总表

| # | 渠道 | 定义文件 | 发布仓库 | 版本 | 状态 |
|---|---|---|---|---|---|
| 1 | **Arch** | `PKGBUILD` | AUR 待定（本地 `makepkg`）| 1.2.1 | ✓ 历史验证 |
| 2 | **Ubuntu / Debian** | `deb/debian/**` | 本地 .deb（未来可 PPA）| `1.2.1-1` | ✓ 2026-09-24 全流程 |
| 3 | **Fedora / RHEL** | `rpm/hov-qt.spec` | **Copr** `houge/hov-qt` | `1.2.1-1` | ✓ 2026-09-24 六架构 |
| 4 | **openSUSE** | `obs/hov-qt.spec` + `obs/hov-qt.changes` | **OBS** `home:houge` | `1.2.1-7.1` | ✓ 2026-09-24 五目标 |
| 5 | **AppImage** | `build-appimage.sh` | 便携单文件 | 1.2.1 | ✓ 脚本就绪 |
| 6 | **Flatpak** | `org.hougelangley.HyperOnlineVideo.yml` | 本地 bundle | 1.2.0 | ✓ 已构建过 |
| 7 | **AUR（Arch）** | `aur/PKGBUILD` + `aur/.SRCINFO` | **AUR** `hov-qt` | 1.2.1 | ⏳ 全流程预演 ✓ 待 GitHub tag |

**上游源码包（三渠道共用同一份 ✓ 单一真相）**：
```bash
# 在仓库根目录生成（内容一致，仅命名不同）：
#   hov-qt-1.2.1.tar.gz         ← Fedora spec / OBS spec 的 Source0
#   hov-qt_1.2.1.orig.tar.gz    ← Debian/Ubuntu 的 orig tarball
COPYFILE_DISABLE=1 tar czf hov-qt-1.2.1.tar.gz      --exclude='._*' hov-qt-1.2.1
COPYFILE_DISABLE=1 tar czf hov-qt_1.2.1.orig.tar.gz --exclude='._*' hov-qt-1.2.1
```
- 顶层目录必须是 `hov-qt-1.2.1/`（rpm 的 `%autosetup -n` 与 dpkg-source 都依赖 ✓）
- **排放**：`.git/ .gradle/ app/build/ core/build/ macos/.build/ macos/build/ desktop/build*  __pycache__/ .DS_Store ._*`
- **硬校验**：`tar tzf … | grep -cE '(^|/)\._|__MACOSX'` 必须为 **0**（避坑 #174 ✓）
- deb 专用：tarball 根部**不得**有 `debian/` ✗（构建时从 `deb/debian/` 复制到源码树根 ✓）

## 二、各渠道速查

### 1) Arch — `PKGBUILD`
```bash
cd desktop/packaging && makepkg -si
```
- `depends=('qt6-base' 'qt6-declarative' 'qt6-webengine' 'mpv' 'ffmpeg' 'yt-dlp')`
- WebEngine **ON** ✓（`qt6-webengine` 在 Arch 主仓 ✓）
- 依赖用**包名** ✓（Arch 无 ffmpeg/ffmpeg-free 分裂问题 ✓）

### 2) Ubuntu / Debian — `deb/debian/**`
**标准流程（隔离环境 ✓ 规则 #11）**：
```bash
# ① 打 orig 源码包（macOS 上 ✓ 防 AppleDouble）
# ② 建隔离环境（打包机 10.6.146.152）
sudo apt-get install -y debootstrap systemd-container
sudo debootstrap --arch=amd64 --variant=buildd \
     --components=main,restricted,universe,multiverse \
     resolute /var/lib/machines/hov-resolute-amd64 http://archive.ubuntu.com/ubuntu/
# ③ 容器装构建依赖
sudo systemd-nspawn -D /var/lib/machines/hov-resolute-amd64 --resolv-conf=copy-host -M hov-build-env \
  -- /bin/bash -c 'export DEBIAN_FRONTEND=noninteractive; apt-get update;
     apt-get install -y build-essential dpkg-dev debhelper cmake ninja-build pkgconf \
       qt6-base-dev qt6-declarative-dev qt6-webengine-dev libmpv-dev lintian fakeroot xvfb tzdata'
# ④ 构建（源码树 bind 进 /build ✓ 加 debian/ 到根）
sudo systemd-nspawn -D /var/lib/machines/hov-resolute-amd64 --resolv-conf=copy-host \
  --bind=$PWD/build-area:/build -M hov-build \
  -- /bin/bash -c 'cd /build/hov-qt-1.2.1 && dpkg-checkbuilddeps && dpkg-buildpackage -us -uc'
# ⑤ 销毁隔离环境（规则 #11 ✓）
sudo rm -rf /var/lib/machines/hov-resolute-amd64
```
- 依赖：`qt6-base-dev qt6-declarative-dev qt6-webengine-dev libmpv-dev`（Debian 系命名 ✓）
- WebEngine **ON** ✓（与 Arch 对齐 ✓）
- 版本放 `deb/debian/changelog`（**星期必须真实** ✗ lintian 会查 ✓）
- 产 7 件：`.deb` / `-dbgsym` / `.dsc` / `.debian.tar.xz` / `.buildinfo` / `.changes` / `.orig.tar.gz`

**多架构（aarch64 / riscv64 via qemu-user ✓ 2026-09-24 落地）**：
```bash
# ① 宿主机装 binfmt（安装后 arm64/riscv64 二进制透明执行 ✓）
sudo apt-get install -y qemu-user-binfmt qemu-user
# ② debootstrap（binfmt 生效后**不需要** --foreign ✗ 直接跑 ✓）
#    aarch64 与 riscv64 都要走 ports 镜像 ✓（不是 archive.ubuntu.com ✗）
sudo debootstrap --arch=arm64   --variant=buildd --components=main,restricted,universe,multiverse \
     resolute /var/lib/machines/hov-resolute-arm64   http://ports.ubuntu.com/ubuntu-ports
sudo debootstrap --arch=riscv64 --variant=buildd --components=main,restricted,universe,multiverse \
     resolute /var/lib/machines/hov-resolute-riscv64 http://ports.ubuntu.com/ubuntu-ports
# ③ 挂载 + chroot 装依赖（⚠️ 该 systemd-nspawn 不认 --architecture ✗ 用 chroot 更直接 ✓）
C=/var/lib/machines/hov-resolute-arm64
for m in proc sys dev dev/pts; do sudo mount --bind /$m $C/$m; done
sudo chroot $C /bin/bash -c 'export DEBIAN_FRONTEND=noninteractive; apt-get update;
   apt-get install -y build-essential dpkg-dev debhelper cmake ninja-build pkgconf \
     qt6-base-dev qt6-declarative-dev qt6-webengine-dev libmpv-dev lintian fakeroot xvfb tzdata'
#    ⚠️ riscv64：**qt6-webengine-dev 不存在** ✗ → 依赖列表去掉它 ✓（control 里已加 [!riscv64] 限定 ✓）
# ④ 构建（bind 源码进 /build ✓）
sudo mount --bind ~/hov-pack-121/build-area-arm64 $C/build
sudo chroot $C /bin/bash -c 'cd /build/hov-qt-1.2.1 && dpkg-checkbuilddeps && dpkg-buildpackage -us -uc -j6'
# ⑤ 销毁（规则 #11 ✓）
```
- **riscv64 的 WebEngine**：Ubuntu 端口仓无 `qt6-webengine-dev` ✗ → `control` 用
  `qt6-webengine-dev [!riscv64]` 架构限定 ✓ CMake `QUIET find_package` 自动降级 ✓（登录窗变提示 ✓ 与 AppImage 一致 ✓）

### 3) Fedora / RHEL — `rpm/hov-qt.spec`（Copr 构建）
**Copr 项目**：`houge/hov-qt`（`https://copr.fedorainfracloud.org/coprs/houge/hov-qt/`）
```bash
# 造 SRPM（Fedora 打包机 10.6.146.194）
rpmbuild -bs --define "_topdir ~/hov-rpm-121" SPECS/hov-qt.spec
# 提交（六 chroot 自动并行 ✓）
copr-cli build hov-qt ~/hov-rpm-121/SRPMS/hov-qt-1.2.1-1.fc44.src.rpm
# 状态 / 下载
copr-cli status <build-id>
copr-cli download-build <build-id>
```
- **chroot 矩阵**：`fedora-44/45 × x86_64/aarch64/riscv64`（6 个 ✓）
- 依赖：`qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtwebengine-devel mpv-devel`（Fedora 命名 ✓）
- `Requires` 用**文件路径**（`/usr/bin/ffmpeg`）✓ —— 避开 RPM Fusion 的 `ffmpeg` 与系统 `ffmpeg-free` 冲突（避坑 #241 的反面情形 ✓）
- WebEngine **ON** ✓（`-DHOV_WEBENGINE=ON`；riscv64/aarch64 的 `qt6-qtwebengine-devel` 均可用 ✓）
- `%changelog` 星期必须真实 ✗ rpmspec 会查（避坑同 deb ✓）

### 4) openSUSE — `obs/hov-qt.spec` + `obs/hov-qt.changes`（OBS 构建）
**OBS 项目**：`home:houge`（`https://build.opensuse.org/project/show/home:houge`）
```bash
cd ~/obs/home:houge/hov-qt
cp <new tarball> . ; osc add hov-qt-<ver>.tar.gz   # 新名字才需 add
osc service localrun source_validator   # .changes 格式校验 ✓ 避坑 #236
osc ci -m "hov-qt <ver>: ..."
osc results home:houge hov-qt           # 看构建
```
- **目标矩阵**：`openSUSE_Tumbleweed`（x86_64/aarch64/riscv64 ✓）· `openSUSE_Leap_16.0`（x86_64/aarch64 ✓）
  - ⚠️ **Leap 16.0 无官方 riscv64 端口** ✗（官方发布仓无 leap 目录 ✓ 已穷尽求证 ✓）
- 依赖：`qt6-widgets-devel qt6-openglwidgets-devel qt6-network-devel qt6-dbus-devel qt6-webenginewidgets-devel mpv-devel`（openSUSE 命名 ✓）
- **`BuildRequires: hicolor-icon-theme`** 必须写（避坑 #237 ✓）
- `Requires` 用**包名**（`ffmpeg mpv yt-dlp`）✓（避坑 #241 ✓ 路径式首次安装解析失败 ✗）
- `.changes` 格式：`Thu Sep 24 00:00:00 UTC 2026 - Name <mail>`（**完整时间+时区** ✗ 避坑 #236 ✓）
- 用户安装：
  ```bash
  sudo zypper addrepo https://download.opensuse.org/repositories/home:/houge/openSUSE_Tumbleweed/ obs-houge
  sudo zypper install hov-qt        # ⚠️ 不加 --repo ✗ 会把依赖求解锁进仓库（避坑 #240）
  ```

### 5) AppImage — `build-appimage.sh`
```bash
cd desktop && bash packaging/build-appimage.sh
```
- WebEngine **OFF**（有意为之 ✗ 依赖 187MB 会让包从 ~90MB 膨胀到 300MB+ ✗）→ 手动放 cookie ✓

### 6) Flatpak — `org.hougelangley.HyperOnlineVideo.yml`
**模块链顺序不可乱**（libass → python-glad2 → libplacebo（关 Vulkan）→ mpv（只出 libmpv）→ yt-dlp → hov-qt）
- WebEngine **不含**（`org.kde.Platform` 运行时无 QtWebEngine ✗ 构建成本过高 ✗）→ 应用自动降级 ✓
- `runtime-version` 必须 ≥ 6.10（6.8 已从 Flathub 下线 ✓）

### 7) AUR（Arch User Repository）
```bash
bash scripts/publish-aur.sh          # 演练（默认不推送 ✗）
bash scripts/publish-aur.sh --push   # 正式发布（推送 ✓ 发布动作由用户决定 ✓）
```
- AUR 是 **Git 仓库** ✗ 不是文件上传 ✗（`ssh://aur@aur.archlinux.org/hov-qt.git` ✓ 首推即建包 ✓）
- 仓库内只需两文件：`aur/PKGBUILD`（source=GitHub tag archive ✓）+ `aur/.SRCINFO`（makepkg 生成 ✓）
- 依赖用**包名** ✓（Arch 无 ffmpeg 分裂问题 ✓）；WebEngine **ON** ✓（`qt6-webengine` 在主仓 ✓）
- 详见 `aur/README.md`（含预演证据 ✓ 维护流程 ✓）

## 三、"App 内登录窗"能力矩阵（三端 + 七渠道）

| 渠道 | 登录窗 | 原因/替代 |
|---|---|---|
| Arch / Ubuntu deb / Fedora Copr / openSUSE OBS | ✅ 含 | 发行版仓有 QtWebEngine ✓ |
| AppImage | ❌ 降级 | 体积代价 ✗ → 手动放 cookie ✓ |
| Flatpak | ❌ 降级 | runtime 不含 ✗ → 自动提示 ✓ |
| macOS | ✅ | WKWebView（系统自带 ✓）|
| Android | ✅ | 系统 WebView ✓ |

cookie 路径三端一致：`~/.config/hov/cookies/<site>.txt`（Netscape 格式 ✓ 跨端可拷贝 ✓）

## 四、验证基线（每次打包后逐项核对 ✓）

| 项 | 期望 |
|---|---|
| 启动器修复 | `grep -c LC_NUMERIC /usr/bin/hov-qt` = **5**；`QT_NO_GLIB` = **2**；`ulimit` = **1** |
| 队列自检 | `hov-qt --queue-selftest` → **120/120**（UTF-8 环境）或 **117/117**（POSIX/容器环境）|
| 文件就位 | `/usr/bin/hov-qt` `/usr/bin/hov-qt-bin` desktop / man×2 / 图标×2 / LICENSE |
| deb 专项 | `lintian …changes` 退出码 **0** |
| rpm 专项 | `desktop-file-validate` 通过；rpmlint 无**真实**错误（拼写检查误报 libmpv/ffmpeg 等 ✗ 可忽略 ✓）|
| 安装验证 | 在对应打包机安装 + 自检通过（规则 #7 ✓）|

## 五、经验与坑（每条都实测过 ✓ 详细见 Obsidian 05-踩坑百科 #236~#246）

| # | 一句话 |
|---|---|
| 236 | OBS `.changes` 必须带**时间与时区** ✗ 只写日期被 validator 拒 |
| 237 | 图标目录拥有者包必须写 **BuildRequires** ✗（不是 Requires ✗）|
| 238 | 判定"包不存在"要用对命令 ✗（`zypper wp "cmake(Qt6X)"` ✓ 不当搜索会假阴性 ✗）|
| 239 | OBS `finished` ≠ **published** ✗ 发布异步 ✓ 要等 state="published" |
| 240 | `zypper install --repo X` 会限制**依赖求解** ✗ → 不要加 |
| 241 | openSUSE 用**包名**依赖 ✓ / Fedora 用**路径**依赖 ✓（两端相反 ✓ 别搞混）|
| 242 | Copr 开 chroot 是 `--chroot`（可重复）✗ 不是 `--chroots` |
| 243 | Copr 每 chroot 状态查 `/api_3/build-chroot?build_id=&chrootname=` ✓（build JSON 的 chroots 是无状态的字符串列表 ✓）|
| 244 | 容器内 `apt-get install <本地deb>` 会卡（man-db 触发器 ✗）→ 用 `dpkg -i` + `apt-get -f install` ✓ |
| 245 | debootstrap 的 buildd 变体没有 tzdata ✗ → nspawn 报 localtime 挂载错（可忽略 ✓ 非致命 ✓）|
| 246 | macOS tar 带 `LIBARCHIVE.xattr.com.apple.provenance` 头 ✗（Linux 侧只是警告 ✓ 无实害 ✓）|

## 六、已知限制

| 项 | 说明 |
|---|---|
| libmpv | 链接系统 libmpv（发行版包提供 ✓） |
| yt-dlp / ffmpeg | 系统包（各渠道已声明依赖 ✓）|
| KMP 共享核心 | 未来接入后需打包 `hovcore` .so（或独立包 ✓）|
| GPL-3.0 | 各渠道均附带 LICENSE 与源码 ✓ |
