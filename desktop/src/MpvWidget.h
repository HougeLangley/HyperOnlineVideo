#pragma once
#include <mutex>
#include <thread>
#include <atomic>
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QByteArray>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include "SubtitleOverlay.h"   // 覆盖层状态对象（值成员，需完整类型）
#include "Subtitles.h"   // SubtitleTrack（多字幕轨）
#include "CoverArt.h"    // 专辑封面（值成员）

class QLabel;
class SubtitleOverlay;
class QTimer;

// libmpv render API 播放部件（Wayland 下唯一可行的嵌入方式；Spike A 已验证）
#include "ColorTheme.h"
#include <functional>

class MpvWidget : public QOpenGLWidget {
public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;
    void playUrl(const QString &url);
    /** ② 缓存策略按来源区分（网络大缓存 / 本地关闭缓存）；必须在 loadfile 前调用 */
    void prepareCacheOptions(bool isNetwork);
    // 播放控制（对应 Android 版播放器控件）
    void togglePause();
    // ── 属性快照（关键：GUI 线程绝不直接读 mpv 属性）──
    // mpv 的属性读取要拿核心锁，**流媒体加载期间**该锁可能被长期持有（网络 demux 阻塞）；
    // 而界面每 0.5 秒就要读一次 time-pos/duration → 起播时会出现"界面冻结数秒"（macOS 侧实测 5 秒×5，看门狗抓到）。
    // 这里由**后台线程**定时采集全部属性到 Snapshot（互斥锁保护），GUI 只读快照（纯内存拷贝，永不阻塞）。
    struct Snapshot {
        double duration = 0, position = 0, panscan = 0, speed = 1, volume = 100;
        bool paused = false, eof = false;
        int width = 0, height = 0;
        QString title;
    };
    Snapshot snapshot() const { std::lock_guard<std::mutex> lk(snapMutex_); return snap_; }
    bool paused();
    double positionSec();
    double durationSec();
    void seekTo(double sec);
    /** 停止并卸载当前媒体（MPRIS Stop 用） */
    void stop();
    int volume();
    bool eofReached(int *out);
    void setSpeed(double sp);   // 倍速（mpv speed 属性）
    void setPanscan(double v);  // B7 全屏铺满：**同步**版（仅 mpv 就绪后调用；就绪前会阻塞 GUI 线程）
    void setPanscanAsync(double v);  // B7：异步下发（推荐；不会阻塞 GUI 线程）
    bool renderReady() const { return renderReady_; }   // 渲染上下文就绪（就绪前禁止碰 mpv 属性）
    double panscan() const;     // 回读（取证：确认 mpv 真的接受了）
    double speed();
    void setVolume(int v);
    // ADR-002：显式选流后播放（视频先播，音轨延迟挂载）
    void playResolved(const QString &videoUrl, const QString &audioUrl);
    // 字幕/歌词（App 层渲染；播放本地文件时自动加载**同名**外挂字幕）
    void toggleSubtitles();
    bool subtitlesAvailable() const;
    // 多字幕轨（本地同名多语言轨道；在线轨由 UrlResolver 先经 yt-dlp 落到本地再传入）
    void setSubtitleTracks(const QVector<SubtitleTrack> &tracks, int initialIndex = 0);
    /** 追加在线字幕轨（B站 CC 等）；当前未开字幕时自动启用语言优先级最高的一条 */
    void addSubtitleTracks(const QVector<SubtitleTrack> &extra);
    /** 直接给播放器一组字幕/歌词（在线歌词走这里，不需要落盘成文件） */
    void setSubtitleCues(const QVector<SubtitleCue> &cues, const QString &label, bool karaoke = false);
    /** 专辑封面（音乐场景；传空地址 = 清除） */
    void setCoverArt(const QString &url, const QString &title, const QString &artist);
    /// 玻璃质感开关（设置键 ui.glass，默认开；关闭后完全回到纯色背景，零残留）
    void setGlassEnabled(bool on) { glassOn_ = on; update(); }
    bool glassEnabled() const { return glassOn_; }
    /** 字幕/歌词字号倍数（设置页） */
    void setSubtitleFontScale(double s) { subs_.setFontScale(s); }
    void cycleSubtitleTrack();                       // C 键：在「关 → 轨 1 → 轨 2 → … → 关」间循环
    void adjustSubtitleDelay(double deltaSec);        // 逗号/句号：字幕延迟微调
    /** 预设期望的字幕轨序号（-1=关；用于 --sub-track 自动化参数，在加载轨道时生效） */
    void setPreferredSubtitleTrack(int index);
    bool ready() const { return ctx_ != nullptr; }
protected:
    void initializeGL() override;
    void paintGL() override;      // 只做 mpv 的 GL 渲染 ✗
    /** QPainter 内容（封面/玻璃/字幕）在此绘制：GL 之后独立光栅合成 ✓
     *  2026-09-25 实测：在 paintGL 里画会被 mpv 残留 GL 状态破坏（fillRect/drawImage 均不显示 ✗）*/
    void paintEvent(QPaintEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;   // 同步子控件几何（封面/玻璃层 ✓）
private:
    // ── 玻璃质感（v1.2.0，设置键 ui.glass）──
    ColorTheme glass_;               // 按封面 URL 缓存的主题（取色 + 磨砂底 + 渐变）
    QString    glassKey_;            // 已算主题对应的封面 URL（换了才算）
    // 磨砂底/渐变 的 QPixmap 缓存（drawPixmap 走 GL 原生纹理 ✓ drawImage 实测不显示 ✗）
    QPixmap    glassBdPix_;
    QString    glassBdKey_;
    QPixmap    glassGradPix_;
    QString    glassGradKey_;
    QString    coverUrl_;            // 当前封面 URL（空 = 视频/无封面 → 不画玻璃）
    bool       glassOn_ = true;      // 默认开；关闭后零残留
    bool renderReady_ = false;
    // 属性快照：后台线程写入、GUI 线程读取
    mutable std::mutex snapMutex_;
    Snapshot snap_;
    std::atomic<bool> snapStop_{false};
    std::thread snapThread_;
    std::thread logThread_;      // mpv 事件/日志泵（此前无消费者，错误全静默 ✗ 2026-09-25 加）
    void startSnapshotter();
    void captureSnapshot();   // B7：渲染上下文就绪（就绪前禁止读写 mpv 属性，否则挂 GUI 线程）
    void loadSidecarSubtitles(const QString &mediaPath);
    void clearSubtitles();
    void applySubtitleTrack(int index);               // -1 = 关闭字幕
    mpv_handle *mpv_ = nullptr;
    mpv_render_context *ctx_ = nullptr;
    // 封面/玻璃背景用**子控件**承载（2026-09-25 实测：软件 GL 下 QOpenGLWidget 的 QPainter
    //   光栅绘制（fillRect/drawImage）全部不显示 ✗ 仅 glyph 文本可见 ✗；子控件经实测可见 ✓✓）
    QLabel *coverLabel_ = nullptr;
    std::atomic<bool> wantPlaying_{false};   // keep-open 继承暂停的竞态加固（快照线程自动纠正 ✓）
    QImage     glassBase_;           // 玻璃底图缓存（磨砂+渐变+压暗；仅封面变化时重建 ✓）
    QString    glassBaseKey_;        // 底图缓存键（封面 URL ✓）
    QLabel *glassLabel_ = nullptr;
    QLabel *lyricLabel_ = nullptr;   // 音乐模式的歌词层（透明子控件：绕开 GL 下 QPainter 绘制的限制 ✓）
    void refreshOverlays();                          // 按 cover_/glass_ 状态刷新子控件
    mutable QString coverPixKey_;                    // 已生成封面 pixmap 对应的 url
    mutable QString glassPixKey_;                    // 已生成玻璃图对应的 url
    mutable int coverLabelSide_ = 0;
    SubtitleOverlay subs_;   // 值成员：由 paintGL 用 QPainter 绘制（子控件方案实测不显示）
    CoverArt cover_;         // 专辑封面（同样在 paintGL 里画）
    QTimer *subsTimer_ = nullptr;
    QVector<SubtitleTrack> tracks_;
    int trackIndex_ = -1;
    int preferredTrack_ = -2;   // -2=未指定，-1=关闭，>=0=指定轨序号

    // ── glFenceSync 泄漏防护（fd 泄漏真凶，见 fd 排查报告）──
    // mpv ≤0.41 的 libmpv GL 渲染模式：ra_gl_ctx_submit_frame 每帧 glFenceSync 入队 vsync_fences，
    // 而清理（ClientWaitSync+DeleteSync）只在 vo=gpu 的 ra_gl_ctx_swap_buffers 里做；libmpv 模式由宿主
    // （本类）swap，该函数永不被调 → GLsync 无限累积。virgl 等驱动下每个 GLsync 持有一个 sync_file fd
    // （flush 时 execbuffer FENCE_FD_OUT 创建，DeleteSync 才 close）→ 每渲染一帧泄一个 fd（实测 ~26/s），
    // 数小时后打满 fd 上限 → Qt 事件分发器/mpv 建 fd 失败 → abort（此前崩溃真因）。
    // 上游已修：mpv f74adc4 "opengl/context: require swap_buffers param for FenceSync"（issue #17217，
    // 将随 0.42 发布）；但本包动态链接系统 libmpv 0.41，只能在应用侧兜底——mpv 的 GL 函数全部经由我们
    // 提供的 get_proc_address 取得，故在此处包装 glFenceSync/glDeleteSync 做"谁创建谁释放"记账：
    // mpv 自己 DeleteSync 的（如 PBO 上传路径）原样放行并从登记簿移除；被遗弃超过 kSyncGraceFrames 帧的
    // 由我们代为 DeleteSync（与 vo=gpu swapchain_depth 修剪等价）。
    void *mpvGlProc(const char *name);
    static void *fenceSyncShim(unsigned int condition, unsigned int flags);
    static void deleteSyncShim(void *sync);
    using FenceSyncFn = void *(*)(unsigned int, unsigned int);
    using DeleteSyncFn = void (*)(void *);
    FenceSyncFn realFenceSync_ = nullptr;
    DeleteSyncFn realDeleteSync_ = nullptr;
    QVector<QPair<void *, qint64>> pendingSyncs_;   // 登记簿：（GLsync, 创建时的帧序号）
    qint64 syncFrame_ = 0;
    static constexpr int kSyncGraceFrames = 8;      // 宽限窗口（vo=gpu 的 swapchain_depth 默认为 3）
};
