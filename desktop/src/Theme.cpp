#include "HovLog.h"
#include "Theme.h"
#include "Icons.h"
#include "Ui.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QTemporaryDir>

bool Theme::s_glass = false;
Theme::Mode Theme::s_mode = Theme::Mode::Dark;

namespace {

// 色板：两列一一对应，顺序与 Tok 一致。对比度验算见 design/desktop-design-language.md「色彩」。
constexpr const char *kDark[] = {
    "#0F1115", "#171A1F", "#1E2127", "#282B31", "#33363D",   // Surface … SurfaceHighest
    "#E3E5EB", "#C3C7CF", "#8F939B", "#4A4F57",              // OnSurface, OnSurfaceVariant, Outline, OutlineVariant
    "#8AB4F8", "#0B1B33", "#1E3A5F", "#D3E3FD",              // Primary, OnPrimary, PrimaryContainer, OnPrimaryContainer
    "#F28B82", "#6DD58C", "#000000", "#C3C7CF",              // Error, Success, Scrim, OnScrim
};
constexpr const char *kLight[] = {
    "#F7F8FA", "#FFFFFF", "#EEF0F3", "#E4E7EC", "#D9DDE3",
    "#1A1C20", "#44474E", "#74777F", "#C4C7CE",
    "#0B57D0", "#FFFFFF", "#D3E3FD", "#0B1B33",
    "#B3261E", "#146C2E", "#000000", "#C3C7CF",
};
static_assert(sizeof(kDark) / sizeof(kDark[0]) == int(Tok::Count), "kDark must cover every Tok");
static_assert(sizeof(kLight) / sizeof(kLight[0]) == int(Tok::Count), "kLight must cover every Tok");

// 占位符 → 值。颜色用 Tok 名，尺寸用 M:: 名；模板里不写任何颜色或像素字面量。
QString expand(QString q)
{
    static const struct { const char *key; Tok tok; } kColors[] = {
        {"@surface@", Tok::Surface}, {"@surfaceLow@", Tok::SurfaceLow},
        {"@surfaceContainer@", Tok::SurfaceContainer}, {"@surfaceHigh@", Tok::SurfaceHigh},
        {"@surfaceHighest@", Tok::SurfaceHighest}, {"@onSurface@", Tok::OnSurface},
        {"@onSurfaceVariant@", Tok::OnSurfaceVariant}, {"@outline@", Tok::Outline},
        {"@outlineVariant@", Tok::OutlineVariant}, {"@primary@", Tok::Primary},
        {"@onPrimary@", Tok::OnPrimary}, {"@primaryContainer@", Tok::PrimaryContainer},
        {"@onPrimaryContainer@", Tok::OnPrimaryContainer}, {"@error@", Tok::Error},
        {"@success@", Tok::Success}, {"@scrim@", Tok::Scrim}, {"@onScrim@", Tok::OnScrim},
    };
    static const struct { const char *key; int px; } kMetrics[] = {
        {"@space1@", M::space1}, {"@space2@", M::space2}, {"@space3@", M::space3},
        {"@space4@", M::space4}, {"@space6@", M::space6},
        {"@controlH@", M::controlH}, {"@controlSmallH@", M::controlSmallH},
        {"@controlLargeH@", M::controlLargeH},
        {"@radiusXs@", M::radiusXs}, {"@radiusSm@", M::radiusSm}, {"@radiusMd@", M::radiusMd},
        {"@radiusLg@", M::radiusLg},
        {"@pill@", M::controlH / 2}, {"@pillSmall@", M::controlSmallH / 2},
        {"@pillLarge@", M::controlLargeH / 2}, {"@halfInner@", (M::controlH - 2 * M::space1) / 2 - 2},
        {"@iconSmall@", M::iconSmall}, {"@fontTitle@", M::fontTitle}, {"@fontBody@", M::fontBody}, {"@fontCaption@", M::fontCaption},
    };
    // QSS 的 image: 只认文件：把用到的图标按当前主题渲染成 PNG（含 @2x），放进进程内的临时目录
    static QTemporaryDir imgDir;
    static const struct { const char *key; const char *name; IconId id; } kImages[] = {
        {"@img:expand_more@", "expand_more", IconId::ExpandMore},
        {"@img:expand_less@", "expand_less", IconId::ExpandLess},
    };
    for (const auto &img : kImages) {
        const QString base = QStringLiteral("%1/%2-%3").arg(imgDir.path(), QLatin1String(img.name),
                                                            Theme::isDark() ? "dark" : "light");
        for (int scale : {1, 2}) {
            QPixmap pm(M::iconSmall * scale, M::iconSmall * scale);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            Icons::paint(p, QRectF(0, 0, pm.width(), pm.height()), img.id, Theme::color(Tok::OnSurfaceVariant));
            p.end();
            pm.save(base + (scale == 2 ? QStringLiteral("@2x.png") : QStringLiteral(".png")));
        }
        q.replace(QLatin1String(img.key), base + QStringLiteral(".png"));
    }
    for (const auto &c : kColors) q.replace(QLatin1String(c.key), Theme::hex(c.tok));
    for (const auto &m : kMetrics) q.replace(QLatin1String(m.key), QString::number(m.px));
    return q;
}

const char *kQss = R"QSS(
* { font-family: "Noto Sans CJK SC", "Source Han Sans SC", "PingFang SC", "Noto Sans", sans-serif; }
QWidget { background: transparent; color: @onSurface@; font-size: @fontBody@px; }
QMainWindow, QWidget#hovGallery { background: @surface@; }
QLabel { background: transparent; }
QToolTip { background: @surfaceHighest@; color: @onSurface@; border: none; border-radius: @radiusSm@px; padding: @space2@px; }

/* 面板：结果区、播放条、对话框里的分组 */
QFrame[hov="panel"] { background: @surfaceLow@; border: 1px solid @outlineVariant@; border-radius: @radiusLg@px; }
QDialog { background: @surfaceLow@; }
QDialog QFrame[hov="panel"] { background: @surfaceContainer@; }
QAbstractScrollArea[hov="panel"] { padding: @space2@px; }
QAbstractScrollArea[hov="panel"] > QWidget#qt_scrollarea_viewport, QWidget#hovCanvas { background: transparent; }

/* 文字角色 */
QLabel[hov="title"] { font-size: @fontTitle@px; font-weight: 600; color: @onSurface@; }
QLabel[hov="body"] { font-size: @fontBody@px; color: @onSurface@; }
QLabel[hov="caption"] { font-size: @fontCaption@px; color: @onSurface@; }
QLabel[hov="captionVariant"] { font-size: @fontCaption@px; color: @onSurfaceVariant@; }
QLabel[hov="onScrim"] { font-size: @fontBody@px; color: @onScrim@; }

/* 按钮：四种变体，高度由 Ui.cpp 固定 */
QPushButton { border: none; border-radius: @pill@px; padding: 0px @space4@px; font-weight: 500; }
QPushButton:disabled { color: @outline@; }
QPushButton[hov="filled"] { background: @primary@; color: @onPrimary@; }
QPushButton[hov="filled"]:hover { background: @onPrimaryContainer@; }
QPushButton[hov="filled"]:pressed { background: @primaryContainer@; color: @onPrimaryContainer@; }
QPushButton[hov="tonal"] { background: @surfaceContainer@; color: @onSurface@; border: 1px solid @outlineVariant@; }
QPushButton[hov="tonal"]:hover { background: @surfaceHigh@; }
QPushButton[hov="tonal"]:pressed { background: @surfaceHighest@; }
QPushButton[hov="text"] { background: transparent; color: @primary@; padding: 0px @space3@px; }
QPushButton[hov="text"]:hover { background: @surfaceHigh@; }
QPushButton[hov="icon"] { background: transparent; padding: 0px; }
QPushButton[hov="icon"]:hover { background: @surfaceHigh@; }
QPushButton[hov="icon"]:pressed { background: @surfaceHighest@; }
QPushButton[hov="icon"]:checked { background: @primaryContainer@; }
QPushButton[hov="icon"][size="small"] { border-radius: @pillSmall@px; }
QPushButton[hov="iconFilled"] { background: @primary@; padding: 0px; border-radius: @pillLarge@px; }
QPushButton[hov="iconFilled"]:hover { background: @onPrimaryContainer@; }
QPushButton[hov="chip"] { background: transparent; color: @onSurfaceVariant@; border: 1px solid @outlineVariant@;
    border-radius: @pillSmall@px; padding: 0px @space3@px; }
QPushButton[hov="chip"]:hover { background: @surfaceHigh@; }
QPushButton[hov="chip"]:checked { background: @primaryContainer@; color: @onPrimaryContainer@; border-color: @primaryContainer@; }
QPushButton::menu-indicator { image: none; width: 0px; }

/* 输入框与下拉框 */
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: @surfaceContainer@; color: @onSurface@; border: 1px solid @outlineVariant@;
    border-radius: @pill@px; padding: 0px @space4@px; min-height: @controlH@px;
    selection-background-color: @primary@; selection-color: @onPrimary@;
}
QLineEdit:hover, QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: @outline@; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 2px solid @primary@; }
QLineEdit[hov="search"] { padding-left: @space2@px; }
QComboBox::drop-down { border: none; width: @controlSmallH@px; }
QComboBox::down-arrow, QAbstractSpinBox::down-arrow { image: url(@img:expand_more@); width: @iconSmall@px; height: @iconSmall@px; }
QAbstractSpinBox::up-arrow { image: url(@img:expand_less@); width: @iconSmall@px; height: @iconSmall@px; }
QAbstractSpinBox { padding-right: @controlSmallH@px; }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { subcontrol-origin: padding; width: @iconSmall@px;
    height: @halfInner@px; border: none; background: transparent; margin-right: @space2@px; }
QAbstractSpinBox::up-button { subcontrol-position: top right; margin-top: @space1@px; }
QAbstractSpinBox::down-button { subcontrol-position: bottom right; margin-bottom: @space1@px; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: @surfaceHigh@; border-radius: @radiusXs@px; }
QComboBox QAbstractItemView { background: @surfaceContainer@; color: @onSurface@; border: 1px solid @outlineVariant@;
    selection-background-color: @surfaceHighest@; selection-color: @onSurface@; outline: none; padding: @space1@px; }

/* 复选框与单选框：指示器由 Ui.cpp 的 HovStyle 绘制 */
QCheckBox, QRadioButton { spacing: @space3@px; background: transparent; min-height: @controlSmallH@px; }

/* 列表、表格 */
QListWidget, QListView, QTableWidget {
    background: @surfaceLow@; color: @onSurface@; border: 1px solid @outlineVariant@; border-radius: @radiusMd@px;
    outline: none; padding: @space1@px; gridline-color: transparent;
    alternate-background-color: @surfaceContainer@;
}
QListWidget::item { padding: @space2@px @space3@px; border-radius: @radiusSm@px; }
QListWidget::item:hover { background: @surfaceHigh@; }
QListWidget::item:selected { background: @primaryContainer@; color: @onPrimaryContainer@; }
QTableWidget::item { padding: @space1@px @space2@px; }
QTableWidget::item:selected { background: @primaryContainer@; color: @onPrimaryContainer@; }
QHeaderView::section { background: @surfaceLow@; color: @onSurfaceVariant@; border: none;
    border-bottom: 1px solid @outlineVariant@; padding: @space2@px; font-size: @fontCaption@px; }
QTableCornerButton::section { background: @surfaceLow@; border: none; }

/* 进度条：state 属性取 running / done / failed */
QProgressBar { background: @surfaceHighest@; border: none; border-radius: @radiusXs@px; }
QProgressBar::chunk { background: @primary@; border-radius: @radiusXs@px; }
QProgressBar[state="done"]::chunk { background: @success@; }
QProgressBar[state="failed"]::chunk { background: @error@; }

/* 滑块 */
QSlider { background: transparent; min-height: @space6@px; }
QSlider::groove:horizontal { height: @space1@px; background: @surfaceHighest@; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @primary@; border-radius: 2px; }
QSlider::handle:horizontal { background: @primary@; width: @space4@px; height: @space4@px; margin: -6px 0; border-radius: @space2@px; }

/* 分隔条 */
QSplitter::handle { background: transparent; }
QSplitter::handle:hover { background: @outlineVariant@; }

/* 滚动条 */
QScrollBar:vertical { background: transparent; width: @space3@px; margin: @space1@px 2px; }
QScrollBar::handle:vertical { background: @outlineVariant@; border-radius: @space1@px; min-height: @space6@px; }
QScrollBar::handle:vertical:hover { background: @outline@; }
QScrollBar:horizontal { background: transparent; height: @space3@px; margin: 2px @space1@px; }
QScrollBar::handle:horizontal { background: @outlineVariant@; border-radius: @space1@px; min-width: @space6@px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0px; width: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* 菜单 */
QMenu { background: @surfaceContainer@; color: @onSurface@; border: 1px solid @outlineVariant@; border-radius: @radiusSm@px; padding: @space1@px; }
QMenu::item { padding: @space2@px @space4@px @space2@px @space3@px; border-radius: @radiusSm@px; min-height: @space6@px; }
QMenu::item:selected { background: @surfaceHigh@; }
QMenu::icon { padding-left: @space2@px; }
QMenu::separator { height: 1px; background: @outlineVariant@; margin: @space1@px @space2@px; }

/* 视频区 */
QOpenGLWidget { background: @scrim@; }
)QSS";

// 玻璃模式（ui.glassWindow=on，仅深色）：追加在主样式之后，同优先级的后写规则生效。
const char *kGlassQss = R"QSS(
QWidget { background: transparent; }
QMainWindow { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(30, 34, 42, 0.82), stop:1 rgba(10, 11, 15, 0.70)); }
QDialog { background: @surfaceLow@; }
QFrame[hov="panel"] { background: rgba(23, 26, 31, 0.62); border: 1px solid rgba(255, 255, 255, 0.10); }
QOpenGLWidget { background: @scrim@; }
)QSS";

}  // namespace

QColor Theme::color(Tok t)
{
    const int i = int(t);
    if (i < 0 || i >= int(Tok::Count)) return QColor();
    return QColor(QLatin1String(s_mode == Mode::Light ? kLight[i] : kDark[i]));
}

void Theme::apply(QApplication &app, Mode m)
{
    s_mode = m;
    hovLog("[THEME] apply(mode=%s glass=%d)\n", m == Mode::Light ? "light" : "dark", s_glass ? 1 : 0);
    Ui::installStyle(app);

    QPalette pal;
    pal.setColor(QPalette::Window, color(Tok::Surface));
    pal.setColor(QPalette::WindowText, color(Tok::OnSurface));
    pal.setColor(QPalette::Base, color(Tok::SurfaceContainer));
    pal.setColor(QPalette::AlternateBase, color(Tok::SurfaceLow));
    pal.setColor(QPalette::Text, color(Tok::OnSurface));
    pal.setColor(QPalette::Button, color(Tok::SurfaceContainer));
    pal.setColor(QPalette::ButtonText, color(Tok::OnSurface));
    pal.setColor(QPalette::Highlight, color(Tok::Primary));
    pal.setColor(QPalette::HighlightedText, color(Tok::OnPrimary));
    pal.setColor(QPalette::ToolTipBase, color(Tok::SurfaceHighest));
    pal.setColor(QPalette::ToolTipText, color(Tok::OnSurface));
    pal.setColor(QPalette::PlaceholderText, color(Tok::OnSurfaceVariant));
    pal.setColor(QPalette::Link, color(Tok::Primary));
    app.setPalette(pal);

    QFont f = app.font();
    f.setPixelSize(M::fontBody);
    app.setFont(f);

    QString qss = expand(QString::fromUtf8(kQss));
    if (s_glass && m == Mode::Dark) qss += expand(QString::fromUtf8(kGlassQss));
    app.setStyleSheet(qss);
}

void Theme::setMode(Mode m)
{
    s_mode = m;
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) apply(*app, m);
}

bool Theme::parseMode(const QString &name, Mode *out)
{
    const QString n = name.trimmed().toLower();
    if (n == "dark") { if (out) *out = Mode::Dark; return true; }
    if (n == "light") { if (out) *out = Mode::Light; return true; }
    return false;   // auto 由调用方处理
}

void Theme::setGlass(bool on)
{
    s_glass = on;
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) apply(*app, s_mode);
}
