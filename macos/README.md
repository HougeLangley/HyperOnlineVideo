# 聚合视频 · macOS 端（Phase 5）

纯 **SPM + AppKit** 实现（**不需要 Xcode 工程**），libmpv 通过 system library 目标接入。

## 构建与运行

```bash
brew install mpv                 # 提供 /opt/homebrew/include/mpv/*.h 与 libmpv.dylib
cd macos && swift build          # 产物 .build/debug/HyperOnlineVideo
.build/debug/HyperOnlineVideo --open <本地文件或直链> --exit-after 15 --selfcheck
```

自动化参数（与 Linux/Qt 端同一套风格，无鼠标点击也能全流程验证）：

| 参数 | 作用 |
|---|---|
| `--open <路径/URL>` | 播放本地文件或**直链**（在线解析由后续的 App 层负责，与 Qt 端一致） |
| `--exit-after <秒>` | N 秒后自动退出（自动化必备） |
| `--selfcheck` | 结束时打印 `[SPIKE]`/`[SELFTEST]` 判据行并以退出码表示成败 |
| `--dump-frame <png>` | **应用内抓帧存 PNG**：macOS 的 `screencapture` 需要"屏幕录制"权限，测试环境常没有，自抓最可靠 |

## Spike 结果（Phase 5.1，2026-09-13，Apple Silicon）

| 场景 | 结果 |
|---|---|
| 本地视频（640×360 H.264 + AAC） | `dur=10.0 maxPos=8.7 渲染帧=266` → **PASS** |
| 网络直链（yt-dlp 解析出的 googlevideo URL） | `视频=640x360 音频=floatp 44100Hz 2ch 渲染帧=578` → **PASS（播放✓ 渲染✓ 音频✓）** |
| 画面朝向 | 用"上半红/下半蓝"素材判定：需 `FLIP_Y=1`（与 Qt 的 `QOpenGLWidget` 同样的翻转目标） |
| 抓帧取证 | 本地 testsrc 彩条 ✓ / 网络流 Big Buck Bunny 片头 ✓（方向均正确） |

结论：**libmpv render API 在 macOS 上可用**（画面进 NSView、声音走 coreaudio），Phase 5 的方案不需要换技术路线。

## 与 Linux/Qt 端的关系

架构一致：App 层持有 `mpv_render_context`，mpv 通知"有新帧"→ 视图重绘 → `draw` 里 render，
之后字幕/歌词/封面同样由 App 层绘制（Qt 端已验证该路线）。差异仅在：Qt 用 `QOpenGLWidget`+`QPainter`，
macOS 用 `NSOpenGLView`+CoreGraphics。

## Phase 5.2（网络/逻辑层，✅ 2026-09-13）

与另两端共用同一套配置文件（`~/.config/hov/`：settings/queue/favorites/progress/cookies），
已在 macOS 侧实现并验证：

| 模块 | 说明 |
|---|---|
| `Config/Settings` | 键名与 Qt 端一致；`validate` 拒绝未知键/越界/类型错；音质上限裁剪 |
| `ProgressStore/Favorites/PlayQueue` | JSON 结构与 Qt 端**逐字段兼容**（可跨端读写） |
| `Subtitles` | SRT/VTT/LRC/JSON(B站 body、YouTube json3)/YRC/QRC + 语言优先级 + 同名外挂发现 |
| `MusicApi` | 网易云（搜索/取流/歌词/封面）+ QQ（搜索/vkey 取流/歌词/封面，含档位降级与 104003 明确提示）|
| `UrlResolver` | yt-dlp 解析（视频+音轨分流，`requested_formats` 处理）+ B站官方 CC API + YouTube 字幕落地 |
| `MpvView.playResolved` | 分流挂载（先 loadfile，再 audio-add），命令数组以 nil 结尾 |

**实测（本机真实网络）**

| 链路 | 证据 |
|---|---|
| 逻辑自检 | `--selftest-logic` → **41 / 41 通过** |
| 网易云 | 搜索 12 首 → 取流 320k mp3 → 歌词 936 字节 → 封面 HTTP 200 / 59770 字节 → **播放 PASS**（纯音频✓ 出声✓） |
| QQ音乐 | 搜索 → VIP 曲目跳过（104003 明确提示）→ 免费曲目取流 `M800` → 歌词 1266 字节 → 封面 HTTP 200 / 25254 字节 |
| B站 CC | `view code=0 aid=80433022 cid=137649199` → `player/v2 code=0`（该样本无 CC，链路已通） |
| yt-dlp 解析 | 成功（视频+音频分流）→ **4K 视频播放 PASS**（881 帧、音频 floatp 48000Hz 2ch、抓帧非黑像素 90%） |
| **YouTube 在线字幕** | **取回 1 条轨道：`zh-Hans · SRT`**（中文优先生效；VM 里被 429 限制的部分在本机成功） |

> 新增自动化参数：`--selftest-logic` / `--show-settings` / `--set k=v` / `--resolve <url>` / `--music <kw>` /
> `--qqmusic <kw>` / `--bilicc <url>` / `--ytsubs <url>`，均可加 `--play` 继续验证"网络 → 播放器"整链路。
