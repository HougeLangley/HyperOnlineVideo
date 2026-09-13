#pragma once
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include "SubtitleOverlay.h"   // 覆盖层状态对象（值成员，需完整类型）
#include "Subtitles.h"   // SubtitleTrack（多字幕轨）
#include "CoverArt.h"    // 专辑封面（值成员）

class SubtitleOverlay;
class QTimer;

// libmpv render API 播放部件（Wayland 下唯一可行的嵌入方式；Spike A 已验证）
class MpvWidget : public QOpenGLWidget {
public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;
    void playUrl(const QString &url);
    // 播放控制（对应 Android 版播放器控件）
    void togglePause();
    bool paused();
    double positionSec();
    double durationSec();
    void seekTo(double sec);
    /** 停止并卸载当前媒体（MPRIS Stop 用） */
    void stop();
    int volume();
    bool eofReached(int *out);
    void setSpeed(double sp);   // 倍速（mpv speed 属性）
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
    /** 字幕/歌词字号倍数（设置页） */
    void setSubtitleFontScale(double s) { subs_.setFontScale(s); }
    void cycleSubtitleTrack();                       // C 键：在「关 → 轨 1 → 轨 2 → … → 关」间循环
    void adjustSubtitleDelay(double deltaSec);        // 逗号/句号：字幕延迟微调
    /** 预设期望的字幕轨序号（-1=关；用于 --sub-track 自动化参数，在加载轨道时生效） */
    void setPreferredSubtitleTrack(int index);
    bool ready() const { return ctx_ != nullptr; }
protected:
    void initializeGL() override;
    void paintGL() override;
private:
    void loadSidecarSubtitles(const QString &mediaPath);
    void clearSubtitles();
    void applySubtitleTrack(int index);               // -1 = 关闭字幕
    mpv_handle *mpv_ = nullptr;
    mpv_render_context *ctx_ = nullptr;
    SubtitleOverlay subs_;   // 值成员：由 paintGL 用 QPainter 绘制（子控件方案实测不显示）
    CoverArt cover_;         // 专辑封面（同样在 paintGL 里画）
    QTimer *subsTimer_ = nullptr;
    QVector<SubtitleTrack> tracks_;
    int trackIndex_ = -1;
    int preferredTrack_ = -2;   // -2=未指定，-1=关闭，>=0=指定轨序号
};
