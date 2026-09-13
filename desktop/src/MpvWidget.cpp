#include "MpvWidget.h"
#include "SubtitleOverlay.h"
#include "Subtitles.h"
#include <QDebug>
#include <algorithm>   // std::stable_sort（字幕轨语言优先级排序）
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QtMath>

MpvWidget::MpvWidget(QWidget *parent) : QOpenGLWidget(parent) {
    mpv_ = mpv_create();
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
    // 先摘掉回调，避免 mpv 之后仍回调到将析构的对象（网络播放耗时长时最容易命中）
    if (ctx_) mpv_render_context_set_update_callback(ctx_, nullptr, nullptr);
    if (ctx_) mpv_render_context_free(ctx_);
    if (mpv_) mpv_terminate_destroy(mpv_);
}

void MpvWidget::initializeGL() {
    mpv_opengl_init_params ip{
        [](void *, const char *name) -> void * {
            return (void *)QOpenGLContext::currentContext()->getProcAddress(name);
        }, nullptr};
    char api[] = MPV_RENDER_API_TYPE_OPENGL;              // 注意：是字符串常量 "opengl"
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, api},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &ip},
        {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (mpv_render_context_create(&ctx_, mpv_, params) < 0) {
        qWarning() << "mpv_render_context_create 失败";
        return;
    }
    mpv_render_context_set_update_callback(ctx_, [](void *p) {
        auto *w = static_cast<MpvWidget *>(p);
        QMetaObject::invokeMethod(w, [w] { w->update(); }, Qt::QueuedConnection);
    }, this);
    qInfo() << "mpv render context ready";
    // 注：早期为定位"卡在解析还是渲染"曾在这里挂 30 秒诊断定时器（打印 core-idle 等）。
    // 问题已定位，诊断代码按约定清理 —— 需要时用 mpv 的 log-file（/tmp/hov-mpv.log）即可。
}

void MpvWidget::paintGL() {
    // 与原始 GL 调用混用时，Qt 要求的写法：QPainter + beginNativePainting 包住 GL 段，
    // 之后再回到 QPainter 画字幕（否则字幕完全不显示 —— 实测踩过）。
    QPainter painter(this);
    painter.beginNativePainting();
    if (ctx_) {
        mpv_opengl_fbo fbo{(int)defaultFramebufferObject(),
                           (int)(width() * devicePixelRatio()),
                           (int)(height() * devicePixelRatio())};
        int flip = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip},
            {MPV_RENDER_PARAM_INVALID, nullptr}};
        mpv_render_context_render(ctx_, params);
    }
    painter.endNativePainting();
    if (!subs_.forceHidden()) subs_.paint(painter, rect());   // 字幕/歌词（App 层渲染）
    cover_.paint(painter, rect());                            // 专辑封面（音乐场景）
}

void MpvWidget::setCoverArt(const QString &url, const QString &title, const QString &artist) {
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

void MpvWidget::playUrl(const QString &url) {
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

// ---------- 播放控制（mpv 属性接口，与 Android 版用法一致） ----------
void MpvWidget::togglePause() {
    int p = 0;
    if (mpv_get_property(mpv_, "pause", MPV_FORMAT_FLAG, &p) < 0) return;
    int np = p ? 0 : 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &np);
}
bool MpvWidget::paused() {
    int p = 0;
    mpv_get_property(mpv_, "pause", MPV_FORMAT_FLAG, &p);
    return p != 0;
}
double MpvWidget::positionSec() {
    double d = 0;
    mpv_get_property(mpv_, "time-pos", MPV_FORMAT_DOUBLE, &d);
    return d;
}
double MpvWidget::durationSec() {
    double d = 0;
    mpv_get_property(mpv_, "duration", MPV_FORMAT_DOUBLE, &d);
    return d;
}
void MpvWidget::seekTo(double sec) {
    mpv_set_property(mpv_, "time-pos", MPV_FORMAT_DOUBLE, &sec);
}
int MpvWidget::volume() {
    double d = 100;
    mpv_get_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &d);
    return (int)d;
}
void MpvWidget::setVolume(int v) {
    double d = v;
    mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &d);
}

bool MpvWidget::eofReached(int *out) {
    int v = 0;
    if (mpv_get_property(mpv_, "eof-reached", MPV_FORMAT_FLAG, &v) < 0) return false;
    *out = v;
    return true;
}

// 倍速（对应 Android 版 0.5x~2.0x）
void MpvWidget::setSpeed(double sp) {
    mpv_set_property(mpv_, "speed", MPV_FORMAT_DOUBLE, &sp);
}
double MpvWidget::speed() {
    double d = 1.0;
    mpv_get_property(mpv_, "speed", MPV_FORMAT_DOUBLE, &d);
    return d;
}
