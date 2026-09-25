#include "SubtitleOverlay.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QRect>
#include <QDebug>

void SubtitleOverlay::setCues(const QVector<SubtitleCue> &cues) {
    cues_ = cues;
    current_.clear();
}

void SubtitleOverlay::clearCues() {
    cues_.clear();
    current_.clear();
    sourceName_.clear();
}

void SubtitleOverlay::setDelay(double sec) {
    delay_ = sec;
    current_.clear();   // 强制下一次 setPosition 重算
}

void SubtitleOverlay::setPosition(double seconds) {
    if (cues_.isEmpty() || hidden_) return;
    const double t = seconds - delay_;                     // 正延迟 = 字幕晚出现
    const SubtitleCue *c = Subtitles::cueAt(cues_, t);
    current_ = c ? c->text : QString();
    progress_ = c ? Subtitles::lineProgress(*c, t) : 0.0;
    curTime_ = t;
    cur_ = c ? *c : SubtitleCue{};
    index_ = -1;
    if (c) {
        for (int i = 0; i < cues_.size(); ++i)
            if (&cues_.at(i) == c) { index_ = i; break; }
    }
}

void SubtitleOverlay::paint(QPainter &p, const QRect &area) const {
    if (area.width() <= 0 || area.height() <= 0) return;
    if (current_.isEmpty() && qAbs(delay_) < 0.01) return;

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QFont f = p.font();
    f.setPixelSize(fontPxFor(area.height(), fontScale_));   // 画面高 2.8%、下限 13px（原注释写 3.5%/14px 已过期 ✗）
    f.setBold(true);
    p.setFont(f);

    const int margin = qMax(12, area.height() / 40);
    const int maxW = qMax(40, area.width() - margin * 2);

    // 延迟校准角标（非 0 时显示，便于用户知道当前偏移）
    if (qAbs(delay_) > 0.01) {
        QFont sf = f;
        sf.setPixelSize(qMax(11, fontPxFor(area.height(), fontScale_) * 2 / 3));
        sf.setBold(false);
        p.setFont(sf);
        p.setPen(QColor(255, 214, 102));
        const QString txt = QString("字幕延迟 %1%2s")
                                .arg(delay_ > 0 ? "+" : "")
                                .arg(delay_, 0, 'g', 2);
        p.drawText(QRect(area.left() + margin, area.top() + margin, maxW, area.height() / 12),
                   int(Qt::AlignRight | Qt::AlignTop), txt);
        p.setFont(f);
    }

    if (current_.isEmpty()) return;

    const QFontMetrics fm(f);
    const int alignWrap = int(Qt::AlignHCenter | Qt::TextWordWrap);
    const int alignBox = int(Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap);

    // 描边绘制的小工具：黑底八方向描边 + 指定颜色
    auto drawOutlined = [&p](const QRect &box, int flags, const QString &text, const QColor &color) {
        p.setPen(Qt::black);
        for (int dx = -2; dx <= 2; dx += 2)
            for (int dy = -2; dy <= 2; dy += 2) {
                if (dx == 0 && dy == 0) continue;
                p.drawText(box.translated(dx, dy), flags, text);
            }
        p.setPen(color);
        p.drawText(box, flags, text);
    };

    if (!karaoke_) {
        // —— 字幕模式：贴底、白字黑边（原行为） ——
        QRect br = fm.boundingRect(QRect(0, 0, maxW, area.height()), alignWrap, current_);
        br.setHeight(br.height() + 8);
        const int bottomGap = qMax(28, area.height() / 10);
        const QRect box(area.left() + margin, area.bottom() - bottomGap - br.height(), maxW, br.height());
        drawOutlined(box, alignBox, current_, QColor(255, 255, 255));
        return;
    }

    // —— 歌词模式：面板（上一行暗 / 当前行大且逐字高亮 / 下一行暗） ——
    // 版式（2026-09-25 对齐 macOS 音乐模式 ✓ 用户反馈"太过于居中/可能被封面遮挡"）：
    //   有封面（topSafe_>0，由 MpvWidget 依 cover_.hasImage() 传入 ✓）→ **右列**（44% 起、宽 52%）左对齐
    //     —— 封面在左已放大居中（CoverArt ✓），歌词不再与它同高重叠 ✓
    //   无封面 → 全区居中（与字幕一致，原行为不变 ✓）
    const bool hasCover = topSafe_ > 0;
    const QRect lyricBox = hasCover
        ? QRect(area.left() + int(area.width() * 0.44), area.top() + int(area.height() * 0.10),
                int(area.width() * 0.52), int(area.height() * 0.80))
        : area;
    const int lyricW = qMax(40, lyricBox.width() - margin * 2);
    const int lyricLeft = lyricBox.left() + margin;
    const int lyricAlign = hasCover ? int(Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap) : alignBox;
    QStringList cur = current_.split('\n');
    const QString line1 = cur.isEmpty() ? QString() : cur.first();          // 原文
    const QString line2 = cur.size() > 1 ? cur.at(1) : QString();           // 翻译（如有）

    QFont big = f;
QFont small = f;

const QString prevLine = (index_ > 0) ? cues_.at(index_ - 1).text.split('\n').first() : QString();
const QString nextLine = (index_ >= 0 && index_ + 1 < cues_.size())
                             ? cues_.at(index_ + 1).text.split('\n').first()
                             : QString();

// 字号自适应（用户实测：歌词太长会超出播放窗口 ✗ 2026-09-25）：
//   总高超出面板 → 逐档缩小（至 0.55 倍 ✓）；绘制段引用同一组 h1..h4 ✓
int h1 = 0, h2 = 0, h3 = 0, h4 = 0, totalH = 0;
auto layoutFor = [&](double k) {
    big.setPixelSize(qMax(12, int(int(qBound(20, area.height() / 13, 52)) * fontScale_ * k)));
    small.setPixelSize(qMax(10, int(int(qBound(14, area.height() / 26, 30)) * fontScale_ * k)));
    small.setBold(false);
    const QFontMetrics fb(big), fs(small);
    h1 = fs.boundingRect(QRect(0, 0, lyricW, lyricBox.height()), alignWrap, prevLine).height() + 4;
    h2 = fb.boundingRect(QRect(0, 0, lyricW, lyricBox.height()), alignWrap, line1).height() + 8;
    h3 = (line2.isEmpty() ? 0 : fs.boundingRect(QRect(0, 0, lyricW, lyricBox.height()), alignWrap, line2).height() + 4);
    h4 = fs.boundingRect(QRect(0, 0, lyricW, lyricBox.height()), alignWrap, nextLine).height() + 4;
};
const int gap = qMax(8, area.height() / 60);
for (double k = 1.0; ; k *= 0.86) {
    layoutFor(k);
    totalH = h1 + gap + h2 + (h3 ? gap / 2 + h3 : 0) + gap + h4;
    if (totalH <= lyricBox.height() - 8 || k <= 0.55) break;
}
const QFontMetrics fmBig(big), fmSmall(small);    // 与最终字号一致 ✓（绘制段复用 ✓）

    // 垂直：有封面（右列）→ 列内居中（对齐 macOS 音乐模式）；无封面 → 55% 略偏下（不再呆板居中 ✗）
    const int availTop = lyricBox.top();
    const int availH = qMax(80, lyricBox.height());
    int y = availTop + (availH - totalH) * (hasCover ? 50 : 55) / 100;
    y = qBound(availTop, y, qMax(availTop, lyricBox.bottom() - totalH - 8));

    // 上一行
    if (!prevLine.isEmpty()) {
        p.setFont(small);
        drawOutlined(QRect(lyricLeft, y, lyricW, h1), lyricAlign, prevLine, QColor(160, 160, 160));
        y += h1 + gap;
    }
    // 当前行：先整行白字，再按进度用高亮色覆盖已唱部分（逐字高亮的常用实现）
    p.setFont(big);
    const QRect curBox(lyricLeft, y, lyricW, h2);
    const QColor accent(80, 220, 255);          // 已唱：青蓝
    const QColor plain(255, 255, 255);

    if (!cur_.words.isEmpty()) {
        // —— 真·逐字高亮：按字级时间戳切分"已唱 / 未唱"两段，居中对齐整体绘制 ——
        QString sung, rest;
        for (const auto &w : cur_.words) {
            if (w.start <= curTime_ + 0.001) sung += w.text;
            else rest += w.text;
        }
        const int wSung = fmBig.horizontalAdvance(sung);
        const int wAll = wSung + fmBig.horizontalAdvance(rest);
        int x = curBox.center().x() - wAll / 2;
        const int flagsLeft = int(Qt::AlignLeft | Qt::AlignVCenter);
        if (!sung.isEmpty()) {
            drawOutlined(QRect(x, curBox.top(), wSung + 4, curBox.height()), flagsLeft, sung, accent);
            x += wSung;
        }
        if (!rest.isEmpty())
            drawOutlined(QRect(x, curBox.top(), fmBig.horizontalAdvance(rest) + 4, curBox.height()),
                         flagsLeft, rest, plain);
    } else {
        // —— 只有行级时间：用行内进度做近似（原方案） ——
        drawOutlined(curBox, alignBox, line1, plain);
        if (progress_ > 0.001 && !line1.isEmpty()) {
            p.save();
            QRect clip = curBox;
            clip.setWidth(int(curBox.width() * qBound(0.0, progress_, 1.0)));
            p.setClipRect(clip);
            drawOutlined(curBox, alignBox, line1, accent);
            p.restore();
        }
    }
    y += h2;
    // 翻译行（较小、偏灰）
    if (!line2.isEmpty()) {
        y += gap / 2;
        p.setFont(small);
        drawOutlined(QRect(lyricLeft, y, lyricW, h3), lyricAlign, line2, QColor(220, 220, 220));
        y += h3;
    }
    y += gap;
    // 下一行
    if (!nextLine.isEmpty()) {
        p.setFont(small);
        drawOutlined(QRect(lyricLeft, y, lyricW, h4), lyricAlign, nextLine, QColor(160, 160, 160));
    }
}
