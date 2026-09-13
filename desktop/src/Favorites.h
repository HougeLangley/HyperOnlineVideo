#pragma once
#include <QString>
#include <QVector>

/**
 * 收藏（桌面端；语义与 Android core/data/Favorites.kt 对齐）
 *
 * 落盘为 ~/.config/hov/favorites.json（与 Android 一样用 JSON，不引数据库依赖）。
 * 收藏列表本身也可作为播放队列的来源。
 */
class Favorites {
public:
    struct Fav {
        QString id;          // 平台内唯一 id（netease:xxx / qq:xxx / URL）
        QString title;
        QString uploader;
        QString platform;    // youtube / netease / qqmusic
        QString key;         // 与播放队列同一套解析标识
        qint64 addedAt = 0;  // 毫秒时间戳
    };

    static QString filePath();

    void load();
    bool save() const;

    bool contains(const QString &id) const;
    /** 加入收藏（重复则不重复添加）；返回是否发生了变更 */
    bool add(const Fav &f);
    bool remove(const QString &id);
    /** 按加入时间倒序 */
    QVector<Fav> items() const;
    int size() const { return items_.size(); }

private:
    QVector<Fav> items_;
};
