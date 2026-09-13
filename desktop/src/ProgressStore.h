#pragma once
#include <QMap>
#include <QPair>
#include <QString>
#include <QVector>

/**
 * 播放进度记忆（桌面端；语义与 Android 版"进度记忆"对齐）
 *
 * 落盘 ~/.config/hov/progress.json。键 = 解析标识（与播放队列/收藏同一套）：
 *   YouTube/B站等网页 → 原始 watch URL（**不是**解析后的直链，直链会过期）
 *   netease:<id> / qq:<songmid> / 本地文件绝对路径
 *
 * 续播规则（避免"其实只想从头看"的打扰）：
 *   记录：位置 ≥ 5 秒才记（太短无意义）
 *   续播：位置 ≥ 15 秒 且 距结尾 > 15 秒
 *   看完：距结尾 ≤ 15 秒 → 视为已看完，删除条目（下次从头）
 */
class ProgressStore {
public:
    struct Entry {
        double pos = 0.0;       // 上次位置（秒）
        double dur = 0.0;       // 总时长（秒）
        QString title;
        qint64 updatedAt = 0;   // 毫秒时间戳
    };

    static QString filePath();

    static constexpr double kRecordMin = 5.0;    // 少于 5 秒不值得记
    static constexpr double kResumeMin = 15.0;   // 少于 15 秒不值得"续播"
    static constexpr double kEndGuard = 15.0;    // 距结尾 15 秒内视为已看完
    static constexpr int kMaxEntries = 500;
    static constexpr qint64 kMaxAgeMs = 120LL * 24 * 3600 * 1000;   // 120 天

    void load();
    bool save() const;

    /** 记录进度；返回是否留有条目（"已看完"会删除并返回 false） */
    bool remember(const QString &key, double pos, double dur, const QString &title = QString());

    /** 该键应续播的位置；不需要续播返回 0 */
    double resumePos(const QString &key) const;

    bool has(const QString &key) const;
    bool remove(const QString &key);
    void clear();
    int size() const { return items_.size(); }

    /** 按更新时间倒序（供 --progress-show） */
    QVector<QPair<QString, Entry>> items() const;

    /** 清理：过期条目 + 超出上限时丢最旧的 */
    void prune(qint64 nowMs);

private:
    QMap<QString, Entry> items_;
};
