#include "Playlist.h"
#include <QFileInfo>

QString Playlist::displayName() const {
    if (index_ < 0) return QString();
    const QString name = QFileInfo(files_.at(index_)).fileName();
    if (files_.size() <= 1) return name;
    return QString("%1（第 %2/%3 个）").arg(name).arg(index_ + 1).arg(files_.size());
}
