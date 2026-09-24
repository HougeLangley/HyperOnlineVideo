#!/usr/bin/env bash
# ── publish-aur.sh —— 把当前版本发布到 AUR（Arch User Repository）──────────────────
# 用法：
#   bash scripts/publish-aur.sh            # 演练：下载+算sha256+生成.SRCINFO+本地commit（不推送 ✓）
#   bash scripts/publish-aur.sh --push     # 正式：额外执行 git push 到 AUR（发布动作 ✗ 由用户决定 ✓）
#
# 前置：
#   1) GitHub 上已有 v<pkgver> tag（tag 推送由项目维护者决定 ✓）
#   2) 本机 SSH 密钥已被 AUR 账号接受（验证：ssh aur@aur.archlinux.org help ✓）
#   3) 若本机无 makepkg（macOS ✗）→ 需可 ssh 到的 Arch 机器生成 .SRCINFO
#      （环境变量 HOV_ARCH_VM=user@host 可覆盖，默认 houge@192.168.64.9 ✓）
#
# 设计纪律（避坑固化 ✓）：
#   - 默认**不推送** ✗（发布由用户决定 ✓ 规则 #8）
#   - sha256sums 必须真实（AUR 规范 ✗ 禁用 SKIP ✗）→ 从 GitHub 实际下载计算 ✓
#   - .SRCINFO 必须用 makepkg --printsrcinfo 生成 ✓（手写易错 ✗）
#   - git 身份显式指定 ✓（不依赖全局配置 ✗）
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AUR_DIR="${REPO_ROOT}/desktop/packaging/aur"
SRC_PKGBUILD="${AUR_DIR}/PKGBUILD"              # 模板（只读 ✓ 保持 PLACEHOLDER ✓）
ARCH_VM="${HOV_ARCH_VM:-houge@192.168.64.9}"
WORK_DIR="${TMPDIR:-/tmp}/hov-aur-publish"
PKGBUILD="${WORK_DIR}/PKGBUILD"                  # 工作副本（sha256 填在这里 ✓ 不污染仓库 ✓）

PUSH=0
[ "${1:-}" = "--push" ] && PUSH=1

say() { printf '  %s\n' "$*"; }

say "═══ [0] 前置检查 ═══"
[ -f "${SRC_PKGBUILD}" ] || { say "✗ 缺少 ${SRC_PKGBUILD}"; exit 1; }
mkdir -p "${WORK_DIR}"
cp -f "${SRC_PKGBUILD}" "${PKGBUILD}"            # 模板 → 工作副本 ✓
PKGVER="$(grep -m1 '^pkgver=' "${SRC_PKGBUILD}" | cut -d= -f2)"
PKGNAME="$(grep -m1 '^pkgname=' "${SRC_PKGBUILD}" | cut -d= -f2)"
say "包: ${PKGNAME} ${PKGVER}（模板: ${SRC_PKGBUILD} → 副本: ${PKGBUILD}）"

TAG_URL="https://github.com/HougeLangley/HyperOnlineVideo/archive/refs/tags/v${PKGVER}.tar.gz"
say "检查 GitHub tag：v${PKGVER} …"
if ! curl -sIL --max-time 45 -o /dev/null -w '%{http_code}' "${TAG_URL}" | grep -q 200; then
    say "✗ GitHub 上不存在 v${PKGVER} tag ✗（AUR 的 source 必须公开可下载）"
    say "  → 等你推送 tag 后再跑本脚本 ✓（推送 GitHub 由你决定 ✓）"
    exit 2
fi
say "✓ tag 就绪"

say "═══ [1] 下载源码包 + 计算 sha256 ═══"
mkdir -p "${WORK_DIR}"
TARBALL="${WORK_DIR}/${PKGNAME}-${PKGVER}.tar.gz"
curl -sL --max-time 300 -o "${TARBALL}" "${TAG_URL}"
if command -v sha256sum >/dev/null 2>&1; then
    SHA="$(sha256sum "${TARBALL}" | cut -d' ' -f1)"
else
    SHA="$(shasum -a 256 "${TARBALL}" | cut -d' ' -f1)"
fi
say "✓ $(du -h "${TARBALL}" | cut -f1) / sha256=${SHA}"

say "═══ [2] 更新 PKGBUILD 的 sha256sums ═══"
python3 - "$PKGBUILD" "$SHA" <<'PYEOF'
import re
import sys
p, sha = sys.argv[1], sys.argv[2]
s = open(p, encoding='utf-8').read()
s2 = re.sub(r"sha256sums=\('[^']*'\)", "sha256sums=('%s')" % sha, s, count=1)
assert s2 != s, 'sha256sums 未替换（PKGBUILD 格式变了 ✗）'
# ⚠️ 不要断言 'PLACEHOLDER' not in s2 ✗ —— 注释里也含该字样（本次实测踩到 ✓）
#    改为断言"替换结果确实出现" ✓（正向验证 ✓ 更可靠 ✓）
assert "sha256sums=('%s')" % sha in s2, '替换结果未出现（替换逻辑异常 ✗）'
open(p, 'w', encoding='utf-8').write(s2)
print('  ✓ sha256sums 已更新为 %s…' % sha[:16])
PYEOF

say "═══ [3] 生成 .SRCINFO（makepkg --printsrcinfo ✓）═══"
if command -v makepkg >/dev/null 2>&1; then
    (cd "${WORK_DIR}" && makepkg --printsrcinfo > .SRCINFO)
    say "✓ 本机 makepkg 生成"
else
    say "本机无 makepkg ✗ → 走 Arch VM（${ARCH_VM}）"
    ssh -o BatchMode=yes -o ConnectTimeout=15 "${ARCH_VM}" \
        'rm -rf /tmp/hov-aur-srcinfo && mkdir -p /tmp/hov-aur-srcinfo && cat > /tmp/hov-aur-srcinfo/PKGBUILD' < "${PKGBUILD}"
    ssh -o BatchMode=yes -o ConnectTimeout=15 "${ARCH_VM}" \
        'cd /tmp/hov-aur-srcinfo && makepkg --printsrcinfo' > "${WORK_DIR}/.SRCINFO"
    say "✓ .SRCINFO 已由 Arch VM 生成（$(wc -l < "${WORK_DIR}/.SRCINFO" | tr -d ' ') 行）"
fi

say "═══ [4] 克隆/更新 AUR 仓库 ═══"
AUR_REPO="${WORK_DIR}/aur-repo"
if [ -d "${AUR_REPO}/.git" ]; then
    git -C "${AUR_REPO}" fetch origin 2>/dev/null || true
    # 空仓库（首次发布前）无 origin/master ✗ → 仅在存在时同步 ✓
    if git -C "${AUR_REPO}" rev-parse --verify origin/master >/dev/null 2>&1; then
        git -C "${AUR_REPO}" reset --hard origin/master
    fi
    git -C "${AUR_REPO}" clean -fd PKGBUILD .SRCINFO 2>/dev/null || true
else
    git clone "ssh://aur@aur.archlinux.org/${PKGNAME}.git" "${AUR_REPO}"
fi
# AUR 默认分支 = master ✓ 用 symbolic-ref 统一（unborn HEAD 也适用 ✓ 不依赖 git 默认分支名 ✗）
git -C "${AUR_REPO}" symbolic-ref HEAD refs/heads/master
say "✓ 仓库就绪: ${AUR_REPO}（分支: master ✓）"

say "═══ [5] 提交（本地 ✓ 不代表已发布）═══"
cp -f "${PKGBUILD}" "${AUR_REPO}/PKGBUILD"
cp -f "${WORK_DIR}/.SRCINFO" "${AUR_REPO}/.SRCINFO"
git -C "${AUR_REPO}" add PKGBUILD .SRCINFO
if git -C "${AUR_REPO}" diff --cached --quiet; then
    say "（无变化 → 已经是该版本 ✓）"
else
    git -C "${AUR_REPO}" -c user.name='Houge Langley' -c user.email='hougelangley1987@gmail.com' \
        commit -m "${PKGNAME} ${PKGVER}-$(grep -m1 '^pkgrel=' "${PKGBUILD}" | cut -d= -f2)"
    say "✓ 已本地提交"
fi

if [ "${PUSH}" = "1" ]; then
    say "═══ [6] 推送到 AUR（发布动作 ✓）═══"
    git -C "${AUR_REPO}" push origin HEAD:master 2>&1 | tail -5 | sed 's/^/  /'
    say "✓ 已推送 → https://aur.archlinux.org/packages/${PKGNAME}"
else
    say "═══ [6] 演练模式：未推送 ✗（发布由你决定 ✓）═══"
    say "确认无误后执行： bash scripts/publish-aur.sh --push"
fi
