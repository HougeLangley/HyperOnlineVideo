// ── MainWindow：主窗口（S0.5b ✓ 从 main.cpp 原样搬出 ✓ 零行为改动 ✓）──
// 说明：类体暂含内联实现 ✓ —— 目的是让后续阶段能在**独立 .cpp** 里定义成员函数（零 Ctx 注入 ✓✓）
#pragma once
#include <iostream>
#ifdef HOV_HAVE_KWINDOWSYSTEM
#include <KWindowEffects>
#endif
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QChar>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLatin1Char>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QMenu>
#include <QMetaObject>
#include <QModelIndex>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QOpenGLWidget>
#include <QOverload>
#include <QPair>
#include <QPixmap>
#include <QPoint>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRect>
#include <QScrollBar>
#include <QSet>
#include <QShowEvent>
#include <QSize>
#include <QSlider>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <QWindow>
#include "CookieImport.h"
#include "DownloadManager.h"
#include "Favorites.h"
#include "LocalLibrary.h"
#include "LoginDialog.h"
#include "MasonryView.h"
#include "Mpris.h"
#include "MpvWidget.h"
#include "NetEaseApi.h"
#include "NetPolicy.h"
#include "PlayQueue.h"
#include "Playlist.h"
#include "ProgressStore.h"
#include "QQMusicApi.h"
#include "SelfTest.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "Subtitles.h"
#include "Theme.h"
#include "ThemeWatcher.h"
#include "UrlResolver.h"
#include "ResultCardDelegate.h"

// main.cpp 中定义的自由函数（类体内联实现会调用 ✓ 故此处需可见 ✓）
QString titleOf(const QString &fmt);

class DownloadPanel;
class FavoritesPanel;

class MainWindow : public QMainWindow {
public:

    /// 供各 API 客户端统一注入 UA/Cookie（**模板必须在头文件里** ✗ 定义不能出 TU ✓）
template <typename T>
    void tuneApi(T *api) {
        if (api) api->setForceDirect(settings_.boolean("network.forceDirectDomestic", false));
}
    MainWindow();

    bool autoplay_ = false;
    bool eofLatch_ = false;   // 播完续播闩锁：eof 期间只触发一次（见 eofTimer 注释）
    QLineEdit *search_;
    QComboBox *srcBox_ = nullptr;   // 搜索来源：YouTube / 网易云 / QQ音乐
    QListWidget *results_;
    MpvWidget *player_;

    void demoSearch(const QString &q);
    void setAutoplay(bool v);
    /** 打开本地音视频文件（对应 Android 版本地库；不依赖网络，可离线验证） */
    /** 打开多个文件（播放列表；--open 支持多参数） */
    /** 设置倍速（供 --speed 参数与按钮共用） */
    void applySpeed(double sp);
    /** QQ音乐：搜索并播放第一条（自动化入口；取流需登录态，匿名会明确提示无权限） */
    void qqMusicSearch(const QString &keyword);

    /** 取网易云歌词并合并翻译后交给字幕层（匿名接口即可，实测部分曲目 40+ 行） */
    void loadLyricsFor(const QString &id, const QString &label, NetEaseApi *api);

    /** 队列 / 收藏逻辑自检（--queue-selftest）：纯逻辑，不触网、不动真实收藏文件 */
    /** 队列 / 收藏逻辑自检（--queue-selftest）：纯逻辑，不触网、不动真实收藏文件。
     *  实现已搬到 src/SelfTest.cpp（S1 拆分第一步 ✓ 零行为改动 ✓ 见文档 45/49）
     *  这里只做**上下文注入**：把自检真正需要的 6 样东西传进去 ✓（不改成 public ✗）*/
    void runQueueSelfTest();

    /** 登录窗口：App 内登录并把 cookie 自动落盘（无 WebEngine 时给出降级提示） */
    void openLoginDialog(const QString &site);

    /** 设置窗口（用户可见入口）：改动即时应用并落盘 */
    /** 设置对话框（S2 拆分 ✓ 实现已搬到 src/SettingsDialog.cpp ✓ 零行为改动 ✓ 见文档 45/51）
     *  这里只做上下文注入 ✓ 不把私有成员改成 public ✗ */
    void openSettingsDialog();
    /** UI-4-B ✓ 搜索过滤器（与 macOS searchSort/searchDuration、Android SearchFilters 对齐 ✓） */
    void openFilterDialog();
    /** W2 ✓ 下载面板（带进度条 + 重试/清理/打开目录 ✓ 与 macOS 面板功能一致）与收藏面板（对齐 macOS FavoritesPanel ✓）
     *  懒创建 + 复用：面板可长期开着看实时进度 ✓ 关闭不销毁（指针常驻 ✓） */
    void openDownloadPanel();
    void openFavoritesPanel();
    void applySearchFilters(int sort, int duration);     // 写会话态 + 同步到 resolver ✓ + 打日志 ✓
    int  searchSort_     = 1;                            // 1=相关度 2=最新 3=播放最多（会话态 ✓ 不落盘 ✗）
    int  searchDuration_ = 0;                            // 0=全部 1=短视频 2=中等 3=长篇

    /** 命令行设置一项：写入 settings.json，并尽量**立即生效**（能热更新的就热更新） */
    bool applySetting(const QString &key, const QString &value);
    bool applySettingInner(const QString &key, const QString &value);
    QString dumpSettings() const { return settings_.dump(); }

    /** 下载当前播放的直链（按钮 / 命令行共用） */
    void downloadCurrent();

    /** 命令行：下载某个直链（--download <URL> [文件名]） */
    void downloadUrl(const QString &url, const QString &name);

    /** 命令行：搜索并下载第一首可播放的（--download-music <关键词>） */
    void downloadMusic(const QString &keyword, int count = 1);

    /** 自动化入口：预选搜索来源（0=YouTube 1=B站 2=网易云 3=QQ音乐 4=本地库） */
    // 上限跟着下拉项数走（加了「本地库」后仍写死 2 会把第 4 项夹回 QQ音乐 —— 实测踩到）
    void applySource(int n);

    /** 列表点击的统一入口：按条目类型分发（netease: / qq: / 普通网页 URL） */
    void playItem(const QString &key, const QString &label);

    // ---------------- 进度记忆 ----------------
    // 设计要点（与 Android 版对齐）：
    //   * 键用「解析标识」而不是解析后的直链 —— 直链会过期，存了下次也放不了
    //   * 续播是**加载完再 seek**：mpv 刚 playResolved 时 duration 还是 0，立刻 seek 会被丢掉
    /** 播放任何内容前调用：结掉上一项的进度，并安排好本项的续播位置 */
    void beginPlayback(const QString &key, const QString &label);

    /** 音乐"开始播放"信息（文档 60 ✓）：把曾经散落三处的同一套设置收成**唯一一份** ✗ → ✓
     *  顺序（调用方无需关心 ✓）：进度落盘/playKey_ → 元数据 → 封面 → 结果行 → 封面下发 → 直链 → 起播 → 歌词 */
    struct MusicPlayInfo {
        QString key;                 // "netease:<id>" / "qq:<mid>"（进度记忆与切档重解析的键 ✓）
        QString label;               // "标题 — 艺术家"
        QString url;                 // 直链
        QString artist, album;       // 元数据
        QString coverUrl;            // 已解析好的封面直链（来源因端而异 ✓）
        QString statusText;          // 结果行文案（各路径不同 ✓ **原样保留** ✗）
        std::function<void(const QString &label)> fetchLyrics;   // 歌词（内部接管 api ✓）
    };
    void startMusicPlayback(const MusicPlayInfo &info);

    /** 500ms 定时器：执行待办的续播 seek + 记录/落盘进度（带节流） */
    void tickProgress();

    /** 立即把当前进度落盘（退出 / 切歌 / 停止时调） */
    void saveProgressNow();

    /** --progress-show：列出进度记录（倒序） */
    QString dumpProgress() const;
    void clearProgress();
    // ---------------- 本地库（下载目录） ----------------
    /** 列出下载目录里的媒体文件；filter 为文件名过滤 */
    void refreshLocalLibrary(const QString &filter = QString());
    void showLocalLibrary();

    /** R 键：重命名选中的本地库文件（保持扩展名；冲突/非法字符都有明确提示） */
    /** presetName 非空时不弹对话框（供 --rename-selected 自动化用，与界面走同一条逻辑） */
    void renameSelectedLocal(const QString &presetName = QString());

    int downloadMaxTotalMb() const { return dl_ ? dl_->maxTotalSizeMb() : 2048; }
    DownloadManager::CleanResult cleanupDownloads(qint64 maxBytes);
    void setResumeEnabled(bool b);

    /** --subs-url：取在线字幕并挂到当前播放上（自动化验证整条链路用，也方便手工挂字幕） */
    void applySubsUrl(const QString &url);
    static QString fmtClock(double s);

    /** ⏭/播完续播：有队列走队列，否则走本地文件播放列表 */
    void queueOrFileNext(bool autoAdvance = false);
    void queueOrFilePrev();

    /** 收藏当前播放项（F 键 / 命令行） */
    void toggleFavoriteCurrent();

    /** 用当前搜索结果建立队列（各来源搜索完都调它） */
    void rebuildQueueFromList();

    /** 播放网易云歌曲：取地址 → 播放 → 顺带取歌词（原文 + 翻译合并为双语） */
    void playNetEase(const QString &id, const QString &label);

    /** QQ 专辑封面地址（不需额外请求：搜索接口已带 albummid） */
    static QString qqCoverUrl(const QString &albumMid);
    /** 「歌名 — 歌手」是本项目中音乐标题的统一约定（_MOC 文档与队列/收藏都用它） */
    static QString titleOf(const QString &label);
    static QString artistOf(const QString &label);

    /** 音乐播放时挂封面：搜索里记过的直接用，网易云没记过就按 id 取详情 */
    void applyCoverFor(const QString &key, const QString &label);

    // ---------------- 播放中切音质 ----------------
    /** 当前有效档位：会话档位（若有）再受设置里的上限裁剪 */
    QString effectiveQuality() const;
    static QString qualityLabel(const QString &q);

    /** 切到指定档位；正在放音乐时**重新取流并回到原进度**（视频不适用：由 yt-dlp 按清晰度选流） */
    void switchQualityTo(const QString &q);

    /** Q 键：在 标准 → 较高 → 无损 之间循环 */
    void cycleQuality();

    /** 取网易云封面（走独立实例，避免与歌词请求争同一个对象的生命周期） */
    void fetchNetEaseCover(const QString &id, const QString &title, const QString &artist);

    /** 取 QQ音乐歌词（LRC 明文 + 翻译）并合并后交给字幕层 */
    void loadQQLyricsFor(const QString &mid, const QString &label, QQMusicApi *api);

    /** 播放 QQ音乐歌曲：逐档降级取流（VIP 曲目匿名会返回 104003，明确提示） */
    void playQQ(const QString &mid, const QString &label);

    /** 播放内容的平台标签与"搜索源跟随"（用户反馈：源显示 YouTube 却在放 B 站 —— 下拉是搜索源，容易误读） */
    /** 当前有效视频清晰度（会话值优先，其次设置） */
    int effectiveVideoHeight() const;
    /** 把左下角画质盒同步到"当前有效档位"（会话优先，否则设置值 ✓）——
     *  单一数据源原则：V 键 / --video-quality / 设置面板 任何路径改档位后都调用 ✓ */
    void syncQualityBox();

    /** 切清晰度：重新解析并回到原进度（视频专属；与"切音质"同一思路） */
    void switchVideoQuality(int h);

    /** B5 画中画：小窗 + 置顶 + 隐藏界面（不重建播放器，播放不中断）
     *  说明：Wayland 下"置顶"由合成器决定（Qt 只能表达意图），故同时把界面收成纯播放窗口。 */
    // ---- B7 全屏铺满（与 Android/macOS 同键 video.fillScreen）----
    // 会话级覆盖（#123 纪律 ✓）：CLI 探针（--fill-screen / --no-fill-screen）只改这里 ✓ **绝不写用户设置** ✗
    bool fillScreenOverride_ = false;
    bool fillScreenHasOverride_ = false;
    /// 铺满开关的**有效值**：优先会话覆盖 ✓ 否则读用户设置 ✓（唯一取值口 ✓ 避免多处各读一份 ✗）
    bool fillScreenSetting() const { return fillScreenHasOverride_ ? fillScreenOverride_
                                                                 : settings_.boolean("video.fillScreen", true); }
    /** 期望：全屏 && 设置开启 → panscan=1（裁切填满、去左右黑边）；否则 0（保持比例） */
    void applyFillMode(bool force = false);

    // ── 视频全屏（v1.2.0：对齐 macOS VideoFullscreen / Android 沉浸式）──────────────
    // 用户实测（Arch Linux）：双击画面无反应、点「铺满」在非全屏时毫无效果。
    // 根因两条：① 全项目从无 mouseDoubleClickEvent → 双击从未绑定任何行为；
    //          ② applyFillMode 的门控是 isFullScreen() && 设置 → 非全屏时点它什么都不做。
    // 更深一层：Linux 端根本没有"视频全屏"模式（窗口全屏时工具栏/结果区仍在），macOS/Android 都有。
    bool videoFull_ = false;              // 视频全屏中
    bool vfStatusWasVisible_ = false;     // 进入前的状态行可见性（退出时要还原，不能强显）
    QList<int> vfSplitSizes_;             // 进入前的分隔器尺寸（退出还原 → 配合结果区宽度记忆）

    /** 只留画面：收起/恢复工具栏、结果面板、状态行与所有按钮/滑块/标签（复用 PiP 的既有惯例） */
    void setPlayerOnlyVisible(bool only);

    // ── 全屏浮出侧栏（对齐 macOS：轮询鼠标位置 + 浮层显隐；只重挂 masonry_，不动布局）──────
    /** 进入视频全屏时把 masonry_ 移进浮层框；退出时移回分隔器（顺序/尺寸都不变） */
    void setMasonryFloating(bool floating);

    /** 浮出/收起侧栏（含控制条） */
    void setSidebarRevealed(bool on);

    /** 悬停轮询：鼠标贴左边缘 12px 浮出侧栏；贴底边 12px 浮出控制条；离开 400ms 收起 */
    void pollHover();

    /// ui.theme 的**唯一入口**（构造时 + 设置里改动时都走这里 ✓ 避免两处重复 ✗）
    void applyThemeSettingNow()
    {
        const QString t = settings_.str("ui.theme", "auto").trimmed().toLower();
        Theme::Mode forced = Theme::Mode::Dark;
        if (t == "auto") {
            if (!themeWatcher_) {
                themeWatcher_ = new ThemeWatcher(this);
                connect(themeWatcher_, &ThemeWatcher::modeChanged, this,
                        [](Theme::Mode m, const QString &src) {
                            Theme::setMode(m);
                            std::fprintf(stderr, "[THEME] 应用模式=%s（来源=%s）\n",
                                         m == Theme::Mode::Light ? "light" : "dark", qPrintable(src));
                        });
            }
            themeWatcher_->start();          // start() 会立刻 emit 一次 → setMode ✓
        } else if (Theme::parseMode(t, &forced)) {
            if (themeWatcher_) themeWatcher_->stop();   // 用户显式指定 → 不再跟随系统 ✓
            Theme::setMode(forced);
            std::fprintf(stderr, "[THEME] 用户指定 ui.theme=%s（不跟随系统）\n", qPrintable(t));
        }
    }

    void enterVideoFullscreen();

    void exitVideoFullscreen();

    void toggleVideoFullscreen();

    /** Z 键 / 铺满按钮：非全屏时**先给出视频全屏效果**（用户直觉），全屏后再切铺满开关 */
    void toggleFillScreen();

    /** 析构时先摘掉瀑布流的数据源：teardown 时 results_（QListWidget）先于 masonry_ 销毁，
     *  其模型析构发出 modelReset/rowsRemoved → 触发 masonry_->rebuild() 回读半销毁对象 →
     *  退出时段错误（coredump 实证：~QListWidget → modelReset → MasonryView::layout → viewport()）。 */
    ~MainWindow() override {
        if (masonry_) masonry_->setSource(nullptr);
    }

    void togglePiP();

    /** V 键：自动 → 1080 → 720 → 480 → 360 → 仅音频 循环 */
    void cycleVideoQuality();

    // ---------------- B4：列表缩略图 ----------------
    // 内存缓存 + 异步下载 + **按条目指针守卫**（行可能在后续搜索里重排，避免错图）
    QHash<QString, QPixmap> thumbCache_;
    QNetworkAccessManager *thumbNet_ = nullptr;
    int thumbLoaded_ = 0;

    void setItemThumb(QListWidgetItem *item, const QString &url);

    /** 在线缩略图地址：YouTube 由 id 推出；B站用返回的 pic；QQ 用 albummid */
    static QString youTubeThumb(const QString &id);
    static QString qqThumb(const QString &albumMid) { return albumMid.isEmpty() ? QString()
        : QString("https://y.gtimg.cn/music/photo_new/T002R300x300M000%1.jpg").arg(albumMid); }

    void followSourceFor(const QString &service);

    /** B站搜索（官方接口；结果点击后走统一的在线解析 + CC 字幕路径） */
    void biliSearchPublic(const QString &k, bool play);
    /** A0：从系统浏览器导入 cookie（登录菜单与 --import-cookies CLI 共用同一条实现）
     *  产物包（AppImage/Flatpak）没有 WebEngine，这条路径就是它们的"登录"方式。 */
    QString importCookiesFromBrowser(const QString &browser);
    void togglePiPPublic();
    /** W3 ✓ Bug3 验证探针用：与 macOS `--download-current` 同名同义（点一次「下载当前」✓） */
    void downloadCurrentPublic() { downloadCurrent(); }
    void showFullScreenPublic() { enterVideoFullscreen(); }   // CLI --fullscreen：与双击/F 同一语义

    /// CLI 探针 --fs-probe：进视频全屏 → 强制浮出侧栏 → 打印可判定状态 → 收起 → 退出
    /// （与 macOS 的 --fs-probe 同名同义；探针期间**停掉鼠标轮询**，否则真实鼠标会覆盖强制状态）
    void fsProbePublic(const QString &tag);
    /// 探针入口（--fill-screen / --no-fill-screen）：**只改会话覆盖** ✓ 不写用户设置 ✗（#123 ✓ 见文档 50）
    void setFillScreenPublic(bool on) { fillScreenOverride_ = on; fillScreenHasOverride_ = true;
                                        if (fillBtn_) fillBtn_->setChecked(on);
                                        applyFillMode(true); }
    /** 自动化：按来源搜索（0=YouTube 1=B站 2=网易云 3=QQ） */
    void searchPublic(const QString &kw, int src);
    /** 自动化探针：连续触发 n 次"加载更多"（每隔 6 秒一次）——等价用户反复拖到底。
     *  与项目里既有的 --queue-selftest / --dump-frame 等同属自动化入口，可脚本化验收。 */
    void loadMoreForTest(int n);

    /** 连播压测探针：--play-storm <次数> [间隔毫秒]（配合 --yt/--bili 搜索使用）。
     *  等价用户快速连点搜索结果：每次都走完整 解析→在线字幕→播放 链路。
     *  fd 泄漏排查的复现入口，与 --load-more / --queue-selftest 同属自动化探针。 */
    void playStormForTest(int n, int intervalMs);

    void biliSearch(const QString &keyword, bool autoPlayFirst = false);

    /** 网易云音乐：搜索并播放第一条（自动化入口，也是后续音乐 UI 的基础） */
    void musicSearch(const QString &keyword);

    /** 自动化入口：指定字幕轨序号（-1=关闭）与字幕延迟（秒） */
    void applySubtitleTrackIndex(int n);
    void applySubtitleDelay(double sec);

    void openFiles(const QStringList &files);
    static bool isWebUrl(const QString &s);
    void openLocal(const QString &f);
    void selfTest(const QString &f);

protected:
    /** 视频全屏：双击画面 / 单击已在前端处理（mpv 不抢鼠标事件） */
    /// 双击画面 → 视频全屏（窗口级兜底：即便 GL 子控件吞了事件，冒泡到窗口也能生效）
    void mouseDoubleClickEvent(QMouseEvent *e) override;

    /// 玻璃：首次显示时向合成器申请模糊（KWin Wayland 的 org_kde_kwin_blur 与 X11 原子都经此转发）
    void showEvent(QShowEvent *e) override;

    bool eventFilter(QObject *o, QEvent *e) override;

    /** 桌面键盘快捷键：空格播放暂停 / ←→ 快进退 / ↑↓ 音量 / F 全屏 / Esc 退出全屏 */
    void keyPressEvent(QKeyEvent *e) override;
    /** W1 ✓ 关闭时保存窗口几何（ui.windowGeometry ✓ 下次启动恢复 ✓） */
    void closeEvent(QCloseEvent *e) override;

    void seekBy(double d);
    void setVol(int v);

private:
    Playlist playlist_;
    UrlResolver *resolver_ = nullptr;
    Settings settings_;      // 设置（~/.config/hov/settings.json）
    PlayQueue queue_;        // 播放队列（顺序/单曲/随机）
    ProgressStore prog_;     // 进度记忆（~/.config/hov/progress.json）
    QString playKey_;        // 当前播放项的解析标识（进度记忆的键）
    QString playLabel_;      // 标题（仅用于展示）
    QString playTitle_;      // MPRIS 用：标题
    QString playArtist_;     // MPRIS 用：艺术家
    bool metaSent_ = false;  // 元数据是否已随时长报过
    Mpris *mpris_ = nullptr; // 桌面媒体键/锁屏控件接口
    double pendingResume_ = 0.0;    // 待续播位置（等媒体加载完再 seek）
    qint64 lastProgRemember_ = 0;   // 记录节流（毫秒）
    qint64 lastProgSave_ = 0;       // 落盘节流（毫秒）
    bool resumeEnabled_ = true;     // --no-resume 时关闭（自动化测试用）
    QString resolutionKey_;         // 最近一次在线视频的页面地址（切清晰度要重新解析它）
    QString sessionQuality_;        // 会话内音质档位（空 = 跟随设置；Q 键 / --quality 会改它）
    QString playPlatform_;          // 当前播放内容所属平台（状态栏 / 列表提示显示）
    QFrame *topCard_ = nullptr;     // B5：画中画时隐藏
    QLabel *statusLabel_ = nullptr;  // v1.2.0：状态行（结果区只放卡片）
    QLabel *labelInfo_   = nullptr;
    // UI-3b（文档 61 ✓）：底栏新增控件（画质/倍速下拉 + 播放模式按钮）
    QComboBox *qualityBox_ = nullptr;
    bool qualityBoxMusic_ = false;                 // 左下角下拉当前是否"音质档"（音乐内容 ✓ 对齐 macOS B1）
    void setQualityBoxForMusic(bool music);        // 切换下拉内容（画质档 ↔ 音质档 ✓）
    QComboBox *speedBox_   = nullptr;
    QPushButton *modeBtn_  = nullptr;  // UI-3 ✓ 底栏信息行（[来源] 标题  时间）
    QPushButton *autoNextBtn_ = nullptr;
    MasonryView *masonry_ = nullptr;  // v1.2.0：真·瀑布流结果视图（results_ 作为数据源被隐藏）

    // ── 搜索结果分页（用户所说的"瀑布流"：滚到底自动加载下一页）────────────
    // 完全对齐 macOS/Android 三处约定：每页 **20** 条、**无开关**（始终开启）、页数上限 **YouTube 6 / 其它 5**。
    // 翻页策略也与 macOS 相同："多取后截尾" —— 按 20*page 条去请求，只追加没见过的条目（URL 去重）。
    QString moreKw_;
    int  moreSrc_ = -1;
    int  morePage_ = 1;
    bool moreHasMore_ = true;
    bool moreLoading_ = false;
    int  moreReqId_ = 0;            // 代次：换词/换源后旧回调一律丢弃（防竞态）
    QSet<QString> moreSeen_;        // 已出现过的条目 URL
    static int maxPageFor(int src) { return src == 0 ? 6 : 5; }   // 对齐 macOS
    void noteSearchContext(const QString &kw, int src) {           // 每个搜索入口都要调用
        std::fprintf(stderr, "[MORE] 会话建立: kw=%s src=%d\n", qPrintable(kw), src);
        moreKw_ = kw; moreSrc_ = src; morePage_ = 1; moreHasMore_ = true;
        moreLoading_ = false; moreSeen_.clear(); ++moreReqId_;
    }
    void setStatusLine(const QString &t);
    void syncSeenFromList() {                                      // 懒同步：把列表已有 URL 记为"已见"
        if (!results_) return;
        for (int i = 0; i < results_->count(); ++i)
            if (auto *it = results_->item(i)) {
                const QString u = it->data(Qt::UserRole).toString();
                if (!u.isEmpty()) moreSeen_.insert(u);
            }
    }
    /** 追加"没见过"的条目；返回新增条数（对齐 macOS 的"多取后截尾"） */
    int appendNewRows(const QVector<QPair<QString, QString>> &rows,
                      const QVector<QPair<QString, QString>> &thumbs = {}) {
        int added = 0;
        for (int i = 0; i < rows.size(); ++i) {
            const QString url = rows[i].second;
            if (url.isEmpty()) continue;
            if (rows[i].first.trimmed().isEmpty()) continue;   // yt-dlp 偶有无标题条目 → 不要建成空卡片
            if (moreSeen_.contains(url)) continue;
            moreSeen_.insert(url);
            auto *it = new QListWidgetItem(rows[i].first, results_);
            it->setData(Qt::UserRole, url);
            if (i < thumbs.size() && !thumbs[i].second.isEmpty()) setItemThumb(it, thumbs[i].second);
            ++added;
        }
        return added;
    }
    void finishMore(int reqId, int added);
    void prefetchNetEaseCovers(const QVector<NetEaseApi::Song> &songs);   // 第一页/翻页共用 ✓
    void fetchMorePage(int src, const QString &kw, int page);
    /** 滚到底触发：与 macOS loadMoreIfNeeded 等价 */
    void loadMore();

    QSplitter *split_ = nullptr;    // B5：画中画时隐藏
    bool pipActive_ = false;        // B5：画中画状态
    bool blurRequested_ = false;    // 玻璃：是否已向合成器申请过模糊（只申请一次）
    ThemeWatcher *themeWatcher_ = nullptr;   // ui.theme=auto 时跟随系统 light/dark
    // ── 全屏浮出侧栏（ui.hoverReveal）──
    static constexpr int kEdgeBand = 12;   // 边缘感应带宽（像素，唯一真相）
    QFrame *masonryHost_ = nullptr;        // 浮层框（进入全屏时 masonry_ 挂进来）
    QTimer *hoverTimer_ = nullptr;         // 鼠标轮询（仅全屏时运行）
    qint64 hideDeadline_ = 0;              // 离开边缘后的收起时刻（400ms 防抖）
    bool sidebarRevealed_ = false;         // 浮层是否展开
    int vfMasonryWidth_ = 0;               // 进入全屏前的结果区宽度（退出按它还原）
    // 初值必须与【非全屏】的初始状态一致（false）。设 true 会导致启动后第一拍就下发一次，
    // 而那一拍恰好撞上 mpv 打开视频文件的窗口期，同步的 mpv_get_property 会阻塞 GUI 线程 -> 播放卡死。
    bool lastFillApplied_ = false;  // B7：上次下发的铺满状态
    QPushButton *fillBtn_ = nullptr;// B7：控制条「铺满」开关
    QRect pipSavedGeometry_;
    QSize pipSavedMinSize_;         // W5 ✓ 进 PiP 前的最小尺寸（退出必须还原 ✓）
    int sessionVideoHeight_ = 0;    // 会话内视频清晰度（0=自动，-1=仅音频；V 键 / --video-quality 会改）
    DownloadManager *dl_ = nullptr;   // 下载管理（并发 2 + 重试）
    DownloadPanel *dlPanel_ = nullptr;     // W2 ✓ 下载面板（懒创建；开着即随进度回调实时刷新 ✓）
    FavoritesPanel *favPanel_ = nullptr;   // W2 ✓ 收藏面板（懒创建；每次打开前 refresh ✓）
    LocalLibrary lib_;                   // 本地库（下载目录）
    QHash<QString, QString> coverMap_;   // 播放键 → 封面地址（搜索时顺便记录）
    QHash<QString, QString> titleMap_;   // 播放键 → 标题/艺术家（封面下方文字）
    QString lastStreamUrl_;           // 当前播放的直链（下载按钮用）
    QString lastArtist_, lastAlbum_, lastCoverUrl_;   // 下载时嵌入的音乐标签
    QString lastAudioUrl_;            // ③ 当前视频的独立音轨直链（下载时用 ffmpeg 混流，否则下载无声）

    // ── 直链过期自愈（与 Android 端同法；macOS 端同样需要）──────────────────
    // 现象：暂停很久后继续播 → 播一会儿停住 → 手动**切清晰度**才能继续（用户实测）。
    // 根因：解析出的直链有有效期（googlevideo / B站 数小时失效）；切档会重新解析 → 与观察吻合。
    qint64 resolvedAtMs_ = 0;      // 本直链解析时刻
    bool   healTried_ = false;     // 每条内容最多自愈一次（防抖）
    double lastPos_ = -1;          // 停滞检测：上次进度
    qint64 lastPosAtMs_ = 0;
    /** 保守 TTL（按来源；本地文件与未知来源视为不过期） */
    qint64 ttlMs() const;
    /** 过期自愈：走"切清晰度"同一条链路（重新解析 + 用 pendingResume_ 回到原位置） */
    void healIfExpired(bool force = false);
    /** 停滞看门狗：播放中且进度 15 秒不推进（非暂停）→ 自愈一次 */
    void startStallWatch();
    QString lastLRC_;                 // ③ 当前曲目的歌词原文（下载时写同名 .lrc 侧车）
    QString lastStreamTitle_;
    Favorites favs_;         // 收藏（~/.config/hov/favorites.json）

    /** 播放列表当前项 */
    void playCurrent();
    void playNextFile();
    void playPrevFile();

    QPushButton *speedBtn_ = nullptr;
    QPushButton *btnPlay_ = nullptr;
    QSlider *seek_ = nullptr;
    QSlider *vol_ = nullptr;
    QLabel *labelTime_ = nullptr;
    QTimer *ctlTimer_ = nullptr;

    static QString fmtTime(double sec);

    /** 定时刷新播放控件（用户拖动进度条时不打断） */
    void refreshControls();

    // 启动探活：判断国内音乐接口是否可达；失败时给出"指名域名 + 规则片段"的可执行提示
    void probeNetwork();

    void doSearch();
};
