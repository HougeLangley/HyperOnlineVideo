# Linux 打包说明（P4）

三种分发形态，均为「不上架商店」的自用/分享方式。

| 形态 | 文件 | 特点 |
|---|---|---|
| **Arch 包** | `PKGBUILD` | 最贴合你的环境；`makepkg -si` 一条命令 |
| **AppImage** | `build-appimage.sh` | 便携、免安装、跨发行版 |
| **Flatpak** | `org.hougelangley.HyperOnlineVideo.yml` | 沙箱化、依赖可控（本地构建，不必上 Flathub）· **清单已修正但未实测构建**（本机磁盘不足，见下） |

## 共同前置：构建依赖

```bash
# Arch
sudo pacman -S --needed base-devel cmake ninja pkgconf qt6-base qt6-declarative mpv ffmpeg yt-dlp
# Fedora
sudo dnf install -y cmake ninja-build pkgconf-pkg-config qt6-qtbase-devel qt6-qtdeclarative-devel mpv mpv-devel ffmpeg yt-dlp
```

## 用法

```bash
# Arch 包
cd desktop/packaging && makepkg -si

# AppImage（自动下载 linuxdeploy）
cd desktop && bash packaging/build-appimage.sh

# Flatpak（本地构建并安装）
flatpak-builder --user --install --force-clean build-dir desktop/packaging/org.hougelangley.HyperOnlineVideo.yml
flatpak run org.hougelangley.HyperOnlineVideo
```


## Flatpak 状态说明（✅ 2026-09-12 实构建通过）

**产物**：`HyperOnlineVideo.flatpak`（单文件 bundle，40.3 MB）· 已安装应用体积 54.4 MB

**完整的模块链**（顺序不可乱）

| 顺序 | 模块 | 说明 |
|---|---|---|
| 1 | libass 0.17.5 | mpv 0.36+ 的**硬依赖**（本项目字幕走 App 层渲染，但仍必须存在） |
| 2 | python-glad2 | libplacebo 的 OpenGL 后端生成 GL 加载器需要（SDK 无，用 pip 装到 /app） |
| 3 | libplacebo v7.351.0 | 同样是硬依赖；**关掉 Vulkan**（见下） |
| 4 | mpv 0.41.0 | 只出 libmpv（`-Dcplayer=false`） |
| 5 | yt-dlp 2026.08.19 | 官方单文件版，固定 sha256 |
| 6 | hov-qt | 主程序（CMake 在 desktop/ 子目录，故用 simple 构建） |

**构建命令**

```bash
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y flathub org.kde.Platform//6.11 org.kde.Sdk//6.11   # 下载 1.5 GB / 占用 5.3 GB
flatpak-builder --user --install --force-clean build-dir \
  desktop/packaging/org.hougelangley.HyperOnlineVideo.yml
flatpak run org.hougelangley.HyperOnlineVideo
# 导出可分发的单文件包：
flatpak build-bundle ~/.local/share/flatpak/repo HyperOnlineVideo.flatpak org.hougelangley.HyperOnlineVideo
```

**验证记录**

| 项 | 结果 |
|---|---|
| 应用内自检 | `/app/bin` 有 hov-qt 与 yt-dlp；`ldd hov-qt` 引用 **libmpv** ✓；`yt-dlp --version` → 2026.08.19 ✓ |
| 本地播放 | **截图确认**：沙箱内播放彩条视频，时间 `0:16/0:20`、控制条与播放列表正常 |
| 在线能力 | 沙箱内 `yt-dlp` 搜索**返回真实 YouTube 结果** ✓ |

**三个必须知道的坑（都已实测定位）**

1. **`runtime-version: '6.8'` 已从 Flathub 下线** → 必须用 6.11/6.10（`flatpak remote-ls` 可查）
2. **mpv 0.36+ 把 libass/libplacebo 变成硬依赖**：mpv 0.38/0.39/0.40/0.41 的 meson 里**没有**对应开关，只能先建这两个库
3. **libplacebo 关掉 Vulkan**，原因有二：① App 用 libmpv 的 **OpenGL render API**，不需要 Vulkan 后端；② libplacebo 7.351 的 Vulkan 代码生成脚本（`src/vulkan/utils_gen.py`）与 SDK 的 **Python 3.13 不兼容**（`VkXML(ET.parse(...))` 类型错误）

另外两条环境细节：pip 访问 PyPI 需要在模块上加 `build-options: build-args: [--share=network]`；Flatpak 沙箱内 **`/tmp` 是私有的**，测试文件要放家目录（`--filesystem=home` 已授权）。


## 已知限制（打包相关）

| 项 | 说明 |
|---|---|
| **libmpv 依赖** | 当前链接系统 libmpv（Arch 的 `mpv` 包自带头与库）。Flatpak 里需在 runtime 中提供，或后续改为随包分发 |
| **yt-dlp / ffmpeg** | 走系统包（Flatpak 需在清单里追加对应 module） |
| **KMP 共享核心** | 接入后需把 `hovcore` 的 `.so` 与头文件一并打包（或作为独立 `hovcore` 包提供） |
| **GPL-3.0** | 所有分发形态都需附 LICENSE 与对应源码（本项目已开源，天然合规） |

## 各形态的「App 内登录」能力（2026-09-12 决策）

| 形态 | 是否含内置登录窗口 | 原因 / 替代方案 |
|---|---|---|
| **AppImage** | ❌ 不含 | QtWebEngine 依赖达 **187 MB**，会让便携包从 ~90 MB 膨胀到 300 MB+；改为**手动放置 cookie 文件**（`~/.config/hov/cookies/<站点>.txt`，与 Android 导出格式一致） |
| **Arch 包** | ✅ 含 | 本机原生安装，`qt6-webengine` 以 **optdepends** 声明；装上即可在应用内登录 |
| **Flatpak** | ❌ 降级 | `org.kde.Platform` 运行时不含 QtWebEngine；清单**未**额外打包（构建 QtWebEngine 成本过高），应用会给出提示 |
| 开发构建 | ✅ 含 | 直接 `cmake -B build` 即可（检测到 QtWebEngine 就启用） |

无论哪种形态，**cookie 文件路径与格式完全一致**，所以"在 Arch 版里登录一次 → 把文件拷到 AppImage/Flatpak 的用户目录"也能直接生效。
