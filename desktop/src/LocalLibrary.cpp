#include "LocalLibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace {
const char *kMediaExts[] = {"mp3", "flac", "m4a", "wav", "ogg", "opus", "mp4", "mkv", "webm", "mov", "ts", "avi", nullptr};
}   // namespace

LocalLibrary::LocalLibrary(const QString &dir) : dir_(dir) {}

bool LocalLibrary::isMediaFile(const QString &path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    for (int i = 0; kMediaExts[i]; ++i)
        if (ext == QLatin1String(kMediaExts[i])) return true;
    return false;
}

void LocalLibrary::cycleSort() {
    sort_ = (sort_ == Sort::Name) ? Sort::Date : (sort_ == Sort::Date ? Sort::Size : Sort::Name);
}

QString LocalLibrary::sortLabel(Sort s) {
    switch (s) {
    case Sort::Date: return QStringLiteral("按时间（新→旧）");
    case Sort::Size: return QStringLiteral("按大小（大→小）");
    default: return QStringLiteral("按名称（A→Z）");
    }
}

QString LocalLibrary::formatSize(qint64 bytes) {
    if (bytes >= 1024LL * 1024 * 1024) return QString("%1 GB").arg(bytes / 1024.0 / 1024 / 1024, 0, 'f', 1);
    if (bytes >= 1024 * 1024) return QString("%1 MB").arg(bytes / 1024.0 / 1024, 0, 'f', 1);
    if (bytes >= 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
    return QString("%1 B").arg(bytes);
}

QVector<LocalLibrary::Entry> LocalLibrary::scan(const QString &filter) const {
    QVector<Entry> out;
    QDir d(dir_);
    if (!d.exists()) return out;
    const QFileInfoList list = d.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name);
    const QString f = filter.trimmed().toLower();
    for (const QFileInfo &fi : list) {
        if (!isMediaFile(fi.fileName())) continue;
        if (!f.isEmpty() && !fi.fileName().toLower().contains(f)) continue;
        Entry e;
        e.path = fi.absoluteFilePath();
        e.name = fi.fileName();
        e.size = fi.size();
        e.mtime = fi.lastModified().toMSecsSinceEpoch();
        out.append(e);
    }
    switch (sort_) {
    case Sort::Date:
        std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.mtime > b.mtime; });
        break;
    case Sort::Size:
        std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.size > b.size; });
        break;
    case Sort::Name:
        std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
            return QString::localeAwareCompare(a.name, b.name) < 0;
        });
        break;
    }
    return out;
}

QString LocalLibrary::rename(const QString &path, const QString &newBaseName) const {
    const QString base = newBaseName.trimmed();
    if (base.isEmpty()) return QStringLiteral("新名称为空");
    if (base.contains('/') || base.contains('\\')) return QStringLiteral("名称不能包含路径分隔符");
    static const QRegularExpression bad(R"([:*?"<>|])");
    if (bad.match(base).hasMatch()) return QStringLiteral("名称包含非法字符 : * ? \" < > |");
    const QFileInfo fi(path);
    if (!fi.exists()) return QStringLiteral("原文件不存在");
    const QString ext = fi.suffix();
    // 用户可能连着扩展名一起改：避免出现 a.mp3.mp3
    QString target = base;
    if (!ext.isEmpty() && !target.endsWith("." + ext, Qt::CaseInsensitive)) target += "." + ext;
    const QString targetPath = fi.absolutePath() + "/" + target;
    if (targetPath == fi.absoluteFilePath()) return QString();       // 没变：算成功
    if (QFile::exists(targetPath)) return QStringLiteral("目标已存在：%1").arg(target);
    if (!QFile::rename(fi.absoluteFilePath(), targetPath)) return QStringLiteral("重命名失败（权限或文件被占用）");
    return QString();
}
