#!/usr/bin/env bash
# ── CI：deb 隔离构建（systemd-nspawn 全新环境 ✓ 用后即毁 ✓）──────────────────
# 运行位置：自托管 runner（Ubuntu 打包机 ✓）。产物 → out/hov-qt_<ver>-1_amd64.deb
set -euo pipefail
VER=$(grep -oE 'project\(HyperOnlineVideo VERSION [0-9.]+' desktop/CMakeLists.txt | grep -oE '[0-9.]+$')
SUITE=resolute; ROOT=/var/lib/machines/hov-ci-deb; OUT="$(pwd)/out"; mkdir -p "$OUT"
cleanup() { sudo rm -rf "$ROOT"; }
trap cleanup EXIT
sudo rm -rf "$ROOT"
sudo debootstrap --arch=amd64 --variant=buildd --components=main,universe "$SUITE" "$ROOT" \
  http://archive.ubuntu.com/ubuntu/ >/dev/null
# 源码树 + 打包资产进容器
# ⚠️ 3.0(quilt) 源码格式要求 **../ 存在 hov-qt_<ver>.orig.tar.gz** ✓（本项目第 4 次踩 ✗ 见避坑 #？）
#    → 按手工实证流程组织：/build/hov-qt-<ver>/（树 ✓）+ debian/ ✓ + /build/hov-qt_<ver>.orig.tar.gz ✓
WORK=$(mktemp -d); git archive HEAD | tar x -C "$WORK"
sudo mkdir -p "$ROOT/build/hov-qt-$VER"
sudo cp -a "$WORK/." "$ROOT/build/hov-qt-$VER/"
sudo cp -a desktop/packaging/deb/debian "$ROOT/build/hov-qt-$VER/debian"
sudo tar czf "$ROOT/build/hov-qt_$VER.orig.tar.gz" -C "$ROOT/build" "hov-qt-$VER"
sudo systemd-nspawn -D "$ROOT" --resolv-conf=copy-host -q -- \
  bash -c "export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq build-essential dpkg-dev debhelper cmake ninja-build pkgconf \
      qt6-base-dev qt6-declarative-dev qt6-webengine-dev libmpv-dev clang lld fakeroot tzdata >/dev/null
    cd /build/hov-qt-$VER && dpkg-buildpackage -us -uc -j\$(nproc) >/tmp/build.log 2>&1 || { tail -30 /tmp/build.log; exit 1; }"
sudo find "$ROOT/build" -maxdepth 1 -name "hov-qt_${VER}-1_amd64.deb" -exec cp {} "$OUT/" \;
ls -la "$OUT"/hov-qt_*_amd64.deb
