#include "QQMusicApi.h"

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

namespace {
const char *kUserAgent =
    "Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/120.0.0.0 Safari/537.36";
const char *kReferer = "https://y.qq.com/";
const char *kCgi = "https://u.y.qq.com/cgi-bin/musicu.fcg";
const char *kSearchApi = "https://c.y.qq.com/soso/fcgi-bin/client_search_cp";
const char *kGuid = "10000";

// 档位 → filename 前缀 / 扩展名 / 说明
struct Tier { const char *prefix; const char *ext; int br; const char *label; };
Tier tierOf(const QString &t) {
    if (t == "lossless") return { "F000", "flac", 999000, "无损 FLAC" };
    if (t == "standard") return { "M500", "mp3", 128000, "128k" };
    return { "M800", "mp3", 320000, "320k" };
}
}  // namespace

QQMusicApi::QQMusicApi(QObject *parent) : QObject(parent) {
    net_ = new QNetworkAccessManager(this);
}

QString QQMusicApi::cookieFile() {
    return QDir::homePath() + "/.config/hov/cookies/qqmusic.txt";
}

bool QQMusicApi::hasCookie() {
    return QFile::exists(cookieFile());
}

QQMusicApi::Cookie QQMusicApi::readCookie() const {
    Cookie ck;
    QFile f(cookieFile());
    if (!f.open(QIODevice::ReadOnly)) return ck;
    QStringList kv;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QStringList parts = line.split('\t');
        if (parts.size() < 7) continue;
        kv << QString("%1=%2").arg(parts[5], parts[6]);
        if (parts[5] == "uin" || parts[5] == "wxuin") {
            QString v = parts[6];
            if (v.startsWith("o")) v.remove(0, 1);          // QQ 登录常见 "o123456" 形式
            ck.uin = parts[6];
            ck.uinNumeric = v;
        } else if (parts[5] == "qm_keyst" || parts[5] == "qqmusic_key") {
            ck.musickey = parts[6];
        }
    }
    ck.header = kv.join("; ");
    return ck;
}

void QQMusicApi::search(const QString &keyword, int limit,
                        std::function<void(const QVector<Song> &)> done) {
    QUrlQuery q;
    q.addQueryItem("p", "1");
    q.addQueryItem("n", QString::number(limit));
    q.addQueryItem("w", keyword);
    q.addQueryItem("format", "json");
    const QString url = QString("%1?%2").arg(kSearchApi, q.toString(QUrl::FullyEncoded));

    QNetworkRequest r{QUrl(url)};
    r.setRawHeader("Referer", kReferer);
    r.setRawHeader("User-Agent", kUserAgent);
    const Cookie ck = readCookie();
    if (!ck.header.isEmpty()) {
        r.setRawHeader("Cookie", ck.header.toUtf8());
        qInfo() << "[cookie] QQ音乐请求附带 cookie（" << ck.header.size() << "字节，来自 qqmusic.txt）";
    }

    if (status_) status_(hasCookie() ? "QQ音乐搜索中（已带 cookie）" : "QQ音乐搜索中（匿名）");
    QNetworkReply *reply = net_->get(r);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (status_) status_(QString("QQ音乐搜索失败：%1").arg(reply->errorString()));
            done({});
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray list = root.value("data").toObject().value("song")
                                    .toObject().value("list").toArray();
        QVector<Song> out;
        for (const auto &v : list) {
            const QJsonObject o = v.toObject();
            Song s;
            s.mid = o.value("songmid").toString();
            s.name = o.value("songname").toString();
            s.album = o.value("albumname").toString();
            s.albumMid = o.value("albummid").toString();
            s.durationSec = o.value("interval").toInt();
            QStringList names;
            for (const auto &a : o.value("singer").toArray())
                names << a.toObject().value("name").toString();
            s.artist = names.join(" / ");
            if (!s.mid.isEmpty() && !s.name.isEmpty()) out.append(s);
        }
        if (status_) status_(QString("QQ音乐返回 %1 首").arg(out.size()));
        done(out);
    });
}

void QQMusicApi::vkey(const QString &mid, const QString &filename, const Cookie &ck, bool anon,
                      std::function<void(const QString &, int, const QString &)> done) {
    const QString effUin = anon ? QString("0") : (ck.uin.isEmpty() ? QString("0") : ck.uin);
    QJsonObject comm;
    comm["uin"] = effUin;
    comm["format"] = "json";
    comm["ct"] = 19;
    comm["cv"] = 0;
    if (!anon && !ck.musickey.isEmpty()) comm["authst"] = ck.musickey;   // VIP 鉴权关键

    QJsonObject param;
    param["guid"] = kGuid;
    param["songmid"] = QJsonArray{ mid };
    param["songtype"] = QJsonArray{ 0 };
    param["uin"] = effUin;
    param["loginflag"] = 1;
    param["platform"] = "20";
    param["filename"] = QJsonArray{ filename };

    QJsonObject req0;
    req0["module"] = "vkey.GetVkeyServer";
    req0["method"] = "CgiGetVkey";
    req0["param"] = param;

    QJsonObject body;
    body["comm"] = comm;
    body["req_0"] = req0;

    const QString query =
        "?-=getplaysongvkey&g_tk=5381&loginUin=" + QString::fromUtf8(QUrl::toPercentEncoding(effUin)) +
        "&hostUin=0&format=json&inCharset=utf8&outCharset=utf-8&notice=0"
        "&platform=yqq.json&needNewCode=0";

    QNetworkRequest r{QUrl(QString(kCgi) + query)};
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    r.setRawHeader("Referer", kReferer);
    r.setRawHeader("User-Agent", kUserAgent);
    if (!anon && !ck.header.isEmpty()) r.setRawHeader("Cookie", ck.header.toUtf8());

    QNetworkReply *reply = net_->post(r, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(QString(), -1, reply->errorString());
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject data = root.value("req_0").toObject().value("data").toObject();
        const QString sip = data.value("sip").toArray().isEmpty()
                                ? QString()
                                : data.value("sip").toArray().first().toString();
        const QJsonObject mu = data.value("midurlinfo").toArray().isEmpty()
                                   ? QJsonObject()
                                   : data.value("midurlinfo").toArray().first().toObject();
        const int result = mu.value("result").toInt(0);
        const QString purl = mu.value("purl").toString();
        if (purl.isEmpty() || purl == "null") {
            done(QString(), result, result == 104003 ? "无权限（该账号对此歌/此音质无权限）"
                                                     : QString("服务端未给出地址（result=%1）").arg(result));
            return;
        }
        const QString base = sip.isEmpty() ? QString("http://dl.stream.qqmusic.qq.com/") : sip;
        done(base + purl, result, QString());
    });
}

void QQMusicApi::lyric(const QString &mid,
                       std::function<void(const QString &, const QString &)> done) {
    QUrlQuery q;
    q.addQueryItem("songmid", mid);
    q.addQueryItem("format", "json");
    q.addQueryItem("nobase64", "1");     // 直接要明文 LRC；否则服务端给 base64
    q.addQueryItem("g_tk", "5381");
    const QString url =
        "https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?" + q.toString(QUrl::FullyEncoded);

    QNetworkRequest r{QUrl(url)};
    r.setRawHeader("Referer", kReferer);          // 该接口强校验 Referer
    r.setRawHeader("User-Agent", kUserAgent);
    const Cookie ck = readCookie();
    if (!ck.header.isEmpty()) r.setRawHeader("Cookie", ck.header.toUtf8());

    QNetworkReply *reply = net_->get(r);
    connect(reply, &QNetworkReply::finished, this, [reply, done, mid] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { done(QString(), QString()); return; }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QString lrc = root.value("lyric").toString();
        QString trans = root.value("trans").toString();
        // 兼容未加 nobase64 时返回 base64 的情况
        auto maybeB64 = [](QString t) {
            if (!t.isEmpty() && !t.trimmed().startsWith('[')) {
                const QByteArray dec = QByteArray::fromBase64(t.toUtf8());
                if (!dec.isEmpty()) t = QString::fromUtf8(dec);
            }
            return t;
        };
        lrc = maybeB64(lrc);
        trans = maybeB64(trans);
        qInfo() << "QQ歌词 mid=" << mid << "原文" << lrc.size() << "字节，翻译" << trans.size() << "字节";
        done(lrc, trans);
    });
}

void QQMusicApi::streamUrl(const QString &mid, const QString &tier,
                           std::function<void(const QString &, const QString &)> done) {
    const Tier t = tierOf(tier);
    const QString filename = QString("%1%2%2.%3").arg(t.prefix, mid, t.ext);
    const Cookie ck = readCookie();

    // 先在登录态试；失败且 cookie 里有数字 uin 时再试一次数字形式（与 Android 版同策略）
    auto attemptNumeric = [this, mid, filename, ck, done](const QString &err) {
        if (ck.uinNumeric.isEmpty() || ck.uinNumeric == ck.uin) { done(QString(), err); return; }
        Cookie alt = ck;
        alt.uin = ck.uinNumeric;
        vkey(mid, filename, alt, false, [done, err](const QString &url, int, const QString &e) {
            if (!url.isEmpty()) done(url, QString());
            else done(QString(), e.isEmpty() ? err : e);
        });
    };

    qInfo() << "QQ音乐取流：档位=" << t.label << " filename 前缀=" << t.prefix;
    vkey(mid, filename, ck, false, [this, mid, filename, ck, t, done, attemptNumeric](
                                      const QString &url, int result, const QString &err) {
        if (!url.isEmpty()) {
            if (status_) status_(QString("音质：%1").arg(t.label));
            done(url, QString());
            return;
        }
        if (result == 104003 || result == -1) {
            // 无权限/未登录 → 说明清楚（VIP 曲目需登录且有会员）
            qInfo() << "QQ音乐取流失败 result=" << result << err;
            attemptNumeric(err);
            return;
        }
        attemptNumeric(err);
    });
}

void QQMusicApi::setForceDirect(bool on) {
    if (!net_) return;
    net_->setProxy(on ? QNetworkProxy(QNetworkProxy::NoProxy) : QNetworkProxy::applicationProxy());
    qInfo() << "QQMusicApi 代理设置:" << (on ? "强制直连" : "跟随系统");
}
