#pragma once
#include <QtGlobal>
#include <QString>

#include "Subtitles.h"

class QPainter;
class QRect;

/**
 * 字幕 / 歌词覆盖层（桌面端 App 层渲染）
 *
 * 与 Android 版同样不依赖 libmpv 的 libass（Android 侧该构建缺 font provider），
 * 由 App 自己解析 + 自己绘制。
 *
 * 【实现要点：为什么不是子控件】
 *   最初做成 MpvWidget 的子 QWidget，结果**完全不显示**：QOpenGLWidget 上的
 *   半透明子控件（WA_TranslucentBackground）不会与 GL 内容正确合成。
 *   实测把 mpv 的 sub-auto 关掉后屏幕上一条字幕都没有 —— 之前看到的字幕其实
 *   全是 mpv 自己的 libass 渲染，App 层覆盖层一直没生效。
 *   现改为：本类只保存状态并提供 paint(QPainter&)，由 MpvWidget::paintGL 在
 *   mpv 渲染完成后用 QPainter 直接画在同一块 FBO 上（Qt 官方推荐做法）。
 */
class SubtitleOverlay {
public:
    SubtitleOverlay() = default;

    void setCues(const QVector<SubtitleCue> &cues);
    void clearCues();
    /** 由播放器定时调用：更新当前应显示的那一条 */
    void setPosition(double seconds);

    bool hasCues() const { return !cues_.isEmpty(); }
    QString sourceName() const { return sourceName_; }
    void setSourceName(const QString &n) { sourceName_ = n; }

    /** 用户用 S 键临时隐藏/显示（不影响已加载的字幕数据） */
    void setForceHidden(bool hidden) { hidden_ = hidden; }
    bool forceHidden() const { return hidden_; }

    /** 歌词模式（卡拉OK）：居中成面板，当前行按行内进度左→右高亮，并显示上下相邻行 */
    /// 字幕主字号（像素）—— **与 macOS 同一基准**：画面高的 2.8%（下限 13px），再乘用户倍数。
    /// 历史坑：Linux 曾用 height/16（= 6.25%）→ 比 macOS 大近 1.8 倍，用户截图实测"字幕太大"✗。
    static int fontPxFor(int areaHeight, double scale) {
        const double s = qBound(0.5, scale, 2.0);
        return qMax(13, int(double(areaHeight) * 0.028 * s));   // 2.8%：用户反馈"还可以再小" → 向 macOS 观感靠拢
    }

    void setKaraokeMode(bool on) { karaoke_ = on; }
    /** 字号倍数（设置页 0.5~2.0），绘制时统一乘上去 */
    void setFontScale(double s) { fontScale_ = (s < 0.5) ? 0.5 : (s > 2.0 ? 2.0 : s); }
    double fontScale() const { return fontScale_; }
    /** 歌词面板顶部安全区（像素 ✗ 非字幕）：由 MpvWidget 按专辑封面底边传入 ✗
     *  避免歌词大字与左上角封面同高重叠（用户实测反馈 ✓） */
    void setTopSafe(int px) { topSafe_ = (px < 0) ? 0 : px; }
    bool karaokeMode() const { return karaoke_; }

    /** 字幕时间偏移（秒）：正数 = 字幕**延后**出现（做同步校准用） */
    void setDelay(double sec);
    double delay() const { return delay_; }

    /** 在给定区域内绘制当前字幕（含延迟角标）；由 MpvWidget::paintGL 调用 */
    void paint(QPainter &p, const QRect &area) const;

    /** 当前绘制状态的指纹（歌词文本+逐字进度+行号+隐藏）——
     *  MpvWidget 把歌词画进缓存的玻璃图 ✗，缓存 key 必须含它，否则歌词永远停在首帧 ✗（2026-09-25 实测 ✓） */
    QString paintKey() const {
        return (hidden_ ? QStringLiteral("H") : QStringLiteral("V")) + QString::number(index_) + "|"
               + QString::number(int(progress_ * 100.0)) + "|" + current_;
    }

private:
    QVector<SubtitleCue> cues_;
    QString current_;
    QString sourceName_;
    bool hidden_ = false;
    double delay_ = 0.0;
    bool karaoke_ = false;
    double fontScale_ = 1.0;
    int topSafe_ = 0;          // 歌词面板顶部安全区（封面占用高度 ✗ 视频/无封面时为 0 ✓）
    double progress_ = 0.0;    // 当前行已唱比例（0~1），paint 用（无字级时间时的近似）
    SubtitleCue cur_;          // 当前行副本（含 words：有则做真·逐字高亮）
    double curTime_ = 0.0;
    int index_ = -1;           // 当前行下标（用于取上下相邻行）
};
