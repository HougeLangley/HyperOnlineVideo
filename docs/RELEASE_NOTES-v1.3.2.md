# 聚合视频 v1.3.2 —— 桌面界面重做 + Android 12 兼容（全平台）

> Android · macOS · Linux（Arch / Fedora / openSUSE / Debian / Ubuntu / NixOS / Gentoo）
> GPL-3.0-only

## 📱 Android 兼容范围扩大（重要）

**最低支持从 Android 15 降到 Android 12（API 31）**，覆盖更多设备；返回键迁移 `OnBackPressedDispatcher`
（手势/三键行为统一，旧系统不再失效）。要求：**Android 12+（arm64-v8a）**。

## 🖥 桌面端界面重做（Linux/macOS 视觉统一）

设计 token（17 个颜色角色 × 明暗两套）+ Material Symbols 图标 + 组件库；等高 16:9 卡片网格；
macOS 同步落地（不透明底/图标字体/胶囊搜索框）。`scripts/check-ui.py` 保证界面只走 token。

## 🌟 新功能：搜索历史（三端）

空搜索词时显示历史词条（最多 15 条，去重置顶），点词条立即再搜；设置里可开关/清空，纯本地存储。

## 🐛 重要修复

- **拖动进度条后停播**：seek 瞬间被误判"播放结束"→ 自动切条且画面冻结（Android）；三端 mpv 增加
  network-timeout 与断线重连
- **YouTube 画质受限**：SABR 实验下默认客户端只给 360p → `web_safari` 备选 + 档位不足自动带 cookie 重试
  + "请求 2K 实际 1080p"如实提示
- **「点下一个视频出错」**：YouTube 搜索混入频道/播放列表条目 → 过滤 + 失败自动跳过（三端）
- **字幕「时有时无」**：切歌不刷新字幕轨等四根因（Android）
- B站搜索标题 HTML 实体与时长格式（如 `419:2` → `6:59`）；macOS 输入即搜改为回车触发
- Linux 隐藏来源下拉框偶发浮出压住搜索框

## 🙏 特别鸣谢

- **[@Zakkaus](https://github.com/Zakkaus)**：桌面端 splitter/封面比例/B站标题时长四项修复（PR #3），
  以及整套桌面界面重做（设计 token + Material Symbols + 组件库 + check-ui 检查）—— 本版桌面端的
  新面貌主要出自他手
- **[@lishoujun](https://github.com/lishoujun)**：v1.3.0 的 YouTube 直链代理与播放链贡献

## 📦 安装

| 平台 | 方式 |
|---|---|
| **Android 12+**（arm64） | Releases 下载 `hov-1.3.2-arm64.apk` |
| macOS（Apple 芯片） | Releases 下载 `HyperOnlineVideo-1.3.2-arm64.dmg` |
| Arch / Manjaro | AUR：`yay -S hov-qt`（1.3.2-1） |
| Fedora 44/45 | `sudo dnf copr enable houge/hov-qt && sudo dnf install hov-qt` |
| openSUSE（TW / Leap） | OBS `home:houge`（x86_64 / aarch64 / riscv64） |
| Gentoo | gentoo-zh overlay：`media-video/hov-qt` |
| Debian / Ubuntu | Releases 下载 deb |
| 任意发行版 | Releases 下载 AppImage |
| NixOS | flake：`github:HougeLangley/HyperOnlineVideo` |
| 容器制品 | `oras pull ghcr.io/hougelangley/hov-qt:1.3.2` |

**完整变更**：`desktop/packaging/deb/debian/changelog` 与提交历史。
