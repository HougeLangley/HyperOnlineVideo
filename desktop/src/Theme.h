#pragma once
#include "Tokens.h"

#include <QColor>
#include <QString>

class QApplication;

/**
 * 统一视觉主题：色板（Tok）→ QPalette + 全局 QSS。
 * 所有颜色、圆角、间距只从 Tokens.h 取；组件外观见 Ui.h。
 */
class Theme {
public:
    enum class Mode { Dark, Light };
    static void apply(QApplication &app, Mode m = Mode::Dark);
    static void setMode(Mode m);
    static Mode mode() { return s_mode; }
    static bool isDark() { return s_mode == Mode::Dark; }
    static bool parseMode(const QString &name, Mode *out);
    static void setGlass(bool on);
    static bool glass() { return s_glass; }
    static QColor color(Tok t);
    static QString hex(Tok t) { return color(t).name(); }
    // 旧接口，保留给画布内绘制的调用方
    static QString accent()  { return hex(Tok::Primary); }
    static QString text()    { return hex(Tok::OnSurface); }
    static QString textDim() { return hex(Tok::OnSurfaceVariant); }
    static QString surface() { return hex(Tok::SurfaceContainer); }
    static QString canvas()  { return hex(Tok::Surface); }

private:
    static bool s_glass;   // 窗口级玻璃是否开启（设置键 ui.glassWindow）
    static Mode s_mode;    // 当前明暗（ui.theme=auto 时由 ThemeWatcher 驱动）
};
