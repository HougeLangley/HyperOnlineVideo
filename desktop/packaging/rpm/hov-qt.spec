# ── Hyper Online Video（Linux Qt6 桌面端）Fedora RPM spec ─────────────────────
# 说明：本仓库是**单仓库多平台**（Android/macOS/Linux-Qt6）✓
# 桌面端源码在 desktop 子目录 → 用 cmake 的 -S 参数指过去（不用打补丁 ✗）
# 手册的**唯一真相**是 desktop/packaging/man/hov-qt.1（deb/rpm 共用 ✓ 防漂移 ✗）

Name:           hov-qt
Version:        1.2.1
Release:        2%{?dist}
Summary:        聚合视频 —— YouTube/哔哩哔哩/网易云音乐/QQ音乐 聚合客户端（Qt6 桌面端）

License:        GPL-3.0-only
URL:            https://github.com/HougeLangley/HyperOnlineVideo
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  pkgconf-pkg-config
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtwebengine-devel
BuildRequires:  mpv-devel
BuildRequires:  desktop-file-utils

# 用**文件路径**而不是包名（Fedora 规范 ✓）：
#   `Requires: ffmpeg` 会挑 RPM Fusion 的 ffmpeg ✗ 与系统自带的 ffmpeg-free **冲突** ✗（实测踩到 ✓）
#   路径依赖则不挑提供者 ✓ ffmpeg-free 或 ffmpeg 都能满足 ✓
Requires:       /usr/bin/yt-dlp
Requires:       /usr/bin/ffmpeg

%description
Hyper Online Video 把 YouTube、哔哩哔哩、网易云音乐与 QQ 音乐聚合到同一个客户端，
提供搜索聚合、本地媒体库、离线下载与无广告播放。

播放使用 libmpv（可选 gpu-next/libplacebo，带渲染校验与失败回退）；
在线地址解析与下载使用 yt-dlp 与 ffmpeg；需要账号的平台可用内嵌
QtWebEngine 登录窗口完成登录。

（说明：ffmpeg 在 Fedora 上可由 ffmpeg-free 提供 ✓ 无需启用第三方源 ✓）

%prep
%autosetup -n %{name}-%{version}

%build
cd desktop
%cmake -DCMAKE_BUILD_TYPE=Release -DHOV_WEBENGINE=ON
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
* Thu Sep 24 2026 Houge Langley <hougelangley@users.noreply.github.com> - 1.2.1-2
- Fix license tag: GPL-3.0-only (LICENSE grants version 3 only; the
  earlier "or-later" statement was an over-declaration)

* Thu Sep 24 2026 Houge Langley <hougelangley@users.noreply.github.com> - 1.2.1-1
- Update to 1.2.1: download progress/cancel/retry + favorites panel,
  window geometry persistence, PiP and subtitle fixes

* Mon Sep 21 2026 Houge Langley <hougelangley@users.noreply.github.com> - 1.2.0-1
- Initial RPM packaging for Fedora (libmpv playback / yt-dlp downloads / QtWebEngine login)
