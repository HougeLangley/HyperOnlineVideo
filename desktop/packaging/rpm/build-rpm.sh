#!/bin/bash
# ── Fedora RPM 隔离构建驱动（规则 #12 ✓ 容器化隔离 ✓ 构建完成即销毁隔离环境 ✓）──────────
# 用法: ./build-rpm.sh <源码tarball目录>
#   <源码tarball目录> 内需有 hov-qt-1.2.0.tar.gz 与 hov-qt.spec
# ⚠ 设计纪律（#183 教训 ✗）：**输入目录与工作目录必须分开** ——
#   本脚本第 1 步会 rm -rf 工作目录，若它与脚本/源码同目录 → 自我删除 ✗✓
set -euo pipefail
IN_DIR="${1:?用法: build-rpm.sh <含 tarball+spec 的目录>}"
WORK="${HOME}/hov-rpm-work"          # ★ 工作区（每次重建 + 结束后销毁 ★）
LOG="${HOME}/hov-rpm-build.log"      # ★ 日志放工作区之外（否则被删 ✗）★
# ★ 用 /etc/mock/ 下的**自定义配置名**（mock 的 include() 相对 /etc/mock 解析 ✓）★
#   hov-fedora-44-ustc.cfg = 系统 fedora-44-x86_64 模板 + USTC 镜像 + 关闭镜像引导
#   （官方源在本机不可达 ✗ 实测超时；USTC 2.28 MB/s ✓）
CHROOT="hov-fedora-44-ustc"
exec > "$LOG" 2>&1
trap 'echo "（脚本退出码 $?）"' EXIT

echo "═══ [0] 环境取证 ═══"
. /etc/os-release; echo "  打包机: $PRETTY_NAME $(uname -m)"
echo "  mock: $(rpm -q --qf '%{VERSION}-%{RELEASE}' mock)"
echo "  chroot 配置: $CHROOT（$(ls /etc/mock/${CHROOT}.cfg 2>/dev/null || echo '使用内置 ✓')）"
echo "  输入目录: $IN_DIR"; ls -la "$IN_DIR" | sed 's/^/    /'
echo "  工作区:   $WORK（本步将被清空重建 ✓）"

echo "═══ [1] 重置工作区（隔离起点 ✓）═══"
rm -rf "$WORK"
mkdir -p "$WORK"/{SOURCES,SPECS,SRPMS,RPMS/result,BUILD,BUILDROOT}
cp -f "$IN_DIR"/hov-qt-1.2.0.tar.gz "$WORK/SOURCES/"
cp -f "$IN_DIR"/hov-qt.spec "$WORK/SPECS/"
echo "  SOURCES: $(ls "$WORK/SOURCES")"
echo "  SPECS:   $(ls "$WORK/SPECS")"

echo "═══ [2] 构建 SRPM（源包 ✓ 标准流程 ✓）═══"
rpmbuild --define "_topdir $WORK" -bs "$WORK/SPECS/hov-qt.spec" 2>&1 | tail -4
SRPM=$(ls -t "$WORK"/SRPMS/*.src.rpm | head -1)
echo "  SRPM: $(basename "$SRPM")  ($(du -k "$SRPM" | cut -f1) KB)"

echo "═══ [3] mock 隔离构建（干净 buildroot ✓ 只用 spec 声明的 BuildRequires ✓）═══"
# mock 需 root 或 mock 组权限 → 本机 houge 不在 mock 组 → 用 sudo ✓（sudo 免密 ✓）
# 输出**完整落盘** ✓（#183 教训：不要用 | tail 吞掉失败证据 ✗）
if sudo -n mock -r "$CHROOT" --resultdir="$WORK/RPMS/result" --rebuild "$SRPM" > "$WORK/mock.log" 2>&1; then
    echo "  ✓ mock 构建成功（完整日志 $WORK/mock.log ✓）"
else
    echo "  ✗ mock 构建失败 → 关键错误："
    grep -E "ERROR|error:|Child return code|Failed|failure|Access|denied|not in|WARNING: " "$WORK/mock.log" | tail -25 | sed 's/^/    /'
    echo "  ── 日志末尾 15 行 ──"; tail -15 "$WORK/mock.log" | sed 's/^/    /'
    exit 1
fi
echo "  ── mock 关键行 ──"
grep -E "Start:|Finish:|Result:|Child return code|Wrote:|warning: |ERROR" "$WORK/mock.log" | tail -12 | sed 's/^/    /' 

echo "═══ [4] 产物 ═══"
ls -la "$WORK/RPMS/result/" | grep -E "\.rpm$" | awk '{printf "  %-56s %9.1f KB\n", $NF, $5/1024}'
BINRPM=$(ls "$WORK/RPMS/result/"*.x86_64.rpm 2>/dev/null | grep -v debuginfo | grep -v debugsource | grep -v "\.src\." | head -1)
DBGRPM=$(ls "$WORK/RPMS/result/"*.x86_64.rpm 2>/dev/null | grep debuginfo | head -1)
echo "  主包: $(basename "${BINRPM:-未产出 ✗}")"
echo "  调试包: $(basename "${DBGRPM:-无}")"

echo "═══ [5] 校验（rpm -qpi / -qpl / rpmlint ✓）═══"
echo "  ── rpm -qpi ──"; rpm -qpi "$BINRPM" 2>&1 | sed 's/^/  /'
echo "  ── 文件清单 ──"; rpm -qpl "$BINRPM" 2>&1 | sed 's/^/  /'
echo "  ── Requires（自动依赖 ✓）──"; rpm -qR "$BINRPM" 2>&1 | grep -vE "^(rpmlib|rtld)" | sed 's/^/  /'
echo "  ── rpmlint ──"; rpmlint "$BINRPM" 2>&1 | sed 's/^/  /' || true
echo "  ── 抽查包内文件（启动器修复必须在内 ✓）──"
TMP=$(mktemp -d); cd "$TMP"; rpm2cpio "$BINRPM" | cpio -idm --quiet 2>/dev/null
echo "    /usr/bin/hov-qt: LC_NUMERIC×$(grep -c LC_NUMERIC usr/bin/hov-qt 2>/dev/null) QT_NO_GLIB×$(grep -c QT_NO_GLIB usr/bin/hov-qt 2>/dev/null) ulimit×$(grep -c 'ulimit -n' usr/bin/hov-qt 2>/dev/null)"
echo "    /usr/bin/hov-qt-bin: $(file -b usr/bin/hov-qt-bin 2>/dev/null | cut -c1-60)"
echo "    man: $(ls usr/share/man/man1/ 2>/dev/null | tr '\n' ' ')"
echo "    license: $(ls usr/share/licenses/hov-qt/ 2>/dev/null | tr '\n' ' ')"
echo "    desktop: $(ls usr/share/applications/ 2>/dev/null)"
cd /; rm -rf "$TMP"

echo "═══ [6] 销毁隔离环境（规则 #12 ✓ mock 缓存与 chroot 全部清除 ✓）═══"
sudo -n mock -r "$CHROOT" --scrub=all 2>&1 | tail -3 | sed 's/^/  /'
echo "  销毁后 chroot 目录: $(sudo du -sh /var/lib/mock/${CHROOT} 2>/dev/null | cut -f1 || echo '不存在 ✓')"
echo "  构建工作区保留供取回 ✓（$WORK）"
echo DONE
