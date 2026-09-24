#!/usr/bin/env bash
# macOS 打包（Phase 5.5）：把 SPM 产物打成自包含 .app + DMG
#
# 内容：
#   1) swift build -c release
#   2) .app 骨架（Info.plist / 图标 / 可执行）
#   3) 递归收集 libmpv 的全部非系统依赖 → Contents/Frameworks，并重写 install_name
#   4) ad-hoc 签名（Apple Silicon 上修改过的二进制必须签名才能运行）
#   5) create-dmg 出包（附"去隔离"说明）
#
# 用法：bash packaging/build-app.sh [--no-dmg]
set -euo pipefail

cd "$(dirname "$0")/.."          # → macos/
MACOS_DIR="$(pwd)"
REPO_DIR="$(cd .. && pwd)"
APP_NAME="HyperOnlineVideo"
APP="$MACOS_DIR/build/$APP_NAME.app"
VER="1.2.1"
DO_DMG=1
[ "${1:-}" = "--no-dmg" ] && DO_DMG=0

echo "== 1/6 构建 release =="
swift build -c release
# 用 SPM 报告的产物目录（.build/release 软链目标会随构建系统变化，写死路径会拿到过期二进制）
BIN="$(swift build -c release --show-bin-path)/$APP_NAME"
[ -x "$BIN" ] || { echo "找不到可执行文件 $BIN"; exit 1; }

echo "== 2/6 组装 .app =="
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"
cp "$BIN" "$APP/Contents/MacOS/$APP_NAME"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>聚合视频</string>
  <key>CFBundleDisplayName</key><string>聚合视频 Hyper Online Video</string>
  <key>CFBundleExecutable</key><string>$APP_NAME</string>
  <key>CFBundleIdentifier</key><string>com.hougelangley.hyperonlinevideo</string>
  <key>CFBundleVersion</key><string>$VER</string>
  <key>CFBundleShortVersionString</key><string>$VER</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.entertainment</string>
  <key>NSHumanReadableCopyright</key><string>GPL-3.0-only · Hyper Online Video</string>
  <!-- 音乐/视频 CDN 大量用明文 http（网易云 m80x.music.126.net、QQ dl.stream.qqmusic.qq.com 等），
       下载走本进程 URLSession，会被 ATS 拦成 "App Transport Security policy requires the use of
       a secure connection"（用户实测：下载必失败）。mpv 走 C 层网络不受 ATS 限制，所以只有下载受影响。 -->
  <key>NSAppTransportSecurity</key>
  <dict>
    <key>NSAllowsArbitraryLoads</key><true/>
  </dict>
</dict>
</plist>
PLIST

echo "== 3/6 图标 =="
ICON_SRC="$REPO_DIR/desktop/packaging/hov-qt-512.png"
if [ -f "$ICON_SRC" ]; then
  ICONSET="$MACOS_DIR/build/AppIcon.iconset"
  rm -rf "$ICONSET"; mkdir -p "$ICONSET"
  for s in 16 32 64 128 256 512; do
    sips -z $s $s "$ICON_SRC" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    d=$((s*2)); sips -z $d $d "$ICON_SRC" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
  echo "  图标已生成（源自 desktop/packaging/hov-qt-512.png）"
else
  echo "  跳过（找不到 $ICON_SRC）"
fi

echo "== 4/6 收集动态库闭包 =="
python3 - "$APP" <<'PY'
import subprocess, os, shutil, sys
app = sys.argv[1]
fw = os.path.join(app, "Contents/Frameworks")
target_bin = os.path.join(app, "Contents/MacOS/HyperOnlineVideo")

def deps(path):
    out = subprocess.run(["otool", "-L", path], capture_output=True, text=True).stdout
    res = []
    for line in out.splitlines()[1:]:
        lib = line.strip().split(" ")[0]
        if lib.startswith("/usr/lib/") or lib.startswith("/System/"):
            continue
        res.append(lib)
    return res

# 递归闭包
seen, stack = {}, [d for d in deps(target_bin) if "libmpv" in d or "libav" in d or "libass" in d or "libplacebo" in d]
while stack:
    p = stack.pop()
    real = os.path.realpath(p)
    if real in seen:
        continue
    if not os.path.exists(real):
        continue
    seen[real] = os.path.basename(real)
    for d in deps(real):
        stack.append(d)

os.makedirs(fw, exist_ok=True)
mapping = {}
for real, name in seen.items():
    dst = os.path.join(fw, name)
    if not os.path.exists(dst):
        shutil.copy2(real, dst)
        os.chmod(dst, 0o755)
    mapping[real] = "@executable_path/../Frameworks/" + name
print(f"  已复制 {len(mapping)} 个动态库，合计 {sum(os.path.getsize(os.path.join(fw,n)) for n in os.listdir(fw))/1048576:.1f} MB")

# 重写 install_name：二进制 + 每个已打包库
def rewrite(path):
    for old, new in mapping.items():
        subprocess.run(["install_name_tool", "-change", old, new, path],
                       capture_output=True, text=True)
        # 有的记录用的是软链路径（/opt/homebrew/lib/...），一并处理
        alt = old.replace("/opt/homebrew/opt/", "/opt/homebrew/")
        if alt != old:
            subprocess.run(["install_name_tool", "-change", alt, new, path],
                           capture_output=True, text=True)

rewrite(target_bin)
for name in os.listdir(fw):
    p = os.path.join(fw, name)
    rewrite(p)
    subprocess.run(["install_name_tool", "-id", "@executable_path/../Frameworks/" + name, p],
                   capture_output=True, text=True)
print("  install_name 已重写")
PY

echo "== 5/6 ad-hoc 签名 =="
codesign --force --deep --sign - "$APP" 2>&1 | tail -2 | sed 's/^/  /'
codesign --verify --verbose=1 "$APP" 2>&1 | tail -2 | sed 's/^/  /'

echo "== 6/6 DMG =="
if [ "$DO_DMG" = "1" ]; then
  DMG="$MACOS_DIR/build/HyperOnlineVideo-$VER-arm64.dmg"
  rm -f "$DMG"
  # 用 hdiutil 直接出包：create-dmg 依赖 Finder/AppleScript，自动化环境里常失败
  STAGE="$MACOS_DIR/build/dmg-stage"
  rm -rf "$STAGE"; mkdir -p "$STAGE"
  cp -R "$APP" "$STAGE/"
  ln -s /Applications "$STAGE/Applications"
  cat > "$STAGE/安装说明.txt" <<'NOTE'
首次打开若提示"无法验证开发者"：
  1) 在「访达」里右键点 App → 打开 → 再点「打开」；或
  2) 终端执行：xattr -dr com.apple.quarantine /Applications/HyperOnlineVideo.app
（App 未做 Apple 公证，这是 Gatekeeper 的正常提示）

依赖：本 App 自带 libmpv/ffmpeg 等 48 个动态库，无需额外安装。
yt-dlp 需系统提供（brew install yt-dlp），搜索/在线解析要用它。
NOTE
  hdiutil create -volname "聚合视频 Hyper Online Video" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
  rm -rf "$STAGE"
  [ -f "$DMG" ] && echo "  DMG: ${DMG} ($(du -h "${DMG}" | cut -f1))" || echo "  DMG 生成失败（.app 可用）"
fi
echo "完成：${APP}"
