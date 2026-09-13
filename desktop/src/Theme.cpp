#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStringList>

namespace {

// 深空底 + 电蓝主色，与 App 图标同色系
const char *kQss = R"QSS(
* { font-family: "Noto Sans CJK SC", "Source Han Sans SC", "PingFang SC", "Noto Sans", sans-serif; }

QWidget { background: #121212; color: #eaeaea; font-size: 14px; }
QMainWindow, QDialog { background: #121212; }

/* 顶栏与分组容器 */
QFrame#card, QWidget#card { background: #1a1a1a; border: 1px solid #262626; border-radius: 10px; }

/* 输入类：圆角 + 聚焦高亮 */
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: #1e1e1e; border: 1px solid #2e2e2e; border-radius: 8px;
    padding: 6px 10px; selection-background-color: #4c8dff; selection-color: #ffffff;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid #4c8dff; }
QComboBox::drop-down { border: none; width: 20px; }
QComboBox QAbstractItemView {
    background: #1e1e1e; border: 1px solid #2e2e2e; selection-background-color: #2b4a86;
    outline: none;
}

/* 按钮：圆角、hover/按下有明显反馈 */
QPushButton {
    background: #232323; border: 1px solid #333333; border-radius: 8px;
    padding: 7px 14px; color: #eaeaea;
}
QPushButton:hover { background: #2b2b2b; border-color: #3d3d3d; }
QPushButton:pressed { background: #1a1a1a; }
QPushButton:default { background: #2b4a86; border-color: #4c8dff; }
QPushButton:default:hover { background: #33549a; }

/* 列表：去掉白底、条目有内边距、选中用主色 */
QListWidget, QListView {
    background: #161616; border: 1px solid #262626; border-radius: 10px; outline: none;
    padding: 4px;
}
QListWidget::item { padding: 7px 9px; border-radius: 6px; color: #e0e0e0; }
QListWidget::item:hover { background: #222222; }
QListWidget::item:selected { background: #2b4a86; color: #ffffff; }

/* 滑块 */
QSlider::groove:horizontal { height: 5px; background: #2c2c2c; border-radius: 3px; }
QSlider::sub-page:horizontal { background: #4c8dff; border-radius: 3px; }
QSlider::handle:horizontal {
    background: #eaeaea; width: 13px; margin: -5px 0; border-radius: 6px;
}
QSlider::handle:horizontal:hover { background: #ffffff; }

/* 复选与下拉箭头 */
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px;
    border: 1px solid #3a3a3a; background: #1e1e1e; }
QCheckBox::indicator:checked { background: #4c8dff; border-color: #4c8dff; }

/* 分隔条 */
QSplitter::handle { background: #1a1a1a; width: 2px; }
QSplitter::handle:hover { background: #4c8dff; }

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
QMenu::item:selected { background: #2b4a86; }
QToolTip { background: #262626; color: #eaeaea; border: 1px solid #3a3a3a; border-radius: 6px; padding: 6px; }

/* 标签页 / 表单标签 */
QLabel { background: transparent; }
)QSS";

}  // namespace

void Theme::apply(QApplication &app) {
    app.setStyle("Fusion");   // 统一各平台控件绘制，避免系统主题差异

    QPalette pal;
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
    app.setPalette(pal);

    if (!app.font().family().isEmpty()) {
        QFont f = app.font();
        f.setPointSizeF(10.5);
        app.setFont(f);
    }
    app.setStyleSheet(QString::fromUtf8(kQss));
}
