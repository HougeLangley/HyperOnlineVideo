#include "NetEaseApi.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

/**
 * 实现说明（重要，勿随意改回 weapi）
 *
 * 本项目 Android 版走的是 weapi（两层 AES + RSA），那是为了拿无损/VIP 音质。
 * 桌面端这里先用**网易云旧版公开接口**，理由：
 *   - 实测（2026-09-12）：weapi 在本环境返回 **HTTP 200 但响应体为空**（服务端静默拒绝），
 *     而旧版接口 `api/search/get/web` 与 `api/song/enhance/player/url` **匿名即可用**：
 *     搜索返回真实歌曲，取地址返回可播放的 mp3 直链 ✓
 *   - 旧接口不需要加密，代码量与依赖都小很多（无需 OpenSSL）
 * Cookie 仍然支持：~/.config/hov/cookies/netease.txt 存在时附带，登录后可拿到更高音质。
 * 若日后要上无损/VIP，再按 Android 版的路子补 weapi 作为高音质通道即可。
 */

namespace {
const char *kUserAgent =
    "Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/120.0.0.0 Safari/537.36";
}

NetEaseApi::NetEaseApi(QObject *parent) : QObject(parent) {
    net_ = new QNetworkAccessManager(this);
}

QString NetEaseApi::cookieFile() {
    return QDir::homePath() + "/.config/hov/cookies/netease.txt";
}

bool NetEaseApi::hasCookie() {
    return QFile::exists(cookieFile());
}

QString NetEaseApi::cookieHeader() const {
    // 从 Netscape cookie 文件拼 Cookie 头（与 Android 版导出格式一致）
    QFile f(cookieFile());
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QStringList kv;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QStringList parts = line.split('\t');
        if (parts.size() < 7) continue;
        kv << QString("%1=%2").arg(parts[5], parts[6]);
    }
    return kv.join("; ");
}

QNetworkRequest NetEaseApi::makeRequest(const QString &url) const {
    QNetworkRequest r{QUrl(url)};
    r.setRawHeader("Referer", "https://music.163.com/");
    r.setRawHeader("User-Agent", kUserAgent);
    const QString cookie = cookieHeader();
    if (!cookie.isEmpty()) r.setRawHeader("Cookie", cookie.toUtf8());
    return r;
}

void NetEaseApi::search(const QString &keyword, int limit,
                        std::function<void(const QVector<Song> &)> done) {
    QUrlQuery q;
    q.addQueryItem("s", keyword);
    q.addQueryItem("type", "1");
    q.addQueryItem("limit", QString::number(limit));
    q.addQueryItem("offset", "0");
    const QString url = "https://music.163.com/api/search/get/web?" + q.toString(QUrl::FullyEncoded);

    if (status_) status_(hasCookie() ? "网易云搜索中（已带 cookie）" : "网易云搜索中（匿名）");
    QNetworkReply *reply = net_->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            if (status_) status_(QString("网易云搜索失败：%1").arg(reply->errorString()));
            done({});
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(data).object();
        const QJsonArray songs = root.value("result").toObject().value("songs").toArray();
        QVector<Song> out;
        for (const auto &v : songs) {
            const QJsonObject o = v.toObject();
            Song s;
            // 注意：必须按整数格式化 —— QString::number(double) 默认最短表示，
            // 大 id 会变成 "3.3577e+09" 这种科学计数法，传给接口就查不到任何数据（实测踩过）。
            s.id = QString::number(static_cast<qint64>(o.value("id").toDouble()));
            s.name = o.value("name").toString();
            s.durationMs = o.value("duration").toInt();
            QStringList names;
            for (const auto &a : o.value("artists").toArray())
                names << a.toObject().value("name").toString();
            s.artist = names.join(" / ");
            s.album = o.value("album").toObject().value("name").toString();
            if (!s.id.isEmpty() && !s.name.isEmpty()) out.append(s);
        }
        if (status_) status_(QString("网易云返回 %1 首").arg(out.size()));
        done(out);
    });
}

void NetEaseApi::coverUrl(const QString &songId, std::function<void(const QString &)> done) {
    QNetworkRequest req{QUrl("https://music.163.com/api/song/detail")};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/120.0");
    // 旧版详情接口仍然匿名可用（实测：songs[0].album.picUrl）
    QNetworkReply *r = net_->post(req, QByteArray("ids=[") + songId.toUtf8() + "]");
    connect(r, &QNetworkReply::finished, this, [r, done] {
        r->deleteLater();
        const QJsonObject root = QJsonDocument::fromJson(r->readAll()).object();
        const QJsonArray songs = root.value("songs").toArray();
        if (songs.isEmpty()) { done(QString()); return; }
        const QString pic = songs.first().toObject().value("album").toObject().value("picUrl").toString();
        done(pic);
    });
}

void NetEaseApi::lyric(const QString &songId,
                       std::function<void(const QString &, const QString &)> done) {
    lyricFull(songId, [done](const QString &lrc, const QString &trans, const QString &) {
        done(lrc, trans);
    });
}

void NetEaseApi::lyricFull(const QString &songId,
                           std::function<void(const QString &, const QString &, const QString &)> done) {
    QUrlQuery q;
    q.addQueryItem("id", songId);
    q.addQueryItem("lv", "1");
    q.addQueryItem("kv", "1");
    q.addQueryItem("tv", "-1");
    q.addQueryItem("yv", "-1");       // 逐字歌词（YRC；登录后才有内容）
    const QString url = "https://music.163.com/api/song/lyric?" + q.toString(QUrl::FullyEncoded);
    QNetworkReply *reply = net_->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { done(QString(), QString(), QString()); return; }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        done(root.value("lrc").toObject().value("lyric").toString(),
             root.value("tlyric").toObject().value("lyric").toString(),
             root.value("yrc").toObject().value("lyric").toString());
    });
}

void NetEaseApi::songUrl(const QString &songId, const QString &level,
                         std::function<void(const QString &, const QString &)> done) {
    // br 由 level 映射（旧接口用 br，单位为 bps）
    int br = 320000;
    if (level == "standard") br = 128000;
    else if (level == "lossless") br = 999000;

    QUrlQuery q;
    q.addQueryItem("ids", QString("[%1]").arg(songId));
    q.addQueryItem("br", QString::number(br));
    const QString url =
        "https://music.163.com/api/song/enhance/player/url?" + q.toString(QUrl::FullyEncoded);

    QNetworkReply *reply = net_->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done, songId, level] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(QString(), reply->errorString());
            return;
        }
        const QByteArray body = reply->readAll();
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        const QJsonArray arr = root.value("data").toArray();
        qInfo() << "网易云取地址 id=" << songId << "响应" << body.size() << "字节，data 条数=" << arr.size();
        if (arr.isEmpty()) { done(QString(), "返回为空"); return; }
        const QJsonObject o = arr.first().toObject();
        const QString u = o.value("url").toString();
        if (u.isEmpty()) {
            done(QString(), QString("无可用地址（code=%1，可能需要登录或该曲目受版权限制）")
                                .arg(o.value("code").toInt()));
            return;
        }
        qInfo() << "网易云取流: 请求档位=" << level << " 返回码率=" << o.value("br").toInt()
                << " 格式=" << o.value("type").toString() << " 大小=" << o.value("size").toDouble();
        if (status_) {
            status_(QString("音质：%1 kbps · %2")
                        .arg(o.value("br").toInt() / 1000)
                        .arg(o.value("type").toString()));
        }
        done(u, QString());
    });
}

void NetEaseApi::setForceDirect(bool on) {
    if (!net_) return;
    net_->setProxy(on ? QNetworkProxy(QNetworkProxy::NoProxy) : QNetworkProxy::applicationProxy());
    qInfo() << "NetEaseApi 代理设置:" << (on ? "强制直连" : "跟随系统");
}
