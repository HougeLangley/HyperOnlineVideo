#pragma once
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QThread>
#include <atomic>

/**
 * YouTube googlevideo 分块 Range 本地代理（Issue #1 真修 ✓）
 *
 * 背景：部分出口（运营商/SoSim/VPS 等被判“受限”的 IP）对 googlevideo
 * 放行 **有上限的 Range**，但对「无 Range / Range: bytes=0-」的开放式拉取一律 403。
 * mpv/ffmpeg 起播正是开放式 GET → 表现「解析成功但播不了」；
 * 而 4MB capped Range 预检会误报「可播 ✓」。
 *
 * 做法：把 googlevideo 直链映射成 http://127.0.0.1:<port>/s/<token>，
 * 本代理对上游强制按 ≤1MiB 的 capped Range 分块拉取，再拼给播放器。
 * 支持客户端 Range（拖动进度条），音视频分轨各自 map 一次。
 */
class YtRangeProxy : public QObject {
    Q_OBJECT
public:
    explicit YtRangeProxy(QObject *parent = nullptr);
    ~YtRangeProxy() override;

    /** 非 googlevideo 原样返回；googlevideo → 本机代理 URL（失败则退回原 URL） */
    QString map(const QString &upstreamUrl);

    int port() const { return port_.load(); }
    bool running() const { return port_.load() > 0; }

    /** 从 videoplayback URL 的 clen= 解析总长度；没有则返回 0 */
    static qint64 contentLengthFromUrl(const QString &url);

private:
    bool ensureListening();
    void onNewConnection();
    void handleClient(qintptr socketDescriptor);

    QTcpServer *server_ = nullptr;
    QThread *ioThread_ = nullptr;    // accept 必须离开 GUI：否则 mpv loadfile 阻塞主线程时死锁，音轨永远连不上
    std::atomic<int> port_{0};
    mutable QMutex mu_;
    QHash<QString, QString> tokens_;   // token → upstream
};
