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
    f.setPixelSize(int(qBound(18, area.height() / 16, 44) * fontScale_));   // 字号随画面高度自适应 × 设置倍数
    f.setBold(true);
    p.setFont(f);

    const int margin = qMax(12, area.height() / 40);
    const int maxW = qMax(40, area.width() - margin * 2);

    // 延迟校准角标（非 0 时显示，便于用户知道当前偏移）
    if (qAbs(delay_) > 0.01) {
        QFont sf = f;
        sf.setPixelSize(qBound(12, area.height() / 34, 22));
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

    // —— 歌词模式：居中面板（上一行暗 / 当前行大且逐字高亮 / 下一行暗） ——
    QStringList cur = current_.split('\n');
    const QString line1 = cur.isEmpty() ? QString() : cur.first();          // 原文
    const QString line2 = cur.size() > 1 ? cur.at(1) : QString();           // 翻译（如有）

    QFont big = f;
    big.setPixelSize(int(qBound(20, area.height() / 13, 52) * fontScale_));
    QFont small = f;
    small.setPixelSize(int(qBound(14, area.height() / 26, 30) * fontScale_));
    small.setBold(false);

    const QString prevLine = (index_ > 0) ? cues_.at(index_ - 1).text.split('\n').first() : QString();
    const QString nextLine = (index_ >= 0 && index_ + 1 < cues_.size())
                                 ? cues_.at(index_ + 1).text.split('\n').first()
                                 : QString();

    const QFontMetrics fmBig(big), fmSmall(small);
    const int gap = qMax(8, area.height() / 60);
    const int h1 = fmSmall.boundingRect(QRect(0, 0, maxW, area.height()), alignWrap, prevLine).height() + 4;
    const int h2 = fmBig.boundingRect(QRect(0, 0, maxW, area.height()), alignWrap, line1).height() + 8;
    const int h3 = (line2.isEmpty() ? 0 : fmSmall.boundingRect(QRect(0, 0, maxW, area.height()), alignWrap, line2).height() + 4);
    const int h4 = fmSmall.boundingRect(QRect(0, 0, maxW, area.height()), alignWrap, nextLine).height() + 4;
    const int totalH = h1 + gap + h2 + (h3 ? gap / 2 + h3 : 0) + gap + h4;
    int y = area.top() + (area.height() - totalH) / 2;   // 垂直居中（歌词面板）

    // 上一行
    if (!prevLine.isEmpty()) {
        p.setFont(small);
        drawOutlined(QRect(area.left() + margin, y, maxW, h1), alignBox, prevLine, QColor(160, 160, 160));
        y += h1 + gap;
    }
    // 当前行：先整行白字，再按进度用高亮色覆盖已唱部分（逐字高亮的常用实现）
    p.setFont(big);
    const QRect curBox(area.left() + margin, y, maxW, h2);
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
        drawOutlined(QRect(area.left() + margin, y, maxW, h3), alignBox, line2, QColor(220, 220, 220));
        y += h3;
    }
    y += gap;
    // 下一行
    if (!nextLine.isEmpty()) {
        p.setFont(small);
        drawOutlined(QRect(area.left() + margin, y, maxW, h4), alignBox, nextLine, QColor(160, 160, 160));
    }
}
