#include "UrlResolver.h"
#include "NetPolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QProcess>
#include <QStandardPaths>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

QString UrlResolver::configDir() {
    return QDir::homePath() + "/.config/hov";
}

QString UrlResolver::serviceOf(const QString &pageUrl) {
    if (pageUrl.contains("youtube.com") || pageUrl.contains("youtu.be")) return "youtube";
    if (pageUrl.contains("bilibili.com") || pageUrl.contains("b23.tv")) return "bilibili";
    if (pageUrl.contains("music.163.com")) return "netease";
    if (pageUrl.contains("y.qq.com") || pageUrl.contains("qqmusic")) return "qqmusic";
    return QString();
}

QString UrlResolver::cookieFileFor(const QString &pageUrl) {
    const QString s = serviceOf(pageUrl);
    if (s.isEmpty()) return QString();
    const QString f = configDir() + "/cookies/" + s + ".txt";
    return QFile::exists(f) ? f : QString();
}

bool UrlResolver::isDirectMedia(const QString &url) {
    if (url.startsWith('/')) return true;                       // 本地文件
    if (url.contains("googlevideo.com") || url.contains("bilivideo.com")
        || url.contains("hdslb.com") || url.contains("126.net")) return true;
    static const char *exts[] = {".mp4", ".m3u8", ".mpd", ".mkv", ".webm",
                                 ".mp3", ".flac", ".m4a", ".aac", nullptr};
    for (int i = 0; exts[i]; ++i) {
        if (url.contains(exts[i])) return true;
    }
    return false;
}

UrlResolver::UrlResolver(QObject *parent) : QObject(parent) {}

void UrlResolver::resolveAndPlay(const QString &pageUrl) {
    if (pageUrl.isEmpty()) return;

    if (isDirectMedia(pageUrl)) {                                // 已经是直链/本地文件
        Stream s;
        s.videoUrl = pageUrl;
        if (play_) play_(s);
        return;
    }

    const QString service = serviceOf(pageUrl);
    const QString cookies = cookieFileFor(pageUrl);
    if (status_) {
        status_(QString("解析中…（%1）").arg(service.isEmpty() ? "未知站点" : service));
        if (!service.isEmpty()) {
            status_(cookies.isEmpty()
                        ? QString("未找到 cookie 文件：%1/cookies/%2.txt，按匿名解析").arg(configDir(), service)
                        : QString("使用 cookie：%1").arg(QFileInfo(cookies).fileName()));
        }
    }

    auto *p = new QProcess(this);
    QStringList args{"-J", "--no-warnings", "--no-playlist", "-f", "bv*+ba/b"};
    if (!cookies.isEmpty()) args << "--cookies" << cookies;
    args << pageUrl;

    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, p, service, pageUrl](int code, QProcess::ExitStatus) {
                const QByteArray out = p->readAllStandardOutput();
                const QByteArray err = p->readAllStandardError();
                p->deleteLater();

                if (code != 0) {
                    if (status_) {
                        status_(QString("解析失败（yt-dlp 退出码 %1）").arg(code));
                        const QString e = QString::fromUtf8(err).trimmed();
                        if (!e.isEmpty()) status_(e.left(300));
                        status_(NetPolicy::hintForFailure(service.isEmpty() ? "yt-dlp" : service));
                    }
                    return;
                }

                const QJsonObject o = QJsonDocument::fromJson(out).object();
                Stream s;
                s.title = o.value("title").toString();

                // 取地址要分层：当选择的是「视频+音频」合并格式时，requested_downloads 的条目
                // 只给出 format_id（如 397+251）而**没有 url**，真正的每条流在其 requested_formats 里。
                // 曾因此出现「解析结果为空」的假失败。
                auto takeFromFormats = [&s](const QJsonArray &arr, bool overwrite) {
                    for (const auto &v : arr) {
                        const QJsonObject f = v.toObject();
                        const QString u = f.value("url").toString();
                        if (u.isEmpty()) continue;
                        const QString vcodec = f.value("vcodec").toString();
                        const QString acodec = f.value("acodec").toString();
                        const bool isVideo = !vcodec.isEmpty() && vcodec != "none";
                        const bool isAudio = !acodec.isEmpty() && acodec != "none" && !isVideo;
                        if (isVideo) {
                            if (s.videoUrl.isEmpty() || overwrite) s.videoUrl = u;
                        } else if (isAudio) {
                            if (s.audioUrl.isEmpty() || overwrite) s.audioUrl = u;
                        } else if (s.videoUrl.isEmpty()) {
                            s.videoUrl = u;              // 未标注编码的单一流
                        }
                    }
                };

                for (const auto &v : o.value("requested_downloads").toArray()) {
                    const QJsonObject r = v.toObject();
                    const QString u = r.value("url").toString();   // 单格式选择：直接有 url
                    if (!u.isEmpty()) {
                        if (s.videoUrl.isEmpty()) s.videoUrl = u;
                        else if (s.audioUrl.isEmpty()) s.audioUrl = u;
                    }
                    takeFromFormats(r.value("requested_formats").toArray(), false);
                }
                if (s.videoUrl.isEmpty() && s.audioUrl.isEmpty()) {
                    // 最后回退：从 formats 里挑，覆盖写入所以留下的是最高画质/最佳音质
                    takeFromFormats(o.value("formats").toArray(), true);
                }
                if (s.videoUrl.isEmpty()) s.videoUrl = o.value("url").toString();

                if (s.videoUrl.isEmpty()) {
                    if (status_) status_("解析结果为空（该视频可能需要登录或受地区限制）");
                    return;
                }
                if (status_) {
                    status_(QString("正在播放：%1（%2）")
                                .arg(s.title.left(60), s.audioUrl.isEmpty() ? "单流" : "视频+音频分轨"));
                }
                // 在线视频：顺带抓字幕（best-effort，失败照样播）
                fetchSubtitles(pageUrl, service, s);
            });

    p->start("yt-dlp", args);
}

// ---------------- 在线字幕 ----------------
// 两条路线（都在 App 层做，播放器只管拿结果）：
//   ① B站：官方 CC API（x/web-interface/view → x/player/v2 → subtitle_url）
//      —— 直接拿 JSON 解析成内存字幕轨，**不落盘**；比 yt-dlp 稳（yt-dlp 在部分网络对 B站 常被 412）
//   ② YouTube：yt-dlp 抓 json3/srt 落地文件，再按本地轨加载（yt-dlp 对 YouTube 更省心）
// 任一环节失败都不影响播放：只记日志、退回另一条路线或直接播。
namespace {
QNetworkRequest siteRequest(const QUrl &url, const QString &referer) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36");
    if (!referer.isEmpty()) req.setRawHeader("Referer", referer.toUtf8());
    return req;
}
}   // namespace

void UrlResolver::applyProxy() {
    if (!net_) net_ = new QNetworkAccessManager(this);
    net_->setProxy(forceDirect_ ? QNetworkProxy(QNetworkProxy::NoProxy)
                                : QNetworkProxy::applicationProxy());
}

void UrlResolver::fetchSubtitles(const QString &pageUrl, const QString &service, Stream result) {
    if (service != "bilibili" && service != "youtube") {       // 其他站点：直接播，不做字幕尝试
        if (play_) play_(result);
        return;
    }
    auto shared = std::make_shared<Stream>(std::move(result));
    auto done = [this, shared](const QVector<SubtitleTrack> &tracks, const QString &err) {
        if (!err.isEmpty() && status_) status_(err);
        shared->subtitles = tracks;
        if (status_ && !tracks.isEmpty())
            status_(QString("已获取 %1 条字幕轨（按 C 键切换）").arg(tracks.size()));
        else if (status_ && err.isEmpty())
            status_("未获取到字幕轨（可继续观看）");
        if (play_) play_(*shared);
    };
    if (service == "bilibili") fetchBiliCc(pageUrl, done);
    else fetchByYtDlp(pageUrl, done);
}

// ① B站 CC（官方接口；无字幕清单 = 该视频确实没有 CC，不再回退，免得白等）
void UrlResolver::fetchBiliCc(const QString &pageUrl,
                              std::function<void(QVector<SubtitleTrack>, QString)> done) {
    static const QRegularExpression bvRe("(BV[0-9A-Za-z]{10})");
    static const QRegularExpression avRe("av(\\d+)", QRegularExpression::CaseInsensitiveOption);
    const QString bv = bvRe.match(pageUrl).captured(1);
    const QString av = avRe.match(pageUrl).captured(1);
    if (bv.isEmpty() && av.isEmpty()) {
        done({}, "B站字幕：URL 里没有 BV/av 号");
        return;
    }
    applyProxy();
    qInfo() << "B站字幕：查询 CC 列表" << bv;
    if (status_) status_("B站字幕：查询 CC 列表…");
    auto tracks = std::make_shared<QVector<SubtitleTrack>>();

    const QUrl viewUrl(bv.isEmpty()
                           ? QString("https://api.bilibili.com/x/web-interface/view?aid=%1").arg(av)
                           : QString("https://api.bilibili.com/x/web-interface/view?bvid=%1").arg(bv));
    QNetworkReply *r1 = net_->get(siteRequest(viewUrl, "https://www.bilibili.com"));
    connect(r1, &QNetworkReply::finished, this, [this, r1, tracks, done, pageUrl] {
        r1->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(r1->readAll()).object();
        const QJsonObject d = o.value("data").toObject();
        const qint64 aid = d.value("aid").toVariant().toLongLong();
        const qint64 cid = d.value("cid").toVariant().toLongLong();
        qInfo() << "B站字幕：view 接口 code=" << o.value("code").toInt() << "aid=" << aid << "cid=" << cid;
        if (o.value("code").toInt() != 0 || aid <= 0 || cid <= 0) {
            done({}, QString("B站字幕：view 接口失败（code=%1）").arg(o.value("code").toInt()));
            return;
        }
        const QUrl pv2(QString("https://api.bilibili.com/x/player/v2?aid=%1&cid=%2").arg(aid).arg(cid));
        QNetworkReply *r2 = net_->get(siteRequest(pv2, pageUrl));
        connect(r2, &QNetworkReply::finished, this, [this, r2, tracks, done] {
            r2->deleteLater();
            const QJsonObject o = QJsonDocument::fromJson(r2->readAll()).object();
            const QJsonArray subs =
                o.value("data").toObject().value("subtitle").toObject().value("subtitles").toArray();
            qInfo() << "B站字幕：player/v2 code=" << o.value("code").toInt() << "轨数=" << subs.size();
            if (o.value("code").toInt() != 0) {
                done({}, QString("B站字幕：player/v2 失败（code=%1）").arg(o.value("code").toInt()));
                return;
            }
            if (subs.isEmpty()) {
                done({}, QString());                 // 该视频没有 CC（不是错误）
                return;
            }
            auto pending = std::make_shared<int>(subs.size());
            for (const auto &v : subs) {
                const QJsonObject so = v.toObject();
                QString url = so.value("subtitle_url").toString();
                if (url.startsWith("//")) url.prepend("https:");
                const QString label = QString("%1 · CC")
                                          .arg(so.value("lan_doc").toString(so.value("lan").toString()));
                if (url.isEmpty()) {
                    if (--*pending == 0) done(*tracks, QString());
                    continue;
                }
                QNetworkReply *r3 = net_->get(siteRequest(QUrl(url), "https://www.bilibili.com"));
                connect(r3, &QNetworkReply::finished, this, [this, r3, tracks, label, pending, done] {
                    r3->deleteLater();
                    const QVector<SubtitleCue> cues =
                        Subtitles::parseJson(QString::fromUtf8(r3->readAll()));
                    if (!cues.isEmpty()) {
                        SubtitleTrack t;
                        t.label = label;
                        t.cues = cues;               // 内存轨：不落盘
                        tracks->append(t);
                        qInfo() << "B站字幕轨已取回:" << label << cues.size() << "行";
                    } else {
                        qInfo() << "B站字幕：内容解析为空" << label;
                    }
                    if (--*pending == 0) {
                        if (tracks->isEmpty()) done({}, "B站：字幕内容为空");
                        else done(*tracks, QString());
                    }
                });
            }
        });
    });
}

// ② yt-dlp 路线（YouTube；B站 API 失败时的兜底）
void UrlResolver::fetchByYtDlp(const QString &pageUrl,
                               std::function<void(QVector<SubtitleTrack>, QString)> done) {
    const QString dirBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(pageUrl.toUtf8(), QCryptographicHash::Md5).toHex().left(16));
    const QString dir = (dirBase.isEmpty() ? QDir::homePath() + "/.cache" : dirBase) + "/hov/subs/" + hash;
    QDir().mkpath(dir);
    if (status_) status_("正在获取字幕轨…");

    auto *p = new QProcess(this);
    QStringList args{"--skip-download", "--no-warnings", "--no-playlist",
                     "--write-subs", "--write-auto-subs",
                     "--sub-langs", "zh-Hans,zh-CN,zh,zh-Hant,zh-TW,en",
                     "--sub-format", "srt/vtt/best",
                     "-o", dir + "/%(id)s.%(ext)s", pageUrl};
    const QString cookies = cookieFileFor(pageUrl);
    if (!cookies.isEmpty()) args << "--cookies" << cookies;
    qInfo() << "在线字幕：yt-dlp 抓取（" << args.size() << "个参数）→" << dir;

    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, p, dir, done](int code, QProcess::ExitStatus) {
                const QString errText = QString::fromUtf8(p->readAllStandardError()).trimmed();
                p->deleteLater();
                // 扫描落地文件：<id>.<lang>.<ext>
                QDir d(dir);
                QVector<SubtitleTrack> tracks;
                const QStringList subs = d.entryList({"*.srt", "*.vtt", "*.json3", "*.json"}, QDir::Files, QDir::Name);
                for (const QString &name : subs) {
                    const QFileInfo fi(d.filePath(name));
                    if (fi.size() <= 0) continue;
                    QString lang = fi.completeBaseName();
                    lang = lang.section('.', 1);                     // 去掉视频 id
                    if (lang.isEmpty()) lang = "默认";
                    SubtitleTrack t;
                    t.label = QString("%1 · %2").arg(lang, fi.suffix().toUpper());
                    t.source = fi.absoluteFilePath();
                    tracks.append(t);
                }
                // 语言优先级：中文简体排在英文前面（否则自动启用的是英文字幕）
                std::stable_sort(tracks.begin(), tracks.end(),
                                 [](const SubtitleTrack &a, const SubtitleTrack &b) {
                                     return Subtitles::languageRank(a.label) < Subtitles::languageRank(b.label);
                                 });
                if (!tracks.isEmpty()) {
                    qInfo() << "在线字幕：yt-dlp 取回" << tracks.size() << "条";
                    done(tracks, QString());
                } else {
                    // 把 yt-dlp 的真实原因带出来（实测本环境是 timedtext HTTP 429）
                    const QString tail = errText.section('\n', -3).trimmed();
                    qInfo() << "在线字幕：yt-dlp 未取到字幕 exit=" << code << tail;
                    done({}, QString("字幕获取失败（exit=%1）%2").arg(code).arg(tail.left(160)));
                }
            });
    connect(p, &QProcess::errorOccurred, this, [this, p, done](QProcess::ProcessError) {
        p->deleteLater();
        done({}, "字幕获取失败：yt-dlp 无法启动");
    });
    p->start("yt-dlp", args);
}

// ③ 单独取在线字幕（--subs-url）：视频页 → 站点路线；字幕文件地址 → 直接取
void UrlResolver::fetchOnlineSubs(const QString &urlOrPage,
                                  std::function<void(const QVector<SubtitleTrack> &, const QString &)> done) {
    const QString service = serviceOf(urlOrPage);
    const QString path = QFileInfo(QUrl(urlOrPage).path()).suffix().toLower();
    const bool looksLikeSubFile = (path == "json" || path == "srt" || path == "vtt" || path == "lrc");
    if (!service.isEmpty() && !looksLikeSubFile) {
        const bool bili = (service == "bilibili");
        auto inner = [done](const QVector<SubtitleTrack> &tracks, const QString &err) { done(tracks, err); };
        if (bili) fetchBiliCc(urlOrPage, inner);
        else if (service == "youtube") fetchByYtDlp(urlOrPage, inner);
        else done({}, QString("该站点暂不支持在线字幕：%1").arg(service));
        return;
    }
    fetchSubtitleUrl(urlOrPage, done);
}

// ③ 直接取一个字幕地址（--subs-url：自动化验证用；也方便手工挂在线字幕）
void UrlResolver::fetchSubtitleUrl(const QString &url,
                                   std::function<void(const QVector<SubtitleTrack> &, const QString &)> done) {
    applyProxy();
    QNetworkReply *r = net_->get(siteRequest(QUrl(url), "https://www.bilibili.com"));
    connect(r, &QNetworkReply::finished, this, [r, url, done = std::move(done)] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            done({}, QString("取字幕失败：%1").arg(r->errorString()));
            return;
        }
        const QByteArray body = r->readAll();
        QString ext = QFileInfo(QUrl(url).path()).suffix().toLower();
        const QString head = QString::fromUtf8(body.left(200));
        if (ext != "json" && ext != "srt" && ext != "vtt" && ext != "lrc")
            ext = head.contains("\"body\"") || head.contains("\"events\"") ? "json"
                  : (head.contains("WEBVTT") ? "vtt" : "srt");
        const QVector<SubtitleCue> cues = Subtitles::parse(QString::fromUtf8(body), ext);
        qInfo() << "在线字幕：收到" << body.size() << "字节，按" << ext << "解析出" << cues.size() << "行";
        if (cues.isEmpty()) {
            qInfo() << "在线字幕：解析为空，内容前 80 字节=" << body.left(80);
            done({}, "解析后没有字幕内容（格式不支持或文件为空）");
            return;
        }
        SubtitleTrack t;
        t.label = QString("在线字幕 · %1（%2 行）").arg(ext.toUpper()).arg(cues.size());
        t.cues = cues;
        done({ t }, QString());
    });
}
