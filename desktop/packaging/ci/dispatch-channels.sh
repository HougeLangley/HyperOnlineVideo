#!/usr/bin/env bash
# ── CI：渠道推送（OBS 实推 ✓ AUR（有 key 才推 ✓）COPR 交由服务端构建后由 fetch 步骤取回 ✓）
#    以及**渠道版本自检**（COPR/AUR/OBS 三处读到什么版本 → 一目了然 ✓）
#    凭据：单条 Secret HOVBUILD（KEY=VALUE 多行 ✓ 只报键名 ✗ 不落值 ✓）
#    仅 release-approval 人工闸门后运行 ✓（规则 #8 硬化 ✓）
#    设计：任何单渠道失败**不中断**后续渠道与 Release 步骤 ✓（出错由维护者补 ✓）
set -uo pipefail
if [ -n "${HOVBUILD:-}" ]; then
  while IFS='=' read -r k v; do
    case "$k" in ''|\#*) continue;; esac
    export "$k=$v"
  done <<< "$HOVBUILD"
fi
for key in COPR_TOKEN OBS_USER OBS_PASS AUR_SSH_KEY; do
  [ -n "${!key:-}" ] && echo "  ✓ 凭据存在: $key" || echo "  - 凭据缺失: $key（跳过对应渠道 ✗）"
done
VER="${GITHUB_REF_NAME#v}"
[ -n "$VER" ] || VER=$(grep -oE 'project\(HyperOnlineVideo VERSION [0-9.]+' desktop/CMakeLists.txt | grep -oE '[0-9.]+$')
TAG="v$VER"
TARURL="https://github.com/HougeLangley/HyperOnlineVideo/archive/refs/tags/${TAG}.tar.gz"
WORK=$(mktemp -d)
echo "== 渠道推送：$VER（tag $TAG）=="

# ── ① OBS（osc ✓ spec + changes + tarball + dsc 四件套）──────────────────────
if [ -n "${OBS_USER:-}" ] && [ -n "${OBS_PASS:-}" ]; then
  echo "── OBS 推送 ──"
  command -v osc >/dev/null || pip install --quiet --user osc || pip install --quiet --break-system-packages osc || true
  export PATH="$HOME/.local/bin:$PATH"
  if command -v osc >/dev/null; then
    printf '[general]\napiurl = https://api.opensuse.org\n\n[https://api.opensuse.org]\nuser = %s\npass = %s\n' "$OBS_USER" "$OBS_PASS" > ~/.oscrc
    chmod 600 ~/.oscrc
    ( cd "$WORK" && osc checkout home:houge hov-qt >/dev/null 2>&1 || osc checkout home:houge/hov-qt >/dev/null 2>&1 )
    PKG=$(find "$WORK" -maxdepth 2 -name "*.spec" -path "*hov-qt*" | head -1)
    if [ -n "$PKG" ]; then
      D="$WORK/$(dirname "${PKG#$WORK/}")"
      cp desktop/packaging/obs/hov-qt.spec "$D/hov-qt.spec"
      cp desktop/packaging/obs/hov-qt.changes "$D/hov-qt.changes" 2>/dev/null || true
      curl -fsSL --max-time 600 -o "$WORK/hov-qt-$VER.tar.gz" "$TARURL" || echo "  ✗ tarball 下载失败"
      cp "$WORK/hov-qt-$VER.tar.gz" "$D/" 2>/dev/null || true
      # deb 源（dsc + orig + debian.tar）供 OBS 的 Debian/Ubuntu 仓库 ✓
      if command -v dpkg-source >/dev/null; then
        mkdir -p "$WORK/hov-qt-$VER" && tar xzf "$WORK/hov-qt-$VER.tar.gz" -C "$WORK" --strip-components=1 -C "$WORK/hov-qt-$VER" 2>/dev/null || true
        cp -r desktop/packaging/deb/debian "$WORK/hov-qt-$VER/debian" 2>/dev/null || true
        ( cd "$WORK" && tar czf "hov-qt_$VER.orig.tar.gz" "hov-qt-$VER" && cd "hov-qt-$VER" && dpkg-source -b . >/dev/null 2>&1 || true )
        cp "$WORK"/hov-qt_$VER*.orig.tar.gz "$WORK"/hov-qt_$VER-*.dsc "$WORK"/hov-qt_$VER-*.debian.tar.* "$D/" 2>/dev/null || true
      fi
      ( cd "$D" && osc addremove >/dev/null 2>&1; osc ci -m "hov-qt $VER（CI 自动推送 ✓）" 2>&1 | tail -6 )
    else
      echo "  ✗ 未找到 OBS 检出目录 ✗"
    fi
  else
    echo "  ✗ osc 安装失败 ✗"
  fi
else
  echo "── OBS：无凭据，跳过 ✗ ──"
fi

# ── ② AUR（有 SSH 私钥才推 ✓ 公开包无凭据 ✗）──────────────────────────────
if [ -n "${AUR_SSH_KEY:-}" ]; then
  echo "── AUR 推送 ──"
  mkdir -p ~/.ssh && printf '%s\n' "$AUR_SSH_KEY" > ~/.ssh/aur && chmod 600 ~/.ssh/aur
  export GIT_SSH_COMMAND='ssh -i ~/.ssh/aur -o StrictHostKeyChecking=accept-new'
  T=$(mktemp -d)
  if git clone -q ssh://aur@aur.archlinux.org/hov-qt.git "$T" 2>/dev/null; then
    cp desktop/packaging/aur/PKGBUILD "$T/PKGBUILD"
    SHA=$(sha256sum "$WORK/hov-qt-$VER.tar.gz" 2>/dev/null | cut -d' ' -f1 || true)
    [ -f "$WORK/hov-qt-$VER.tar.gz" ] || curl -fsSL -o "$WORK/hov-qt-$VER.tar.gz" "$TARURL"
    SHA=$(sha256sum "$WORK/hov-qt-$VER.tar.gz" | cut -d' ' -f1)
    python3 - "$T/PKGBUILD" "$SHA" <<'PY'
import re, sys
p, sha = sys.argv[1], sys.argv[2]
s = open(p, encoding='utf-8').read()
s2 = re.sub(r"sha256sums=\('[^']*'\)", "sha256sums=('%s')" % sha, s, count=1)
assert s2 != s and ("sha256sums=('%s')" % sha) in s2
open(p, 'w', encoding='utf-8').write(s2)
print('  ✓ sha256sums 已更新', sha[:16])
PY
    # .SRCINFO：确定性生成（CI 无 makepkg ✗ 手写格式固定 ✓ 与 PKGBUILD 字段一一对应 ✓）
    cat > "$T/.SRCINFO" <<SRCINFO
pkgbase = hov-qt
	pkgdesc = Aggregated YouTube/Bilibili/NetEase/QQ Music client (Qt6 desktop)
	pkgver = $VER
	pkgrel = 1
	url = https://github.com/HougeLangley/HyperOnlineVideo
	arch = x86_64
	arch = aarch64
	arch = riscv64
	license = GPL-3.0-only
	makedepends = cmake
	makedepends = ninja
	makedepends = pkgconf
	depends = qt6-base
	depends = qt6-declarative
	depends = qt6-webengine
	depends = mpv
	depends = ffmpeg
	depends = yt-dlp
	source = hov-qt-$VER.tar.gz::https://github.com/HougeLangley/HyperOnlineVideo/archive/refs/tags/v$VER.tar.gz
	sha256sums = $SHA

pkgname = hov-qt
SRCINFO
    ( cd "$T" && git add -A && git -c user.name=ci -c user.email=ci@local commit -qm "hov-qt $VER" && git push origin master 2>&1 | tail -3 )
  else
    echo "  ✗ AUR 克隆失败（key 未被接受？ ✗）"
  fi
else
  echo "── AUR：无 key，跳过 ✗（由本机 publish-aur.sh 补 ✓）──"
fi

# ── ③ 渠道版本自检（公开读 ✓ 无凭据 ✓ 一眼看清谁更新了 ✓）───────────────────
echo "== 渠道版本自检 =="
COPRV=$(curl -fsSL -m 30 "https://copr.fedorainfracloud.org/api_3/build/list?ownername=houge&projectname=hov-qt&limit=1" 2>/dev/null | python3 -c "import json,sys; d=json.load(sys.stdin); i=d['items'][0]; print(i['id'], i['state'], i.get('source_package',{}).get('version','?'))" 2>/dev/null || echo "查询失败")
echo "  COPR: $COPRV"
AURV=$(curl -fsSL -m 30 "https://aur.archlinux.org/rpc/v5/info/hov-qt" 2>/dev/null | python3 -c "import json,sys; r=json.load(sys.stdin).get('results',[]); print(r[0]['Version'] if r else '?')" 2>/dev/null || echo "查询失败")
echo "  AUR : $AURV"
OBV=$(curl -fsSL -m 30 -u "${OBS_USER:-x}:${OBS_PASS:-x}" "https://api.opensuse.org/source/home:houge/hov-qt/hov-qt.spec" 2>/dev/null | grep -m1 "^Version:" || echo "查询失败")
echo "  OBS : $OBV"
echo "✓ 渠道调度完成（$VER）"
