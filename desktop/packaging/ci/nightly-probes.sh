#!/usr/bin/env bash
# ── CI 夜巡：上游接口健康 + 依赖版本（失败即非零退出 → 工作流自动开 issue ✓）────
set -uo pipefail
FAIL=0
chk() { # 名称 期望码 curl 参数...
  local name="$1"; shift
  local code
  code=$(curl -s -o /tmp/probe.out -w '%{http_code}' -m 20 "$@" || echo 000)
  if [ "$code" = "200" ]; then echo "  ✓ $name"; else echo "  ✗ $name (HTTP $code)"; FAIL=1; fi
}
echo "── 上游接口探针 ──"
chk "B站 API（视频详情）" "https://api.bilibili.com/x/web-interface/view?bvid=BV1FPjy6TEiE" \
  -H "User-Agent: Mozilla/5.0" -H "Referer: https://www.bilibili.com/"
chk "网易云（歌曲详情）" "https://music.163.com/api/song/detail?id=347230&ids=%5B347230%5D" \
  -H "User-Agent: Mozilla/5.0"
chk "QQ 音乐（musicu）" "https://u.y.qq.com/cgi-bin/musicu.fcg" \
  -H "Content-Type: application/json" -H "Referer: https://y.qq.com/" \
  -d '{"comm":{"ct":19,"cv":1859},"req":{"method":"DoSearchForQQMusicDesktop","module":"music.search.SearchCgiService","param":{"grp":1,"num_per_page":1,"page_num":1,"query":"test"}}}'
echo "── 依赖版本 ──"
LATEST=$(curl -s -m 20 https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest | grep -oE '"tag_name": "[^"]+"' | cut -d'"' -f4)
echo "  yt-dlp 最新稳定: $LATEST"
[ "$FAIL" = "0" ] && echo "✓ 夜巡全部通过" || { echo "✗ 夜巡存在失败（见上 ✓）"; exit 1; }
