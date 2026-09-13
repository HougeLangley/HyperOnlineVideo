cmake_minimum_required(VERSION 3.20)
project(HyperOnlineVideo VERSION 1.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Network DBus)
# 登录窗口用 QtWebEngine（可选：Flatpak 的 KDE 运行时不含它，缺了也能正常构建运行）
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
if(Qt6WebEngineWidgets_FOUND)
  message(STATUS "QtWebEngine 找到 → 启用 App 内登录窗口")
  add_compile_definitions(HOV_WEBENGINE=1)
else()
  message(STATUS "QtWebEngine 未找到 → 登录窗口降级为提示（可手动放置 cookie 文件）")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
add_executable(hov-qt src/main.cpp src/MpvWidget.cpp src/MpvWidget.h
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
                         src/LoginDialog.cpp src/LoginDialog.h)
target_link_libraries(hov-qt PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network Qt6::DBus PkgConfig::MPV)
if(Qt6WebEngineWidgets_FOUND)
  target_link_libraries(hov-qt PRIVATE Qt6::WebEngineWidgets)
endif()
install(TARGETS hov-qt RUNTIME DESTINATION bin)
install(FILES packaging/hov-qt.desktop DESTINATION share/applications)
install(FILES packaging/hov-qt.png DESTINATION share/icons/hicolor/256x256/apps RENAME hov-qt.png)
install(FILES packaging/hov-qt-512.png DESTINATION share/icons/hicolor/512x512/apps RENAME hov-qt.png)
