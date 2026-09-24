#pragma once
#include <QDir>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

class QNetworkAccessManager;
class QNetworkRequest;
class QNetworkReply;
class QFile;
class QProcess;

/**
 * 下载管理（桌面端）
 *
 * 与 Android 版的下载管理 2.0 对齐的**核心语义**：并发队列（2）+ 进度 + 重试 + 失败原因。
 * 实现上用 QNetworkAccessManager 直下**直链媒体**（音乐 mp3/flac、已解析的视频流），
 * 不重造 yt-dlp 的解析能力 —— 解析仍由 UrlResolver / NetEaseApi / QQMusicApi 负责。
 *
 * 落盘目录：~/Downloads/hov/（找不到 Downloads 时用 ~/下载/hov/）
 */
class DownloadManager : public QObject {
    Q_OBJECT
public:
    enum class State { Queued, Running, Done, Failed, Canceled };

    struct Job {
        QString id;
        QString title;      // 界面显示名
        QString url;
        QString filePath;   // 目标文件
        State state = State::Queued;
        qint64 bytesDone = 0;
        qint64 bytesTotal = -1;   // -1 = 未知
        int retries = 0;
        QString error;
        // 音乐标签（音乐下载完成后嵌入；为空则跳过）
        QString artist;
        QString album;
        QString coverUrl;
        QString audioUrl;      // ③ 视频任务的独立音轨（yt-dlp 的 bv*+ba 会把音视频拆成两路）
        QString lyrics;        // ③ 歌词原文（LRC 文本）→ 下载后写同名 .lrc 侧车
        QString audioPath;     // ②(第2阶段) 独立音轨先落到本地临时文件，再由 ffmpeg 本地混流
        qint64  videoBytes = 0;// ② 视频段已完成字节数（音轨段进度叠加在它之上）
    };

    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager() override;

    static QString defaultDir();
    /** 覆盖落盘目录（设置页 download.dir；空串=回到默认） */
    void setDir(const QString &dir) { dir_ = dir; if (!dir_.isEmpty()) QDir().mkpath(dir_); }
    QString dir() const { return dir_.isEmpty() ? defaultDir() : dir_; }

    /** 入队一个直链下载；返回任务 id（空串表示参数不合法） */
    QString enqueue(const QString &url, const QString &title, const QString &fileNameHint = QString(),
                    const QString &artist = QString(), const QString &album = QString(),
                    const QString &coverUrl = QString(),
                    const QString &audioUrl = QString(),   // ③ 视频任务：独立音轨直链（空=无）
                    const QString &lyrics = QString());    // ③ 歌词 LRC 文本（空=无）

    /** 进度/状态变化回调（在 GUI 线程调用；UI 自行节流） */
    void setProgressHandler(std::function<void(const Job &)> h) { progress_ = std::move(h); }

    QVector<Job> jobs() const { return jobs_; }
    const Job *find(const QString &id) const;
    Job *mutableFind(const QString &id);   // 内部用：避免 const_cast
    int activeCount() const;
    bool idle() const { return activeCount() == 0; }
    /** 下载目录总量上限（MB；0=不限制）。超出后每次下载完成会自动做一次 LRU 清理。 */
    void setMaxTotalSizeMb(int mb) { maxTotalMb_ = mb; }
    int maxTotalSizeMb() const { return maxTotalMb_; }

    struct CleanResult {
        qint64 beforeBytes = 0;
        qint64 afterBytes = 0;
        QStringList removed;      // 被删掉的文件路径
    };
    /**
     * LRU 清理：目录总量超上限时，按「最近使用时间」（atime 与 mtime 取新）从旧到新删，
     * 直到降到上限的 90% 以内。只动本目录下的媒体文件；正在下载/排队中的文件绝不碰。
     */
    CleanResult cleanupLru(qint64 maxBytes);

    /** 失败任务重试一轮（清空 retries 与 error） */
    void retryFailed();
    /** W5 ✓ 「清理」同时清列表：移除全部**已结束**条目（完成/失败/已取消 ✓ 进行中的绝不碰 ✗）
     *  用户口径（2026-09-23）：点「清理」后已下载内容要从下载面板消失 ✓
     *  （原实现只在“有文件被 LRU 删掉”时才同步列表 ✗ → 默认 2GB 上限下几乎不删文件 ✗ → 条目永远留着 ✗）
     *  @return 被移除的条目数 */
    int dropFinishedJobs();
    /** 取消**单个**任务（排队/下载中/混流中均可 ✓）—— 同时删半截文件并把条目从列表移除 ✓
     *  返 true = 真的取消了（对已完成/已失败/已取消的条目返 false ✓ 不误删记录 ✗） */
    bool cancel(const QString &id);
    void cancelAll();

private:
    void pump();                     // 在并发上限内启动排队任务
    void start(const QString &id);
    void fail(const QString &id, const QString &reason, bool retryable);
    void update(const QString &id);
    /** 音乐标签嵌入：写临时文件 → ffprobe 读回校验 → 替换；任一步失败都保留原文件 */
    bool embedTags(Job &j);
    void finishJob(Job &j);                 // ③ 收尾：写歌词侧车 + 音频才嵌标签 + 置 Done + 日志
    void startMux(const QString &id);       // ③ ffmpeg 把独立音轨混进视频（无音轨任务不走这里）
    void startAudioPhase(const QString &id); // ②(第2阶段) 先把独立音轨下到本地（不再让 ffmpeg 自己去拉流）
    static void applyHeaders(QNetworkRequest &req, const QString &url);   // 统一请求头（含各站 Referer）
    bool writeLyrics(const Job &j);         // ③ 写同名 .lrc（UTF-8）

    QNetworkAccessManager *net_ = nullptr;
    QVector<Job> jobs_;
    QHash<QString, QNetworkReply *> running_;
    QHash<QString, QFile *> files_;
    QHash<QString, QProcess *> muxProcs_;   // W4 ✓ 混流进程句柄（取消时要能杀掉 ✗ 原来进程没存 → 取消不掉 ✓）
    QString dir_;              // 落盘目录覆盖（空=默认）
    std::function<void(const Job &)> progress_;
    int seq_ = 0;
    int maxTotalMb_ = 2048;
    static constexpr int kMaxConcurrent = 2;
    static constexpr int kMaxRetries = 2;
};
