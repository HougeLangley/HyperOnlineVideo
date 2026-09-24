#include <QApplication>
#include <QDesktopServices>
#include <QMenu>
#include <QUrl>
#include <iostream>
#include <QMainWindow>
#include <QLineEdit>
#include <QListWidget>
#include <QFontMetrics>
#include <QPainterPath>
#include <QPainter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QPushButton>
#include <QFileDialog>
#include <QSplitter>
#include <QVBoxLayout>
#include <QLabel>
#include <QProcess>
#include <memory>
#include <QFileInfo>
#include <QSlider>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QEvent>
#include <QTimer>
#include <QThread>   // 自检轮询等待快照刷新（QThread::msleep）
#include <QDir>
#include <QHash>
#include <QtMath>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "SelfTest.h"
#include "SettingsDialog.h"
#include "MasonryView.h"
#include <cmath>
#include <limits>
#include <QScrollBar>
#include "MpvWidget.h"
#include "ColorTheme.h"
#include "ThemeWatcher.h"
#ifdef HOV_HAVE_KWINDOWSYSTEM
#include <KWindowEffects>
#endif
#include "NetPolicy.h"
#include "Playlist.h"
#include "NetEaseApi.h"
#include "DownloadManager.h"
#include "Favorites.h"
#include "PlayQueue.h"
#include "QQMusicApi.h"
#include "LoginDialog.h"
#include "Settings.h"
#include "Theme.h"
#include "ProgressStore.h"
#include "Mpris.h"
#include "LocalLibrary.h"
#include "CookieImport.h"
#include <QInputDialog>
#include <QDateTime>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include "UrlResolver.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

// 《聚合视频》Linux 桌面版 MVP
// 搜索走 yt-dlp（与 Android 版同款引擎）；后续 P2 后半段改为调用 KMP 共享核心
// ── 结果卡片绘制（对齐 macOS 的 ResultGrid）───────────────────────────────
// 为什么**必须自绘**：QListWidget 的 IconMode + 全局深色样式表组合下，默认 delegate 会
// 把 item 文字压没、缩略图还被裁成半截（用户对比截图实测：只有半截图、没有标题/来源）。
// 自绘后完全可控：圆角缩略图 + 标题 + 来源标签，悬停/选中状态也自己做。

#include <algorithm>
#include "MainWindow.h"


// 数值 locale 必须是 C：Qt/Chromium/mpv/ffmpeg 内部都按 C 解析小数点。
// 中文系统常见 LC_NUMERIC=zh_CN.UTF-8 —— 某些库（尤其 QtWebEngine 的 Chromium）会在**初始化阶段**
// 直接段错误。这里用 init-priority 构造函数尽量早地设定（比 main() 更早），main() 内再兜一次。
#include <clocale>
#include <unistd.h>   // execv（locale 自重执行修复）
#include <cstdlib>
#include <vector>
#include <cstring>
__attribute__((constructor(101)))
static void hov_force_c_numeric_locale() {
    std::setlocale(LC_NUMERIC, "C");

    // 与启动器同源的双保险：禁 GLib 事件分发器（Qt 6.10+ 的该回归会在线程里 glib abort）
    qputenv("QT_NO_GLIB", "1");
}

#ifdef __linux__
// HOV_QPA=<平台>：手动指定 Qt 平台插件（排障旁路，如 HOV_QPA=xcb 强制走 XWayland）。
// 正常不需要：Wayland 下 libmpv 逐帧渲染的 sync_file fence fd 泄漏（virgl 驱动每帧一个，
// ≈视频帧率 +30~40/s，数小时打满 fd 上限 → 事件分发器建管道失败 abort）已由
// MpvWidget 的 glFenceSync 记账 shim 根治（见 MpvWidget.h“glFenceSync 泄漏防护”注释，
// 上游 mpv f74adc4 亦已修、将随 0.42 发布）。原生 Wayland 可放心使用。
#endif

int main(int argc, char **argv) {
#ifdef __linux__
    // ── libmpv 的数值 locale 崩溃修复（Arch 端实测复现）────────────────────────────
    // 现象：系统 LC_NUMERIC 非 C（如中文环境 zh_CN.UTF-8）时，**任何**子命令都直接段错误（rc=139），
    //       连 --show-settings 也一样；报错为 libmpv 自带的
    //       "Non-C locale detected. This is not supported. Call 'setlocale(LC_NUMERIC, \"C\");'"
    // 原因：libmpv 在**初始化阶段**就检查数值 locale，此时进程内的 setlocale 已经来不及
    //       （构造函数/达 main 都太晚，实测均无效）→ 只能用正确的环境**重新 exec 自己**。
    // 处理：仅把 LC_NUMERIC 设为 C（其它类别不动 → 界面语言照旧），并加标记位防止无限递归。
    // ⚠ 判定必须看**环境变量**而不是 setlocale 当前值：进程启动时 C 库的 locale
    // 恒为 "C"（环境还没 apply），用 setlocale(LC_NUMERIC, nullptr) 判断永远为假 ——
    // 旧写法因此从不 re-exec，而 QApplication 构造时会 setlocale(LC_ALL, "") 把
    // LC_NUMERIC 重新拉回环境值（如 zh_CN.UTF-8）→ libmpv 初始化直接 abort
    //（本轮实测：LC_NUMERIC=en_US.UTF-8 直启 hov-qt 必 core dump）。
    // 按 POSIX 优先级取生效值：LC_ALL > LC_NUMERIC > LANG。
    {
        auto effectiveNumeric = []() -> const char * {
            if (const char *v = std::getenv("LC_ALL"); v && *v) return v;
            if (const char *v = std::getenv("LC_NUMERIC"); v && *v) return v;
            if (const char *v = std::getenv("LANG"); v && *v) return v;
            return "C";
        };
        const char *cur = effectiveNumeric();
        const bool isC = std::strcmp(cur, "C") == 0 || std::strcmp(cur, "POSIX") == 0;
        if (!isC && std::getenv("HOV_LOCALE_REEXEC") == nullptr) {
            // LC_ALL 优先级高于 LC_NUMERIC：脏的话必须先清掉（置空=按 unset 处理），
            // 否则只设 LC_NUMERIC 不生效（启动器同款处理，见 hov-qt-launcher.sh.in）。
            if (const char *v = std::getenv("LC_ALL"); v && *v) setenv("LC_ALL", "", 1);
            setenv("LC_NUMERIC", "C", 1);
            setenv("HOV_LOCALE_REEXEC", "1", 1);
            std::vector<char *> av;
            av.reserve(static_cast<size_t>(argc) + 1);
            for (int i = 0; i < argc; ++i) av.push_back(argv[i]);
            av.push_back(nullptr);
            execv("/proc/self/exe", av.data());      // 失败则继续跑（保持原行为，不会更糟）
        }
    }
#endif

#ifdef __linux__
    // HOV_QPA=<平台>：手动指定 Qt 平台插件（见上方注释）；用户显式设了 QT_QPA_PLATFORM 时尊重用户。
    if (std::getenv("QT_QPA_PLATFORM") == nullptr) {
        if (const char *q = std::getenv("HOV_QPA"); q && *q) qputenv("QT_QPA_PLATFORM", q);
    }
#endif

    // 保留用户环境的语言（LC_CTYPE 等），只把**数值**格式统一成 C（见上方构造函数注释）
    std::setlocale(LC_ALL, "");
    std::setlocale(LC_NUMERIC, "C");

    // 纯命令行维护动作（导入 cookie / 清理下载 / 进度查询 / 设置读写）不需要图形界面。
    // 在服务器、SSH、CI 等没有显示环境的地方，Qt 会因为连不上显示而直接 abort；
    // 这里提前把这类调用切到 offscreen 平台，让脚本/产物包也能用（用户桌面上仍用真实平台）。
    {
        static const QStringList kHeadlessCmds{
            "--import-cookies", "--cleanup-downloads", "--progress-show", "--progress-clear",
            "--show-settings", "--set", "--queue-selftest"};
        bool headlessCmd = false;
        for (int i = 1; i < argc; ++i)
            if (kHeadlessCmds.contains(QString::fromLocal8Bit(argv[i]))) { headlessCmd = true; break; }
        const bool hasDisplay = qEnvironmentVariableIsSet("WAYLAND_DISPLAY")
                                || qEnvironmentVariableIsSet("DISPLAY");
        if (headlessCmd && !hasDisplay && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    Theme::apply(app);   // 统一深色主题（主窗口/对话框/播放器同一套观感）
    MainWindow w;
    w.show();
    // 自动化验证：--demo "<关键词>" 启动即搜索（供无人值守截图/回归用）
    const auto args = app.arguments();
    const int di = args.indexOf("--demo");
    if (args.contains("--autoplay")) w.setAutoplay(true);
    const int si = args.indexOf("--speed");
    if (si > 0 && si + 1 < args.size()) {
        bool ok = false;
        const double sp = args[si + 1].toDouble(&ok);
        if (ok && sp >= 0.25 && sp <= 4.0)
            QMetaObject::invokeMethod(&w, [&w, sp] { w.applySpeed(sp); }, Qt::QueuedConnection);
    }
    const int oi = args.indexOf("--open");
    if (oi > 0 && oi + 1 < args.size()) {
        QStringList files;
        for (int i = oi + 1; i < args.size(); ++i) {
            if (args[i].startsWith("--")) break;
            files << args[i];
        }
        QMetaObject::invokeMethod(&w, [&w, files] { w.openFiles(files); }, Qt::QueuedConnection);
    }
    // 自动化参数：--sub-track N（字幕轨序号，-1=关闭）/ --sub-delay SEC（字幕延迟）
    // 说明：无头 VM 里键盘注入不可靠，故提供与 --speed 同风格的命令行入口
    {
        const int ai = args.indexOf("--sub-track");
        if (ai > 0 && ai + 1 < args.size()) {
            bool ok = false;
            const int n = args[ai + 1].toInt(&ok);
            if (ok) QMetaObject::invokeMethod(&w, [&w, n] { w.applySubtitleTrackIndex(n); }, Qt::QueuedConnection);
        }
        const int di = args.indexOf("--sub-delay");
        if (di > 0 && di + 1 < args.size()) {
            bool ok = false;
            const double v = args[di + 1].toDouble(&ok);
            if (ok) QMetaObject::invokeMethod(&w, [&w, v] { w.applySubtitleDelay(v); }, Qt::QueuedConnection);
        }
    }
    // 登录窗口：--login <站点>（youtube/bilibili/netease/qqmusic）
    {
        const int li = args.indexOf("--login");
        if (li > 0) {
            const QString site = (li + 1 < args.size() && !args[li + 1].startsWith("--"))
                                     ? args[li + 1] : QString("youtube");
            QMetaObject::invokeMethod(&w, [&w, site] { w.openLoginDialog(site); }, Qt::QueuedConnection);
        }
    }

    // 设置窗口：--settings 打开（人工验证用）
    if (args.contains("--settings"))
        QMetaObject::invokeMethod(&w, [&w] { w.openSettingsDialog(); }, Qt::QueuedConnection);
    // UI-4-B ✓：--filters 打开搜索过滤器面板（与 macOS Automation.swift:322 同名同语义 ✓ 供自动化取证）
    if (args.contains("--filters"))
        QMetaObject::invokeMethod(&w, [&w] { w.openFilterDialog(); }, Qt::QueuedConnection);

    // 进度记忆：--progress-show（列出）/ --progress-clear（清空）—— 纯查询/维护，完事就退出
    // 下载目录 LRU 清理：--cleanup-downloads（纯维护操作，执行完退出）
    // A0：--import-cookies <浏览器> —— 与 macOS 端同名同语义；产物包（无 WebEngine）的登录路径
    if (const int ci = args.indexOf("--import-cookies"); ci > 0) {
        const QString browser = (ci + 1 < args.size() && !args[ci + 1].startsWith("--"))
                                    ? args[ci + 1] : QString("firefox");
        const QString msg = w.importCookiesFromBrowser(browser);
        return msg.startsWith("cookie 导入完成") ? 0 : 1;
    }
    if (args.contains("--cleanup-downloads")) {
        const int mb = args.indexOf("--max-total-mb");
        const qint64 limit = ((mb > 0 && mb + 1 < args.size()) ? args[mb + 1].toLongLong()
                                                               : static_cast<qint64>(w.downloadMaxTotalMb()))
                             * 1024 * 1024;
        const auto r = w.cleanupDownloads(limit);
        std::cout << "---- 下载目录 LRU 清理 ----\n"
                  << "清理前: " << r.beforeBytes / 1024 / 1024 << " MB\n"
                  << "清理后: " << r.afterBytes / 1024 / 1024 << " MB（上限 " << limit / 1024 / 1024 << " MB）\n"
                  << "删除 " << r.removed.size() << " 个文件\n";
        for (const QString &p : r.removed) std::cout << "  - " << p.toStdString() << std::endl;
        return 0;
    }
    if (args.contains("--progress-show") || args.contains("--progress-clear")) {
        if (args.contains("--progress-clear")) {
            w.clearProgress();
            std::cout << "(进度记录已清空: " << ProgressStore::filePath().toStdString() << ")" << std::endl;
        } else {
            std::cout << w.dumpProgress().toStdString();
        }
        return 0;
    }
    if (args.contains("--no-resume")) w.setResumeEnabled(false);   // 自动化测试用：不做续播
    if (args.contains("--local")) {                                 // 打开本地库（下载目录）
        QMetaObject::invokeMethod(&w, [&w] { w.applySource(3); w.showLocalLibrary(); }, Qt::QueuedConnection);
    }
    // 本地库重命名（自动化：--rename-selected <新名>，与界面 R 键同一条逻辑）
    if (const int ri = args.indexOf("--rename-selected"); ri > 0 && ri + 1 < args.size()) {
        const QString nm = args[ri + 1];
        qInfo() << "[CLI] --rename-selected" << nm << "（排队执行）";
        QMetaObject::invokeMethod(&w, [&w, nm] {
            qInfo() << "[CLI] 执行重命名";
            w.renameSelectedLocal(nm);
        }, Qt::QueuedConnection);
    }
    // 音质：--quality <standard|exhigh|lossless> 立即设档位（起播就用它）；
    //       --cycle-quality-after <秒> 播放 N 秒后执行一次「Q 键的循环切档」——
    //       与用户按 Q 完全同一条代码路径，便于自动化验证"播放中切音质"
    const int qi = args.indexOf("--quality");
    const QString qWant = (qi > 0 && qi + 1 < args.size()) ? args[qi + 1] : QString();
    qInfo() << "[CLI] --quality 参数=" << qWant << " （空=未指定）";
    if (!qWant.isEmpty())
        QMetaObject::invokeMethod(&w, [&w, qWant] { w.switchQualityTo(qWant); }, Qt::QueuedConnection);
    if (const int si = args.indexOf("--cycle-quality-after"); si > 0 && si + 1 < args.size()) {
        bool ok = false;
        const int secs = args[si + 1].toInt(&ok);
        if (ok && secs > 0) QTimer::singleShot(secs * 1000, &w, [&w] { w.cycleQuality(); });
    }
    // 在线字幕直取：--subs-url <地址>（srt/vtt/json，B站 CC JSON 也能直接给）
    if (const int si = args.indexOf("--subs-url"); si > 0 && si + 1 < args.size()) {
        const QString u = args[si + 1];
        QMetaObject::invokeMethod(&w, [&w, u] { w.applySubsUrl(u); }, Qt::QueuedConnection);
    }
    // 退出前把进度落盘（关窗口 / aboutToQuit 都会走到）
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &w, [&w] { w.saveProgressNow(); });

    // 设置：--set key=value（可重复）/ --show-settings
    {
        const QStringList all = args;
        bool anySet = false;
        for (int i = 1; i + 1 < all.size(); ++i) {
            if (all[i] == "--set") {
                const QString kv = all[i + 1];
                const int eq = kv.indexOf('=');
                if (eq > 0) {
                    const QString k = kv.left(eq), v = kv.mid(eq + 1);
                    const bool changed = w.applySetting(k, v);
                    const bool valid = Settings::validate(k, v).isEmpty();
                    std::cout << (valid ? (changed ? "[set]    " : "[nochange] ") : "[reject] ")
                              << k.toStdString() << " = " << v.toStdString() << std::endl;
                    anySet = true;
                }
                ++i;
            }
        }
        // 纯配置用法（只带 --set / --show-settings，没有别的动作参数）→ 执行完就退出，
        // 否则 GUI 应用会常驻，脚本里会一直挂着（实测踩过）。
        static const char *kActions[] = { "--open", "--music", "--qqmusic", "--download", "--download-music",
                                          "--demo", "--selftest", "--queue-selftest", "--settings",
                                          "--subs-url", nullptr };
        bool hasAction = false;
        for (int i = 0; kActions[i]; ++i)
            if (args.contains(kActions[i])) { hasAction = true; break; }
        const bool query = args.contains("--show-settings");
        if (query || (anySet && !hasAction)) {
            if (query)
                std::cout << "---- settings (" << Settings::filePath().toStdString() << ") ----\n"
                          << w.dumpSettings().toStdString() << std::endl;
            else
                std::cout << "(设置已写入 " << Settings::filePath().toStdString() << "；重启后完全生效)" << std::endl;
            return 0;
        }
    }

    // --exit-after <秒>：自动化用，N 秒后自动退出（避免无头环境下进程常驻）
    if (const int ei = args.indexOf("--exit-after"); ei > 0 && ei + 1 < args.size()) {
        bool ok = false;
        const int secs = args[ei + 1].toInt(&ok);
        if (ok && secs > 0)
            QTimer::singleShot(secs * 1000, &app, &QCoreApplication::quit);
    }

    // 下载：--download <URL> [文件名] / --download-music <关键词>
    {
        const int di = args.indexOf("--download");
        if (di > 0 && di + 1 < args.size()) {
            const QString url = args[di + 1];
            const QString nm = (di + 2 < args.size() && !args[di + 2].startsWith("--")) ? args[di + 2] : QString();
            QMetaObject::invokeMethod(&w, [&w, url, nm] { w.downloadUrl(url, nm); }, Qt::QueuedConnection);
        }
          int count = 1;
        const int ci = args.indexOf("--download-count");
        if (ci > 0 && ci + 1 < args.size()) {
            bool ok = false; const int n = args[ci + 1].toInt(&ok);
            if (ok && n > 0 && n <= 20) count = n;
        }
        const int dmi = args.indexOf("--download-music");
        if (dmi > 0 && dmi + 1 < args.size())
            QMetaObject::invokeMethod(&w, [&w, kw = args[dmi + 1], count] { w.downloadMusic(kw, count); }, Qt::QueuedConnection);
    }

    // 自检：--queue-selftest 跑播放队列与收藏的逻辑断言
    if (args.contains("--queue-selftest"))
        QMetaObject::invokeMethod(&w, [&w] { w.runQueueSelfTest(); }, Qt::QueuedConnection);

    // 界面：--source N 预选搜索来源（0=YouTube 1=网易云 2=QQ音乐；无头环境无法点击下拉框）
    {
        const int si2 = args.indexOf("--source");
        if (si2 > 0 && si2 + 1 < args.size()) {
            bool ok = false;
            const int n = args[si2 + 1].toInt(&ok);
            if (ok) QMetaObject::invokeMethod(&w, [&w, n] { w.applySource(n); }, Qt::QueuedConnection);
        }
    }
    // 音乐：--music <关键词> → 网易云搜索并播放第一条
    {
        // B1：--video-quality <auto|audio|360|480|720|1080…> 与 --cycle-video-quality-after <秒>
    if (const int vi = args.indexOf("--video-quality"); vi > 0 && vi + 1 < args.size()) {
        const QString v = args[vi + 1].toLower();
        int h = 0;
        if (v == "auto") h = 0;
        else if (v == "audio" || v == "audio-only") h = -1;
        else h = v.split('p').first().toInt();
        // 必须**同步**应用：--open 的解析是队列调用，排队的话档位会晚于解析生效（实测踩过）
        w.switchVideoQuality(h);
    }
    if (const int ci2 = args.indexOf("--cycle-video-quality-after"); ci2 > 0 && ci2 + 1 < args.size()) {
        bool ok = false; const int secs = args[ci2 + 1].toInt(&ok);
        if (ok && secs > 0) QTimer::singleShot(secs * 1000, &w, [&w] { w.cycleVideoQuality(); });
    }
    // --fill-screen / --no-fill-screen / --fullscreen / --fullscreen-after <秒>（B7，自动化验证用）
    // 注意：这些动作要等窗口/mpv 就绪后再做（早期同步调用会与 mpv 初始化竞争，实测会挂住）
    if (app.arguments().contains("--fill-screen")) QTimer::singleShot(900, &w, [&w] { w.setFillScreenPublic(true); });
    if (app.arguments().contains("--no-fill-screen")) QTimer::singleShot(900, &w, [&w] { w.setFillScreenPublic(false); });
    if (app.arguments().contains("--fullscreen")) QTimer::singleShot(1500, &w, [&w] {
        qInfo().noquote() << "[CLI] 进入全屏（--fullscreen 触发）"; w.showFullScreenPublic(); });
    if (const int fi = args.indexOf("--fullscreen-after"); fi > 0 && fi + 1 < args.size()) {
        bool ok = false; const int secs = args[fi + 1].toInt(&ok);
        if (ok && secs > 0) QTimer::singleShot(secs * 1000, &w, [&w] {
            qInfo().noquote() << "[CLI] 进入全屏（--fullscreen-after 触发）"; w.showFullScreenPublic(); });
    }
    // --pip-after <秒>：N 秒后进入画中画（自动化验证用）
    if (const int pi = args.indexOf("--pip-after"); pi > 0 && pi + 1 < args.size()) {
        // 支持**逗号多时刻** ✓：`--pip-after 8,20` = 8s 进 PiP、20s 退出 ✓
        //   （"PiP 往返后左侧列表宽度变化"这类问题必须能一次跑完往返才能取证 ✓ 单时刻做不到 ✗）
        const QStringList parts = args[pi + 1].split(',', Qt::SkipEmptyParts);
        for (const QString &p : parts) {
            bool ok = false; const int secs = p.trimmed().toInt(&ok);
            if (ok && secs > 0) QTimer::singleShot(secs * 1000, &w, [&w] { w.togglePiPPublic(); });
        }
    }
    // --min-probe <秒>：打印**窗口与关键子控件的最小尺寸**（定位"窗口被内容撑大"类问题 ✓）
    //   用户 2026-09-23 实测：播放开始后窗口从 1200x720 变 1332x775 ✗ 且高 DPI 下更明显 ✓
    //   → 怀疑"长文本标签把最小宽度撑大" ✗ 本探针直接量出来（不猜 ✓）
    if (const int mi = args.indexOf("--min-probe"); mi > 0 && mi + 1 < args.size()) {
        bool ok = false; const int secs = args[mi + 1].toInt(&ok);
        if (ok && secs > 0) QTimer::singleShot(secs * 1000, &w, [&w] {
            const QSize win = w.minimumSizeHint();
            std::fprintf(stderr, "[MINPROBE] 窗口 minimumSizeHint=%dx%d 当前=%dx%d 最小宽=%d\n",
                         win.width(), win.height(), w.width(), w.height(), w.minimumWidth());
            for (auto *l : w.findChildren<QLabel *>()) {
                const QSize sz = l->minimumSizeHint();
                if (sz.width() < 200) continue;                 // 只关心"宽的"（撑窗口的元凶 ✓）
                std::fprintf(stderr, "[MINPROBE]   QLabel 最小=%dx%d 可见=%d 文本(%d 字)=%s\n",
                             sz.width(), sz.height(), int(l->isVisible()), int(l->text().size()),
                             l->text().left(44).toUtf8().constData());
            }
            for (auto *c : w.findChildren<QComboBox *>()) {
                std::fprintf(stderr, "[MINPROBE]   QComboBox 最小=%dx%d 可见=%d\n",
                             c->minimumSizeHint().width(), c->minimumSizeHint().height(), int(c->isVisible()));
            }
            // 任意类型的可见控件，谁的最小宽大就点名谁 ✓（找"窗口被夹住"的元凶 ✓ 不限 QLabel/QComboBox）
            {
                QVector<QPair<int, QString>> wide;
                for (auto *c : w.findChildren<QWidget *>()) {
                    if (!c->isVisible()) continue;
                    const int mw = c->minimumSizeHint().width();
                    if (mw >= 250) wide.append({ mw, QStringLiteral("%1(%2)").arg(QString::fromLatin1(c->metaObject()->className()), c->objectName()) });
                }
                std::sort(wide.begin(), wide.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
                for (int i = 0; i < qMin(6, wide.size()); ++i)
                    std::fprintf(stderr, "[MINPROBE]   宽控件 #%d 最小宽=%d %s\n", i + 1, wide[i].first,
                                 wide[i].second.toUtf8().constData());
            }
            // ── 决定性 A/B ✓：给状态行塞**超长文本**（模拟用户那条长状态行 ✗）→ 看窗口最小宽是否被撑 ✓
            if (auto *sl = w.findChild<QLabel *>("hovStatus")) {
                const QString keep = sl->text();
                const QSize before = w.minimumSizeHint();
                sl->setText(QString(200, QChar(0x5b57)));       // 200 个汉字 ✓
                w.layout()->activate();
                const QSize after = w.minimumSizeHint();
                sl->setText(keep);
                w.layout()->activate();
                std::fprintf(stderr, "[MINPROBE] 长文本 A/B：塞前 %dx%d → 塞后 %dx%d %s\n",
                             before.width(), before.height(), after.width(), after.height(),
                             (after.width() == before.width() ? "✓ 不再撑窗口 ✓" : "✗ 仍被撑大 ✗"));
            }
        });
    }
    // --download-current <秒>：N 秒后点一次「下载当前」（与 macOS `--download-current` 同名同义 ✓）
    //   Bug3 验证：播放视频后触发下载 → 看队列/落盘/是否带音轨（ffmpeg 混流 ✓）
    if (const int dci = args.indexOf("--download-current"); dci > 0 && dci + 1 < args.size()) {
        bool ok = false; const int secs = args[dci + 1].toInt(&ok);
        if (ok && secs > 0) QTimer::singleShot(secs * 1000, &w, [&w] { w.downloadCurrentPublic(); });
    }
    // --yt <关键词>：按来源搜索（自动化；默认 YouTube）
    // CLI 探针：--fs-probe [标签]（验证"全屏浮出侧栏"的显隐状态机，可脚本化验收）
    if (const int fpi = args.indexOf("--fs-probe"); fpi > 0) {
        const QString tag = (fpi + 1 < args.size() && !args[fpi + 1].startsWith("--")) ? args[fpi + 1] : QString("default");
        QTimer::singleShot(12000, &w, [&w, tag] { w.fsProbePublic(tag); });   // 12s：留足搜索出卡片的时间
    }
    if (const int lmi = args.indexOf("--load-more"); lmi > 0 && lmi + 1 < args.size()) {
        const int n = qBound(1, args[lmi + 1].toInt(), 20);
        QMetaObject::invokeMethod(&w, [&w, n] { w.loadMoreForTest(n); }, Qt::QueuedConnection);
    }
    if (const int yi = args.indexOf("--yt"); yi > 0 && yi + 1 < args.size()) {
        QMetaObject::invokeMethod(&w, [&w, kw = args[yi + 1]] { w.searchPublic(kw, 0); }, Qt::QueuedConnection);
    }
    // --play-storm <次数> [间隔毫秒]：连播压测（复现"快速连点"场景，fd 泄漏排查用）
    if (const int psi = args.indexOf("--play-storm"); psi > 0 && psi + 1 < args.size()) {
        const int n = qBound(1, args[psi + 1].toInt(), 500);
        const int iv = (psi + 2 < args.size() && !args[psi + 2].startsWith("--"))
                           ? qMax(500, args[psi + 2].toInt()) : 3000;
        QMetaObject::invokeMethod(&w, [&w, n, iv] { w.playStormForTest(n, iv); }, Qt::QueuedConnection);
    }
    // CLI 探针：--win-probe-save 1200x800 / --win-probe-check 1200x800
    //   W1 ✓ 窗口几何记忆的**确定性**验收（不经过窗口管理器：实测 xvfb + xdotool 的关闭路径在无 WM 下不生效 ✗）
    //   ① save：程序化 resize → close()（走真实 closeEvent → 落盘）→ 退出
    //   ② check：启动即对比 restoreGeometry 结果 → 不符退出码 2（可脚本断言 ✓）
    if (const int wi = args.indexOf("--win-probe-save"); wi > 0 && wi + 1 < args.size()) {
        const QStringList wh = args[wi + 1].split('x');
        if (wh.size() == 2) {
            const int pw = wh[0].toInt(), ph = wh[1].toInt();
            QTimer::singleShot(2000, &w, [&w, pw, ph] {
                w.resize(pw, ph);
                std::fprintf(stderr, "[WINPROBE] 设定 %dx%d → 实际 %dx%d\n", pw, ph, w.width(), w.height());
            });
            QTimer::singleShot(3000, &w, [&w] {
                std::fprintf(stderr, "[WINPROBE] close() → 应触发 closeEvent 落盘\n");
                w.close();
            });
            QTimer::singleShot(5000, &app, [&app] {              // 兜底：close 若未结束进程也收工
                std::fprintf(stderr, "[WINPROBE] 兜底退出（close 未结束进程）\n");
                app.exit(0);
            });
        }
    }
    if (const int wi = args.indexOf("--win-probe-check"); wi > 0 && wi + 1 < args.size()) {
        const QStringList wh = args[wi + 1].split('x');
        if (wh.size() == 2) {
            const int pw = wh[0].toInt(), ph = wh[1].toInt();
            QTimer::singleShot(2000, &w, [&w, pw, ph, &app] {
                const bool ok = (w.width() == pw && w.height() == ph);
                std::fprintf(stderr, "[WINPROBE] 恢复核对：期望 %dx%d，实际 %dx%d @(%d,%d) → %s\n",
                             pw, ph, w.width(), w.height(), w.x(), w.y(), ok ? "一致 ✓" : "不一致 ✗");
                app.exit(ok ? 0 : 2);
            });
        }
    }
    const int bi = args.indexOf("--bili");
        if (bi > 0 && bi + 1 < args.size()) {
            QMetaObject::invokeMethod(&w, [&w, kw = args[bi + 1]] { w.applySource(1); w.biliSearchPublic(kw, true); },
                                      Qt::QueuedConnection);
        }
        const int mi = args.indexOf("--music");
        if (mi > 0 && mi + 1 < args.size())
            QMetaObject::invokeMethod(&w, [&w, kw = args[mi + 1]] { w.musicSearch(kw); }, Qt::QueuedConnection);
    }
    // 音乐：--qqmusic <关键词> → QQ音乐搜索并播放第一条
    {
        const int qi = args.indexOf("--qqmusic");
        if (qi > 0 && qi + 1 < args.size())
            QMetaObject::invokeMethod(&w, [&w, kw = args[qi + 1]] { w.qqMusicSearch(kw); }, Qt::QueuedConnection);
    }
    // W2 ✓ 面板自动化（与 macOS Automation.swift:12-24 的 `--panel settings|favorites|queue` 同族 ✓）
    //   Linux 支持 settings / favorites / downloads（macOS 侧本轮同步加 downloads ✓ 两端旗标一致 ✓）
    if (const int pi = args.indexOf("--panel"); pi > 0 && pi + 1 < args.size()) {
        const QString which = args[pi + 1];
        if (which == "settings")
            QMetaObject::invokeMethod(&w, [&w] { w.openSettingsDialog(); }, Qt::QueuedConnection);
        else if (which == "favorites")
            QMetaObject::invokeMethod(&w, [&w] { w.openFavoritesPanel(); }, Qt::QueuedConnection);
        else if (which == "downloads")
            QMetaObject::invokeMethod(&w, [&w] { w.openDownloadPanel(); }, Qt::QueuedConnection);
        else
            std::fprintf(stderr, "[PANEL] 未知面板：%s（可用 settings / favorites / downloads）\n", qPrintable(which));
    }
    if (args.contains("--selftest")) {   // 隔离测试：只验证 playResolved + mpv 渲染链路
        const int si = args.indexOf("--selftest");
        const QString f = (si + 1 < args.size()) ? args[si + 1] : "/tmp/hovtest.mp4";
        QMetaObject::invokeMethod(&w, [&w, f] { w.selfTest(f); }, Qt::QueuedConnection);
    }
    if (di > 0 && di + 1 < args.size()) {
        QMetaObject::invokeMethod(&w, [&w, q = args[di + 1]] { w.demoSearch(q); }, Qt::QueuedConnection);
    }
    return app.exec();
}
