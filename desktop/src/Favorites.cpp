#include "Favorites.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

QString Favorites::filePath() {
    return QDir::homePath() + "/.config/hov/favorites.json";
}

void Favorites::load() {
    items_.clear();
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    for (const auto &v : root.value("items").toArray()) {
        const QJsonObject o = v.toObject();
        Fav fav;
        fav.id = o.value("id").toString();
        fav.title = o.value("title").toString();
        fav.uploader = o.value("uploader").toString();
        fav.platform = o.value("platform").toString();
        fav.key = o.value("key").toString();
        fav.addedAt = static_cast<qint64>(o.value("addedAt").toDouble());
        if (!fav.id.isEmpty()) items_.append(fav);
    }
    qInfo() << "收藏已加载:" << items_.size() << "项";
}

bool Favorites::save() const {
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QJsonArray arr;
    for (const auto &f : items_) {
        QJsonObject o;
        o["id"] = f.id;
        o["title"] = f.title;
        o["uploader"] = f.uploader;
        o["platform"] = f.platform;
        o["key"] = f.key;
        o["addedAt"] = static_cast<double>(f.addedAt);
        arr.append(o);
    }
    QJsonObject root;
    root["items"] = arr;
    root["updatedAt"] = QDateTime::currentMSecsSinceEpoch();
    QFile f(filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "收藏保存失败:" << filePath();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

bool Favorites::contains(const QString &id) const {
    for (const auto &f : items_)
        if (f.id == id) return true;
    return false;
}

bool Favorites::add(const Fav &f) {
    if (f.id.isEmpty() || contains(f.id)) return false;
    items_.append(f);
    return true;
}

bool Favorites::remove(const QString &id) {
    const int before = items_.size();
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [&id](const Fav &f) { return f.id == id; }),
                 items_.end());
    return items_.size() != before;
}

QVector<Favorites::Fav> Favorites::items() const {
    QVector<Fav> out = items_;
    std::sort(out.begin(), out.end(), [](const Fav &a, const Fav &b) { return a.addedAt > b.addedAt; });
    return out;
}
