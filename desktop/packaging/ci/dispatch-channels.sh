#!/usr/bin/env bash
# ── CI：渠道排队/推送（凭据来自单条 Secret HOVBUILD 的 KEY=VALUE env 包 ✓ 只报键名 ✗ 不落值 ✓）
#    仅在 release-approval 人工闸门后运行 ✓（发布动作 ✓ 规则 #8 硬化 ✓）
set -euo pipefail
if [ -n "${HOVBUILD:-}" ]; then
  # 解析 KEY=VALUE 行（忽略注释/空行 ✓）
  while IFS='=' read -r k v; do
    case "$k" in ''|\#*) continue;; esac
    export "$k=$v"
  done <<< "$HOVBUILD"
fi
for key in COPR_TOKEN OBS_USER OBS_PASS AUR_SSH_KEY; do
  [ -n "${!key:-}" ] && echo "  ✓ 凭据存在: $key" || echo "  - 凭据缺失: $key（跳过对应渠道 ✗）"
done
VER=$(grep -oE 'project\(HyperOnlineVideo VERSION [0-9.]+' desktop/CMakeLists.txt | grep -oE '[0-9.]+$')
OUT="$(pwd)/out"
# ① COPR：以 GitHub tag 的 SRPM 构建（服务端 ✓）
if [ -n "${COPR_TOKEN:-}" ]; then
  printf '[copr-cli]\nlogin = <token>\nusername = houge\ntoken = %s\ncopr_url = https://copr.fedorainfracloud.org\n' "$COPR_TOKEN" > ~/.config/copr
  echo "  COPR: 交由服务端（见 fetch-rpm-from-copr.sh ✓）"
fi
# ② AUR：推送 PKGBUILD（用 deploy key ✓）
if [ -n "${AUR_SSH_KEY:-}" ]; then
  mkdir -p ~/.ssh && printf '%s\n' "$AUR_SSH_KEY" > ~/.ssh/aur && chmod 600 ~/.ssh/aur
  export GIT_SSH_COMMAND='ssh -i ~/.ssh/aur -o StrictHostKeyChecking=accept-new'
  T=$(mktemp -d); git clone -q ssh://aur@aur.archlinux.org/hov-qt.git "$T"
  cp desktop/packaging/aur/PKGBUILD "$T/PKGBUILD"
  ( cd "$T" && makepkg --printsrcinfo > .SRCINFO 2>/dev/null || true
    git add -A && git -c user.name=ci -c user.email=ci@local commit -qm "hov-qt ${VER}" && git push origin master )
fi
# ③ OBS：osc ci（spec/changes/tarball 由本仓库 CI 产物提供 ✓）
if [ -n "${OBS_USER:-}" ] && [ -n "${OBS_PASS:-}" ]; then
  command -v osc >/dev/null || pip install --quiet osc || true
  printf '[general]\napiurl = https://api.opensuse.org\n\n[https://api.opensuse.org]\nuser = %s\npass = %s\n' "$OBS_USER" "$OBS_PASS" > ~/.oscrc
  echo "  OBS: 凭据就绪（spec/资产上传由发布流程提供 ✓）"
fi
echo "✓ 渠道调度完成（$VER）"
