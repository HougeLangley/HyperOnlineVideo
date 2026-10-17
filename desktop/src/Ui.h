#pragma once
#include "IconIds.h"
#include "Tokens.h"

#include <QString>

class QAction;
class QApplication;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QPushButton;
class QWidget;

// 组件库：界面上的按钮、输入框、面板、文字只从这里建。尺寸取 Tokens.h，外观在 Theme.cpp 的 QSS 按 hov 属性匹配。
namespace Ui {

enum class IconSize { Small, Standard };                 // 32/20、40/24
enum class TextRole { Title, Body, Caption, CaptionVariant, OnScrim };   // OnScrim：黑色视频区上的文字
enum class ProgressState { Running, Done, Failed };

// 主操作，一个界面最多一个（搜索、应用过滤器、保存登录态）
QPushButton *filledButton(const QString &text, QWidget *parent);
QPushButton *filledButton(IconId icon, const QString &text, QWidget *parent);
// 次要操作（面板里的批量动作、浏览…）
QPushButton *tonalButton(const QString &text, QWidget *parent);
QPushButton *tonalButton(IconId icon, const QString &text, QWidget *parent);
// 退出类操作（取消、关闭）
QPushButton *textButton(const QString &text, QWidget *parent);
// 只有图标的按钮；tooltip 同时作为无障碍名称
QPushButton *iconButton(IconId icon, const QString &tooltip, QWidget *parent,
                        IconSize size = IconSize::Standard, bool checkable = false);
// 播放/暂停专用：48px 实心圆
QPushButton *playButton(QWidget *parent);
// 筛选芯片（来源切换），可勾选
QPushButton *chip(IconId icon, const QString &text, QWidget *parent);
// 换图标（播放/暂停、播放模式）；保持按钮原有的颜色角色
void setIcon(QPushButton *b, IconId icon);

QLineEdit *searchField(const QString &placeholder, QWidget *parent);
void asSelect(QComboBox *box);                          // 统一下拉框高度与外观
QFrame *panel(QWidget *parent);
QLabel *label(const QString &text, TextRole role, QWidget *parent);
void setTextRole(QLabel *l, TextRole role);
QAction *menuAction(QMenu *menu, IconId icon, const QString &text);
QProgressBar *progressBar(QWidget *parent);           // 8px 细条，不显示文字（数值放在旁边的列里）
void setProgressState(QProgressBar *bar, ProgressState s);

// 复选框/单选框指示器、下拉箭头、清除按钮图标：替换 Fusion 的默认绘制
void installStyle(QApplication &app);

}  // namespace Ui

namespace Ui {
// 组件陈列页（--ui-gallery）：每个组件的每种状态各一个实例，改组件后先看这一页
QWidget *gallery();
}
