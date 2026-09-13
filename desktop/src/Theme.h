#pragma once
#include <QString>

class QApplication;

/**
 * 统一视觉主题（深色）
 *
 * 目标：主窗口 / 对话框 / 播放器三处观感一致 —— 之前设置对话框是系统浅色，
 * 与深色主界面割裂（截图里很明显）。
 *
 * 做法：QPalette（避免系统浅色底闪）+ 全局 QSS（圆角、间距、hover/focus 态）+ 统一字体。
 * 颜色贴近 App 图标：深空底 + 电蓝主色。
 */
class Theme {
public:
    static void apply(QApplication &app);
    /** 供其他地方复用的一组颜色（画布内绘制文字等） */
    static QString accent() { return QStringLiteral("#4c8dff"); }
    static QString text() { return QStringLiteral("#eaeaea"); }
    static QString textDim() { return QStringLiteral("#9aa0a6"); }
    static QString surface() { return QStringLiteral("#1e1e1e"); }
    static QString canvas() { return QStringLiteral("#121212"); }
};
