#!/usr/bin/env bash
# Hyper Online Video — Linux 原生构建脚本（任意发行版；专为 Arch 本机测试准备）
#
# 用法：
#   bash desktop/packaging/build-native.sh              # 常规 Release（推荐先跑这个）
#   HOV_OPTIMIZED=1 bash desktop/packaging/build-native.sh   # clang + full-LTO（+PGO 需先备 profdata）
#   HOV_RUN=1 bash desktop/packaging/build-native.sh    # 构建完自动跑一次逻辑自检并启动界面
#
# 依赖（Arch）：
#   sudo pacman -S --needed qt6-base qt6-declarative mpv ffmpeg yt-dlp cmake ninja pkgconf
#   可选（App 内登录窗口）：qt6-webengine
#   可选（HOV_OPTIMIZED=1）：clang lld llvm
# Debian/Ubuntu 对应：qt6-base-dev qt6-declarative-dev libmpv-dev ffmpeg yt-dlp cmake ninja-build pkg-config
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/desktop"
BUILD="$SRC/build-native"

say() { printf '  %s\n' "$*"; }
die() { printf '\n  ✗ %s\n' "$*" >&2; exit 1; }

[ -f "$SRC/CMakeLists.txt" ] || die "找不到 desktop/CMakeLists.txt（请在源码根目录运行）"

echo "== 1/5 检查依赖 =="
for t in cmake ninja pkg-config; do
    command -v "$t" >/dev/null || die "缺少 $t（Arch: sudo pacman -S --needed cmake ninja pkgconf）"
done
pkg-config --exists mpv || die "缺少 libmpv 开发文件（Arch: sudo pacman -S mpv）"
if command -v qmake6 >/dev/null; then
    QT_PREFIX="$(qmake6 -query QT_INSTALL_PREFIX)"
    say "Qt6 前缀: $QT_PREFIX"
else
    die "缺少 qmake6（Arch: sudo pacman -S qt6-base）"
fi
command -v yt-dlp >/dev/null || say "提示：未装 yt-dlp，在线搜索/播放不可用（Arch: sudo pacman -S yt-dlp）"
command -v ffmpeg  >/dev/null || say "提示：未装 ffmpeg，视频下载无法合并音轨（Arch: sudo pacman -S ffmpeg）"

echo "== 2/5 配置 =="
OPTS=(-S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_INSTALL_PREFIX=/usr
      -DCMAKE_PREFIX_PATH="$QT_PREFIX")
if [ "${HOV_OPTIMIZED:-0}" = "1" ]; then
    command -v clang++ >/dev/null || die "HOV_OPTIMIZED=1 需要 clang（Arch: sudo pacman -S clang lld llvm）"
    OPTS+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
           -DHOV_LTO=ON)
    if [ -n "${HOV_PROFDATA:-}" ] && [ -f "${HOV_PROFDATA:-}" ]; then
        OPTS+=(-DHOV_PGO=use -DHOV_PROFDATA="$HOV_PROFDATA")
        say "启用 PGO：$HOV_PROFDATA"
    fi
    say "启用 clang + full-LTO"
fi
cmake "${OPTS[@]}"
cmake --build "$BUILD" -j"$(nproc)"

BIN="$BUILD/hov-qt"
[ -x "$BIN" ] || die "构建产物缺失：$BIN"
echo "== 3/5 构建完成 =="
say "二进制: $BIN（$(stat -c %s "$BIN") 字节）"

echo "== 4/5 逻辑自检（应为 105/105 通过）=="
# 自检只要逻辑，无需真实显示：用 offscreen 平台即可（渲染类验证才需要真实桌面/Xvfb）
QT_QPA_PLATFORM=offscreen "$BIN" --queue-selftest --exit-after 10 2>&1 | grep -a "SELFTEST" || true

echo "== 5/5 完成 =="
cat <<EOF
  启动：  $BIN
  或安装到系统（可选）：sudo cmake --install "$BUILD"

  手工核对建议（Arch + KDE）：
   1) 首次启动会请求通知权限——允许即可（播放控制栏/媒体键需要）
   2) 播放本地视频，拖动进度条数次 —— 不应冻结（本地用小缓存）
   3) 控制条点模式按钮四下 —— 顺序/单曲/随机/列表循环
   4) 快捷键 V —— 自动 → 2160p → 1440p → 1080p → 720p → 480p → 360p → 仅音频
   5) 下载一个视频 —— 产物应有声音（两路下载+本地混流）；音乐下载应有同名 .lrc
   6) 重启应用 —— 队列（含播放模式）与进度应恢复

  出问题请附：截图 + ~/.config/hov/app.log 中 [WATCHDOG]/[SEEK]/[MUX]/[DL]/[SNAPSHOT] 相关行
EOF
if [ "${HOV_RUN:-0}" = "1" ]; then
    say "启动界面…"
    exec "$BIN"
fi
