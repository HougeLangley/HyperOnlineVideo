#!/usr/bin/env bash
# 提交/推送前的**身份断言**（防止把提交归属到他人账号 ✗）
#
# 为什么必须机器断言（2026-09-21 事故教训）：
#   仓库级 user.email 曾被写成 "<他人的数字ID>+<我的登录名>@users.noreply.github.com"
#   —— 形式完全合法 ✓ 但 ID 属于别人 ✗ → GitHub 按 ID 归属，把提交算到了陌生人头上 ✗✗
#   事前的"看一眼"检查**无法**发现它（值长得太像了 ✓）→ 必须用**权威查询**比对 ✓
#
# 用法: bash scripts/gh-identity-guard.sh          # 断言仓库级身份
#       bash scripts/gh-identity-guard.sh --push   # 断言 + 校验最近一次远端提交的真实归属
set -euo pipefail
cd "$(dirname "$0")/.."

fail() { echo "✗ 身份校验失败：$*" >&2; echo "  修正示例：" >&2; echo "    git config --local user.email \"$(gh api user --jq '.id')+$(gh api user --jq '.login')@users.noreply.github.com\"" >&2; exit 1; }

# 1) 取权威值（**必须联网查询** ✓ 不许凭记忆/猜 ✗）
HOOK_MODE=0
[ "${1:-}" = "--hook" ] && HOOK_MODE=1
if ID=$(gh api user --jq .id 2>/dev/null) && LOGIN=$(gh api user --jq .login 2>/dev/null) && [ -n "$ID" ]; then
  :
else
  # 取不到权威 ID（离线/未登录 ✓）→ 退化为**形式校验**（用本地已知登录名 ✓）
  LOGIN=$(git config --get github.user 2>/dev/null || echo "")
  [ -z "$LOGIN" ] && LOGIN=$(git remote get-url origin 2>/dev/null | sed -E 's#.*[:/]([^/]+)/[^/]+\.git#\1#')
  ID="(离线未知)"
  echo "  ⚠️ 无法联网获取账号 ID → 退化为形式校验（登录名=$LOGIN）"
  EMAIL_NOW=$(git config --get user.email || true)
  case "$EMAIL_NOW" in
    *"+${LOGIN}@users.noreply.github.com"|"${LOGIN}@users.noreply.github.com") echo "  ✓ 形式校验通过（离线模式 ✓ 未阻断推送 ✓）"; exit 0 ;;
    *) echo "  ✗ 形式校验失败：$EMAIL_NOW 与登录名 $LOGIN 不符" >&2; exit 1 ;;
  esac
fi

# 2) 取当前生效身份
NAME=$(git config --get user.name  || true)
EMAIL=$(git config --get user.email || true)
ORIGIN=$(git config --show-origin --get user.email 2>/dev/null | awk '{print $1}' | sed 's/file://' || true)

echo "  当前身份: $NAME <$EMAIL>"
echo "  来源文件: ${ORIGIN:-（未设置）}"
echo "  权威账号: $LOGIN (id=$ID)"

# 3) 断言：邮箱必须是「自己的 ID + 自己登录名」或历史形式「自己登录名」
OK=0
case "$EMAIL" in
  "${ID}+${LOGIN}@users.noreply.github.com") OK=1 ;;
  "${LOGIN}@users.noreply.github.com")       OK=1 ;;   # 历史形式（本仓库既有提交使用的 ✓）
esac
[ "$OK" = "1" ] || fail "user.email 不是自己的 noreply 形式：$EMAIL"

# 4) 额外硬断言：邮箱里**绝不能出现别人的数字 ID** ✗
if echo "$EMAIL" | grep -qE '^[0-9]+\+'; then
  PREFIX=$(echo "$EMAIL" | sed -E 's/^([0-9]+)\+.*/\1/')
  [ "$PREFIX" = "$ID" ] || fail "邮箱前缀 ID=$PREFIX 不是你的 ID=$ID（历史上曾因此把提交归属到他人账号 ✗）"
fi

echo "  ✓ 身份断言通过（$LOGIN <$EMAIL>）"

# 4.5) --hook 模式：供 pre-push 钩子调用
#   在线 → 走完整强校验（ID 归属 ✓ 不一致即拒绝 ✗）
#   离线 → 退回"形式校验 + 告警"，**不阻断**正常推送 ✓（避免网络问题挡住工作 ✓）
if [ "${1:-}" = "--hook" ]; then
  if [ "$OK" = "1" ]; then exit 0; fi
  exit 1
fi

# 5) --push：再验"远端实际怎么显示这次提交"（这是当时**唯一**能立刻发现问题的检查 ✓✓）
if [ "${1:-}" = "--push" ]; then
  REPO=$(gh repo view --json nameWithOwner --jq .nameWithOwner)
  SHA=$(git rev-parse HEAD)
  owner=$(gh api "repos/$REPO/commits/$SHA" --jq '.author.login // "无"' 2>/dev/null || echo "查询失败")
  echo "  远端归属校验: $REPO@${SHA:0:7} → author.login=$owner"
  [ "$owner" = "$LOGIN" ] || fail "远端显示的提交归属不是 $LOGIN（而是 $owner）—— 立即停止后续操作 ✗"
  echo "  ✓ 远端归属正确"
fi
