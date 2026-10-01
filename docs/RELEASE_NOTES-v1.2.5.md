# 聚合视频 v1.2.5 —— YouTube 直链可播性预检 + 全平台出口指引 + 日志时间前缀

> 修复性更新：针对受限网络出口（VPS/数据中心 IP）导致的 YouTube「解析成功但无法播放（403）」，
> 新增**可播性预检与明确指引**（Linux / macOS / Android 三端 ✓）；全部日志加入**时间前缀** ✓。

## 一、本版亮点
### 🔎 YouTube 直链可播性预检（Issue #1 ✓ 三端）
- 背景：部分出口被 YouTube 施加“仅探测”策略（>64KB 的 Range 一律 403 ✗）→ 解析成功但 mpv 打不开 ✗
- 修复：交给播放器前先做 **4MB Range 探针**（正常 200/206 ✓；受限 403 ✗）→ 命中即**明确提示更换代理节点/线路** ✓
- 覆盖：Linux（Qt 桌面）· macOS · Android（Toast ✓）

### 🕒 日志时间前缀（社区建议 ✓）
- 所有日志统一 `[HH:MM:SS]` 前缀（含 qInfo/qWarning 与 stderr ✓）——排障对照更直观 ✓

## 二、下载与安装
安装方式与 v1.2.4 相同：Android APK / macOS DMG / Arch（AUR）/ Fedora（Copr）/ openSUSE（OBS）/ Ubuntu 26.04 / Debian 13 / AppImage / NixOS ✓
（详细命令见 v1.2.4 说明或仓库 README ✓）

## 三、更新日志（自 v1.2.4 起）
- **三端**：YouTube 直链 4MB Range 预检 + 受限出口指引 ✓
- **桌面端**：64 处 stderr 日志 + qInfo 全量时间前缀（hovLog / messageHandler ✓）
- **工程**：Issue #1 根因定位与复现实验（出口策略 ✗ 非应用缺陷 ✓）

## 四、已知问题
- 受限出口（VPS/数据中心 IP）下仅有提示、无法解除媒体封锁 ✗（YouTube 服务端策略 ✓）→ 请更换代理线路 ✓
- Ubuntu 24.04 / Debian 12（Qt < 6.5）暂不支持 ✓
