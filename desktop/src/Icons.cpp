#include "Icons.h"
#include "HovLog.h"
#include "Theme.h"

#include <QFont>
#include <QFontDatabase>
#include <QIconEngine>
#include <QPainter>
#include <QPixmap>

namespace {

const QString kOutlined = QStringLiteral("HOV Symbols Outlined");
const QString kFilled   = QStringLiteral("HOV Symbols Filled");

class GlyphEngine : public QIconEngine {
public:
    GlyphEngine(IconId id, Tok role, Tok checkedRole) : id_(id), role_(role), checkedRole_(checkedRole) {}

    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State state) override {
        QColor c = Theme::color(state == QIcon::On ? checkedRole_ : role_);
        if (mode == QIcon::Disabled) c.setAlphaF(0.38);
        Icons::paint(*p, rect, id_, c, state == QIcon::On);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QPixmap pm(size * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }
    QIconEngine *clone() const override { return new GlyphEngine(id_, role_, checkedRole_); }

private:
    IconId id_;
    Tok role_;
    Tok checkedRole_;
};

}  // namespace

bool Icons::loadFonts()
{
    const int a = QFontDatabase::addApplicationFont(QStringLiteral(":/icons/HovSymbolsOutlined.ttf"));
    const int b = QFontDatabase::addApplicationFont(QStringLiteral(":/icons/HovSymbolsFilled.ttf"));
    hovLog("[ICONS] 字体注册 outlined=%d filled=%d\n", a, b);
    return a >= 0 && b >= 0;
}

QIcon Icons::icon(IconId id, Tok role, Tok checkedRole)
{
    return QIcon(new GlyphEngine(id, role, checkedRole));
}

void Icons::paint(QPainter &p, const QRectF &r, IconId id, const QColor &c, bool filled)
{
    QFont f(filled ? kFilled : kOutlined);
    f.setPixelSize(qMax(1, qRound(qMin(r.width(), r.height()))));
    f.setHintingPreference(QFont::PreferNoHinting);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setFont(f);
    p.setPen(c);
    p.drawText(r, Qt::AlignCenter, QString(QChar(char16_t(id))));
    p.restore();
}
