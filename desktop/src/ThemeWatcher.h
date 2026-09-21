#pragma once
// ThemeWatcher —— 跟随系统 light/dark（Linux 保底方案，用户 2026-09-18 指定）
//
// 三级来源（可靠→兜底）：
//   ① XDG Desktop Portal（跨桌面标准 ✓ 已实测 VM 上返回 uint32 2=Light ✓ 且暴露 SettingChanged ✓）
//   ② QStyleHints::colorScheme()（部分平台有效；本机 VM 实测为 Unknown ✗ 故只作次选）
//   ③ ~/.config/kdeglobals 的 General/ColorScheme 名字里含 Dark/Light（KDE 用默认配色时读不到 ✗ 作三选）
//   ④ 全都不行 → 默认深色（保持既有观感 ✓）
#include <QDBusVariant>
#include <QObject>
#include <QString>
#include "Theme.h"

class ThemeWatcher : public QObject {
    Q_OBJECT
public:
    explicit ThemeWatcher(QObject *parent = nullptr);

    /// 探测当前系统明暗（不做任何设置改动 ✓ 只读）
    static Theme::Mode detect();
    /// 探测来源名（写日志用：portal / stylehints / kdeglobals / fallback）
    static QString detectSource();

    /// 开始跟随：先立即应用一次，再订阅 Portal 的 SettingChanged（有 Portal 才订阅 ✓）
    void start();
    /// 停止跟随（用户显式指定 dark/light 时调用 ✓）
    void stop();

signals:
    void modeChanged(Theme::Mode m, const QString &source);

private slots:
    void onSettingChanged(const QString &ns, const QString &key, const QDBusVariant &value);

private:
    bool watching_ = false;
};
