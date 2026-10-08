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
find . -name "hov-qt-${VER}-1.fc*.x86_64.rpm" | head -1 | xargs -I{} cp {} .
ls -la hov-qt-*.rpm
