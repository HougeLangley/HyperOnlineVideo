#pragma once
#include <QString>
#include <QVector>

/**
 * 播放队列（桌面端；语义与 Android core/data/PlayQueue.kt 对齐）
 *
 * - 从搜索结果整列表入队，从指定项开始播
 * - 播放结束自动续播下一项；支持 顺序 / 单曲循环 / 随机 三种模式
 * - 队列记录每项的 key（"netease:<id>" / "qq:<mid>" / 网页 URL），续播时由主窗口按前缀分发解析
 *
 * 纯逻辑类，不依赖界面 —— 便于用 --queue-selftest 做断言自检。
 */
struct QueueEntry {
    QString key;     // 解析用的标识（前缀决定走哪个平台）
    QString label;   // 界面显示名（歌名 — 歌手 / 视频标题）
};

class PlayQueue {
public:
    enum class Mode { Sequential, RepeatOne, Shuffle, RepeatAll };   // 与 macOS/Android 四档对齐

    void setList(const QVector<QueueEntry> &entries, int start);
    void setSingle(const QueueEntry &e);
    void clear();

    int size() const { return entries_.size(); }
    int index() const { return index_; }
    bool isEmpty() const { return entries_.isEmpty(); }
    const QueueEntry *current() const;
    const QueueEntry *at(int i) const;
    QVector<QueueEntry> entries() const { return entries_; }

    /** 下一首：按模式推进；返回是否发生了"换曲"（单曲模式返回 false，表示应重播当前曲） */
    bool next();          // 手动切歌：总是前进（单曲循环也前进）
    bool autoNext();      // 播完自动续播：单曲=重播本曲；顺序=到底停止；列表循环=回到开头；随机=随机
    void prev();
    void jumpTo(int i);
    void cycleMode();

    /** 持久化：退出时保存、启动时恢复（~/.config/hov/queue.json） */
    static QString filePath();
    bool saveTo() const;
    bool loadFrom();
    /** 供自检使用：任意路径往返 */
    bool saveToPath(const QString &path) const;
    bool loadFromPath(const QString &path);

    Mode mode() const { return mode_; }
    QString modeLabel() const;
    /** 形如「第 2/15 首 · 顺序」；空队列返回空串 */
    QString label() const;

private:
    QVector<QueueEntry> entries_;
    int index_ = -1;
    Mode mode_ = Mode::Sequential;
    int lastRandom_ = -1;   // 随机模式避免连续撞同一首
};
