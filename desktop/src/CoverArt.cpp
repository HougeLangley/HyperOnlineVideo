#include "CoverArt.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>

CoverArt::CoverArt() = default;
CoverArt::~CoverArt() = default;

QString CoverArt::cachePathFor(const QString &url) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString dir = (base.isEmpty() ? QDir::homePath() + "/.cache" : base) + "/hov/covers";
    QDir().mkpath(dir);
    const QString h = QString::fromLatin1(
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex());
    return dir + "/" + h + ".img";
}

void CoverArt::clear() {
    img_ = QImage();
    url_.clear();
    title_.clear();
    artist_.clear();
}

void CoverArt::load(const QString &url, const QString &title, const QString &artist) {
    title_ = title;
    artist_ = artist;
    if (url.isEmpty()) {
        clear();
        return;
    }
    if (url == url_ && !img_.isNull()) return;      // 同一张，不重复下载
    url_ = url;
    img_ = QImage();

    const QString cached = cachePathFor(url);
    QFile f(cached);
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        const QByteArray raw = f.readAll();
        f.close();
        if (img_.loadFromData(raw)) {
            qInfo() << "封面：命中缓存" << img_.size();
            return;
        }
    }
    if (!net_) net_ = std::make_unique<QNetworkAccessManager>();   // 懒创建
    QNetworkRequest req{QUrl(url)};   // 注意：括号写法会被解析成函数声明（most vexing parse）
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36");
    req.setRawHeader("Referer", "https://music.163.com/");
    QNetworkReply *r = net_->get(req);
    QObject::connect(r, &QNetworkReply::finished, r, [this, r, cached, url] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            qInfo() << "封面下载失败:" << r->errorString();
            return;
        }
        const QByteArray body = r->readAll();
        // 期间可能已经换歌：只在 url 仍是当前请求时采纳
        if (url != url_ || !img_.loadFromData(body)) {
            qInfo() << "封面：数据无效或已换歌（" << body.size() << "字节）";
            return;
        }
        QFile out(cached);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out.write(body);
            out.close();
        }
        qInfo() << "封面已加载:" << img_.width() << "x" << img_.height();
    });
}

void CoverArt::paint(QPainter &p, const QRect &area) const {
    if (img_.isNull() || area.isEmpty()) return;
    const int side = qBound(96, area.height() / 3, 200);
    const QRect box(area.left() + 18, area.top() + 18, side, side);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPainterPath clip;
    clip.addRoundedRect(box, 12, 12);
    p.setClipPath(clip);
    p.drawImage(box, img_);
    p.setClipping(false);
    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    p.drawRoundedRect(box, 12, 12);

    if (!title_.isEmpty()) {
        QFont f = p.font();
        f.setPointSizeF(qMax(10.0, side * 0.085));
        f.setBold(true);
        p.setFont(f);
        const QRect t1(box.left(), box.bottom() + 6, qMin(area.width() - box.left() - 18, side * 2), 26);
        p.setPen(QColor(255, 255, 255, 235));
        p.drawText(t1, Qt::AlignLeft | Qt::AlignVCenter, title_);
        if (!artist_.isEmpty()) {
            f.setBold(false);
            f.setPointSizeF(qMax(9.0, side * 0.07));
            p.setFont(f);
            p.setPen(QColor(200, 210, 225, 220));
            p.drawText(t1.translated(0, 24), Qt::AlignLeft | Qt::AlignVCenter, artist_);
        }
    }
    p.restore();
}
