#!/usr/bin/env bash
# 提交/推送前的身份断言 —— 防止把提交归属到他人账号
#
# 为什么需要机器断言（2026-09-21 事故教训）：
#   仓库级 user.email 曾被写成 "<他人的数字ID>+<我的登录名>@users.noreply.github.com"
#   —— 形式完全合法，但那个数字 ID 属于**别人**，GitHub 按 ID 归属，
#   于是提交被算到了一个素不相识的用户名下。事前的"看一眼"无法发现它（值长得太像），
#   必须用**权威查询**比对：本脚本用 `gh api user` 取自己的 id/login 再断言相等。
#
# 用法：
#   bash scripts/gh-identity-guard.sh           # 断言当前仓库身份
#   bash scripts/gh-identity-guard.sh --push    # 断言 + 校验最近一次远端提交的真实归属
#   bash scripts/gh-identity-guard.sh --hook    # 供 pre-push 钩子调用（在线强校验 / 离线形式校验）
set -euo pipefail
cd "$(git rev-parse --show-toplevel 2>/dev/null || dirname "$0")"

MODE="${1:-}"

# ── 取权威身份（在线）────────────────────────────────────────────────
GH_ID=""; GH_LOGIN=""
if GH_ID=$(gh api user --jq .id 2>/dev/null) && GH_LOGIN=$(gh api user --jq .login 2>/dev/null) && [ -n "$GH_ID" ] && [ -n "$GH_LOGIN" ]; then
    :
else
    GH_ID=""; GH_LOGIN=""
fi

NAME=$(git config --get user.name  || true)
EMAIL=$(git config --get user.email || true)
ORIGIN=$(git config --show-origin --get user.email 2>/dev/null | awk '{print $1}' | sed 's/file://' || true)

echo "  当前身份: ${NAME:-（未设置）} <${EMAIL:-（未设置）}>"
echo "  来源文件: ${ORIGIN:-（未设置）}"

# ── 离线分支：只做形式校验，**不阻断**推送 ──────────────────────────
if [ -z "$GH_LOGIN" ]; then
    LOGIN_GUESS=$(git config --get github.user 2>/dev/null || true)
    if [ -z "$LOGIN_GUESS" ]; then
        LOGIN_GUESS=$(git remote get-url origin 2>/dev/null \
                      | sed -E 's#.*[:/]([^/]+)/[^/]+(\.git)?$#\1#' || true)
    fi
    echo "  ⚠️ 无法联网获取账号 ID → 退化为形式校验（登录名=${LOGIN_GUESS:-未知}）"
    case "$EMAIL" in
        *"+${LOGIN_GUESS}@users.noreply.github.com"|"${LOGIN_GUESS}@users.noreply.github.com")
            echo "  ✓ 形式校验通过（离线模式，未阻断）"; exit 0 ;;
        *)
            echo "  ✗ 形式校验失败：$EMAIL 与登录名 ${LOGIN_GUESS:-未知} 不符" >&2; exit 1 ;;
    esac
fi

echo "  权威账号: $GH_LOGIN (id=$GH_ID)"

# ── 在线：强校验 ─────────────────────────────────────────────────────
fail() {
    echo "✗ 身份校验失败：$*" >&2
    echo "  修正： git config --local user.email \"${GH_ID}+${GH_LOGIN}@users.noreply.github.com\"" >&2
    exit 1
}

OK=0
case "$EMAIL" in
    "${GH_ID}+${GH_LOGIN}@users.noreply.github.com") OK=1 ;;   # 现代形式（含自己的 ID）
    "${GH_LOGIN}@users.noreply.github.com")           OK=1 ;;   # 历史形式（本仓库既有提交在用）
esac
[ "$OK" = "1" ] || fail "user.email 不是自己的 noreply 形式：$EMAIL"

# 显式拦截"别人的数字 ID"这一事故形态
case "$EMAIL" in
    [0-9]*+*)
        PREFIX="${EMAIL%%+*}"
        [ "$PREFIX" = "$GH_ID" ] || fail "邮箱前缀 ID=$PREFIX 不是你的 ID=$GH_ID" ;;
esac

echo "  ✓ 身份断言通过（$GH_LOGIN <$EMAIL>）"

# ── --hook：给 pre-push 用（走到这里说明在线校验已通过）──────────────
[ "$MODE" = "--hook" ] && exit 0

# ── --push：回读远端实际归属（当初唯一能立刻发现问题的一步）──────────
if [ "$MODE" = "--push" ]; then
    REPO=$(gh repo view --json nameWithOwner --jq .nameWithOwner 2>/dev/null || true)
    [ -n "$REPO" ] || { echo "  ⚠️ 无法确定仓库，跳过远端归属校验"; exit 0; }
    SHA=$(git rev-parse HEAD)
    OWNER=$(gh api "repos/$REPO/commits/$SHA" --jq '.author.login // "未匹配"' 2>/dev/null || echo "查询失败")
    echo "  远端归属: $REPO@${SHA:0:7} → author.login=$OWNER"
    if [ "$OWNER" = "$GH_LOGIN" ]; then
        echo "  ✓ 远端归属正确"
    else
        echo "  ⚠️ 远端归属为 $OWNER（推送刚发生，GitHub 索引可能延迟；请稍后重试本命令复核）" >&2
        [ "$OWNER" = "查询失败" ] || [ "$OWNER" = "未匹配" ] || exit 1
    fi
fi
