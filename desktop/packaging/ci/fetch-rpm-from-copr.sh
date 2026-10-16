#!/usr/bin/env bash
# ── CI：从 COPR 取回 rpm 成品（Fedora 侧由 COPR 服务端构建 ✓ 不再本机 mock ✓）──
set -euo pipefail
# 凭据：仓库单条 Secret HOVBUILD（KEY=VALUE ✓ 只取所需 ✓ 不打印值 ✓）
if [ -n "${HOVBUILD:-}" ]; then
  while IFS='=' read -r k v; do
    case "$k" in ''|\#*) continue;; esac
    export "$k=$v"
  done <<< "$HOVBUILD"
fi
if [ -n "${COPR_TOKEN:-}" ]; then
  mkdir -p ~/.config
  printf '[copr-cli]\nlogin = %s\nusername = %s\ntoken = %s\ncopr_url = %s\n' \
    "${COPR_LOGIN:-token}" "${COPR_USERNAME:-houge}" "$COPR_TOKEN" \
    "${COPR_URL:-https://copr.fedorainfracloud.org}" > ~/.config/copr
  chmod 600 ~/.config/copr
fi
command -v copr-cli >/dev/null || pip install --quiet --user copr-cli || pip install --quiet --break-system-packages copr-cli
export PATH="$HOME/.local/bin:$PATH"
VER=$(grep -oE 'project\(HyperOnlineVideo VERSION [0-9.]+' desktop/CMakeLists.txt | grep -oE '[0-9.]+$')
OUT="$(pwd)/out"; mkdir -p "$OUT"; cd "$OUT"
BUILD_ID=$(copr-cli list-builds houge/hov-qt 2>/dev/null | head -1 | awk '{print $1}')
echo "COPR build: $BUILD_ID（等其 succeeded ✓）"
for i in $(seq 1 60); do
  st=$(copr-cli status "$BUILD_ID" 2>/dev/null | head -1)
  [ "$st" = "succeeded" ] && break
  echo "  ... $st（$i）"; sleep 20
done
[ "$st" = "succeeded" ]
copr-cli download-build "$BUILD_ID" >/dev/null 2>&1 || true
# 2026-10-17 v1.3.1 首发实测：runner 上 copr-cli 取件可能静默失败 ✗（曾致 GHCR 少一层 rpm ✗）
# → 直连兜底（公开 results 目录 ✓ 无需认证 ✓）+ **显式校验**（没有就报错退出 ✓ 不装作成功 ✗）
if ! ls hov-qt-*.rpm >/dev/null 2>&1; then
  echo "  copr-cli 未产出 rpm ✗ → 直连 results 目录兜底 ✓"
  for ch in $(curl -fsSL "https://copr.fedorainfracloud.org/api_3/build/list?ownername=houge&projectname=hov-qt&limit=1" 2>/dev/null | python3 -c "import json,sys; d=json.load(sys.stdin); print(' '.join((d['items'][0].get('chroots') or [])))" 2>/dev/null); do
    DIR="https://download.copr.fedorainfracloud.org/results/houge/hov-qt/${ch}/"
    HREF=$(curl -fsSL "$DIR" 2>/dev/null | grep -oE 'hov-qt-[0-9.]+-1\.fc[0-9]+\.x86_64\.rpm' | head -1)
    [ -n "$HREF" ] && curl -fsSL -o "$HREF" "${DIR}${HREF}" && break
  done
fi
ls -la hov-qt-*.rpm
# 显式校验：确实拿到了才放行 ✓
[ -s "$(ls hov-qt-*.rpm 2>/dev/null | head -1)" ] || { echo "  ✗ 未取到任何 rpm（中止 ✓）"; exit 1; }
