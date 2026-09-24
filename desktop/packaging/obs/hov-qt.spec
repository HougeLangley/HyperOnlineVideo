# ── Hyper Online Video（Linux Qt6 桌面端）openSUSE / OBS spec ──────────────────
# 说明：
#   1) 本仓库是**单仓库多平台**（Android/macOS/Linux-Qt6）✓
#      桌面端源码在 desktop 子目录 → 用 cmake 的 -S 参数指过去（不用打补丁 ✗）
#   2) 与 Fedora spec（packaging/rpm/hov-qt.spec）**并行维护**：
#      Fedora 用 `qt6-qtbase-devel` 系命名 ✓ openSUSE 用 `qt6-widgets-devel` 系 ✓ 无法合一 ✗
#   3) ✅ WebEngineWidgets **有**（Tumbleweed 实测 ✓ 2026-09-24 复核）：
#      qt6-webenginewidgets-devel + libQt6WebEngineWidgets6 6.11.2 均在 OSS 仓 ✓
#      ✗ 曾用错搜索命令（`se --provides …Config.cmake` 语义不对 ✗）误判为“没有” ✗
#      → 正确判定法：`zypper wp "cmake(Qt6WebEngineWidgets)"` 或 `zypper se -s qt6-webengine*`
#      → 本包**开启** App 内登录窗（与 Arch/Fedora 包一致 ✓；启动器已设 LC_NUMERIC=C ✓ 无崩溃风险 ✓）
#   4) 手册的**唯一真相**是 desktop/packaging/man/hov-qt.1（deb/rpm/obs 共用 ✓ 防漂移 ✗）

Name:           hov-qt
Version:        1.2.1
Release:        0
Summary:        聚合视频 —— YouTube/哔哩哔哩/网易云音乐/QQ音乐 聚合客户端（Qt6 桌面端）
License:        GPL-3.0-only
Group:          Productivity/Multimedia/Video/Players
URL:            https://github.com/HougeLangley/HyperOnlineVideo
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja
BuildRequires:  gcc-c++
BuildRequires:  pkgconf-pkg-config
BuildRequires:  qt6-widgets-devel
BuildRequires:  qt6-openglwidgets-devel
BuildRequires:  qt6-network-devel
BuildRequires:  qt6-dbus-devel
# App 内登录窗口（Chromium）：Tumbleweed 有开发包 ✓ 提供 cmake(Qt6WebEngineWidgets) ✓
# 判定命令（照此复核 ✗ 别凭印象 ✓）：zypper wp "cmake(Qt6WebEngineWidgets)"
BuildRequires:  qt6-webenginewidgets-devel
BuildRequires:  mpv-devel
BuildRequires:  desktop-file-utils
# 可选：有则启用 KDE 合成器窗口模糊（CMake 里是 QUIET find ✓ 没有也能构建 ✓）
BuildRequires:  kf6-kwindowsystem-devel

# 运行时依赖：openSUSE 用**包名**（不是文件路径 ✗）—— 实测教训（2026-09-24 ✓）：
#   路径式 `Requires: /usr/bin/ffmpeg` 在**首次安装**（依赖未装）时解析失败 ✗：
#   “没有软件源能提供 … 所需的 /usr/bin/ffmpeg” ✓（zypper 不用 filelists 做依赖解析 ✗）
#   → 包名最稳 ✓ 且 openSUSE OSS 仓里 ffmpeg/mpv/yt-dlp 齐全 ✓（版本 9.0.1 / 0.41 / 2026.08 ✓）
#   （Fedora 那边仍用路径式：为了避开 RPM Fusion 的 ffmpeg 与系统 ffmpeg-free 冲突 ✗ 两端情况不同 ✓）
Requires:       ffmpeg
Requires:       mpv
Requires:       yt-dlp
BuildRequires:  desktop-file-utils
# ⚠️ 必须是 **BuildRequires**（不是 Requires ✗）—— 实测 + 官方基准双证：
#   OBS 的 filelist 检查在**构建期**做，要求包里用到的目录在**构建根**里有拥有者 ✗
#   否则报 "directories not owned by a package: /usr/share/icons/hicolor ..." → 直接判失败 ✗
#   官方 mpv / qbittorrent 都是这么写的 ✓（照抄 ✓）
BuildRequires:  hicolor-icon-theme

%description
Hyper Online Video 把 YouTube、哔哩哔哩、网易云音乐与 QQ 音乐聚合到同一个客户端，
提供搜索聚合、本地媒体库、离线下载与无广告播放。

播放使用 libmpv；在线地址解析与下载使用 yt-dlp 与 ffmpeg。

App 内登录窗口使用 QtWebEngine（Tumbleweed 的 qt6-webenginewidgets-devel ✓），
登录 YouTube/哔哩哔哩/网易云音乐/QQ音乐 后 cookie 自动落盘。

%prep
%autosetup -n %{name}-%{version}

%build
cd desktop
%cmake -DHOV_WEBENGINE=ON
%cmake_build

%install
cd desktop
%cmake_install
# man 手册：唯一真相一份 ✓ 两个可执行文件各要一个名字 → 生成第二个（软链 ✓）
install -Dpm 0644 %{_builddir}/%{name}-%{version}/desktop/packaging/man/hov-qt.1 \
    %{buildroot}%{_mandir}/man1/hov-qt.1
ln -sf hov-qt.1 %{buildroot}%{_mandir}/man1/hov-qt-bin.1

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/hov-qt.desktop

%files
%license LICENSE
%{_bindir}/hov-qt
%{_bindir}/hov-qt-bin
%{_datadir}/applications/hov-qt.desktop
%{_datadir}/icons/hicolor/256x256/apps/hov-qt.png
%{_datadir}/icons/hicolor/512x512/apps/hov-qt.png
%{_mandir}/man1/hov-qt.1*
%{_mandir}/man1/hov-qt-bin.1*

%changelog
* Thu Sep 24 2026 Houge Langley <hougelangley1987@gmail.com> - 1.2.1-0
- Fix: enable QtWebEngine (in-app login window)
  Tumbleweed does ship qt6-webenginewidgets-devel; the earlier
  "not available" conclusion was based on a wrong zypper query and
  caused an unnecessary feature degradation. Now matches the
  Arch/Fedora builds.

* Wed Sep 23 2026 Houge Langley <hougelangley1987@gmail.com> - 1.2.1-0
- Initial OBS package for openSUSE Tumbleweed
