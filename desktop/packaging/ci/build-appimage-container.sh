#!/usr/bin/env bash
# ── CI：AppImage 隔离构建（nspawn 全新 Ubuntu ✓ 用后即毁 ✓）──────────────────
set -euo pipefail
ROOT=/var/lib/machines/hov-ci-appimage; OUT="$(pwd)/out"; mkdir -p "$OUT"
cleanup() { sudo rm -rf "$ROOT"; }
trap cleanup EXIT
sudo rm -rf "$ROOT"
sudo debootstrap --arch=amd64 --variant=buildd --components=main,universe resolute "$ROOT" \
  http://archive.ubuntu.com/ubuntu/ >/dev/null
WORK=$(mktemp -d); git archive HEAD | tar x -C "$WORK"
sudo mkdir -p "$ROOT/build"; sudo cp -a "$WORK/." "$ROOT/build/"
sudo systemd-nspawn -D "$ROOT" --resolv-conf=copy-host -q -- \
  bash -c 'export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq build-essential cmake ninja-build pkgconf curl file zsync \
      qt6-base-dev qt6-declarative-dev qt6-wayland libmpv-dev ffmpeg clang lld fuse3 >/dev/null
    # 容器内无 FUSE ✗ → 自解压运行 ✓；plugin-qt 必须在 PATH ✓（build-appimage.sh 注释 #5 ✓）
    export APPIMAGE_EXTRACT_AND_RUN=1
    curl -fsSLo /usr/local/bin/linuxdeploy https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    curl -fsSLo /usr/local/bin/linuxdeploy-plugin-qt https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
    chmod +x /usr/local/bin/linuxdeploy /usr/local/bin/linuxdeploy-plugin-qt
    export PATH="/usr/local/bin:$PATH"; export QMAKE="$(command -v qmake6)"
    cd /build/desktop && bash packaging/build-appimage.sh >/tmp/ai.log 2>&1 || { tail -30 /tmp/ai.log; exit 1; }'
sudo find "$ROOT/build/desktop" -maxdepth 1 -name "*.AppImage" -exec cp {} "$OUT/" \;
ls -la "$OUT"/*.AppImage
