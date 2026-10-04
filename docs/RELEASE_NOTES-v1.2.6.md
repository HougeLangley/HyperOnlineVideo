# 聚合视频 v1.2.6 —— YouTube 受限出口真实可播（分块 Range 代理）

> 针对 Issue #1：「解析成功但 mpv 403」——根因不是“完全封禁”，
> 而是 googlevideo **只放行 capped Range、拒绝开放式 GET**。
> 作者原先归因为 VPS；实际 **SoSim 等运营商出口** 也会命中同一 CDN 策略（与是否 VPS 无关）。
> 本版用本机分块代理绕过。

## 根因（复现）
| 请求 | 结果 |
|---|---|
| `Range: bytes=0-…N`（有上限） | `206` ✅ |
| 无 Range / `Range: bytes=0-` | `403` ❌ |
| mpv/ffmpeg 直连 | `403` ❌ |
| yt-dlp 原生分块下载 | 可拉 ✅ |

v1.2.5 的 4MB 预检只测 capped Range → 会误报「可播 ✓」，而播放仍 403。

## 修复
- **Linux 桌面**：`YtRangeProxy` 把 googlevideo 直链映射为 `http://127.0.0.1:<port>/s/<token>`，
  对上游强制 ≤1MiB capped Range 分块拉取，再交给 mpv（音视频分轨各自代理）。
- **预检**：改为探测开放式 GET；若仅开放式失败则提示「已启用本机分块代理」。
- **自愈**：`switchVideoQuality` 在 `duration<1`（起播失败）时仍重新解析；HEAL 日志区分强制/TTL。
- **解码兼容**：自动档优先非 AV1 且 ≤1080p；mpv `hwdec=vaapi,no`（避开无效 Vulkan/CUDA）。
- **YouTube 客户端**：强制 `web_embedded,mweb`——默认 `android_vr` 在 SoSim 等出口上
  **只能读文件头约 10MB**（之后分块 403，播十几秒即断）；`web_embedded` 可完整拉 1080p。

## 工作区绕过
仍无法播放时（连 capped Range 也被拒）才需要更换代理节点。

## 致谢与协作者
- **Issue 报告**：[lishoujun](https://github.com/HougeLangley/HyperOnlineVideo/issues/1)（SoSim 出口复现日志）
- **协作开发**：[Cursor](https://cursor.com)（AI 结对编程）——根因复现、`YtRangeProxy` 实现、预检/自愈修正、GitHub Actions 远程打包
- 上游项目：[HougeLangley/HyperOnlineVideo](https://github.com/HougeLangley/HyperOnlineVideo)
