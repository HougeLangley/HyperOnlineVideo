#include "Settings.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace {
const QVariantMap kDefaults{
    { "music.qualityCeiling", "exhigh" },
    { "subtitle.fontScale", 1.0 },
    { "subtitle.defaultDelay", 0.0 },
    { "subtitle.karaoke", true },
    { "download.dir", "" },
    { "download.maxSizeMb", 2048 },   // 下载目录总量上限（MB；0=不限制）；超出按最近使用时间 LRU 清理
    { "network.forceDirectDomestic", false },
    { "ui.showNetworkProbe", true },
    { "playback.rememberProgress", true },   // 进度记忆：续播 + 落盘
    { "network.cookiesFromBrowser", "" },   // 从系统浏览器读 cookie（空=关；brave/chrome/chromium/edge/firefox/opera/safari/vivaldi/whale）
    { "video.fillScreen", true },
    { "ui.masonry", true },        // 结果区用真·瀑布流（false = 回退传统列表视图）
    { "ui.glass", true },          // 玻璃质感（磨砂封面底 + 主题色渐变；与 macOS 观感对齐，false = 纯色背景）
    { "ui.hoverReveal", true },    // 全屏时鼠标贴左边缘浮出结果侧栏（对齐 macOS）
    { "ui.theme", "auto" },        // auto=跟随系统 light/dark（XDG Portal ✓ 实测可用）；dark / light = 用户强制
    { "ui.glassWindow", false },   // 实验：窗口级透明（桌面透出）。**默认关** —— 实测 Wayland + QOpenGLWidget
                                // 组合下 GL 层（mpv 画面/封面磨砂）会被合成器整层丢弃；X11 会话可用。          // 玻璃质感（磨砂封面底 + 主题色渐变；与 macOS 观感对齐，false = 纯色背景）        // 结果区用真·瀑布流（false = 回退传统列表视图）           // B7：全屏时铺满（panscan=1，与 Android/macOS 同键）
    { "video.maxHeight", 0 },               // 视频清晰度上限（0=自动，-1=仅音频，否则 360/480/720/1080…）
};
}

QString Settings::filePath() {
    return QDir::homePath() + "/.config/hov/settings.json";
}

void Settings::load() {
    values_ = kDefaults;
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) {
        qInfo() << "设置：使用默认值（未找到" << filePath() << "）";
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    for (auto it = root.begin(); it != root.end(); ++it)
        values_[it.key()] = it.value().toVariant();     // 未知键也保留（向前兼容）
    qInfo() << "设置已加载:" << values_.size() << "项";
}

bool Settings::save() const {
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QJsonObject root;
    for (auto it = values_.begin(); it != values_.end(); ++it)
        root[it.key()] = QJsonValue::fromVariant(it.value());
    QFile f(filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "设置保存失败:" << filePath();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

QString Settings::str(const QString &key, const QString &def) const {
    const auto it = values_.find(key);
    if (it == values_.end() || it.value().toString().isEmpty()) return def;
    return it.value().toString();
}

bool Settings::boolean(const QString &key, bool def) const {
    const auto it = values_.find(key);
    return it == values_.end() ? def : it.value().toBool();
}

bool Settings::rawBool(const QString &key, bool def)
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return def;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return def;
    const QJsonValue v = doc.object().value(key);
    if (v.isBool()) return v.toBool();
    if (v.isDouble()) return v.toInt() != 0;
    if (v.isString()) return QStringList{"true", "1", "yes", "on"}.contains(v.toString().toLower());
    return def;
}

bool Settings::has(const QString &key) const
{
    // ⚠️ 不能用 values_.contains(key) ✗ —— 那份 map 把**默认值**也合并进来了，
    //    于是"用户没设过"也返回 true ✗（实测把平台自适应默认彻底屏蔽掉了）。
    //    正确语义：**文件里真的写过这个键**才算用户设过 ✓。
    const QString p = filePath();
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() && doc.object().contains(key);
}

double Settings::number(const QString &key, double def) const {
    const auto it = values_.find(key);
    return it == values_.end() ? def : it.value().toDouble();
}

QStringList Settings::knownKeys() {
    return { "music.qualityCeiling", "subtitle.fontScale", "subtitle.defaultDelay",
             "subtitle.karaoke", "download.dir", "download.maxSizeMb", "network.forceDirectDomestic", "ui.showNetworkProbe",
             "playback.rememberProgress", "network.cookiesFromBrowser", "video.maxHeight", "video.fillScreen", "video.gpuNext", "ui.masonry", "ui.glass", "ui.glassWindow", "ui.hoverReveal", "ui.theme" };
}

QString Settings::validate(const QString &key, const QString &valueRaw) {
    if (!knownKeys().contains(key))
        return QString("未知设置项（可用：%1）").arg(knownKeys().join(" / "));
    const QString v = valueRaw.trimmed();
    if (key == "ui.theme") {
        if (v == "auto" || v == "dark" || v == "light") return QString();
        return "取值必须是 auto（跟随系统）/ dark / light";
    }
    if (key == "video.maxHeight") {
        bool ok = false; const double d = v.toDouble(&ok);
        if (!ok) return "取值必须是数字（0=自动，-1=仅音频，或 360/480/720/1080…）";
        if (d == 0 || d == -1) return QString();
        if (d < 144 || d > 4320) return "清晰度上限应在 144 ~ 4320 之间（或 0=自动 / -1=仅音频）";
    } else if (key == "network.cookiesFromBrowser") {
        // 空 = 关闭；否则形如 chrome / firefox+keyring / chrome:Profile 1
        if (v.isEmpty()) return QString();
        const QString head = v.split('+').first().split(':').first().toLower();
        static const QStringList ok{ "brave", "chrome", "chromium", "edge", "firefox",
                                     "opera", "safari", "vivaldi", "whale" };
        if (!ok.contains(head)) return QString("浏览器名必须是 %1 之一（或留空关闭）").arg(ok.join(" / "));
    } else if (key == "music.qualityCeiling") {
        static const QStringList ok{ "standard", "exhigh", "lossless" };
        if (!ok.contains(v)) return QString("取值必须是 %1").arg(ok.join(" / "));
    } else if (key == "subtitle.fontScale") {
        bool ok = false; const double d = v.toDouble(&ok);
        if (!ok || d < 0.5 || d > 2.0) return "取值必须是 0.5 ~ 2.0 之间的数字";
    } else if (key == "subtitle.defaultDelay") {
        bool ok = false; const double d = v.toDouble(&ok);
        if (!ok || d < -5.0 || d > 5.0) return "取值必须是 -5.0 ~ 5.0 之间的秒数";
    } else if (key == "download.maxSizeMb") {
        bool ok = false; const double d = v.toDouble(&ok);
        if (!ok || d < 0 || d > 1048576) return "取值必须是 0 ~ 1048576 之间的数字（MB，0=不限制）";
    } else if (key == "subtitle.karaoke" || key == "network.forceDirectDomestic" || key == "ui.showNetworkProbe"
               || key == "playback.rememberProgress" || key == "video.fillScreen" || key == "video.gpuNext" || key == "ui.masonry"
               || key == "ui.glass" || key == "ui.glassWindow" || key == "ui.hoverReveal") {
        static const QStringList ok{ "true", "false", "1", "0", "yes", "no", "on", "off" };
        if (!ok.contains(v.toLower())) return "取值必须是 true / false";
    }
    return QString();
}

bool Settings::set(const QString &key, const QVariant &value) {
    if (key.isEmpty()) return false;
    QVariant v = value;
    // 少量类型归一化，避免 JSON 里出现 "true" / "1.5" 这类字符串
    if (key.endsWith("fontScale") || key.endsWith("Delay") || key.endsWith("maxSizeMb"))
        v = v.toDouble();
    else if (key.endsWith("karaoke") || key.endsWith("forceDirectDomestic") || key.endsWith("showNetworkProbe")
             || key.endsWith("rememberProgress") || key.endsWith("fillScreen"))
        v = v.toBool();
    if (values_.value(key) == v) return false;
    values_[key] = v;
    return true;
}

QString Settings::dump() const {
    QStringList out;
    for (auto it = kDefaults.begin(); it != kDefaults.end(); ++it) {
        const QVariant cur = values_.value(it.key(), it.value());
        out << QString("%1 = %2%3")
                   .arg(it.key(), cur.toString(),
                        (cur == it.value() ? QString("   (默认)") : QString("   (已改，默认 %1)").arg(it.value().toString())));
    }
    // 额外键（向后兼容保留的）也列出来
    for (auto it = values_.begin(); it != values_.end(); ++it)
        if (!kDefaults.contains(it.key())) out << QString("%1 = %2   (额外)").arg(it.key(), it.value().toString());
    return out.join('\n');
}

QString Settings::effectiveQuality(const QString &requested) const {
    // 档位从低到高：standard < exhigh < lossless；上限只降不升
    static const QStringList ladder{ "standard", "exhigh", "lossless" };
    const QString ceiling = str("music.qualityCeiling", "exhigh");
    const int cap = ladder.indexOf(ceiling) >= 0 ? ladder.indexOf(ceiling) : 1;
    const int want = ladder.indexOf(requested) >= 0 ? ladder.indexOf(requested) : cap;
    return ladder.at(qMin(cap, want));
}
