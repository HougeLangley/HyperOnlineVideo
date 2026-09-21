#include "PlayQueue.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

void PlayQueue::setList(const QVector<QueueEntry> &entries, int start) {
    if (entries.isEmpty()) return;
    entries_ = entries;
    index_ = qBound(0, start, entries_.size() - 1);
    lastRandom_ = -1;
}

void PlayQueue::setSingle(const QueueEntry &e) {
    entries_ = { e };
    index_ = 0;
    lastRandom_ = -1;
}

void PlayQueue::clear() {
    entries_.clear();
    index_ = -1;
    lastRandom_ = -1;
}

const QueueEntry *PlayQueue::current() const {
    return at(index_);
}

const QueueEntry *PlayQueue::at(int i) const {
    if (i < 0 || i >= entries_.size()) return nullptr;
    return &entries_.at(i);
}

bool PlayQueue::autoNext() {
    if (entries_.isEmpty()) return false;
    switch (mode_) {
    case Mode::RepeatOne:
        return true;                                   // 索引不动 → 调用方重播当前曲
    case Mode::Shuffle:
        return next();
    case Mode::RepeatAll:
        index_ = (index_ + 1) % entries_.size();       // 列表循环：到底回到第一项
        return true;
    case Mode::Sequential:
    default:
        if (index_ + 1 >= entries_.size()) return false;   // 顺序播放：到底停止（与 macOS/Android 一致）
        ++index_;
        return true;
    }
}

bool PlayQueue::next() {
    if (entries_.isEmpty()) return false;
    switch (mode_) {
    case Mode::RepeatOne:
        // 手动切歌不受单曲循环限制（只有"播完自动续播"才重播本曲）——与 macOS/Android 语义一致
        index_ = (index_ + 1) % entries_.size();
        return true;
    case Mode::Shuffle: {
        if (entries_.size() == 1) return false;
        int n = index_;
        // 最多试 8 次，避免连续抽到自己（也不与上一首随机结果重复）
        for (int i = 0; i < 8; ++i) {
            n = QRandomGenerator::global()->bounded(entries_.size());
            if (n != index_ && n != lastRandom_) break;
        }
        lastRandom_ = index_;
        index_ = n;
        return true;
    }
    case Mode::Sequential:
    default:
        index_ = (index_ + 1) % entries_.size();   // 末尾回到第一首（持续播放）
        return true;
    }
}

void PlayQueue::prev() {
    if (entries_.isEmpty()) return;
    if (mode_ == Mode::Shuffle && entries_.size() > 1) {
        int n = index_;
        for (int i = 0; i < 8; ++i) {
            n = QRandomGenerator::global()->bounded(entries_.size());
            if (n != index_) break;
        }
        index_ = n;
        return;
    }
    index_ = (index_ - 1 + entries_.size()) % entries_.size();
}

void PlayQueue::jumpTo(int i) {
    if (i >= 0 && i < entries_.size()) index_ = i;
}

void PlayQueue::cycleMode() {
    switch (mode_) {
    case Mode::Sequential: mode_ = Mode::RepeatOne; break;
    case Mode::RepeatOne: mode_ = Mode::Shuffle; break;
    case Mode::Shuffle:    mode_ = Mode::RepeatAll; break;
    case Mode::RepeatAll:  mode_ = Mode::Sequential; break;
    }
}

QString PlayQueue::modeLabel() const {
    switch (mode_) {
    case Mode::RepeatOne: return "单曲循环";
    case Mode::Shuffle: return "随机播放";
    case Mode::RepeatAll: return "列表循环";
    case Mode::Sequential:
    default: return "顺序播放";
    }
}

QString PlayQueue::label() const {
    if (entries_.isEmpty() || index_ < 0) return QString();
    return QString("第 %1/%2 首 · %3").arg(index_ + 1).arg(entries_.size()).arg(modeLabel());
}

// ---------------- 持久化（重启后接着上次的队列听） ----------------
QString PlayQueue::filePath() {
    return QDir::homePath() + "/.config/hov/queue.json";
}

bool PlayQueue::saveToPath(const QString &path) const {
    QJsonArray arr;
    for (const auto &e : entries_) {
        QJsonObject o;
        o["key"] = e.key;
        o["label"] = e.label;
        arr.append(o);
    }
    QJsonObject root;
    root["entries"] = arr;
    root["index"] = index_;
    root["mode"] = static_cast<int>(mode_);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.close();
    return true;
}

bool PlayQueue::loadFromPath(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QVector<QueueEntry> entries;
    for (const auto &v : root.value("entries").toArray()) {
        const QJsonObject o = v.toObject();
        const QString key = o.value("key").toString();
        if (key.isEmpty()) continue;
        entries.append({ key, o.value("label").toString() });
    }
    if (entries.isEmpty()) return false;
    entries_ = entries;
    index_ = qBound(0, root.value("index").toInt(0), entries_.size() - 1);
    const int m = root.value("mode").toInt(0);
    // 注意：新增播放模式后必须在这里同步映射，否则**重启后静默变回顺序播放**（本轮实测踩到：
    // 加了 RepeatAll(=3) 但这里只认 0/1/2 → 列表循环保存再加载就丢了）。越界值一律回落顺序。
    mode_ = (m == 1) ? Mode::RepeatOne
          : (m == 2) ? Mode::Shuffle
          : (m == 3) ? Mode::RepeatAll
                     : Mode::Sequential;
    return true;
}

bool PlayQueue::saveTo() const { return saveToPath(filePath()); }
bool PlayQueue::loadFrom() { return loadFromPath(filePath()); }
