#include "ThemeWatcher.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QMetaType>
#include <QDir>
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

#include <cstdio>

// ThemeWatcher —— 跟随系统 light/dark（Linux 保底方案，用户 2026-09-18 指定）
//
// 三级来源（可靠 → 兜底）：
//   ① XDG Desktop Portal：org.freedesktop.appearance / color-scheme
//      （跨桌面标准 ✓ VM 实测返回 uint32 2 = Light ✓ 且暴露 SettingChanged 可动态跟随 ✓）
//   ② QStyleHints::colorScheme()（部分平台有效；VM 实测 Unknown ✗ → 只作次选）
//   ③ ~/.config/kdeglobals 的 General/ColorScheme 名字含 Dark/Light（KDE 用默认配色时读不到 ✗ 作三选）
//   ④ 全都不行 → 保持既有深色观感 ✓
// 只读，不改系统设置 ✓

namespace {
const char *kPortalSvc = "org.freedesktop.portal.Desktop";
const char *kPortalPath = "/org/freedesktop/portal/desktop";
const char *kPortalIface = "org.freedesktop.portal.Settings";

/// 读 Portal 的 appearance/color-scheme：返回 0=失败/无偏好 1=深色 2=浅色。
/// 用**直接 QDBusMessage** 而不是 QDBusReply<QDBusVariant>：后者依赖 introspection 的类型映射，
/// 实测在这台机器上读不出来 ✗（同一条调用用 gdbus 却是好的 ✓）→ 直接收 QVariant 手动解两层 variant ✓。
uint readPortalColorScheme(QString *raw = nullptr)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kPortalSvc), QLatin1String(kPortalPath),
                                                      QLatin1String(kPortalIface), QStringLiteral("Read"));
    msg << QStringLiteral("org.freedesktop.appearance") << QStringLiteral("color-scheme");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 1500);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        if (raw) *raw = reply.errorName() + QStringLiteral(": ") + reply.errorMessage();
        return 0;
    }
    QVariant v = reply.arguments().at(0);
    if (raw) *raw = QString::fromLatin1(v.typeName());
    // Portal 的 Read 返回是 **两层 variant**（gdbus 打印为 `(<<uint32 2>>,)` ✗）→ 循环解包到位 ✓
    for (int i = 0; i < 3; ++i) {                       // Qt6：用 fromType<> 比较（QMetaType::QDBusVariant 不存在 ✗）
        if (v.metaType() != QMetaType::fromType<QDBusVariant>()) break;
        v = v.value<QDBusVariant>().variant();
    }
    const uint n = v.toUInt();
    return (n == 1 || n == 2) ? n : 0;
}

/// ③ KDE 配置文件里的配色名（可能读不到 → 返回空）
Theme::Mode kdeGlobalsMode()
{
    QSettings kg(QDir::homePath() + QStringLiteral("/.config/kdeglobals"), QSettings::IniFormat);
    const QString name = kg.value(QStringLiteral("General/ColorScheme")).toString();
    if (name.contains(QStringLiteral("Dark"), Qt::CaseInsensitive)) return Theme::Mode::Dark;
    if (name.contains(QStringLiteral("Light"), Qt::CaseInsensitive)) return Theme::Mode::Light;
    return Theme::Mode::Dark;   // 没线索：交给调用方当作"未命中"（这里返回深色只是占位）
}

bool kdeGlobalsUsable()
{
    QSettings kg(QDir::homePath() + QStringLiteral("/.config/kdeglobals"), QSettings::IniFormat);
    const QString name = kg.value(QStringLiteral("General/ColorScheme")).toString();
    return name.contains(QStringLiteral("Dark"), Qt::CaseInsensitive)
        || name.contains(QStringLiteral("Light"), Qt::CaseInsensitive);
}
}  // namespace

ThemeWatcher::ThemeWatcher(QObject *parent) : QObject(parent) {}

QString ThemeWatcher::detectSource()
{
    if (readPortalColorScheme() != 0) return QStringLiteral("portal");
    const Qt::ColorScheme cs = QGuiApplication::styleHints()->colorScheme();
    if (cs == Qt::ColorScheme::Dark || cs == Qt::ColorScheme::Light) return QStringLiteral("stylehints");
    if (kdeGlobalsUsable()) return QStringLiteral("kdeglobals");
    return QStringLiteral("fallback");
}

Theme::Mode ThemeWatcher::detect()
{
    // ① Portal（跨桌面标准 ✓）
    QString raw;
    const uint pv = readPortalColorScheme(&raw);
    if (pv == 1) return Theme::Mode::Dark;
    if (pv == 2) return Theme::Mode::Light;
    std::fprintf(stderr, "[THEME] Portal 未命中（原始=%s）→ 走降级链\n", qPrintable(raw));

    // ② Qt 自带
    const Qt::ColorScheme cs = QGuiApplication::styleHints()->colorScheme();
    if (cs == Qt::ColorScheme::Dark) return Theme::Mode::Dark;
    if (cs == Qt::ColorScheme::Light) return Theme::Mode::Light;

    // ③ KDE 配置
    if (kdeGlobalsUsable()) return kdeGlobalsMode();

    // ④ 兜底：保持既有深色观感
    return Theme::Mode::Dark;
}

void ThemeWatcher::start()
{
    const QString src = detectSource();
    emit modeChanged(detect(), src);   // 立即应用一次（调用方接到 Theme::setMode ✓）
    std::fprintf(stderr, "[THEME] 跟随系统：来源=%s 模式=%s\n", qPrintable(src),
                 Theme::mode() == Theme::Mode::Light ? "light" : "dark");
    if (watching_) return;
    // 订阅 Portal 变更：用户在系统里切深浅色 → 立即跟随（无需重启 ✓）
    const bool ok = QDBusConnection::sessionBus().connect(
        QLatin1String(kPortalSvc), QLatin1String(kPortalPath), QLatin1String(kPortalIface),
        QStringLiteral("SettingChanged"), this, SLOT(onSettingChanged(QString,QString,QDBusVariant)));
    watching_ = ok;
    std::fprintf(stderr, "[THEME] 订阅 SettingChanged：%s（%s）\n", ok ? "成功" : "失败",
                 ok ? "系统切换时会自动跟随" : "只在启动时读取一次");
}

void ThemeWatcher::stop()
{
    if (!watching_) return;
    QDBusConnection::sessionBus().disconnect(
        QLatin1String(kPortalSvc), QLatin1String(kPortalPath), QLatin1String(kPortalIface),
        QStringLiteral("SettingChanged"), this, SLOT(onSettingChanged(QString,QString,QDBusVariant)));
    watching_ = false;
    std::fprintf(stderr, "[THEME] 已停止跟随系统（用户显式指定了 ui.theme）\n");
}

void ThemeWatcher::onSettingChanged(const QString &ns, const QString &key, const QDBusVariant &value)
{
    if (ns != QLatin1String("org.freedesktop.appearance") || key != QLatin1String("color-scheme")) return;
    const uint v = value.variant().toUInt();
    if (v != 1 && v != 2) return;
    const Theme::Mode m = (v == 1) ? Theme::Mode::Dark : Theme::Mode::Light;
    std::fprintf(stderr, "[THEME] 系统明暗变化 → %s\n", m == Theme::Mode::Light ? "light" : "dark");
    emit modeChanged(m, QStringLiteral("portal-live"));
}
