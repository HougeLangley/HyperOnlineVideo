# 聚合视频 v1.2.7 —— YouTube 分轨有声（mpv audio-files 转义）

v1.2.6 已把 googlevideo 映射到 `http://127.0.0.1:<port>/s/<token>`，
但 mpv 的 `audio-files` 在 Unix 上用 `:` 分隔多条路径。
于是音轨被拆成 `http`、`//127.0.0.1`、`<port>/s/<token>`，视频能播、没有声音。

## 修复
- 写入 `audio-files` 前转义 `:` / `;` / `\`。
- 代理关闭改为在 IO 线程上调用 `QTcpServer::close()`（不再 `invokeMethod("close")`）。
- 自检：`--queue-selftest` 断言路径列表；另可用 `python3 scripts/test_mpv_pathlist.py` 本地单测（有 `mpv` 时还会打 CLI）。

## 致谢
Co-developed with Cursor (https://cursor.com)
