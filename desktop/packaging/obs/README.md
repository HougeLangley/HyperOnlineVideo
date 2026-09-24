# openSUSE OBS 打包（hov-qt）

本目录是 **openSUSE Build Service（OBS）** 的包定义。与 Fedora 的 `../rpm/` **并行维护**
（两边的 devel 包命名不同，无法合一）。

## OBS 侧现状

| 项 | 值 |
|---|---|
| 工程 | `home:houge` |
| 包 | `hov-qt` |
| 仓库 | `openSUSE_Tumbleweed`（**x86_64 / aarch64 / riscv64**）+ `openSUSE_Leap_16.0`（**x86_64 / aarch64**）|
| 当前版本 | TW `1.2.1-7.1` · Leap 16.0 `1.2.1-lp160.7.1`（OBS release = 提交修订号 ✓ 自动 ✓）|
| 构建状态 | 5 目标全部 `state="published"` ✓（TW×3 + Leap16×2 ✓）|
| 架构说明 | ⚠️ **Leap 16.0 官方无 riscv64 端口** ✗（发布仓无 leap 目录 ✓ 穷尽求证 ✓）→ **riscv64 仅 Tumbleweed** ✓ |
| 下载源 | TW: https://download.opensuse.org/repositories/home:/houge/openSUSE_Tumbleweed/ · Leap16: …/home:/houge/openSUSE_Leap_16.0/ |

## 用户安装（openSUSE Tumbleweed）

```bash
# Tumbleweed（x86_64 / aarch64 / riscv64）
sudo zypper addrepo https://download.opensuse.org/repositories/home:/houge/openSUSE_Tumbleweed/ obs-houge
sudo zypper install hov-qt          # ⚠️ 不要加 --repo obs-houge ✗
# Leap 16.0（x86_64 / aarch64）
sudo zypper addrepo https://download.opensuse.org/repositories/home:/houge/openSUSE_Leap_16.0/ obs-houge-leap
sudo zypper install hov-qt
```

> ⚠️ **`--repo obs-houge` 是坑** ✗（实测 2026-09-24 ✓）：该参数会把**整个依赖求解**限制在这个仓库内 ✗
> → 报 “没有软件源能提供 … 所需的 ffmpeg” ✗ 装不上 ✓
> 正确做法：`zypper install hov-qt`（依赖从主仓自动解析 ✓ 实测自动拉取 ffmpeg/mpv/yt-dlp ✓）

## 维护者操作（本机 `~/obs/home:houge/hov-qt`）

```bash
# 新版本发布流程：
#   1) 造源码包（顶层目录 hov-qt-<ver>/，mtime 归一化）
#   2) 更新 spec（Version:）与 changes（新增一条，格式见下）
#   3) 提交
cd ~/obs/home:houge/hov-qt
cp /path/hov-qt-<ver>.tar.gz .
osc add hov-qt-<ver>.tar.gz          # 新 tarball 需要 add；同名覆盖则不用
osc rm hov-qt-<旧ver>.tar.gz         # 删旧包（OBS 会保留历史 ✓）
osc ci -m "hov-qt <ver>: <说明>"
osc results home:houge hov-qt        # 看构建状态
osc buildlog home:houge hov-qt openSUSE_Tumbleweed x86_64   # 失败看日志
```

## `.changes` 格式（**必须**，格式错会被 validator 拒 ✗）

```
-------------------------------------------------------------------
Wed Sep 23 00:00:00 UTC 2026 - Name <mail@example.com>
                                  ↑ 注意：完整日期+时间+时区 + " - " 分隔
- 变更条目
```

❌ 只写 `Wed Sep 23 2026 Name <mail>`（缺时间/时区）会被 source_validator 判 **INVALID DATE** ✗

## spec 关键点（都是实测踩坑后固化 ✓）

1. **`BuildRequires: hicolor-icon-theme`** 必须写 ✓
   - 图标装在 `/usr/share/icons/hicolor/**` ✗ 而这些**目录**在构建根里必须"有主" ✓
   - 写成 `Requires:` 无效 ✗（filelist 检查在**构建期**跑 ✓）
   - 官方 `mpv` / `qbittorrent` 都是 `BuildRequires:` ✓（照抄 ✓）
2. **`BuildRequires: qt6-webenginewidgets-devel`** ✓（**有** ✓ 曾经的"没有"是错误结论 ✗）
   - 正确判定法 ✓：`zypper wp "cmake(Qt6WebEngineWidgets)"`（✗ 不要用 `se --provides ...Config.cmake` ✗ 语义不对 ✗）
   - 实测：`qt6-webenginewidgets-devel` + `libQt6WebEngineWidgets6 6.11.2` 都在 OSS 仓 ✓
   - App 内登录窗因此**可用** ✓（与 Arch/Fedora 包一致 ✓ 启动器已设 `LC_NUMERIC=C` ✓ 无崩溃风险 ✓）
3. **运行时用包名** ✓：`Requires: ffmpeg mpv yt-dlp`
   - ✗ **不要用文件路径**（`Requires: /usr/bin/ffmpeg`）：实测**首次安装**（依赖未装）时解析失败 ✗
     —— zypper 不用 filelists 做依赖解析 ✓（Fedora 端用路径式是为了避开 ffmpeg/ffmpeg-free 冲突 ✗ 两端情况不同 ✓）
4. **`kf6-kwindowsystem-devel`** 是可选 ✓ 有则启用 KDE 窗口模糊（CMake 里 QUIET find ✓）

## 功能对齐（2026-09-24 修正 ✓）

OBS 包与 Arch / Fedora 包**功能完全一致** ✓（含 App 内登录窗口 ✓）。
> ⚠️ 曾经一度 `-DHOV_WEBENGINE=OFF` ✗ —— 起因是**判定命令用错**（见上第 2 条 ✓），
> 导致"无必要的降级" ✗；OpenSUSE 的 `qt6-webenginewidgets-devel` **一直都有** ✓。

无论哪个渠道，登录窗都不是必须的：cookie 也可用 `--import-cookies <浏览器>` 导入，
或手动放到 `~/.config/hov/cookies/<站点>.txt`（Netscape 格式 ✓ 与 Android 版通用 ✓）。
