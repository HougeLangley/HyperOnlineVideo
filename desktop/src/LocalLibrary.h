#pragma once
#include <QString>
#include <QVector>

/**
 * 本地库（桌面端）
 *
 * 职责：扫描下载目录里的媒体文件、排序、重命名。**不碰 UI**，因此可以直接单测。
 * 与 Android 版的"本地库增强"（排序/搜索/重命名）语义对齐；桌面端不引数据库，
 * 直接以文件系统为源（文件就是库）。
 */
class LocalLibrary {
public:
    enum class Sort { Name, Date, Size };

    struct Entry {
        QString path;
        QString name;
        qint64 size = 0;
        qint64 mtime = 0;      // 毫秒时间戳
    };

    explicit LocalLibrary(const QString &dir = QString());

    void setDir(const QString &dir) { dir_ = dir; }
    QString dir() const { return dir_; }
    void setSort(Sort s) { sort_ = s; }
    Sort sort() const { return sort_; }
    /** 排序方式循环（对应界面上的 O 键） */
    void cycleSort();
    static QString sortLabel(Sort s);
    static QString formatSize(qint64 bytes);

    /** 扫描目录；filter 非空时按文件名做不区分大小写的包含过滤 */
    QVector<Entry> scan(const QString &filter = QString()) const;

    /** 重命名（保留原扩展名；目标已存在、名称为空或含路径分隔符都拒绝）
     *  返回空串 = 成功，否则返回错误说明 */
    QString rename(const QString &path, const QString &newBaseName) const;

    /** 是否是本库认的媒体文件 */
    static bool isMediaFile(const QString &path);

private:
    QString dir_;
    Sort sort_ = Sort::Name;
};
