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

double Settings::number(const QString &key, double def) const {
    const auto it = values_.find(key);
    return it == values_.end() ? def : it.value().toDouble();
}

QStringList Settings::knownKeys() {
    return { "music.qualityCeiling", "subtitle.fontScale", "subtitle.defaultDelay",
             "subtitle.karaoke", "download.dir", "download.maxSizeMb", "network.forceDirectDomestic", "ui.showNetworkProbe",
             "playback.rememberProgress" };
}

QString Settings::validate(const QString &key, const QString &valueRaw) {
    if (!knownKeys().contains(key))
        return QString("未知设置项（可用：%1）").arg(knownKeys().join(" / "));
    const QString v = valueRaw.trimmed();
    if (key == "music.qualityCeiling") {
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
               || key == "playback.rememberProgress") {
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
             || key.endsWith("rememberProgress"))
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
