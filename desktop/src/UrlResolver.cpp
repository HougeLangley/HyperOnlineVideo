#include "HovLog.h"
#include "UrlResolver.h"
#include "NetPolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <cstdio>
#include <QTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QEventLoop>
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
    if (url.startsWith('/') || url.startsWith("file://")) return true;   // 本地文件（file:// 与 macOS 对齐 ✓ P2-7 ✓）
    if (url.contains("googlevideo.com") || url.contains("bilivideo.com")
        || url.contains("hdslb.com") || url.contains("126.net")) return true;
    // 扩展名白名单：与 macOS 取**并集**（审计 P2-7 ✓ 曾各缺 .mov/.ts 与 .mpd ✗）
    static const char *exts[] = {".mp4", ".m3u8", ".mpd", ".mkv", ".webm",
                                 ".mp3", ".flac", ".m4a", ".aac",
                                 ".mov", ".ts", nullptr};
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
        if (!cookiesFromBrowser_.isEmpty()) {
            status_(QString("使用系统浏览器 cookie（%1），并会回写到 %2.txt")
                        .arg(cookiesFromBrowser_, service.isEmpty() ? QString("cookies") : service));
        } else if (!service.isEmpty()) {
            status_(cookies.isEmpty()
                        ? QString("未找到 cookie 文件：%1/cookies/%2.txt，按匿名解析").arg(configDir(), service)
                        : QString("使用 cookie：%1").arg(QFileInfo(cookies).fileName()));
        }
    }

    auto *p = new QProcess(this);
    // ⚠ 超时保护（v1.2.0）：yt-dlp 在网络受限/被风控时会**长时间无任何输出** →
    // 界面表现为"点了没反应、视频区全黑、无任何提示"（用户实测）。这里 45 秒兜底终止并提示。
    auto *deadline = new QTimer(p);
    deadline->setSingleShot(true);
    deadline->setInterval(45000);
    connect(deadline, &QTimer::timeout, this, [this, p, service] {
        if (p->state() == QProcess::NotRunning) return;
        p->kill();
        hovLog("[PLAY] 解析超时（45s）→ 已终止 yt-dlp\n");
        if (status_)
            status_(QString("解析超时（45 秒）：%1 无响应 —— 可能是网络受限或站点风控，请检查网络/登录状态")
                        .arg(service.isEmpty() ? QString("yt-dlp") : service));
    });
    deadline->start();
    connect(p, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        hovLog("[PLAY] yt-dlp 启动失败\n");
        if (status_) status_("无法启动 yt-dlp（未安装或不在 PATH 中？）");
    });
    QStringList args{"-J", "--no-warnings", "--no-playlist"};
    args << formatArgsFor(maxHeight_);
    args << cookieArgsFor(pageUrl);
    args << pageUrl;

    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, p, service, pageUrl, deadline](int code, QProcess::ExitStatus) {
                deadline->stop();
                const QByteArray out = p->readAllStandardOutput();
                const QByteArray err = p->readAllStandardError();
                // v1.2.0：把 yt-dlp 的 WARNING/ERROR 摘要**送到状态行**。
                // 以前只在退出码非 0 时显示 stderr → "能解析但拿不到正常格式"（n challenge 失败、
                // YouTube 强制 SABR、版本过期等）用户**完全看不到原因**，只看到黑屏（实测踩到）。
                {
                    const QStringList el = QString::fromUtf8(err).split('\n', Qt::SkipEmptyParts);
                    for (const QString &l : el) {
                        if (!l.contains("ERROR") && !l.contains("WARNING")) continue;
                        const QString t = l.trimmed().left(200);
                        if (status_) status_(QString("yt-dlp：%1").arg(t));
                        hovLog("[PLAY] yt-dlp: %s\n", t.toUtf8().constData());
                        break;              // 只报第一条避免刷屏（完整信息仍在 yt-dlp 自身日志里）
                    }
                }
                p->deleteLater();

                if (code != 0) {
                    if (status_) {
                        status_(QString("解析失败（yt-dlp 退出码 %1）").arg(code));
                        hovLog("[PLAY] 解析失败 rc=%d\n", code);
                        const QString e = QString::fromUtf8(err).trimmed();
                        if (!e.isEmpty()) status_(e.left(300));
                        status_(NetPolicy::hintForFailure(service.isEmpty() ? "yt-dlp" : service));
                    }
                    return;
                }

                const QJsonObject o = QJsonDocument::fromJson(out).object();
                Stream s;
                s.title = o.value("title").toString();
                // 客观判据：记下实际选中的视频格式（验证"切清晰度真的生效"）
                {
                    const QJsonArray rd = o.value("requested_downloads").toArray();
                    if (!rd.isEmpty()) {
                        const QJsonObject f0 = rd.first().toObject();
                        qInfo() << "解析档位=≤" << qualityLabel(maxHeight_) << " 实际选中="
                                << f0.value("height").toInt() << "p" << f0.value("format_note").toString()
                                << f0.value("vcodec").toString().left(12);
                    }
                }

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
                    hovLog("[PLAY] 解析成功: 视频=%s(%lld) 音频=%s(%lld)\n",
                                 QUrl(s.videoUrl).host().toUtf8().constData(), (long long)s.videoUrl.size(),
                                 QUrl(s.audioUrl).host().toUtf8().constData(), (long long)s.audioUrl.size());
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

// ── B站 wbi 签名（2026-09-28 ✓ 现行标准）：nav 取密钥 → mixin 表 → w_rid=md5(query+mixin) ──
static const int kBiliWbiTab[64] = {
    46, 47, 18, 2, 53, 8, 23, 32, 15, 50, 10, 31, 58, 3, 45, 35, 27, 43, 5, 49,
    33, 9, 42, 19, 29, 28, 14, 39, 12, 38, 41, 13, 37, 48, 7, 16, 24, 55, 40, 61,
    26, 17, 0, 1, 60, 51, 30, 4, 22, 25, 54, 21, 56, 59, 6, 63, 57, 62, 11, 36,
    20, 34, 44, 52
};

static QString biliWbiSign(const QMap<QString, QString> &params, const QString &mixin) {
    QStringList parts;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {   // QMap 已按 key 排序 ✓
        QString v = it.value();
        v.remove(QRegularExpression("[!'()*]"));
        parts << QUrl::toPercentEncoding(it.key()) + "=" + QUrl::toPercentEncoding(v);   // RFC3986 ✓（与 encodeURIComponent 同语义 ✓）
    }
    const QString query = parts.join("&");
    const QString rid = QString::fromUtf8(QCryptographicHash::hash((query + mixin).toUtf8(), QCryptographicHash::Md5).toHex());
    return query + "&w_rid=" + rid;
}

/** 搜索响应 → 列表（wbi 与旧接口共用 ✓ 2026-09-28 去重 ✓） */
static QVector<UrlResolver::BiliVideo> biliParseSearch(const QJsonObject &root) {
    QVector<UrlResolver::BiliVideo> out;
    for (const auto &v : root.value("data").toObject().value("result").toArray()) {
        const QJsonObject o = v.toObject();
        UrlResolver::BiliVideo b;
        b.bvid = o.value("bvid").toString();
        b.title = o.value("title").toString();
        b.title.remove(QRegularExpression("<[^>]+>"));      // 去掉 <em class="keyword">
        b.author = o.value("author").toString();
        b.duration = o.value("duration").toString();
        b.pic = o.value("pic").toString();
        if (b.pic.startsWith("//")) b.pic.prepend("https:");
        if (!b.bvid.isEmpty() && !b.title.isEmpty()) out.append(b);
    }
    return out;
}

/** wbi 混合密钥（32 位）；缓存 6h ✓（B站每日轮换 → 自动刷新 ✓） */
QString UrlResolver::biliWbiMixin() {
    static QString cached;
    static qint64 at = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!cached.isEmpty() && now - at < 6LL * 3600 * 1000) return cached;
    QNetworkRequest req = siteRequest(QUrl("https://api.bilibili.com/x/web-interface/nav"), "https://www.bilibili.com/");
    QEventLoop loop;
    QNetworkReply *r = net_->get(req);
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    r->deleteLater();
    const QJsonObject wi = QJsonDocument::fromJson(r->readAll()).object()
                               .value("data").toObject().value("wbi_img").toObject();
    const QString img = QUrl(wi.value("img_url").toString()).fileName().section('.', 0, 0);
    const QString sub = QUrl(wi.value("sub_url").toString()).fileName().section('.', 0, 0);
    const QString orig = img + sub;
    if (orig.size() < 64) { qInfo() << "B站 wbi 密钥获取失败 ✗"; return QString(); }
    QString mix;
    for (int idx : kBiliWbiTab) mix += orig.at(idx);
    cached = mix.left(32);
    at = now;
    return cached;
}


// B站搜索（2026-09-28）：wbi 签名路径优先（现行标准 ✓）；失败自动回退旧接口（现状仍可用 ✓ 双保险 ✓）
QVector<UrlResolver::BiliVideo> UrlResolver::searchBili(const QString &keyword, int pageSize) {
    QVector<BiliVideo> out;
    if (keyword.trimmed().isEmpty()) return out;
    applyProxy();
    const QString filt = biliFilterQuery();                       // UI-4-B ✓ 过滤器参数（排序/时长 ✓）
    QMap<QString, QString> wbiParams;
    wbiParams["search_type"] = "video";
    wbiParams["keyword"] = keyword;
    wbiParams["page"] = "1";
    wbiParams["page_size"] = QString::number(pageSize);
    wbiParams["wts"] = QString::number(QDateTime::currentSecsSinceEpoch());
    for (const QString &kv : filt.split('&', Qt::SkipEmptyParts)) {   // "&order=..&duration=.." → 拆进签名参数 ✓
        const int eq = kv.indexOf('=');
        if (eq > 0) wbiParams.insert(kv.left(eq), kv.mid(eq + 1));
    }
    const QString mixin = biliWbiMixin();
    if (!mixin.isEmpty()) {
        const QUrl wu("https://api.bilibili.com/x/web-interface/wbi/search/type?" + biliWbiSign(wbiParams, mixin));
        QNetworkRequest wreq = siteRequest(wu, "https://www.bilibili.com/");
        const QString wck = cookieHeaderFor("bilibili");
        if (!wck.isEmpty()) wreq.setRawHeader("Cookie", wck.toUtf8());
        QEventLoop loop;
        QNetworkReply *wr = net_->get(wreq);
        connect(wr, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        wr->deleteLater();
        const QJsonObject wroot = QJsonDocument::fromJson(wr->readAll()).object();
        if (wroot.value("code").toInt(-1) == 0) {
            out = biliParseSearch(wroot);
            qInfo() << "B站搜索[wbi]:" << keyword << "→" << out.size() << "条";
            return out;
        }
        qInfo() << "B站搜索[wbi] code=" << wroot.value("code").toInt(-1) << "→ 回退旧接口";
    }
    const QString enc = QString::fromUtf8(QUrl::toPercentEncoding(keyword));
    const QUrl u(QString("https://api.bilibili.com/x/web-interface/search/type?search_type=video"
                         "&keyword=%1&page=1&page_size=%2%3").arg(enc).arg(pageSize).arg(filt));
    qInfo() << "[过滤器] B站搜索 URL =" << u.toString();                // 自动化可判定 ✓
    QNetworkRequest req = siteRequest(u, "https://www.bilibili.com/");
    const QString ck = cookieHeaderFor("bilibili");
    if (!ck.isEmpty()) req.setRawHeader("Cookie", ck.toUtf8());
    QEventLoop loop;
    QNetworkReply *r = net_->get(req);
    connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    r->deleteLater();
    const QJsonObject root = QJsonDocument::fromJson(r->readAll()).object();
    const int code = root.value("code").toInt(-1);
    if (code != 0) {
        qInfo() << "B站搜索返回 code=" << code;
        return out;
    }
    out = biliParseSearch(root);
    qInfo() << "B站搜索:" << keyword << "→" << out.size() << "条";
    return out;
}

// 站点 cookie 文件 → Cookie 头（音乐 API 与 B站搜索共用）
QString UrlResolver::cookieHeaderFor(const QString &site) const {
    const QString path = configDir() + "/cookies/" + site + ".txt";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QStringList pairs;
    for (const QString &line : QString::fromUtf8(f.readAll()).split(QRegularExpression("\r?\n"))) {
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith('#')) continue;
        const QStringList parts = t.split('\t');
        if (parts.size() >= 7) pairs << parts.at(5) + "=" + parts.at(6);
    }
    return pairs.join("; ");
}

QStringList UrlResolver::formatArgsFor(int maxHeight) {
    if (maxHeight == -1) return {"-f", "ba/b"};                       // 仅音频
    if (maxHeight <= 0) return {"-f", "bv*+ba/b"};                    // 自动
    // 只给**视频部分**设 height 上限：音频格式没有 height，加了过滤会让整条失配、
    // 悄悄退回不设限的兜底（macOS 端实测踩到，两端统一修正）
    const QString cap = QString::number(maxHeight);
    // 同一个 %1 出现两次：只传一个参数（Qt 会替换所有 %1）。传两个会触发
    // "1 argument(s) missing" 警告——因为字符串里并没有 %2。
    return {"-f", QString("bv*[height<=%1]+ba/b[height<=%1]/bv*+ba/b").arg(cap)};
}

QString UrlResolver::qualityLabel(int h) {
    if (h == -1) return "仅音频";
    if (h <= 0) return "自动";
    return QString("%1p").arg(h);
}

QString UrlResolver::cookiePathFor(const QString &pageUrl) {
    const QString s = serviceOf(pageUrl);
    return s.isEmpty() ? QString() : configDir() + "/cookies/" + s + ".txt";
}

QStringList UrlResolver::cookieArgsFor(const QString &pageUrl) const {
    QStringList a;
    if (!cookiesFromBrowser_.isEmpty()) a << "--cookies-from-browser" << cookiesFromBrowser_;
    const QString f = cookiePathFor(pageUrl);
    if (f.isEmpty()) return a;
    if (cookiesFromBrowser_.isEmpty()) {
        if (QFile::exists(f)) a << "--cookies" << f;      // 老行为：有文件才传
    } else {
        a << "--cookies" << f;                            // 浏览器模式：无论存在与否都传 → yt-dlp 落盘导出
    }
    return a;
}

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
                connect(r3, &QNetworkReply::finished, this, [r3, tracks, label, pending, done] {
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
                     // 字幕语言随**系统语言**（中文系统仍中文优先 ✓ 德语系统会抓 de ✓ 与另两端一致 ✓）
                     "--sub-langs", Subtitles::subLangsForSystem(),
                     "--sub-format", "srt/vtt/best",
                     "-o", dir + "/%(id)s.%(ext)s", pageUrl};
    args << cookieArgsFor(pageUrl);
    qInfo() << "在线字幕：yt-dlp 抓取（" << args.size() << "个参数）→" << dir;

    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [p, dir, done](int code, QProcess::ExitStatus) {
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
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError) {
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
