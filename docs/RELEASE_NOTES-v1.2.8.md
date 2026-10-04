# 聚合视频 v1.2.8 —— DASH 音轨用 mpv NODE 数组挂载

v1.2.7 对 `audio-files` 字符串做了 `:` 转义。若仍看到
`Cannot open file 'http'` **以及** `QTcpServer::close()`，说明进程还是旧包（1.2.6）。

本版不再走路径列表字符串：用 `MPV_FORMAT_NODE` 数组设 `audio-files`，冒号不会被当成分隔符。
起播日志会带 `hov-qt 1.2.8`；没有这行就不是本包。

## 致谢
Co-developed with Cursor (https://cursor.com)
