#!/usr/bin/env bash
# 构建便携版 AppImage（无需安装、双击即用）
# 用法：在 desktop/ 目录下执行   bash packaging/build-appimage.sh
#      加冒烟测试：              HOV_SMOKE=1 bash packaging/build-appimage.sh
# 依赖：cmake ninja curl qt6-base（qmake6）、qt6-wayland、mpv（libmpv）、ffmpeg
# 首次运行会把 linuxdeploy / linuxdeploy-plugin-qt 下载到 ~/.cache/hov-appimage
#
# 踩过的坑（勿改这几条）：
#   1) --output appimage 与依赖部署在「同一次」linuxdeploy 调用内完成，所有要进包的文件
#      必须在调用之前就位。曾把 wayland 插件放在调用之后复制，结果包里只有 xcb。
#   2) Wayland 平台插件（libqwayland.so）只是入口，还必须有子插件目录：
#      wayland-shell-integration（libxdg-shell.so 等）、wayland-graphics-integration-client
#      （libqt-plugin-wayland-egl.so 等）、wayland-decoration-client。
#      缺了它们会报 "No shell integration named xdg-shell found" 然后 SIGABRT。
#   3) 校验必须针对「最终 AppImage 解包后的内容」，只查 AppDir 会放过假包。
#   4) 本机该 AppImage runtime 的 --appimage-extract-and-run 会返回 127（不可用），
#      冒烟测试改用「--appimage-extract 解包后直接跑 AppRun」。
#   5) linuxdeploy 的 qt 插件只有在 PATH 里能找到 linuxdeploy-plugin-qt 且导出了 QMAKE 时才生效。
set -euo pipefail
trap 'echo "[错误] 脚本第 $LINENO 行执行失败，已中止" >&2' ERR

here="$(cd "$(dirname "$0")/.." && pwd)"
cd "$here"

CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/hov-appimage"
APPNAME="HyperOnlineVideo"
ARCH="$(uname -m)"

# 必须随包分发的 Qt 插件目录（相对 $QT_PLUGINS）
EXTRA_PLUGIN_DIRS=(
  wayland-shell-integration
  wayland-graphics-integration-client
  wayland-decoration-client
  platforminputcontexts
)

step() { printf '\n== %s ==\n' "$1"; }

# linuxdeploy 的 qt 插件会按类别部署插件（platforms / imageformats / tls / sqldrivers …），
# 其中**任何一个插件的依赖缺失，整个打包就会失败**，而且报错在 linuxdeploy 内部，不好定位。
# Arch 上这类"可选依赖"默认不装：kimageformats 的 libavif/libheif/jxrlib/libjxl/libraw，
# sqldrivers 的 mariadb-libs/postgresql-libs/unixodbc —— 已实测踩过三轮。
precheck_plugin_deps() {
  # 只检查 linuxdeploy **实际会部署**的类别（写死清单 + 实测新增的 position）。
  # 教训：① 曾漏掉 position → 打包在 position 插件上失败；
  #       ② 改成"扫全部目录"又会误报 kf6/crypto/scenegraph 等根本不部署的插件 → 两者都不可取。
  local dirs="platforms imageformats iconengines styles platformthemes platforminputcontexts
              generic tls sqldrivers networkinformation xcbglintegrations egldeviceintegrations
              position wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration"
  local missing="" d f lib
  for d in $dirs; do
    [ -d "$QT_PLUGINS/$d" ] || continue
    while IFS= read -r f; do
      while IFS= read -r lib; do
        [ -n "$lib" ] && missing="$missing $lib"
      done < <(ldd "$f" 2>/dev/null | awk '/not found/{print $1}')
    done < <(find "$QT_PLUGINS/$d" -maxdepth 1 -name '*.so' 2>/dev/null)
  done
  missing="$(echo $missing | tr ' ' '\n' | sort -u | grep -v '^$' || true)"
  [ -n "$missing" ] || { echo "  ✓ 会被部署的 Qt 插件依赖全部解析"; return 0; }

  echo "  以下 Qt 插件的依赖未解析（会让 linuxdeploy 的 qt 插件直接失败）："
  echo "$missing" | sed 's/^/    缺库: /'
  local pkgs=""
  if command -v pacman >/dev/null 2>&1; then
    echo "$missing" | while IFS= read -r lib; do
      pacman -Fq "$lib" 2>/dev/null | head -1 | cut -d/ -f2
    done | sort -u > /tmp/.hov_pkgs
    pkgs="$(tr '\n' ' ' < /tmp/.hov_pkgs)"
    rm -f /tmp/.hov_pkgs
    if [ -n "$pkgs" ]; then
      echo "  修复命令（Arch）： sudo pacman -S --needed $pkgs"
    else
      echo "  提示：先执行 sudo pacman -Fy 同步文件数据库，即可自动反查所属包"
    fi
  else
    echo "  提示：请用发行版的软件包管理器补齐上述库（例如 Fedora：dnf provides '<lib>'）"
  fi
  return 1
}

step "1/6 环境检查"
for c in cmake ninja curl; do
  command -v "$c" >/dev/null 2>&1 || { echo "  缺少依赖: $c"; exit 1; }
done
QMAKE="$(command -v qmake6 || command -v qmake || true)"
[ -n "$QMAKE" ] || { echo "  缺少 qmake（Arch: qt6-base / Fedora: qt6-qtbase-devel）"; exit 1; }
QT_PLUGINS="$("$QMAKE" -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
[ -n "$QT_PLUGINS" ] || QT_PLUGINS="/usr/lib/qt6/plugins"
echo "  qmake: $QMAKE"
echo "  Qt 插件目录: $QT_PLUGINS"
MPV_SO=""
if command -v ldconfig >/dev/null 2>&1; then
  MPV_SO="$(ldconfig -p 2>/dev/null | awk '/libmpv\.so\.2/{print $NF; exit}' || true)"
fi
if [ -z "$MPV_SO" ]; then
  MPV_SO="$(find /usr/lib /usr/lib64 /usr/local/lib -maxdepth 2 -name 'libmpv.so.2*' 2>/dev/null | head -1 || true)"
fi
[ -n "$MPV_SO" ] || { echo "  缺少 libmpv（Arch: mpv / Fedora: mpv-libs）"; exit 1; }
echo "  libmpv: $MPV_SO"

WAYLAND_SRC=()
while IFS= read -r p; do
  [ -n "$p" ] && WAYLAND_SRC+=("$p")
done < <(find "$QT_PLUGINS/platforms" -maxdepth 1 -name 'libqwayland*.so' 2>/dev/null | sort)
if [ "${#WAYLAND_SRC[@]}" -eq 0 ]; then
  echo "  (警告) 未找到 wayland 平台插件（Arch 请装 qt6-wayland），包将只有 xcb"
else
  echo "  wayland 平台插件: $(basename -a "${WAYLAND_SRC[@]}" | tr '\n' ' ')"
fi
for d in "${EXTRA_PLUGIN_DIRS[@]}"; do
  [ -d "$QT_PLUGINS/$d" ] || echo "  (警告) 缺少插件目录 $d"
done
if ! precheck_plugin_deps; then
  echo "  已中止：先补齐依赖再打包（上述插件与本播放器功能无关，但会让部署流程报错退出）"
  exit 1
fi

step "2/6 构建 Release（独立构建目录，避免污染开发构建）"
# 便携包**不打包** QtWebEngine：它会拉进 187 MB 依赖（登录窗口在 AppImage 里改为
# 「手动放置 cookie 文件」；需要内置登录请用 Arch 包或开发构建，详见 packaging/README.md）。
#
# 注意：必须用**独立构建目录** —— 曾经在开发用的 build/ 里加了这个禁用参数，
# 结果缓存残留导致之后的开发构建也丢了 WebEngine（登录组件凭空消失，排查很久）。
BUILD_DIR="$here/build-appimage"
cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_DISABLE_FIND_PACKAGE_Qt6WebEngineWidgets=ON
cmake --build "$BUILD_DIR"
BIN="$BUILD_DIR/hov-qt"
[ -x "$BIN" ] || { echo "  构建产物缺失: $BIN"; exit 1; }
echo "  二进制: $BIN ($(stat -c %s "$BIN") 字节)"

step "3/6 准备 AppDir（预置全部 Qt 插件，必须在 linuxdeploy 之前）"
rm -rf AppDir
mkdir -p AppDir/usr/bin AppDir/usr/share/applications AppDir/usr/share/icons/hicolor/256x256/apps
install -Dm755 "$BIN" AppDir/usr/bin/hov-qt
# .desktop 与图标使用仓库内单一来源文件（与 PKGBUILD 安装的内容一致）
install -Dm644 "$here/packaging/hov-qt.desktop" AppDir/usr/share/applications/hov-qt.desktop
install -Dm644 "$here/packaging/hov-qt.png" AppDir/usr/share/icons/hicolor/256x256/apps/hov-qt.png
install -Dm644 "$here/packaging/hov-qt-512.png" AppDir/usr/share/icons/hicolor/512x512/apps/hov-qt.png
PLUGIN_SOS=()
copy_plugin_dir() {   # $1 = 插件子目录名
  local d="$1" src="$QT_PLUGINS/$1" dst="AppDir/usr/plugins/$1" n=0 f
  [ -d "$src" ] || return 0
  mkdir -p "$dst"
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    cp -a "$f" "$dst/"
    PLUGIN_SOS+=("$f")
    n=$((n + 1))
  done < <(find "$src" -maxdepth 1 -name '*.so' 2>/dev/null | sort)
  echo "  $d: $n 个插件"
}
# 平台插件：只带 wayland(全部变体) 与 xcb，避免把 eglfs/vnc 等无关后端打进去
mkdir -p AppDir/usr/plugins/platforms
for f in "${WAYLAND_SRC[@]}"; do cp -a "$f" AppDir/usr/plugins/platforms/; PLUGIN_SOS+=("$f"); done
if [ -f "$QT_PLUGINS/platforms/libqxcb.so" ]; then
  cp -a "$QT_PLUGINS/platforms/libqxcb.so" AppDir/usr/plugins/platforms/
  PLUGIN_SOS+=("$QT_PLUGINS/platforms/libqxcb.so")
fi
echo "  platforms: $(ls AppDir/usr/plugins/platforms | tr '\n' ' ')"
for d in "${EXTRA_PLUGIN_DIRS[@]}"; do copy_plugin_dir "$d"; done

step "4/6 准备打包工具"
mkdir -p "$CACHE/bin"
dl() {
  if [ ! -x "$2" ]; then
    echo "  下载 $(basename "$2") ..."
    curl -fsSLo "$2" "$1"
    chmod +x "$2"
  fi
}
dl "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" "$CACHE/linuxdeploy"
dl "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage" "$CACHE/linuxdeploy-plugin-qt"
ln -sf "$CACHE/linuxdeploy" "$CACHE/bin/linuxdeploy"
ln -sf "$CACHE/linuxdeploy-plugin-qt" "$CACHE/bin/linuxdeploy-plugin-qt"
export PATH="$CACHE/bin:$PATH"
export QMAKE
export QML_SOURCES_PATHS="$here/src"

step "5/6 部署依赖（第一遍：只部署，不打包）"
LIB_ARGS=("-l" "$MPV_SO")
for f in "${PLUGIN_SOS[@]}"; do LIB_ARGS+=("-l" "$f"); done   # 让 linuxdeploy 解析插件的依赖
echo "  显式部署: libmpv + ${#PLUGIN_SOS[@]} 个插件"

linuxdeploy --appdir AppDir \
  --executable AppDir/usr/bin/hov-qt \
  --desktop-file AppDir/usr/share/applications/hov-qt.desktop \
  --icon-file AppDir/usr/share/icons/hicolor/256x256/apps/hov-qt.png \
  "${LIB_ARGS[@]}" \
  --plugin qt

step "5b/6 精简（删除与播放无关的插件与末尾未引用的库）"
if [ "${HOV_NO_PRUNE:-0}" = "1" ]; then
  echo "  (已通过 HOV_NO_PRUNE=1 关闭)"
else
  SIZE_BEFORE="$(du -sm AppDir | awk '{print $1}')"
  # 1) 删插件：播放器不使用 Qt 的图像格式/数据库/打印/QML 工具类插件，
  #    但它们会把 libaom/libheif/libjxr/mariadb/pq 等十几 MB 的库拖进包
  for d in imageformats sqldrivers printsupport qmltooling qmllint qmlls kf6; do
    if [ -d "AppDir/usr/plugins/$d" ]; then
      echo "  删除插件目录 $d（$(du -sh AppDir/usr/plugins/$d | awk '{print $1}')）"
      rm -rf "AppDir/usr/plugins/$d"
    fi
  done
  # 2) 迭代删除"没有任何 ELF 引用"的库（快走到不动点，最多 4 轮）
  for round in 1 2 3 4; do
    NEEDED="$(mktemp)"
    # 注：外层套 || true 是必需的 —— 脚本开了 pipefail，而 ldd/grep 在无匹配时会返回非零
    while IFS= read -r elf; do
      LD_LIBRARY_PATH="$PWD/AppDir/usr/lib" ldd "$elf" 2>/dev/null || true
    done < <(find AppDir/usr/bin AppDir/usr/lib AppDir/usr/plugins -type f \( -name '*.so*' -o -perm -u+x \) 2>/dev/null) \
      | awk '{print $3}' | grep -F "$PWD/AppDir/usr/lib/" | xargs -r -n1 basename \
      | sort -u > "$NEEDED" || true
    REMOVED=0
    while IFS= read -r f; do
      base="$(basename "$f")"
      if ! grep -qxF "$base" "$NEEDED"; then rm -f "$f" && REMOVED=$((REMOVED + 1)); fi
    done < <(find AppDir/usr/lib -maxdepth 1 -type f -name '*.so*')
    rm -f "$NEEDED"
    [ "$REMOVED" -eq 0 ] && break
    echo "  第 $round 轮删除未引用库: $REMOVED 个"
  done
  SIZE_AFTER="$(du -sm AppDir | awk '{print $1}')"
  echo "  AppDir 体积: ${SIZE_BEFORE} MB -> ${SIZE_AFTER} MB（省 $((SIZE_BEFORE - SIZE_AFTER)) MB）"
fi

step "5c/6 生成 AppImage（第二遍：只打包）"
linuxdeploy --appdir AppDir --output appimage

step "6/6 校验最终 AppImage（解包后检查内容，防'假包'）"
NEWEST="$(ls -1t "$here"/*.AppImage 2>/dev/null | head -1 || true)"
[ -n "$NEWEST" ] || { echo "  未找到生成的 AppImage"; exit 1; }
TARGET="$here/${APPNAME}-${ARCH}.AppImage"
[ "$NEWEST" = "$TARGET" ] || mv -f "$NEWEST" "$TARGET"

EXDIR="$here/.appimage-check"
rm -rf "$EXDIR" && mkdir -p "$EXDIR"
( cd "$EXDIR" && "$TARGET" --appimage-extract >/dev/null 2>&1 ) || { echo "  无法解包 AppImage"; exit 1; }
ROOT="$EXDIR/squashfs-root"
[ -d "$ROOT" ] || { echo "  解包目录缺失"; exit 1; }

fail=0
chk() {
  local name="$1" pat="$2" n
  n="$(find "$ROOT" -name "$pat" -type f 2>/dev/null | wc -l)"
  printf '  %-26s %s\n' "$name" "$n"
  [ "$n" -gt 0 ] || fail=1
}
chk "libmpv" "libmpv.so.2*"
chk "libQt6Core" "libQt6Core.so.6*"
chk "libQt6DBus" "libQt6DBus.so.6*"      # MPRIS（媒体键/后台播放）依赖它
chk "libQt6Network" "libQt6Network.so.6*"
chk "libavcodec" "libavcodec.so*"
chk "libass" "libass.so*"
chk "平台插件 wayland" "libqwayland*.so"
chk "平台插件 xcb" "libqxcb.so"
chk "xdg-shell 集成" "libxdg-shell.so"
chk "wayland egl 集成" "libqt-plugin-wayland-egl.so"
echo "  解包后 platforms/: $(ls "$ROOT/usr/plugins/platforms" 2>/dev/null | tr '\n' ' ')"
echo "  解包后 shell 集成: $(ls "$ROOT/usr/plugins/wayland-shell-integration" 2>/dev/null | tr '\n' ' ')"
if [ "$fail" -ne 0 ]; then
  echo "  校验未通过：删除该 AppImage（避免留下跑不起来的'假包'）"
  rm -f "$TARGET"
  exit 1
fi

if [ "${HOV_SMOKE:-0}" != "1" ]; then
  rm -rf "$EXDIR"
  echo
  echo "== 完成 =="
  echo "  产物: $TARGET"
  echo "  大小: $(stat -c %s "$TARGET") 字节"
  echo "  校验: sha256=$(sha256sum "$TARGET" | awk '{print $1}')"
  exit 0
fi

step "冒烟测试（解包后直接运行 AppRun；本机 runtime 的 extract-and-run 不可用）"
# 冒烟测试需要**有 Wayland 会话**（否则 Qt 平台插件连不上显示，会被误判成"包坏了"）；
# 没有会话时明确跳过，避免把环境问题当成打包问题（实测踩过：sway 没起 → exit=134）。
if [ -z "${WAYLAND_DISPLAY:-}" ] || [ ! -e "${XDG_RUNTIME_DIR:-/tmp}/$WAYLAND_DISPLAY" ]; then
  echo "  (跳过) 未检测到可用的 Wayland 会话（WAYLAND_DISPLAY='${WAYLAND_DISPLAY:-}'）"
  echo "  提示：先起无头合成器再跑本脚本："
  echo "        export XDG_RUNTIME_DIR=/tmp/xdgrun"
  echo "        WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 sway -c /tmp/sway.conf -d &"
  rm -rf "$EXDIR"
  echo
  echo "== 完成（未做冒烟）=="
  echo "  产物: $TARGET"
  echo "  大小: $(stat -c %s "$TARGET") 字节"
  echo "  校验: sha256=$(sha256sum "$TARGET" | awk '{print $1}')"
  exit 0
fi
# 选择测试用的平台插件：有 Wayland 会话就测 wayland，否则退到 xcb
SMOKE_PLATFORM="wayland"
if [ -z "${WAYLAND_DISPLAY:-}" ]; then
  SMOKE_PLATFORM="xcb"
  echo "  (提示) 未检测到 WAYLAND_DISPLAY，改用 xcb 平台测试"
fi
trap - ERR
SMOKE_LOG="$(mktemp)"
set +e
( cd "$ROOT" && QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_TO_CONSOLE=1 QT_QPA_PLATFORM="$SMOKE_PLATFORM" \
    timeout 20 ./AppRun --selftest >"$SMOKE_LOG" 2>&1 )
CODE=$?
set -e
trap 'echo "[错误] 脚本第 $LINENO 行执行失败，已中止" >&2' ERR

if grep -qE "could not be initialized|No shell integration named|Could not load the Qt platform plugin" "$SMOKE_LOG"; then
  echo "  冒烟测试失败：平台插件未能初始化（exit=$CODE）"
  grep -iE "platform plugin|shell integration|Available platform" "$SMOKE_LOG" | head -6 | sed 's/^/    /'
  rm -f "$SMOKE_LOG"; rm -rf "$EXDIR"
  exit 1
fi
if [ "$CODE" -eq 0 ] || [ "$CODE" -eq 124 ]; then
  echo "  冒烟测试通过（平台=$SMOKE_PLATFORM，exit=$CODE；124 = 超时前一直正常运行）"
else
  echo "  冒烟测试失败（exit=$CODE）"
  tail -8 "$SMOKE_LOG" | sed 's/^/    /'
  rm -f "$SMOKE_LOG"; rm -rf "$EXDIR"
  exit 1
fi
rm -f "$SMOKE_LOG"; rm -rf "$EXDIR"

echo
echo "== 完成 =="
echo "  产物: $TARGET"
echo "  大小: $(stat -c %s "$TARGET") 字节"
echo "  校验: sha256=$(sha256sum "$TARGET" | awk '{print $1}')"
