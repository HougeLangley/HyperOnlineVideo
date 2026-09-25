#include "MpvWidget.h"
#include "SubtitleOverlay.h"
#include "Subtitles.h"
#include "Settings.h"
#include <QOpenGLFunctions>
#include <QLabel>
#include <QPainterPath>
#include <QResizeEvent>
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
    // 渲染后端：**Linux 端固定 vo=libmpv**（2026-09-22 实测修正 ✗ 见文档 46/47）
    // 为什么不用 gpu-next（与 macOS 同源缺陷 ✓ 但表现更严重 ✗）：
    //   ① 嵌入矛盾：本部件走 mpv_render_context（GL 渲染 API）✓，而 `vo=gpu-next` 是**窗口式 vo** ✗
    //   ② **会崩溃**：无硬件 GL 环境下实测 3 次里 1 次 SIGSEGV ✓ coredumpctl 栈 #2~#12 全在
    //      libmpv.so.2 的 vo 线程 ✓（si_code=SEGV_MAPERR ✓）—— 与 Android `vo=gpu` 崩溃同族 ✓
    //   ③ 原"起播 4 秒后抓帧算方差"的守卫**救不了崩溃** ✗（崩溃在 vo 初始化期 ✓ 进程直接没了 ✓）
    // 本端**不再提供** video.gpuNext 设置项（2026-09-22 用户决定 ✓ 全仓删除该键 ✓ 见文档 48）；
    // 渲染后端恒定 `vo=libmpv`（理由见上 ✓ 唯一能嵌进宿主控件的路径 ✓）。
    mpv_set_option_string(mpv_, "vo", "libmpv");
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
    if (logThread_.joinable()) logThread_.join();
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
          // 渲染上下文创建失败即**致命** ✗：本部件没有可用回退路径（vo 已恒定 libmpv ✓）
          //（历史上这里有一段"gpu-next 失败就切回 libmpv 重试"的逻辑 ✗ —— 但 mpv 初始化后不能再改 vo ✗，
          //  且现在 vo 恒定 libmpv ✓ → 该分支永远走不到 ✓ 已按死代码清理纪律移除 ✗ 见文档 46/47）
          qWarning() << "mpv_render_context_create 失败（vo=libmpv）：本视图将无画面";
          std::fprintf(stderr, "[PLAY] ✗ 渲染上下文创建失败（vo=libmpv）—— 请检查 OpenGL/EGL 环境\n");
          return;
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
    // ⚠ 2026-09-25 实测修复：**不在此处绘制任何 QPainter 内容** ✗
    //   mpv 渲染后残留的 GL 状态会破坏 Qt 的"几何图形路径"——fillRect / drawImage /
    //   drawPixmap **全部不显示**（只有 glyph 文本侥幸可见 ✗ 诊断彩块测试证实 ✗）。
    //   正确做法（Qt 文档姿势）：QPainter 内容统一放 paintEvent ✓（GL 之后独立光栅合成 ✓
    //   不受 GL 状态影响 ✓ 实测封面/磨砂底/渐变色块全部正常 ✓）。
}

void MpvWidget::paintEvent(QPaintEvent *ev) {
    QOpenGLWidget::paintEvent(ev);   // 基类：驱动 initializeGL / paintGL（mpv 画面渲染）
    QPainter painter(this);
    // 字幕/歌词：QPainter 文本（glyph 路径在软件 GL 下仍可见 ✓ 实测）
    // 歌词/字幕：视频场景（无封面/无玻璃 label）直接在 GL 帧内画（glyph 可见 ✓）；
    // 音乐模式（玻璃 label 覆盖 ✗）改画进玻璃图 —— 见 refreshOverlays（子控件层无法与 GL 帧混合 ✗）
    if (!subs_.forceHidden() && !(glassOn_ && cover_.hasImage())) {
        subs_.setTopSafe(0);
        subs_.paint(painter, rect());
    }
    // ⚠ 封面与玻璃背景**不走 QPainter**（软件 GL 下图片/矩形绘制不显示 ✗ 2026-09-25 实测 ✗）
    //   → 由 QLabel 子控件承载（refreshOverlays ✓ 子控件在 GL 之上且实测可见 ✓✓）
    refreshOverlays();
}

void MpvWidget::resizeEvent(QResizeEvent *ev) {
    QOpenGLWidget::resizeEvent(ev);
    refreshOverlays();   // 尺寸变化后同步子控件几何 ✓
}

void MpvWidget::refreshOverlays() {
    const bool music = cover_.hasImage() && !rect().isEmpty();

    // ── 玻璃背景（磨砂 + 主题色渐变 + 压暗）：合成到一张图 → glassLabel_（全屏、位于封面之下）──
    if (glassOn_ && music) {
        if (!glassLabel_) {
            glassLabel_ = new QLabel(this);
            glassLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            glassLabel_->setScaledContents(true);
        }
        if (!glass_.valid() || glassKey_ != coverUrl_) {
            glass_ = ColorTheme::make(cover_.image());
            glassKey_ = coverUrl_;
        }
        // 底图（磨砂+渐变+压暗）：**只在封面变化时重建** ✓（歌词每帧在它上面叠加 → 不必重复磨砂缩放 ✗）
        if (glass_.valid() && (glassBaseKey_ != coverUrl_ || glassBase_.size() != size())) {
            QImage base(size(), QImage::Format_ARGB32_Premultiplied);
            base.fill(Qt::transparent);
            QPainter g(&base);
            g.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const QImage &bd = glass_.backdrop();
            if (!bd.isNull()) {
                QSize sc = bd.size();
                sc.scale(base.size(), Qt::KeepAspectRatioByExpanding);
                QRect r(QPoint(0, 0), sc);
                r.moveCenter(base.rect().center());
                g.setOpacity(0.93);
                g.drawImage(r, bd);
            }
            if (!glass_.gradient().isNull()) {
                g.setOpacity(0.55);
                g.drawImage(base.rect(), glass_.gradient());
            }
            g.setOpacity(0.38);
            g.fillRect(base.rect(), QColor(0, 0, 0));         // 压暗层（半透明 ✓）
            g.end();
            glassBase_ = base;
            glassBaseKey_ = coverUrl_;
        }
        // 歌词层：key 含**当前歌词状态**（文本+逐字进度+行号 ✗）——
        //   修复 2026-09-25 用户实测：此前 key 只有封面 URL → 歌词凝固在首帧（"作词/作曲"不动 ✗）
        if (glassBase_.size() == size()) {
            subs_.setTopSafe(1);   // >0 = 有封面 → 歌词右列左对齐 ✓
            const QString lyricKey = coverUrl_ + "||" + subs_.paintKey();
            if (glassPixKey_ != lyricKey) {
                QImage canvas = glassBase_.copy();            // 拷贝底图（快 ✓）+ 叠歌词
                if (!subs_.forceHidden()) {
                    QPainter g(&canvas);
                    g.setRenderHint(QPainter::Antialiasing, true);
                    g.setRenderHint(QPainter::TextAntialiasing, true);
                    // 歌词直接画进玻璃图（同一张 raster ✓ 100% 可见 —— 子控件层无法与 GL 帧混合 ✗）
                    subs_.paint(g, canvas.rect());
                }
                glassPixKey_ = lyricKey;
                glassLabel_->setPixmap(QPixmap::fromImage(canvas));
            }
        }
        if (glassLabel_->geometry() != rect()) glassLabel_->setGeometry(rect());
        glassLabel_->show();
        glassLabel_->lower();   // 封面之下（封面会 raise ✓）
    } else if (glassLabel_) {
        glassLabel_->hide();
    }

    // ── 专辑封面（圆角，左侧垂直居中 —— 版式对齐 macOS 音乐模式 ✓）──
    if (music) {
        const int side = qBound(120, qMin(int(height() * 0.60), int(width() * 0.30)),
                                qMax(120, height() - 72));
        if (!coverLabel_) {
            coverLabel_ = new QLabel(this);
            coverLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        }
        if (coverPixKey_ != coverUrl_ || coverLabelSide_ != side) {
            const QImage &im = cover_.image();
            const QImage scaled = im.scaled(side, side, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            const QPoint off((scaled.width() - side) / 2, (scaled.height() - side) / 2);
            QImage out(side, side, QImage::Format_ARGB32_Premultiplied);
            out.fill(Qt::transparent);
            {
                QPainter rp(&out);
                rp.setRenderHint(QPainter::Antialiasing, true);
                rp.setRenderHint(QPainter::SmoothPixmapTransform, true);
                QPainterPath path;
                path.addRoundedRect(QRectF(0, 0, side, side), 12, 12);
                rp.setClipPath(path);
                rp.drawImage(QPoint(0, 0), scaled, QRect(off, QSize(side, side)));
            }
            coverLabel_->setPixmap(QPixmap::fromImage(out));
            coverLabel_->resize(side, side);
            coverPixKey_ = coverUrl_;
            coverLabelSide_ = side;
        }
        const QPoint pos(int(width() * 0.05), (height() - side) / 2);
        if (coverLabel_->pos() != pos) coverLabel_->move(pos);
        coverLabel_->show();
        coverLabel_->raise();

    } else if (coverLabel_) {
        coverLabel_->hide();
    }
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

void MpvWidget::playUrl(const QString &url) {
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

    wantPlaying_ = true;                              // 竞态加固：加载完成后仍 paused 则由快照线程纠正 ✓
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
    // mpv 事件/日志泵（2026-09-25 ✗）：此前**没有任何事件消费者** →
    //   mpv 的加载错误/HTTP 失败/缓冲卡死全被静默吞掉，用户报"随机不播"无从定位（实测复现 ✓）。
    //   warn 常开；HOV_MPV_VERBOSE=1 时 info 级（诊断用，含 pause/loadfile/HTTP 细节）
    if (!logThread_.joinable()) {
        const bool verbose = qEnvironmentVariableIsSet("HOV_MPV_VERBOSE");
        mpv_request_log_messages(mpv_, verbose ? "info" : "warn");
        logThread_ = std::thread([this, verbose] {
            std::fprintf(stderr, "[MPV] 日志泵已启动（级别=%s）\n", verbose ? "info" : "warn");
            while (!snapStop_) {
                mpv_event *ev = mpv_wait_event(mpv_, 0.2);
                if (!ev || ev->event_id == MPV_EVENT_NONE) continue;
                if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
                    auto *m = static_cast<mpv_event_log_message *>(ev->data);
                    std::fprintf(stderr, "[MPV:%s] %s", m->prefix ? m->prefix : "", m->text ? m->text : "");
                }
            }
        });
    }
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
            // keep-open=yes：新文件加载时可能继承上一文件的"已暂停"状态 ✗ → 停在 0:00 不动
        //（用户实测"随机不播"✓；150ms 定时清除存在加载竞态 ✗）→ 加载完成后强制纠正一次 ✓
        if (wantPlaying_.load() && s.paused && s.duration > 0.1 && s.position < 0.5) {
            int no = 0;
            mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &no);
            std::fprintf(stderr, "[PLAY] 检测到新内容继承暂停（keep-open 竞态）→ 已强制取消暂停\n");
            wantPlaying_ = false;
        } else if (wantPlaying_.load() && !s.paused && s.position > 0.5) {
            wantPlaying_ = false;   // 正常播放中 ✓
        }
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
