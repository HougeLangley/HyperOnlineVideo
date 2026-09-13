#include "ProgressStore.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

QString ProgressStore::filePath() {
    return QDir::homePath() + "/.config/hov/progress.json";
}

void ProgressStore::load() {
    items_.clear();
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return;      // 首次运行无文件：正常
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QJsonObject arr = root.value("items").toObject();
    for (auto it = arr.begin(); it != arr.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.pos = o.value("pos").toDouble();
        e.dur = o.value("dur").toDouble();
        e.title = o.value("title").toString();
        e.updatedAt = static_cast<qint64>(o.value("updatedAt").toDouble());
        if (it.key().isEmpty()) continue;
        items_.insert(it.key(), e);
    }
    prune(QDateTime::currentMSecsSinceEpoch());
    qInfo() << "进度记忆已加载:" << items_.size() << "条";
}

bool ProgressStore::save() const {
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QJsonObject arr;
    for (auto it = items_.begin(); it != items_.end(); ++it) {
        QJsonObject o;
        o["pos"] = it.value().pos;
        o["dur"] = it.value().dur;
        o["title"] = it.value().title;
        o["updatedAt"] = static_cast<double>(it.value().updatedAt);
        arr[it.key()] = o;
    }
    QJsonObject root;
    root["items"] = arr;
    root["updatedAt"] = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    QFile f(filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "进度保存失败:" << filePath();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

bool ProgressStore::remember(const QString &key, double pos, double dur, const QString &title) {
    if (key.isEmpty() || dur < 1.0 || pos < kRecordMin) return false;
    if (pos > dur - kEndGuard) {          // 看到结尾了：清掉，下次从头
        if (items_.contains(key)) {
            items_.remove(key);
            qInfo() << "进度：已看完，清除记录" << key;
        }
        return false;
    }
    Entry e;
    e.pos = pos;
    e.dur = dur;
    e.title = title.isEmpty() ? items_.value(key).title : title;
    e.updatedAt = QDateTime::currentMSecsSinceEpoch();
    items_.insert(key, e);
    return true;
}

double ProgressStore::resumePos(const QString &key) const {
    if (!items_.contains(key)) return 0.0;
    const Entry e = items_.value(key);
    if (e.dur < 1.0 || e.pos < kResumeMin || e.pos > e.dur - kEndGuard) return 0.0;
    return e.pos;
}

bool ProgressStore::has(const QString &key) const { return items_.contains(key); }

bool ProgressStore::remove(const QString &key) { return items_.remove(key) > 0; }

void ProgressStore::clear() { items_.clear(); }

QVector<QPair<QString, ProgressStore::Entry>> ProgressStore::items() const {
    QVector<QPair<QString, Entry>> out;
    out.reserve(items_.size());
    for (auto it = items_.begin(); it != items_.end(); ++it) out.append({it.key(), it.value()});
    std::sort(out.begin(), out.end(), [](const QPair<QString, Entry> &a, const QPair<QString, Entry> &b) {
        return a.second.updatedAt > b.second.updatedAt;
    });
    return out;
}

void ProgressStore::prune(qint64 nowMs) {
    for (auto it = items_.begin(); it != items_.end();) {
        if (it.value().updatedAt > 0 && nowMs - it.value().updatedAt > kMaxAgeMs) it = items_.erase(it);
        else ++it;
    }
    if (items_.size() <= kMaxEntries) return;
    // 超量：按更新时间丢最旧的
    auto all = items();
    std::sort(all.begin(), all.end(), [](const QPair<QString, Entry> &a, const QPair<QString, Entry> &b) {
        return a.second.updatedAt < b.second.updatedAt;
    });
    const int drop = items_.size() - kMaxEntries;
    for (int i = 0; i < drop; ++i) items_.remove(all.at(i).first);
    qInfo() << "进度：超出上限，已丢弃最旧" << drop << "条";
}
