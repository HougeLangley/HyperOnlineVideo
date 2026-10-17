#include "Ui.h"
#include "Icons.h"
#include "Theme.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleFactory>
#include <QStyleOption>

namespace {

constexpr int kCheckBox = 18;   // 复选框边长
constexpr int kRadio    = 20;   // 单选框直径

void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

QPushButton *makeButton(const char *variant, const QString &text, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setProperty("hov", variant);
    b->setFixedHeight(M::controlH);
    b->setCursor(Qt::PointingHandCursor);
    b->setAutoDefault(false);
    return b;
}

void giveIcon(QPushButton *b, IconId icon, Tok role, Tok checkedRole, int px)
{
    b->setProperty("hovIconRole", int(role));
    b->setProperty("hovIconCheckedRole", int(checkedRole));
    b->setIcon(Icons::icon(icon, role, checkedRole));
    b->setIconSize(QSize(px, px));
}

class HovStyle : public QProxyStyle {
public:
    HovStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    int pixelMetric(PixelMetric m, const QStyleOption *opt, const QWidget *w) const override {
        switch (m) {
        case PM_IndicatorWidth: case PM_IndicatorHeight: return kCheckBox;
        case PM_ExclusiveIndicatorWidth: case PM_ExclusiveIndicatorHeight: return kRadio;
        case PM_SmallIconSize: case PM_ButtonIconSize: return M::iconSmall;
        default: return QProxyStyle::pixelMetric(m, opt, w);
        }
    }

    QIcon standardIcon(StandardPixmap sp, const QStyleOption *opt, const QWidget *w) const override {
        if (sp == SP_LineEditClearButton) return Icons::icon(IconId::Close);
        return QProxyStyle::standardIcon(sp, opt, w);
    }

    void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p, const QWidget *w) const override {
        const bool on = opt->state & State_On;
        const bool enabled = opt->state & State_Enabled;
        auto tone = [enabled](Tok t) { QColor c = Theme::color(t); if (!enabled) c.setAlphaF(0.38); return c; };
        switch (pe) {
        case PE_IndicatorCheckBox: {
            const QRectF r = QRectF(opt->rect).adjusted(1, 1, -1, -1);
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            if (on) {
                p->setPen(Qt::NoPen);
                p->setBrush(tone(Tok::Primary));
                p->drawRoundedRect(r, M::radiusXs, M::radiusXs);
                Icons::paint(*p, r, IconId::Check, tone(Tok::OnPrimary));
            } else {
                p->setPen(QPen(tone(Tok::Outline), 2));
                p->setBrush(Qt::NoBrush);
                p->drawRoundedRect(r.adjusted(1, 1, -1, -1), M::radiusXs - 1, M::radiusXs - 1);
            }
            p->restore();
            return;
        }
        case PE_IndicatorRadioButton: {
            const QRectF r = QRectF(opt->rect).adjusted(2, 2, -2, -2);
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            p->setPen(QPen(tone(on ? Tok::Primary : Tok::Outline), 2));
            p->setBrush(Qt::NoBrush);
            p->drawEllipse(r);
            if (on) {
                p->setPen(Qt::NoPen);
                p->setBrush(tone(Tok::Primary));
                p->drawEllipse(r.center(), r.width() / 4, r.height() / 4);
            }
            p->restore();
            return;
        }
        case PE_IndicatorArrowDown:
        case PE_IndicatorSpinDown:
        case PE_IndicatorSpinMinus:
            Icons::paint(*p, opt->rect, IconId::ExpandMore, tone(Tok::OnSurfaceVariant));
            return;
        case PE_IndicatorArrowUp:
        case PE_IndicatorSpinUp:
        case PE_IndicatorSpinPlus:
            Icons::paint(*p, opt->rect, IconId::ExpandLess, tone(Tok::OnSurfaceVariant));
            return;
        default:
            QProxyStyle::drawPrimitive(pe, opt, p, w);
        }
    }
};

}  // namespace

QPushButton *Ui::filledButton(const QString &text, QWidget *parent)
{
    return makeButton("filled", text, parent);
}

QPushButton *Ui::filledButton(IconId icon, const QString &text, QWidget *parent)
{
    auto *b = makeButton("filled", text, parent);
    giveIcon(b, icon, Tok::OnPrimary, Tok::OnPrimary, M::iconSmall);
    return b;
}

QPushButton *Ui::tonalButton(const QString &text, QWidget *parent)
{
    return makeButton("tonal", text, parent);
}

QPushButton *Ui::tonalButton(IconId icon, const QString &text, QWidget *parent)
{
    auto *b = makeButton("tonal", text, parent);
    giveIcon(b, icon, Tok::OnSurface, Tok::OnSurface, M::iconSmall);
    return b;
}

QPushButton *Ui::textButton(const QString &text, QWidget *parent)
{
    return makeButton("text", text, parent);
}

QPushButton *Ui::iconButton(IconId icon, const QString &tooltip, QWidget *parent, IconSize size, bool checkable)
{
    const bool small = size == IconSize::Small;
    const int h = small ? M::controlSmallH : M::controlH;
    auto *b = makeButton("icon", QString(), parent);
    b->setProperty("size", small ? "small" : "standard");
    b->setFixedSize(h, h);
    b->setCheckable(checkable);
    b->setToolTip(tooltip);
    b->setAccessibleName(tooltip);
    giveIcon(b, icon, Tok::OnSurfaceVariant, Tok::OnPrimaryContainer, small ? M::iconSmall : M::iconStandard);
    return b;
}

QPushButton *Ui::playButton(QWidget *parent)
{
    auto *b = makeButton("iconFilled", QString(), parent);
    b->setFixedSize(M::controlLargeH, M::controlLargeH);
    b->setToolTip(QStringLiteral("播放/暂停（空格）"));
    b->setAccessibleName(QStringLiteral("播放/暂停"));
    giveIcon(b, IconId::PlayArrow, Tok::OnPrimary, Tok::OnPrimary, M::iconLarge);
    return b;
}

QPushButton *Ui::chip(IconId icon, const QString &text, QWidget *parent)
{
    auto *b = makeButton("chip", text, parent);
    b->setFixedHeight(M::controlSmallH);
    b->setCheckable(true);
    giveIcon(b, icon, Tok::OnSurfaceVariant, Tok::OnPrimaryContainer, M::iconSmall);
    return b;
}

void Ui::setIcon(QPushButton *b, IconId icon)
{
    const Tok role = Tok(b->property("hovIconRole").toInt());
    const Tok checked = Tok(b->property("hovIconCheckedRole").toInt());
    b->setIcon(Icons::icon(icon, role, checked));
}

QLineEdit *Ui::searchField(const QString &placeholder, QWidget *parent)
{
    auto *e = new QLineEdit(parent);
    e->setProperty("hov", "search");
    e->setPlaceholderText(placeholder);
    e->setFixedHeight(M::controlH);
    e->setClearButtonEnabled(true);
    e->addAction(Icons::icon(IconId::Search), QLineEdit::LeadingPosition);
    return e;
}

void Ui::asSelect(QComboBox *box)
{
    box->setProperty("hov", "select");
    box->setFixedHeight(M::controlH);
    box->setCursor(Qt::PointingHandCursor);
    repolish(box);
}

QFrame *Ui::panel(QWidget *parent)
{
    auto *f = new QFrame(parent);
    f->setProperty("hov", "panel");
    return f;
}

QLabel *Ui::label(const QString &text, TextRole role, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    setTextRole(l, role);
    return l;
}

void Ui::setTextRole(QLabel *l, TextRole role)
{
    static const char *kNames[] = {"title", "body", "caption", "captionVariant", "onScrim"};
    l->setProperty("hov", kNames[int(role)]);
    repolish(l);
}

QAction *Ui::menuAction(QMenu *menu, IconId icon, const QString &text)
{
    return menu->addAction(Icons::icon(icon, Tok::OnSurfaceVariant), text);
}

QProgressBar *Ui::progressBar(QWidget *parent)
{
    auto *bar = new QProgressBar(parent);
    bar->setTextVisible(false);
    bar->setFixedHeight(M::space2);
    bar->setRange(0, 100);
    setProgressState(bar, ProgressState::Running);
    return bar;
}

void Ui::setProgressState(QProgressBar *bar, ProgressState s)
{
    static const char *kNames[] = {"running", "done", "failed"};
    if (bar->property("state").toString() == QLatin1String(kNames[int(s)])) return;
    bar->setProperty("state", kNames[int(s)]);
    repolish(bar);
}

void Ui::installStyle(QApplication &app)
{
    static bool installed = false;
    if (installed) return;
    installed = true;
    app.setStyle(new HovStyle);
}
