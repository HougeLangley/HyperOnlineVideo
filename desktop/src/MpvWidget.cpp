#include "MpvWidget.h"
#include "SubtitleOverlay.h"
#include "Subtitles.h"
#include "Settings.h"
#include <QDebug>
#include <QDateTime>
#include <chrono>
#include <cstdio>
#include <thread>
#include <algorithm>   // std::stable_sort（字幕轨语言优先级排序）
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QtMath>
#include <cstring>   // strcmp（mpvGlProc 按名字拦截 GL 函数）

// GL 调用全部发生在 GUI 线程（paintGL / initializeGL / 析构时的 mpv_render_context_free），
// 用 thread_local 让静态 shim 找回当前实例。
static thread_local MpvWidget *t_currentMpvWidget = nullptr;

// ---- glFenceSync/glDeleteSync 包装：兜底 mpv ≤0.41 libmpv 模式的 fence 泄漏（详见头文件注释）----
void *MpvWidget::fenceSyncShim(unsigned int condition, unsigned int flags) {
    MpvWidget *w = t_currentMpvWidget;
    if (!w || !w->realFenceSync_) return nullptr;
    void *sync = w->realFenceSync_(condition, flags);
    if (sync && w->realDeleteSync_) {
        w->pendingSyncs_.append({sync, ++w->syncFrame_});
        // 修剪：超过宽限窗口仍未被 mpv 释放的，由我们释放（谁创建谁释放）
        while (!w->pendingSyncs_.isEmpty()
               && w->syncFrame_ - w->pendingSyncs_.first().second > kSyncGraceFrames) {
            w->realDeleteSync_(w->pendingSyncs_.first().first);
            w->pendingSyncs_.removeFirst();
        }
    }
    return sync;
}

void MpvWidget::deleteSyncShim(void *sync) {
    MpvWidget *w = t_currentMpvWidget;
    if (!w || !w->realDeleteSync_) return;
    // mpv 自己释放的：从登记簿移除，避免我们稍后二次删除
    for (int i = 0; i < w->pendingSyncs_.size(); ++i) {
        if (w->pendingSyncs_[i].first == sync) { w->pendingSyncs_.remove(i); break; }
    }
    w->realDeleteSync_(sync);
}

void *MpvWidget::mpvGlProc(const char *name) {
    QOpenGLContext *c = QOpenGLContext::currentContext();
    if (!c || !name) return nullptr;
    if (realFenceSync_ && std::strcmp(name, "glFenceSync") == 0)
        return reinterpret_cast<void *>(&MpvWidget::fenceSyncShim);
    if (realDeleteSync_ && std::strcmp(name, "glDeleteSync") == 0)
        return reinterpret_cast<void *>(&MpvWidget::deleteSyncShim);
    return reinterpret_cast<void *>(c->getProcAddress(name));
}

MpvWidget::MpvWidget(QWidget *parent) : QOpenGLWidget(parent) {
    mpv_ = mpv_create();
    // 渲染后端（video.gpuNext ✓ 与 macOS/Android 同键）：
    //   false → vo=libmpv（经典 OpenGL 路径 ✓ 默认 ✓）
    //   true  → vo=gpu-next（libplacebo：缩放/去色带/色调映射 ✓ 本机 mpv 已链接 libplacebo ✓ 实测可行 ✓）
    // 构造时就要定 vo（mpv_initialize 之后不能再改 ✗）→ 用 rawBool 直读文件 ✓（不依赖实例 load ✓）
    gpuNext_ = Settings::rawBool("video.gpuNext", false);
    mpv_set_option_string(mpv_, "vo", gpuNext_ ? "gpu-next" : "libmpv");
    mpv_set_option_string(mpv_, "hwdec", "auto-safe");
    mpv_set_option_string(mpv_, "ytdl", "no");
    // 直链由 App 层解析（UrlResolver 调 yt-dlp），mpv 只负责播放：
    // 这里只设 UA；Referer 按站点在 playResolved 里逐次设置
    // （原先写死为 YouTube，播 B 站时会带错误的 Referer）
    mpv_set_option_string(mpv_, "user-agent",
        "Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    mpv_set_option_string(mpv_, "idle", "yes");
    mpv_set_option_string(mpv_, "keep-open", "yes");
    // 字幕交给 App 层渲染（与 Android 同方案）：关掉 mpv 自己的外挂字幕自动加载与渲染，
    // 否则 mpv 会用 libass 渲染同名 .srt（实测会盖住/混淆 App 层字幕，且 LRC 歌词它也不认）。
    mpv_set_option_string(mpv_, "sub-auto", "no");
    mpv_set_option_string(mpv_, "sid", "no");
    mpv_set_option_string(mpv_, "msg-level", "all=info");
    mpv_set_option_string(mpv_, "log-file", "/tmp/hov-mpv.log");   // 排障用
    mpv_initialize(mpv_);

    // 键盘焦点：窗口内默认可能落在搜索框上，导致空格/左右/S/C 等快捷键被文本框吃掉。
    // 让播放器可获焦，并在主窗口启动时把焦点交给它（未处理的按键会冒泡回主窗口）。
    setFocusPolicy(Qt::StrongFocus);
    subsTimer_ = new QTimer(this);
    subsTimer_->setInterval(120);
    connect(subsTimer_, &QTimer::timeout, this, [this] {
        if (subs_.hasCues()) { subs_.setPosition(positionSec()); if (!subs_.forceHidden()) update(); }
    });
    subsTimer_->start();
}

MpvWidget::~MpvWidget() {
    snapStop_ = true;
    if (snapThread_.joinable()) snapThread_.join();
    // 先摘掉回调，避免 mpv 之后仍回调到将析构的对象（网络播放耗时长时最容易命中）
    if (ctx_) mpv_render_context_set_update_callback(ctx_, nullptr, nullptr);
    t_currentMpvWidget = this;   // mpv_render_context_free 内的 GL 清理会调到 shim
    if (ctx_) mpv_render_context_free(ctx_);
    if (mpv_) mpv_terminate_destroy(mpv_);
    t_currentMpvWidget = nullptr;
}

void MpvWidget::initializeGL() {
    // 先取真实的 glFenceSync/glDeleteSync 备用（有 ARB_sync 才包装；没有则 mpv 侧 gl->FenceSync 为空、不涉泄漏）
    realFenceSync_ = reinterpret_cast<FenceSyncFn>(QOpenGLContext::currentContext()->getProcAddress("glFenceSync"));
    realDeleteSync_ = reinterpret_cast<DeleteSyncFn>(QOpenGLContext::currentContext()->getProcAddress("glDeleteSync"));
    t_currentMpvWidget = this;
    mpv_opengl_init_params ip{
        [](void *opaque, const char *name) -> void * {
            auto *w = static_cast<MpvWidget *>(opaque);
            return w ? w->mpvGlProc(name) : nullptr;
        }, this};
    char api[] = MPV_RENDER_API_TYPE_OPENGL;              // 注意：是字符串常量 "opengl"
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, api},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &ip},
        {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (mpv_render_context_create(&ctx_, mpv_, params) < 0) {
        // 回退（照 macOS 蓝本 ✓）：gpu-next 起不来时切回 libmpv 再试一次（不打扰用户 ✓）
        // 回退：gpu-next 起不来 → 切回 libmpv **重试一次**，然后走**同一条成功路径** ✓
        //（不要手动调 initializeGL() ✗ Qt 虚函数不能手动调，会重复初始化 ✓）
        if (gpuNext_) {
            qWarning() << "gpu-next 渲染上下文创建失败 → 自动回退 libmpv";
            std::fprintf(stderr, "[PLAY] gpu-next 失败 → 回退 libmpv\n");
            mpv_set_property_string(mpv_, "vo", "libmpv");
            gpuNext_ = false;
            if (mpv_render_context_create(&ctx_, mpv_, params) < 0) {
                qWarning() << "回退 libmpv 后仍失败";
                return;
            }
            qInfo() << "回退 libmpv 成功（继续走正常初始化 ✓）";
        } else {
            qWarning() << "mpv_render_context_create 失败";
            return;
        }
    }
    mpv_render_context_set_update_callback(ctx_, [](void *p) {
        auto *w = static_cast<MpvWidget *>(p);
        QMetaObject::invokeMethod(w, [w] { w->update(); }, Qt::QueuedConnection);
    }, this);
    renderReady_ = true;
    startSnapshotter();          // 渲染就绪后再启动采集（采集里也要碰 mpv，必须等 mpv 真的可用）
    qInfo() << "mpv render context ready";
    // 注：早期为定位"卡在解析还是渲染"曾在这里挂 30 秒诊断定时器（打印 core-idle 等）。
    // 问题已定位，诊断代码按约定清理 —— 需要时用 mpv 的 log-file（/tmp/hov-mpv.log）即可。
}

void MpvWidget::paintGL() {
    t_currentMpvWidget = this;   // mpv_render_context_render 内的 GL 调用会调到 shim
    // 与原始 GL 调用混用时，Qt 要求的写法：QPainter + beginNativePainting 包住 GL 段，
    // 之后再回到 QPainter 画字幕（否则字幕完全不显示 —— 实测踩过）。
    QPainter painter(this);
    painter.beginNativePainting();
    if (ctx_) {
        mpv_opengl_fbo fbo{(int)defaultFramebufferObject(),
                           (int)(width() * devicePixelRatio()),
                           (int)(height() * devicePixelRatio()), 0};  // internal_format=0：显式初始化，消除 -Wextra 警告
        int flip = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip},
            {MPV_RENDER_PARAM_INVALID, nullptr}};
        mpv_render_context_render(ctx_, params);
    }
    painter.endNativePainting();
    // ── gpu-next 守卫（本机实测踩到：无硬件 GL 时 gpu-next「创建成功但输出全黑」✗）──
    // 起播约 3 秒后抓一次帧，算亮度方差；纯色/全黑（方差≈0）判定为"没在渲染" → 回调提示 ✓
    // （原"按已绘帧数触发"的旧逻辑已移除 ✗ —— 全黑时根本没有帧回调 ✓ 触发改到 playUrl 的墙钟定时器 ✓）

    if (!subs_.forceHidden()) subs_.paint(painter, rect());   // 字幕/歌词（App 层渲染）
    // ── 玻璃质感（v1.2.0）：磨砂底（模糊封面）+ 主题色竖向渐变 + 压暗层 ──
    // 只在"有封面"时绘制（视频播放会清空封面）→ 视频画面完全不受影响。
    // 主题色按封面 URL 缓存：只换封面时才算一次（取色+模糊都在 220px 小图上，毫秒级）。
    if (glassOn_ && cover_.hasImage()) {
        if (!glass_.valid() || glassKey_ != coverUrl_) {
            const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
            glass_ = ColorTheme::make(cover_.image());
            glassKey_ = coverUrl_;
            const qint64 cost = QDateTime::currentMSecsSinceEpoch() - t0;
            std::fprintf(stderr, "[GLASS] 主题计算 %lld ms 有效=%d url=%s\n",
                         static_cast<long long>(cost), glass_.valid() ? 1 : 0, qPrintable(coverUrl_));
        }
        if (glass_.valid()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const QImage &bd = glass_.backdrop();
            if (!bd.isNull()) {                                  // 磨砂底：等比铺满（aspect-fill）
                QSize s = bd.size();
                s.scale(rect().size(), Qt::KeepAspectRatioByExpanding);
                QRect r(QPoint(0, 0), s);
                r.moveCenter(rect().center());
                // 注意：**必须不透明（1.0）** —— 窗口开了 WA_TranslucentBackground 时，
                // GL 层若带 alpha（原来 0.85）在 Wayland 会被合成器丢弃（实测：封面/磨砂底整层消失）。
                painter.setOpacity(1.0);
                painter.drawImage(r, bd);
            }
            if (!glass_.gradient().isNull()) {                    // 主题色渐变（顶亮→底暗）
                painter.setOpacity(0.55);
                painter.drawImage(rect(), glass_.gradient());
                painter.setOpacity(1.0);
            }
            painter.fillRect(rect(), QColor(0, 0, 0, 90));         // 压暗层：保证白字清晰（macOS 同法）
            // 兜底：即使封面尚未就绪，也把 GL 层填成不透明，避免透明窗下整层被丢弃
            const QImage probe = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
            Q_UNUSED(probe)
        }
    }
    // 无封面/无视频时，把 GL 层压成不透明深色（否则透明窗口下这一层会被合成器丢弃）
    if (!cover_.hasImage() && !glassOn_) painter.fillRect(rect(), QColor(18, 18, 18, 255));
    cover_.paint(painter, rect());                            // 专辑封面（音乐场景）
}

void MpvWidget::setCoverArt(const QString &url, const QString &title, const QString &artist) {
    coverUrl_ = url;
    if (url.isEmpty()) { glass_ = ColorTheme(); glassKey_.clear(); }   // 视频：清封面也清玻璃
    cover_.load(url, title, artist);
    update();
}

// ---------------- 字幕/歌词（App 层渲染） ----------------
void MpvWidget::loadSidecarSubtitles(const QString &mediaPath) {
    // 列出**全部**同名轨道（支持 a.zh-Hans.srt / a.en.srt / a.srt），默认加载第一轨
    const QVector<SubtitleTrack> tracks = Subtitles::findSidecarTracks(mediaPath);
    if (tracks.isEmpty()) {
        tracks_.clear();
        trackIndex_ = -1;
        clearSubtitles();
        return;
    }
    setSubtitleTracks(tracks, 0);
}

void MpvWidget::setSubtitleTracks(const QVector<SubtitleTrack> &tracks, int initialIndex) {
    tracks_ = tracks;
    if (tracks_.isEmpty()) {
        trackIndex_ = -1;
        clearSubtitles();
        return;
    }
    const int idx = (initialIndex < 0 || initialIndex >= tracks_.size()) ? 0 : initialIndex;
    applySubtitleTrack(idx);
    if (tracks_.size() > 1) qInfo() << "发现字幕轨" << tracks_.size() << "条（按 C 键切换）";
}

void MpvWidget::addSubtitleTracks(const QVector<SubtitleTrack> &extra) {
    if (extra.isEmpty()) return;
    QVector<SubtitleTrack> sorted = extra;
    // 排序只作用于"新追加的在线轨"，不动已有的本地同名轨（避免改动既有行为）
    std::stable_sort(sorted.begin(), sorted.end(), [](const SubtitleTrack &a, const SubtitleTrack &b) {
        return Subtitles::languageRank(a.label) < Subtitles::languageRank(b.label);
    });
    const int offset = tracks_.size();
    for (const auto &t : sorted) tracks_.append(t);
    if (trackIndex_ < 0) applySubtitleTrack(offset);      // 当前没开字幕 → 自动启用优先级最高的一条
    qInfo() << "已追加在线字幕轨" << sorted.size() << "条，共" << tracks_.size() << "条（按 C 键切换）";
}

void MpvWidget::setSubtitleCues(const QVector<SubtitleCue> &cues, const QString &label, bool karaoke) {
    tracks_.clear();              // 在线歌词不参与本地多轨切换
    trackIndex_ = -1;
    if (cues.isEmpty()) { subs_.clearCues(); return; }
    subs_.setSourceName(label);
    subs_.setCues(cues);
    subs_.setForceHidden(false);
    subs_.setKaraokeMode(karaoke);   // 歌词→居中面板+逐字高亮；字幕→贴底
    qInfo() << "已加载在线歌词:" << label << cues.size() << "行" << (karaoke ? "[歌词模式]" : "");
}

void MpvWidget::applySubtitleTrack(int index) {
    if (index < 0 || index >= tracks_.size()) {
        subs_.clearCues();
        trackIndex_ = -1;
        qInfo() << "字幕: 关闭";
        return;
    }
    const SubtitleTrack &t = tracks_[index];
    // 在线轨（B站 CC）内容在内存里；本地轨才读文件
    QVector<SubtitleCue> cues = t.cues;
    if (cues.isEmpty()) cues = Subtitles::loadFile(t.source);
    if (cues.isEmpty()) {
        subs_.clearCues();
        trackIndex_ = -1;
        qInfo() << "字幕轨加载失败（无内容）:" << t.label;
        return;
    }
    subs_.setSourceName(t.label);
    if (!t.cues.isEmpty()) {
        subs_.setKaraokeMode(t.karaoke);       // 在线轨自带标记（在线歌词=true，在线 CC=false）
    } else {
        // 歌词类外挂（lrc / yrc / qrc）都进"歌词面板 + 逐字高亮"模式；
        // 之前只判断了 .lrc，导致本地 .yrc 走了贴底字幕分支（逐字能力白费）
        const QString ext = QFileInfo(t.source).suffix().toLower();
        subs_.setKaraokeMode(ext == "lrc" || ext == "yrc" || ext == "qrc");
    }
    subs_.setCues(cues);
    trackIndex_ = index;
    qInfo() << "字幕轨已切换:" << t.label << "共" << cues.size() << "条";
}

void MpvWidget::setPreferredSubtitleTrack(int index) {
    // 注意：--sub-track 的解析可能晚于 --open 触发的播放（都是队列调用），
    // 因此这里既记下期望值，也在轨道已存在时**立即应用**，否则设定会失效。
    preferredTrack_ = index;
    if (tracks_.isEmpty()) return;
    if (index < 0) { applySubtitleTrack(-1); return; }
    applySubtitleTrack(qBound(0, index, tracks_.size() - 1));
}

void MpvWidget::cycleSubtitleTrack() {
    if (tracks_.isEmpty()) {           // 只有一条或不支持时，退化为显示/隐藏
        toggleSubtitles();
        return;
    }
    const int next = (trackIndex_ + 1 >= tracks_.size()) ? -1 : trackIndex_ + 1;
    applySubtitleTrack(next);
}

void MpvWidget::adjustSubtitleDelay(double deltaSec) {
    const double d = subs_.delay() + deltaSec;
    subs_.setDelay(d);
    qInfo() << "字幕延迟:" << d << "秒";
}

void MpvWidget::clearSubtitles() {
    subs_.clearCues();
}

void MpvWidget::toggleSubtitles() {
    subs_.setForceHidden(!subs_.forceHidden());
    qInfo() << "字幕显示:" << (subs_.forceHidden() ? "关闭" : "开启");
}

bool MpvWidget::subtitlesAvailable() const {
    return subs_.hasCues();
}

// ② 缓存策略按来源区分（macOS 端实测结论）：
//   网络流：大缓存换来抗抖动（60s / 64MiB）；
//   本地文件：**绝不能用大缓存** —— 拖动进度条会因缓存回填而冻结/错位（macOS 端轮 43 实测）。
//   mpv 要求这些选项在 loadfile **之前**设定，所以每次播放前按来源重设一次。
void MpvWidget::prepareCacheOptions(bool isNetwork) {
    if (!mpv_) return;
    if (isNetwork) {
        mpv_set_option_string(mpv_, "cache", "yes");
        mpv_set_option_string(mpv_, "demuxer-readahead-secs", "60");
        mpv_set_option_string(mpv_, "demuxer-max-bytes", "64MiB");
        mpv_set_option_string(mpv_, "cache-pause", "yes");
    } else {
        mpv_set_option_string(mpv_, "cache", "no");
        mpv_set_option_string(mpv_, "demuxer-readahead-secs", "4");
        mpv_set_option_string(mpv_, "demuxer-max-bytes", "8MiB");
        mpv_set_option_string(mpv_, "cache-pause", "no");
    }
}

void MpvWidget::verifyGpuNextRendering()
{
    if (gpuNextVerified_ || !gpuNext_) return;
    gpuNextVerified_ = true;                       // 只自检一次 ✓
    const QImage img = grabFramebuffer();
    if (img.isNull()) return;
    const QImage sm = img.scaled(64, 36, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                         .convertToFormat(QImage::Format_RGB32);
    double sum = 0, sum2 = 0; int n = 0;
    for (int y = 0; y < sm.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(sm.constScanLine(y));
        for (int x = 0; x < sm.width(); ++x) {
            const double lum = qGray(line[x]);
            sum += lum; sum2 += lum * lum; ++n;
        }
    }
    const double mean = sum / qMax(1, n);
    const double var = qMax(0.0, sum2 / qMax(1, n) - mean * mean);
    std::fprintf(stderr, "[PLAY] gpu-next 渲染自检：亮度均值=%.1f 方差=%.1f（全黑→两者都近 0 ✓）\n", mean, var);
    if (var < 4.0) {
        gpuNext_ = false;
        std::fprintf(stderr, "[PLAY] gpu-next 判定不可用（画面全黑）→ 本次会话切回 libmpv；下次播放生效 ✓\n");
        if (onGpuNextUnusable) onGpuNextUnusable();
    }
}

void MpvWidget::playUrl(const QString &url) {
    // ── gpu-next 守卫（关键 ✓）：**用墙钟定时器**触发，不能用"已绘帧数" ✗ ──
    // 本机实测：无硬件 GL 时 gpu-next「创建成功但输出全黑」✗ → 此时 mpv 不产生帧更新 →
    // paintGL 永远不会被调用 ✗ → 挂在帧计数上的自检永远不会触发 ✓（我第一版就是这个错误 ✓ 已改）
    if (gpuNext_) {
        gpuNextVerified_ = false;
        QTimer::singleShot(4000, this, [this] { verifyGpuNextRendering(); });
    }
    { const QString u = url.isEmpty() ? QString() : url;
      const bool net = u.contains("://") && !u.startsWith("file:");
      prepareCacheOptions(net); }
    QByteArray u = url.toUtf8();
    const char *cmd[] = {"loadfile", u.constData(), nullptr};
    mpv_command(mpv_, cmd);
    qInfo() << "播放:" << url;
}

// 按站点决定 Referer：YouTube 与 B站的 CDN 对来源头校验不同，不能一概而论
static QString refererForUrl(const QString &u) {
    if (u.contains("googlevideo.com") || u.contains("youtube.com")) return "https://www.youtube.com/";
    if (u.contains("bilivideo.com") || u.contains("bilibili.com") || u.contains("hdslb.com"))
        return "https://www.bilibili.com/";
    return QString();
}

void MpvWidget::stop() {
    if (!mpv_) return;
    const char *cmd[] = { "stop", nullptr };
    mpv_command(mpv_, cmd);
    tracks_.clear();
    trackIndex_ = -1;
    clearSubtitles();
}

void MpvWidget::playResolved(const QString &videoUrl, const QString &audioUrl) {
    { const QString u = videoUrl;
      const bool net = u.contains("://") && !u.startsWith("file:");
      prepareCacheOptions(net); }
    // 本地文件：自动列出同名外挂字幕轨（网络直链不适用外挂字幕，清掉旧的）
    if (videoUrl.startsWith('/') && QFile::exists(videoUrl)) loadSidecarSubtitles(videoUrl);
    else { tracks_.clear(); trackIndex_ = -1; clearSubtitles(); }
    // 先按站点设置 Referer（对随后 loadfile 与 audio-add 的请求都生效）
    const QString ref = refererForUrl(videoUrl);
    const QByteArray headers = ref.isEmpty() ? QByteArray() : ("Referer: " + ref).toUtf8();
    mpv_set_option_string(mpv_, "http-header-fields", headers.constData());

    QByteArray v = videoUrl.toUtf8();
    const char *cmd[] = {"loadfile", v.constData(), nullptr};
    mpv_command(mpv_, cmd);
    // keep-open=yes 会让新文件继承"已暂停"状态 → 显式取消暂停（实测踩到：自动续播后停在 0:00 不动）
    QTimer::singleShot(150, this, [this] {
        int no = 0;
        mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &no);
    });
    qInfo() << "播放视频流:" << videoUrl.left(80);
    if (!audioUrl.isEmpty()) {
        QTimer::singleShot(700, this, [this, audioUrl] {
            QByteArray a = audioUrl.toUtf8();
            const char *c[] = {"audio-add", a.constData(), "select", nullptr};   // 必须有 NULL 终止符：缺了会越界 strlen 崩溃（实测）
            mpv_command(mpv_, c);
            qInfo() << "已挂载音轨:" << audioUrl.left(80);
        });
    }
}

// ---------- 属性快照：后台线程采集，GUI 只读快照 ----------
void MpvWidget::startSnapshotter() {
    if (snapThread_.joinable()) return;
    snapStop_ = false;
    snapThread_ = std::thread([this] {
        std::fprintf(stderr, "[SNAPSHOT] 属性快照线程已启动（后台 5Hz，GUI 线程不再直连 mpv）\n");
        while (!snapStop_) {
            captureSnapshot();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));   // 5Hz，开销可忽略
        }
    });
}

void MpvWidget::captureSnapshot() {
    if (!mpv_) return;
    Snapshot s;
    auto dbl = [this](const char *name, double dflt) {
        double d = dflt;
        mpv_get_property(mpv_, name, MPV_FORMAT_DOUBLE, &d);   // 可能阻塞 —— 但这里已经是后台线程
        return d;
    };
    auto flag = [this](const char *name) {
        int v = 0;
        mpv_get_property(mpv_, name, MPV_FORMAT_FLAG, &v);
        return v != 0;
    };
    s.duration = dbl("duration", 0);
    s.position = dbl("time-pos", 0);
    s.panscan = dbl("panscan", 0);
    s.speed = dbl("speed", 1);
    s.volume = (int)dbl("volume", 100);
    s.paused = flag("pause");
    s.eof = flag("eof-reached");
    s.width = (int)dbl("width", 0);
    s.height = (int)dbl("height", 0);
    {
        char *t = mpv_get_property_string(mpv_, "media-title");
        if (t) { s.title = QString::fromUtf8(t); mpv_free(t); }
    }
    std::lock_guard<std::mutex> lk(snapMutex_);
    snap_ = s;
}

// ---------- 播放控制（mpv 属性接口，与 Android 版用法一致） ----------
void MpvWidget::togglePause() {
    int p = 0;
    if (mpv_get_property(mpv_, "pause", MPV_FORMAT_FLAG, &p) < 0) return;
    int np = p ? 0 : 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &np);
}
bool MpvWidget::paused() {
    return snapshot().paused;          // 读快照（GUI 线程绝不直接碰 mpv）
}
double MpvWidget::positionSec() {
    return snapshot().position;        // 读快照（原注释：这里曾因误删 return 造成未定义行为，见踩坑百科 #106）
}

void MpvWidget::setPanscanAsync(double v) {
    if (!mpv_ || !renderReady_) return;                    // 就绪前绝不碰 mpv（实测会挂死 GUI 线程）
    mpv_set_property_async(mpv_, 0, "panscan", MPV_FORMAT_DOUBLE, &v);
}

void MpvWidget::setPanscan(double v) {
    if (!mpv_) return;
    mpv_set_property(mpv_, "panscan", MPV_FORMAT_DOUBLE, &v);
}

double MpvWidget::panscan() const {
    return snapshot().panscan;         // 读快照
}
double MpvWidget::durationSec() {
    return snapshot().duration;        // 读快照
}
void MpvWidget::seekTo(double sec) {
    mpv_set_property(mpv_, "time-pos", MPV_FORMAT_DOUBLE, &sec);
}
int MpvWidget::volume() {
    return snapshot().volume;          // 读快照
}
void MpvWidget::setVolume(int v) {
    double d = v;
    mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &d);
}

bool MpvWidget::eofReached(int *out) {
    if (!mpv_) return false;
    *out = snapshot().eof ? 1 : 0;     // 读快照（原来每次轮询都直连 mpv，起播时会卡 GUI 线程）
    return true;
}

// 倍速（对应 Android 版 0.5x~2.0x）
void MpvWidget::setSpeed(double sp) {
    mpv_set_property(mpv_, "speed", MPV_FORMAT_DOUBLE, &sp);
}
double MpvWidget::speed() {
    return snapshot().speed;           // 读快照
}
