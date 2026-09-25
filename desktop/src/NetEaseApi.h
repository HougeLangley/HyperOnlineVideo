#pragma once
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

class QNetworkAccessManager;
class QNetworkRequest;

/**
 * 网易云音乐 API（桌面端）
 *
 * 桌面端先用**旧版公开接口**（匿名可用，实测通过），不用 weapi 加密：
 *   - 搜索：/api/search/get/web
 *   - 取地址：/api/song/enhance/player/url
 * 详见 NetEaseApi.cpp 顶部说明（含为什么不用 weapi 的实测依据）。
 * Cookie：~/.config/hov/cookies/netease.txt（Netscape 格式，含 MUSIC_U 为登录态；
 * 匿名也能搜索与播放普通音质，VIP/无损需要登录）。
 */
class NetEaseApi : public QObject {
    Q_OBJECT
public:
    struct Song {
        QString id;
        QString name;
        QString artist;
        QString album;
        int durationMs = 0;
    };

    explicit NetEaseApi(QObject *parent = nullptr);

    static QString cookieFile();          // 路径（不保证存在）
    static bool hasCookie();

    void setStatusHandler(std::function<void(const QString &)> h) { status_ = std::move(h); }
    /** 国内接口强制直连（设置页 network.forceDirectDomestic）：给本对象的网络管理器禁用代理 */
    void setForceDirect(bool on);

    /** 关键词搜索（云端搜索接口） */
    void search(const QString &keyword, int limit,
                std::function<void(const QVector<Song> &)> done);
    /** 取歌词（LRC 原文 + 翻译，任一可为空；接口匿名可用，实测部分曲目 40+ 行） */
    void lyric(const QString &songId, std::function<void(const QString &lrc, const QString &trans)> done);
    /** 取专辑封面地址（搜索接口只给 picId，必须走 song/detail 才有 picUrl） */
    /** 单首封面（播放时按需取 ✓） */
    void coverUrl(const QString &songId, std::function<void(const QString &url)> done);
    /** 批量封面（搜索卡缩略图 + 播放封面预热 ✓）：POST ids=[a,b,c] → id → album.picUrl ✓
     *  搜索接口只给 picId（拿不到图 ✗ 实测确认 ✓）→ 必须走 song/detail（与 macOS 的 coverUrls 对齐 ✓） */
    void coverUrls(const QStringList &songIds,
                   std::function<void(const QHash<QString, QString> &)> done);
    /** 同上，但额外返回逐字歌词 yrc（登录后才有；为空则退回行级） */
    void lyricFull(const QString &songId,
                   std::function<void(const QString &lrc, const QString &trans, const QString &yrc)> done);

    /** 取歌曲播放地址（level 见网易云：standard / exhigh / lossless） */
    void songUrl(const QString &songId, const QString &level,
                 std::function<void(const QString &url, const QString &err)> done);

private:
    QString cookieHeader() const;
    // CDN 节点择优（2026-09-25 "随机不播"治本 ✗→✓）：网易云流 URL 随机落在 m701~m804，
    // 部分节点被 CDN 403；探测替代节点取首个可用，全败回退原 URL。
    void pickWorkingNode(const QString &url, std::function<void(const QString &)> done);
    QNetworkRequest makeRequest(const QString &url) const;
    QNetworkAccessManager *net_ = nullptr;
    std::function<void(const QString &)> status_;
};
