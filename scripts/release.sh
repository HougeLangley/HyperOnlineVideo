#!/usr/bin/env bash
# 三端一次性发布（**默认只演练，不推送**）
#
# 设计原则（对应项目铁律 R1）：
#   * 没有 --yes 或 HOV_RELEASE_CONFIRM=1 时，只打印计划并做本地检查，绝不 commit/push/tag
#   * 推送前必须先跑三端回归（--verify 会跑可离线执行的部分）
#
# 用法：
#   bash scripts/release.sh                  # 演练：检查 + 打印计划
#   bash scripts/release.sh --verify         # 演练 + 跑三端可离线自检
#   HOV_RELEASE_CONFIRM=1 bash scripts/release.sh --yes   # 真正发布（需你明确下令后再用）
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${HOV_VERSION:-1.1.0}"
TAG="v${VERSION}"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
CONFIRM="${HOV_RELEASE_CONFIRM:-0}"
YES=0
VERIFY=0
for a in "$@"; do
  [ "${a}" = "--yes" ] && YES=1
  [ "${a}" = "--verify" ] && VERIFY=1
done

echo "== 发布计划（版本 ${TAG}，分支 ${BRANCH}）=="
echo "  1) git add -A && git commit（提交本轮三端全部改动）"
echo "  2) git tag ${TAG}"
echo "  3) git push origin $BRANCH && git push origin ${TAG}"
echo "  4) gh release create ${TAG}（附 Android APK / Linux 三形态 / macOS DMG）"
echo

echo "== 本地检查 =="
echo "  未提交改动: $(git status --porcelain | wc -l | tr -d ' ') 处"
echo "  当前 HEAD : $(git log --oneline -1)"
echo "  远端       : $(git remote get-url origin 2>/dev/null || echo 无)"
MISSING=""
ART="${HOV_ARTIFACTS:-$HOME/Backups/hov/release-$VERSION}"
for f in app/build/outputs/apk/release/app-release.apk \
         macos/build/HyperOnlineVideo-$VERSION-arm64.dmg \
         "$ART/HyperOnlineVideo-aarch64.AppImage" \
         "$ART/hov-qt-$VERSION-1-aarch64.pkg.tar.xz" \
         "$ART/HyperOnlineVideo.flatpak"; do
  [ -e "${f}" ] && echo "  ✓ 产物存在: ${f}" || { echo "  ✗ 缺少产物: ${f}"; MISSING="$MISSING ${f}"; }
done

if [ "${VERIFY}" = "1" ]; then
  echo
  echo "== 三端可离线自检 =="
  ( cd macos && swift build -c release >/dev/null 2>&1 && \
    .build/out/Products/Release/HyperOnlineVideo --selftest-logic 2>/dev/null | grep -a 自检 || true ) | sed 's/^/  macOS: /'
  # 注意：必须先 source ~/hovenv.sh（否则缺 XDG_RUNTIME_DIR → 静默无输出，见踩坑 #76）
  ssh -o ConnectTimeout=8 houge@192.168.64.9 \
    'source ~/hovenv.sh 2>/dev/null; cd ~/desktop && HOME=/tmp/hovhome QT_QPA_PLATFORM=offscreen ./build/hov-qt --queue-selftest --exit-after 8 2>&1 | grep -a 队列自检' \
    2>/dev/null | sed 's/^/  Linux: /' || echo "  Linux: （VM 不可达，跳过）"
  echo "  Android: 单元测试请手动执行 gradle :app:testDebugUnitTest"
fi

if [ "${YES}" != "1" ] || [ "${CONFIRM}" != "1" ]; then
  echo
  echo "== 演练结束：未做任何提交/推送 =="
  echo "  真正发布需你明确下令后执行：HOV_RELEASE_CONFIRM=1 bash scripts/release.sh --yes"
  [ -n "${MISSING}" ] && echo "  注意：仍缺产物：${MISSING}"
  exit 0
fi

echo
echo "== 正式发布 =="
git add -A
git commit -m "release: $TAG —— 三端（Android / Linux Qt6 / macOS）功能对齐与打包产物"
git tag -a "${TAG}" -m "聚合视频 ${TAG}：三端统一发布"
git push origin "${BRANCH}"
git push origin "${TAG}"
if command -v gh >/dev/null 2>&1; then
  gh release create "${TAG}" \
    app/build/outputs/apk/release/app-release.apk \
    macos/build/HyperOnlineVideo-$VERSION-arm64.dmg \
    "${ART}/HyperOnlineVideo-aarch64.AppImage" \
    "${ART}/hov-qt-$VERSION-1-aarch64.pkg.tar.xz" \
    "${ART}/HyperOnlineVideo.flatpak" \
    --title "聚合视频 ${TAG}" --notes-file "docs/RELEASE_NOTES-$TAG.md"
else
  echo "未安装 gh：请手动在 GitHub 网页创建 Release 并上传上述产物"
fi
echo "完成：${TAG}"
