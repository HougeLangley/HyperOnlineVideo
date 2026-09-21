cmake_minimum_required(VERSION 3.20)
project(HyperOnlineVideo VERSION 1.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Network DBus)
# 登录窗口用 QtWebEngine（可选：Flatpak 的 KDE 运行时不含它，缺了也能正常构建运行）
# ⚠ 默认**关闭**：QtWebEngine 内部是 Chromium，在 **非 C 的数值 locale**（如中文系统 LC_NUMERIC=zh_CN.UTF-8）
# 下会在**库初始化阶段直接段错误**（rc=139），连 --show-settings 这种无界面路径都崩（Arch 端实测复现）。
# Chromium 自己的提示是 setlocale(LC_NUMERIC,"C")，但它的初始化早于本程序 main()，程序侧改不了，
# 所以最稳妥的做法是**默认不链接**它：登录窗口降级为"手动放置 cookie 文件"（与 AppImage 一致）。
# 需要 App 内登录窗口时显式打开：-DHOV_WEBENGINE=ON
# 默认**开启**（恢复 App 内登录窗口）。曾因误判 locale 崩溃来自 Chromium 而默认关闭，
# 后来定位到真凶是 **libmpv**（要求 LC_NUMERIC=C，见 packaging/hov-qt-launcher.sh.in），
# 行内已由启动器在 exec 前修好 → WebEngine 无 locale 风险，恢复内置登录。
# 需要瘦身/无 WebEngine 环境（AppImage、Flatpak）时：-DHOV_WEBENGINE=OFF
option(HOV_WEBENGINE "启用 QtWebEngine（App 内登录窗口）" ON)
# ⚠ Qt6 的 find_package 会**记住请求过的组件**：若把它放在主 find_package(Qt6 ... Widgets ...)
# 之前，第二次调用会因"缺少 Widgets"而直接失败（实测报错 CMake Error at CMakeLists.txt:5）
# → 必须放在主调用**之后**。
if(HOV_WEBENGINE)
  find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
endif()

if(Qt6WebEngineWidgets_FOUND)
  message(STATUS "QtWebEngine 找到 → 启用 App 内登录窗口")
  add_compile_definitions(HOV_WEBENGINE=1)
else()
  message(STATUS "QtWebEngine 未启用 → 登录窗口降级为提示（可手动放置 cookie 文件；需内嵌登录用 -DHOV_WEBENGINE=ON）")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
add_executable(hov-qt src/main.cpp src/MpvWidget.cpp
    src/ColorTheme.cpp
    src/ThemeWatcher.cpp src/MpvWidget.h
                         src/MasonryView.cpp src/MasonryView.h
                         src/NetPolicy.cpp src/NetPolicy.h
                         src/Playlist.cpp src/Playlist.h
                         src/UrlResolver.cpp src/UrlResolver.h
                         src/Subtitles.cpp src/Subtitles.h
                         src/SubtitleOverlay.cpp src/SubtitleOverlay.h
                         src/NetEaseApi.cpp src/NetEaseApi.h
                         src/QQMusicApi.cpp src/QQMusicApi.h
                         src/PlayQueue.cpp src/PlayQueue.h
                         src/Favorites.cpp src/Favorites.h
                         src/DownloadManager.cpp src/DownloadManager.h
                         src/Settings.cpp src/Settings.h
                         src/Theme.cpp src/Theme.h
                         src/ProgressStore.cpp src/ProgressStore.h
                         src/Mpris.cpp src/Mpris.h
                         src/CoverArt.cpp src/CoverArt.h
                         src/LocalLibrary.cpp src/LocalLibrary.h
                         src/CookieImport.cpp src/CookieImport.h
                         src/LoginDialog.cpp src/LoginDialog.h)
target_link_libraries(hov-qt PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network Qt6::DBus PkgConfig::MPV)
# 编译期体检：-Wall -Wextra 能挡住「无返回值 / 未初始化 / 未使用」这一类隐性问题
# （本项目曾因误删 `return d;` 造成未定义行为 → 主线程自旋，见踩坑百科 #106）
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hov-qt PRIVATE -Wall -Wextra)
endif()

# ── 发布级优化：clang + full-LTO + PGO ─────────────────────────────────────────
# 用法（opt 构建）：
#   1) 采样：  cmake -B build-pgo -G Ninja -DCMAKE_BUILD_TYPE=Release -DHOV_PGO=generate
#              cmake --build build-pgo && ./build-pgo/hov-qt --selftest ...（跑训练剧本）
#              llvm-profdata merge -output=hov.profdata default.profraw*
#   2) 优化：  cmake -B build-opt -G Ninja -DCMAKE_BUILD_TYPE=Release -DHOV_LTO=ON -DHOV_PGO=use
# 说明：PGO 只影响 opt 产物；常规构建（AppImage/Flatpak/PKGBUILD）默认不开启，保证可复现。
option(HOV_LTO "启用 full LTO（需 clang + lld）" OFF)
set(HOV_PGO "off" CACHE STRING "PGO 模式：off | generate | use")
set(HOV_PROFDATA "${CMAKE_BINARY_DIR}/hov.profdata" CACHE FILEPATH "PGO 样本文件")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(HOV_LTO)
        # full LTO：跨模块全局优化（内存吃紧时可换 thin）
        target_compile_options(hov-qt PRIVATE -flto=full)
        target_link_options(hov-qt PRIVATE -flto=full -fuse-ld=lld)
        message(STATUS "已启用 full LTO（链接器 lld）")
    endif()
    if(HOV_PGO STREQUAL "generate")
        target_compile_options(hov-qt PRIVATE -fprofile-instr-generate)
        target_link_options(hov-qt PRIVATE -fprofile-instr-generate)
        message(STATUS "PGO：采样构建（-fprofile-instr-generate）")
    elseif(HOV_PGO STREQUAL "use")
        target_compile_options(hov-qt PRIVATE "-fprofile-instr-use=${HOV_PROFDATA}" -Wno-profile-instr-unprofiled)
        target_link_options(hov-qt PRIVATE "-fprofile-instr-use=${HOV_PROFDATA}")
        message(STATUS "PGO：优化构建（-fprofile-instr-use=${HOV_PROFDATA}）")
    endif()
else()
    if(HOV_LTO OR NOT HOV_PGO STREQUAL "off")
        message(WARNING "HOV_LTO/HOV_PGO 仅支持 clang（当前 ${CMAKE_CXX_COMPILER_ID}）—— 已忽略")
    endif()
endif()
if(Qt6WebEngineWidgets_FOUND)
  target_link_libraries(hov-qt PRIVATE Qt6::WebEngineWidgets)
endif()
# 真实二进制装成 hov-qt-bin；对外暴露的同名命令是下面的启动器（见该文件头注释：
# libmpv 要求 LC_NUMERIC=C，否则在中文等非 C 数值 locale 下**加载阶段**即段错误）
# ── 安装布局（重要：libmpv 的 locale 崩溃修复）────────────────────────────────
#   /usr/bin/hov-qt       → 启动器脚本（先设 LC_NUMERIC=C 再 exec）
#   /usr/bin/hov-qt-bin   → 真实二进制
# 为什么：libmpv 要求 LC_NUMERIC=C，否则在中文等非 C 数值 locale 下**动态库加载阶段**
# 就段错误（rc=139，连 --show-settings 都崩）。该检查早于 main()，进程内改 locale 无效。
# 注：install(TARGETS) 不支持 RENAME（曾因此配置失败又被 /dev/null 吞掉），故用生成器表达式。
configure_file(packaging/hov-qt-launcher.sh.in ${CMAKE_CURRENT_BINARY_DIR}/hov-qt-launcher @ONLY)
install(PROGRAMS $<TARGET_FILE:hov-qt> DESTINATION bin RENAME hov-qt-bin)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/hov-qt-launcher DESTINATION bin RENAME hov-qt)
# ── 玻璃质感：可选依赖 KWindowSystem（KDE 模糊协议）──────────────────────────
# KWin 的 Wayland 客户端模糊（org_kde_kwin_blur）与 X11 的 _KDE_NET_WM_BLUR_BEHIND_REGION
# 都由 KWindowEffects 转发。**可选**：找不到就用自绘降级（不新增硬依赖，非 KDE 环境照常工作）。
find_package(KF6WindowSystem QUIET)
if(KF6WindowSystem_FOUND)
    target_link_libraries(hov-qt PRIVATE KF6::WindowSystem)
    target_compile_definitions(hov-qt PRIVATE HOV_HAVE_KWINDOWSYSTEM=1)
    message(STATUS "玻璃：已启用 KDE 合成器模糊（KWindowSystem）")
else()
    message(STATUS "玻璃：未找到 KWindowSystem —— 仅自绘降级（功能不受影响）")
endif()

install(FILES packaging/hov-qt.desktop DESTINATION share/applications)
install(FILES packaging/hov-qt.png DESTINATION share/icons/hicolor/256x256/apps RENAME hov-qt.png)
install(FILES packaging/hov-qt-512.png DESTINATION share/icons/hicolor/512x512/apps RENAME hov-qt.png)
