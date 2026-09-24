{
  description = "Hyper Online Video —— 聚合视频（YouTube / 哔哩哔哩 / 网易云音乐 / QQ音乐，Qt6 桌面端）";

  # ── 输入 ─────────────────────────────────────────────────────────────────────
  # 只依赖 nixpkgs ✓（不用 flake-utils 等外部辅助 ✗ 减少拉取面 ✓）
  # ⚠️ 集成建议（重要 ✓）：在你的 NixOS flake 里加 `inputs.hov-qt.inputs.nixpkgs.follows = "nixpkgs";`
  #    这样只用你已有的一份 nixpkgs（本机不必再拉一份 ✗ 下载与磁盘都省 ✓）
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      # 本项目为 Linux 桌面端：x86_64 与 aarch64 两平台 ✓
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));
    in
    {
      packages = forAllSystems (pkgs:
        let
          hov-qt = pkgs.stdenv.mkDerivation (finalAttrs: {
            pname = "hov-qt";
            version = "1.2.1";

            # 源码 = 本 flake 仓库自身 ✓
            # 过滤掉一切构建产物/缓存（app/build 200MB+ ✗ macos/.build ✗ .gradle ✗ …）
            src = pkgs.lib.cleanSourceWith {
              src = self;
              filter = path: type:
                let
                  base = baseNameOf path;
                in
                !(builtins.elem base [
                  ".git"
                  "build"
                  ".build"
                  ".gradle"
                  "__pycache__"
                  "node_modules"
                  ".direnv"
                  "result"
                ]) && !(pkgs.lib.hasSuffix ".app" base);
            };

            # 桌面端 CMakeLists 在 desktop/ 子目录（单仓库多平台 ✓）
            # flake 自引用时 nix 把源码放在名为 source 的目录 → sourceRoot = source/desktop ✓
            sourceRoot = "source/desktop";

            nativeBuildInputs = with pkgs; [
              cmake
              ninja
              pkg-config
              qt6.wrapQtAppsHook
            ];

            buildInputs = with pkgs; [
              qt6.qtbase
              qt6.qtwebengine       # App 内登录窗口（与 Arch/Fedora/openSUSE/deb 渠道一致 ✓）
              qt6.qtwayland         # Wayland 会话支持（Hyprland/KDE 等 ✓）
              qt6.qtsvg             # Qt 图标引擎（hicolor 图标显示 ✓）
              kdePackages.kwindowsystem  # 可选：KDE 合成器模糊（CMake QUIET 查找 ✓ 找不到自动降级 ✓）
              mpv                   # 提供 libmpv + 头文件（播放引擎 ✓）
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
              "-DHOV_WEBENGINE=ON"
            ];

            # 运行时工具注入（应用通过 PATH 调用 yt-dlp/ffmpeg ✓）
            # 链路：hov-qt（启动器脚本，设 LC_NUMERIC=C 等）→ exec hov-qt-bin（被 wrapQtAppsHook 包装 ✓）
            #      → wrapper 注入 PATH → 应用内 QProcess 能找到 yt-dlp/ffmpeg ✓
            # ⚠️ 不要试图 wrap 启动器 ✗：wrapQtAppsHook 只处理 ELF ✓ 而启动器是脚本 ✓
            #    （但其 exec 目标 hov-qt-bin 被 wrap ✓ 链路成立 ✓）
            qtWrapperArgs = [
              "--prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.yt-dlp pkgs.ffmpeg ]}"
            ];

            # CMakeLists 已自动安装：hov-qt-bin（真二进制）、hov-qt（启动器）、.desktop、两档图标 ✓
            # 这里补：LICENSE 与 man 手册（对齐 deb/rpm 渠道的内容 ✓）
            # ⚠️ 用 ${self}（flake 顶层 store 路径 ✓）而非相对路径 ../ ✗ ——
            #   实测 ../LICENSE 在 stdenv 的目录型 src 下解析不到（避坑 #258 ✓）
            postInstall = ''
              install -Dm644 ${self}/LICENSE $out/share/licenses/hov-qt/LICENSE
              install -Dm644 ${self}/desktop/packaging/man/hov-qt.1 $out/share/man/man1/hov-qt.1
              ln -s hov-qt.1 $out/share/man/man1/hov-qt-bin.1
            '';

            meta = with pkgs.lib; {
              description = "Aggregated YouTube/Bilibili/NetEase/QQ Music client (Qt6 desktop)";
              homepage = "https://github.com/HougeLangley/HyperOnlineVideo";
              license = licenses.gpl3Only;   # 与仓库 LICENSE 及全渠道声明一致 ✓
              mainProgram = "hov-qt";
              platforms = platforms.linux;
            };
          });
        in
        {
          inherit hov-qt;
          default = hov-qt;
        });
    };
}
