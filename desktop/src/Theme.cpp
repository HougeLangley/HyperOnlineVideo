#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <cstdio>
#include <QStringList>

bool Theme::s_glass = false;   // 定义（头文件里声明）
Theme::Mode Theme::s_mode = Theme::Mode::Dark;

namespace {

// 深空底 + 电蓝主色，与 App 图标同色系
// 玻璃模式（ui.glassWindow=on）：只替换"大面积底色"那几处，控件配色不动 —— 保证文字对比度
static QString glassify(QString q)
{
    q.replace("QWidget { background: #121212; color: #eaeaea; font-size: 14px; }",
              "QWidget { background: transparent; color: #eaeaea; font-size: 14px; }\n"
              // ⚠️ 关键：QOpenGLWidget（mpv 画面）**必须显式给不透明背景** ——
              // 它继承 QWidget 的 transparent 后，Wayland 下 GL 层会被合成器整层丢弃
              //（实测：封面/磨砂底/视频画面全部消失，只剩 App 层文字）。
              "QOpenGLWidget { background: #101216; }");
    // 窗口：竖向渐变（上亮下暗）—— 玻璃材质的"体"（纯色半透明会像"透明窟窿"）
    q.replace("QMainWindow, QDialog { background: #121212; }",
              "QMainWindow { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
              " stop:0 rgba(30, 34, 42, 0.82), stop:1 rgba(10, 11, 15, 0.70)); }\n"
              "QDialog { background: #121212; }");
    // 卡片：半透明 + 上亮下暗 + 亮边框（"面"的立体感，macOS 材质同法）
    q.replace("QFrame#card, QWidget#card { background: #1a1a1a; border: 1px solid #262626; border-radius: 10px; }",
              "QFrame#card, QWidget#card { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
              " stop:0 rgba(52, 57, 68, 0.62), stop:1 rgba(26, 28, 34, 0.58));"
              " border: 1px solid rgba(255, 255, 255, 0.11); border-radius: 10px; }");
    q.replace("background: #161616; border: 1px solid #262626; border-radius: 10px; outline: none;",
              "background: rgba(18, 20, 25, 0.46); border: 1px solid rgba(255, 255, 255, 0.09); border-radius: 10px; outline: none;");
    q.replace("background: #1e1e1e; border: 1px solid #2e2e2e; border-radius: 8px;",
              "background: rgba(40, 44, 52, 0.58); border: 1px solid rgba(255,255,255,0.10); border-radius: 8px;");
    q.replace("background: #1e1e1e; border: 1px solid #2e2e2e; selection-background-color: #274A94;",
              "background: rgba(30, 32, 38, 0.62); border: 1px solid rgba(255,255,255,0.08); selection-background-color: #274A94;");
    return q;
}

// 浅色化：只做**颜色替换**（几何/间距/圆角/字号一律不动 ✗ 避免"换主题就变形"）
static QString lightify(QString q)
{
    struct Pair { const char *from, *to; };
    static const Pair kMap[] = {
        { "#121212", "#f4f5f7" },   // 画布/窗口
        { "#161616", "#ffffff" },   // 列表
        { "#1a1a1a", "#ffffff" },   // 卡片
        { "#1e1e1e", "#ffffff" },   // 输入/按钮
        { "#222222", "#eef0f3" },   // hover
        { "#232323", "#ffffff" },   // 按钮
        { "#262626", "#dcdee3" },   // 边框
        { "#2b2b2b", "#e8eaee" },   // 按钮 hover
        { "#2c2c2c", "#d7dae0" },   // 滑轨
        { "#2e2e2e", "#d3d6db" },   // 边框
        { "#333333", "#c9cdd4" },
        { "#3a3a3a", "#c9cdd4" },
        { "#3d3d3d", "#c2c6cd" },
        { "#454545", "#b8bcc4" },
        { "#8b93a3", "#5b6169" },   // 次要文字
        { "#9aa0a6", "#5b6169" },
        { "#e0e0e0", "#24262b" },
        { "#eaeaea", "#1b1d21" },   // 主文字
    };
    for (const auto &p : kMap) q.replace(QLatin1String(p.from), QLatin1String(p.to));
    return q;   // 强调色 #3869D3 / 选中底 #274A94 / 纯白文字保持不动 ✓
}

const char *kQss = R"QSS(
* { font-family: "Noto Sans CJK SC", "Source Han Sans SC", "PingFang SC", "Noto Sans", sans-serif; }

QWidget { background: #121212; color: #eaeaea; font-size: 14px; }
QMainWindow, QDialog { background: #121212; }

/* 顶栏与分组容器 */
QFrame#card, QWidget#card { background: #1a1a1a; border: 1px solid #262626; border-radius: 10px; }

/* 输入类：圆角 + 聚焦高亮 */
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: #1e1e1e; border: 1px solid #2e2e2e; border-radius: 8px;
    padding: 6px 10px; selection-background-color: #3869D3; selection-color: #ffffff;
}
QComboBox::drop-down { border: none; width: 20px; }
QComboBox QAbstractItemView {
    background: #1e1e1e; border: 1px solid #2e2e2e; selection-background-color: #274A94;
    outline: none;
}

/* 按钮：圆角、hover/按下有明显反馈 */
QPushButton {
    background: #232323; border: 1px solid #333333; border-radius: 8px;
    padding: 7px 14px; color: #eaeaea;
}
QPushButton:hover { background: #2b2b2b; border-color: #3d3d3d; }
QPushButton:pressed { background: #1a1a1a; }
QPushButton:default { background: #274A94; border-color: #3869D3; }
/* R1 ✓ 面板按钮（应用过滤器/取消）也统一胶囊（高 28 ✓）*/
QDialog QPushButton { border-radius: 14px; padding: 2px 18px; min-height: 26px; }   /* R2 ✓ 与胶囊一致（纵向收紧 ✓） */
QPushButton:default:hover { background: #33549a; }

/* ── UI-2（文档 61 ✓）：与 macOS 对齐的三条 ★ 色值均为 macOS 截图**像素实测** ── */
/* ① 主按钮（顶栏「搜索」）：实心 #3869D3 ✓ 白字 ✓ 圆角 ✓ */
QPushButton#primaryBtn {
    background: #3869D3; color: #ffffff; border: none; border-radius: 14px;
    padding: 0px 18px; min-height: 28px; font-weight: 600;   /* R2 ✓ 纵向 0（同上原因 ✗→✓） */
}
QPushButton#primaryBtn:hover { background: #4474DB; }
QPushButton#primaryBtn:pressed { background: #2C55AC; }
/* ② 图标按钮（下载/更多/登录）：圆形 ✓（28x28 → 半径 14 ✓）透明底 + 细边框 ✓ */
/* R1 ✓ 胶囊化（用户 2026-09-23：圆角方形 → "两端全圆"）：
   · 图标按钮 = 正圆（28×28 半径 14 ✓ 见 #iconBtn）
   · 带文字的按钮/下拉/输入框 = 胶囊（高 28 → 半径 14 = 两端全圆 ✓）
   —— 全部控件统一 **高 28** ✓ 唯一来源 ✓ #135 */
QPushButton#pillBtn, QComboBox#pillBox, QLineEdit#pillField {
    border-radius: 14px;
    padding: 0px 12px;            /* R2 ✓ 纵向 0：否则 6px 基础 padding + 28 固定高 → 文字上半截被切 ✗（用户截图实测） */
    min-height: 28px;
}
QComboBox#pillBox::drop-down { width: 22px; border: none; }
QComboBox#pillBox { padding-left: 10px; }
QPushButton#iconBtn {
    background: transparent; border: 1px solid #3d3d3d; border-radius: 14px;
    padding: 0px; min-width: 28px; min-height: 28px;   /* R2 ✓ 圆：等宽高 ✓ */
}
QPushButton#iconBtn:hover { background: #2b2b2b; border-color: #4a4a4a; }
QPushButton#iconBtn:pressed { background: #1a1a1a; }
QPushButton#iconBtn::menu-indicator { image: none; width: 0px; }   /* 去掉 Qt 自带 ▾（图标按钮保持圆形 ✓） */
QPushButton#iconBtn:checked { background: rgba(56, 105, 211, 0.28); border-color: #3869D3; }   /* UI-4 ✓ 开启态可见（连播/铺满 ✓） */
QPushButton#iconBtn:checked:hover { background: rgba(56, 105, 211, 0.40); }
/* ③ 搜索框聚焦：macOS 实测的钢蓝边框 #4B779F ✓（顶栏搜索框与全部输入框统一 ✓） */
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid #4B779F; }
QLineEdit#searchField { padding-left: 6px; }

/* 列表：去掉白底、条目有内边距、选中用主色 */
QListWidget, QListView {
    background: #161616; border: 1px solid #262626; border-radius: 10px; outline: none;
    padding: 4px;
}
QListWidget::item { padding: 7px 9px; border-radius: 6px; color: #e0e0e0; }
QListWidget::item:hover { background: #222222; }
QListWidget::item:selected { background: #274A94; color: #ffffff; }

/* 滑块 */
QSlider::groove:horizontal { height: 5px; background: #2c2c2c; border-radius: 3px; }
QSlider::sub-page:horizontal { background: #3869D3; border-radius: 3px; }
QSlider::handle:horizontal {
    background: #eaeaea; width: 13px; margin: -5px 0; border-radius: 6px;
}
QSlider::handle:horizontal:hover { background: #ffffff; }

/* 复选与下拉箭头 */
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px;
    border: 1px solid #3a3a3a; background: #1e1e1e; }
QCheckBox::indicator:checked { background: #3869D3; border-color: #3869D3; }

/* 分隔条 */
QSplitter::handle { background: #1a1a1a; width: 2px; }
QSplitter::handle:hover { background: #3869D3; }

/* 滚动条：细、深色 */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #333333; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #454545; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #333333; border-radius: 5px; min-width: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* 菜单与提示 */
QMenu { background: #1e1e1e; border: 1px solid #2e2e2e; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 7px 18px; border-radius: 6px; }
QMenu::item:selected { background: #274A94; }
QToolTip { background: #262626; color: #eaeaea; border: 1px solid #3a3a3a; border-radius: 6px; padding: 6px; }

/* 标签页 / 表单标签 */
QLabel { background: transparent; }
)QSS";

}  // namespace

void Theme::apply(QApplication &app, Mode m) {
    s_mode = m;
    std::fprintf(stderr, "[THEME] apply(mode=%s glass=%d)\n", m == Mode::Light ? "light" : "dark", s_glass ? 1 : 0);
    app.setStyle("Fusion");   // 统一各平台控件绘制，避免系统主题差异

    QPalette pal;
    if (m == Mode::Light) {
        pal.setColor(QPalette::Window, QColor(244, 245, 247));
        pal.setColor(QPalette::WindowText, QColor(27, 29, 33));
        pal.setColor(QPalette::Base, QColor(255, 255, 255));
        pal.setColor(QPalette::AlternateBase, QColor(248, 249, 251));
        pal.setColor(QPalette::Text, QColor(27, 29, 33));
        pal.setColor(QPalette::Button, QColor(255, 255, 255));
        pal.setColor(QPalette::ButtonText, QColor(27, 29, 33));
        pal.setColor(QPalette::Highlight, QColor(76, 141, 255));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
        pal.setColor(QPalette::ToolTipText, QColor(27, 29, 33));
        pal.setColor(QPalette::PlaceholderText, QColor(120, 126, 136));
    } else {
        pal.setColor(QPalette::Window, QColor(18, 18, 18));
        pal.setColor(QPalette::WindowText, QColor(234, 234, 234));
        pal.setColor(QPalette::Base, QColor(22, 22, 22));
        pal.setColor(QPalette::AlternateBase, QColor(26, 26, 26));
        pal.setColor(QPalette::Text, QColor(234, 234, 234));
        pal.setColor(QPalette::Button, QColor(35, 35, 35));
        pal.setColor(QPalette::ButtonText, QColor(234, 234, 234));
        pal.setColor(QPalette::Highlight, QColor(76, 141, 255));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::ToolTipBase, QColor(38, 38, 38));
        pal.setColor(QPalette::ToolTipText, QColor(234, 234, 234));
        pal.setColor(QPalette::PlaceholderText, QColor(140, 140, 140));
    }
    app.setPalette(pal);

    if (!app.font().family().isEmpty()) {
        QFont f = app.font();
        f.setPointSizeF(10.5);
        app.setFont(f);
    }

    // ⚠️ 样式表必须**单点决定**（这次踩的坑 ✗）：
    //    原来浅色分支里 setStyleSheet(lightify(...)) 后 return ✓，但紧接着构造里又调 Theme::setGlass(true)
    //    → glassify() 里的**深色 rgba(...)** 把 lightify 的结果又盖回去 ✗ → 于是"浅色模式下背景仍是深色" ✗✓
    //    正解：浅色 = lightify（不套玻璃 ✓ 浅色玻璃是另一套设计，暂不做 ✗）；深色 = 可选 glassify ✓
    const QString base = QString::fromUtf8(kQss);
    const QString qss = (m == Mode::Light) ? lightify(base) : (s_glass ? glassify(base) : base);
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
    return false;   // auto 由调用方处理（不在本函数的语义内 ✓）
}
void Theme::setGlass(bool on)
{
    s_glass = on;
    // 重放"当前模式 + 当前玻璃开关"（不要只重刷玻璃 ✗ 那样会把浅色主题盖回深色 —— 本次踩到的坑 ✓）
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) apply(*app, s_mode);
}

