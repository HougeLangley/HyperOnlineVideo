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
    enum class Mode { Dark, Light };
    static void apply(QApplication &app, Mode m = Mode::Dark);
    /// 运行时切换（重刷 palette + QSS，不重启）—— 供 ThemeWatcher 跟随系统时调用
    static void setMode(Mode m);
    static Mode mode() { return s_mode; }
    static bool isDark() { return s_mode == Mode::Dark; }
    /// 解析设置值：接受 auto/dark/light（大小写不敏感）；不认识返回 false
    static bool parseMode(const QString &name, Mode *out);
    /// 窗口级玻璃（v1.2.0，设置键 ui.glassWindow）：把窗口/卡片/列表/输入框背景换成半透明，
    /// 让桌面透出来（macOS 的 NSVisualEffectView 等价观感）。
    /// 说明：Linux 无跨合成器的"请求模糊"接口 —— X11/KWin 可用窗口规则补模糊，
    /// Wayland 下第三方应用无法请求（合成器不支持），故只保证"透明"，模糊由用户规则决定。
    static void setGlass(bool on);
    static bool glass();
    /** 供其他地方复用的一组颜色（画布内绘制文字等） */
    static QString accent() { return QStringLiteral("#3869D3"); }
    static QString text() { return isDark() ? QStringLiteral("#eaeaea") : QStringLiteral("#1b1d21"); }
    static QString textDim() { return isDark() ? QStringLiteral("#9aa0a6") : QStringLiteral("#5b6169"); }
    static QString surface() { return isDark() ? QStringLiteral("#1e1e1e") : QStringLiteral("#ffffff"); }
    static QString canvas() { return isDark() ? QStringLiteral("#121212") : QStringLiteral("#f4f5f7"); }

private:
    static bool s_glass;   // 窗口级玻璃是否开启（设置键 ui.glassWindow）
    static Mode s_mode;    // 当前明暗（ui.theme=auto 时由 ThemeWatcher 驱动）
};
