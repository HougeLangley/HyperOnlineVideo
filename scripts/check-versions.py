#!/usr/bin/env python3
# ── 版本一致性守卫（CI 门禁 ✓ 2026-10-08）────────────────────────────
# 用途：一个版本号散布在 14+ 处（CMake/gradle/mac/flake/PKGBUILD×2/spec×2/README…），
#       历史上曾出现 1.2.8/1.2.5 混存 ✗ → 本脚本一键断言"全部一致" ✓
# 用法：python3 scripts/check-versions.py [期望版本]   （省略则从 CMakeLists.txt 取基准）
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read(p):
    return (ROOT / p).read_text(encoding="utf-8")


def main():
    base = read("desktop/CMakeLists.txt")
    m = re.search(r"project\(HyperOnlineVideo VERSION ([\d.]+)", base)
    if not m:
        print("✗ CMakeLists.txt 中未找到 project VERSION")
        return 1
    want = sys.argv[1] if len(sys.argv) > 1 else m.group(1)
    errors = []

    def check(path, pattern, label):
        text = read(path)
        found = re.findall(pattern, text)
        if not found:
            errors.append(f"{label}: 未匹配到 ({path})")
        else:
            for v in found:
                if v != want:
                    errors.append(f"{label}: {v} ≠ {want} ({path})")

    check("desktop/CMakeLists.txt", r"project\(HyperOnlineVideo VERSION ([\d.]+)", "cmake")
    check("app/build.gradle.kts", r'versionName = "([\d.]+)"', "android versionName")
    check("macos/packaging/build-app.sh", r'VER="([\d.]+)"', "mac VER")
    check("flake.nix", r'version = "([\d.]+)"', "flake")
    check("desktop/packaging/PKGBUILD", r"pkgver=([\d.]+)", "arch PKGBUILD")
    check("desktop/packaging/aur/PKGBUILD", r"pkgver=([\d.]+)", "aur PKGBUILD")
    check("desktop/packaging/obs/hov-qt.spec", r"Version:\s+([\d.]+)", "obs spec")
    check("desktop/packaging/rpm/hov-qt.spec", r"Version:\s+([\d.]+)", "rpm spec")
    check("desktop/packaging/deb/debian/changelog", r"^hov-qt \(([\d.]+)-", "deb changelog")
    readme = read("README.md")
    hits = len(re.findall(re.escape("v" + want), readme))
    if hits < 8:
        errors.append(f"README: 仅 {hits} 处 v{want}（期望 ≥8）")

    if errors:
        print("✗ 版本不一致：")
        for e in errors:
            print("   -", e)
        return 1
    print(f"✓ 版本一致性通过：全部 = {want}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
