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
    // 版式对齐 macOS 音乐模式（2026-09-25 用户反馈：封面过小、歌词拥挤 ✓）：
    //   macOS：side = min(viewH×0.60, viewW×0.30)，x = viewW×0.05，**垂直居中**
    //   本端同比例；只保留一个绝对下限（小窗也能看清 ✓）
    const int side = qBound(120, qMin(int(area.height() * 0.60), int(area.width() * 0.30)),
                            qMax(120, area.height() - 72));
    const QRect box(area.left() + int(area.width() * 0.05), area.top() + (area.height() - side) / 2, side, side);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // ⚠ 2026-09-25 修复（实测）：在 QOpenGLWidget 的 paintGL 里，"setClipPath + 缩放 drawImage"
    //   组合不渲染（同函数中的 drawText/描边正常 ✓ 唯独该图不可见 ✗）。
    //   改为"预缩放 + 预圆角"到独立 QImage（不缩放、不 clip、直绘 ✓ 兼容性最好 ✓）——
    //   同时也避免了每帧做高质量缩放的 CPU 开销（side 变化时才重算 ✓）。
    // ⚠ 2026-09-25 修复（实测）：在 QOpenGLWidget 的 paintGL 里，`QPainter::drawImage`
    //   不渲染 —— 与 drawText（GL 原生 glyph 路径 ✓ 可见）不同，drawImage 在 GL 引擎下
    //   走 raster fallback ✗（QOpenGLWidget 中不显示 ✗）。改用 **drawPixmap**（GL 原生纹理路径 ✓）
    //   即可稳定显示；同时预缩放+预圆角（side/url 变时才重算 ✓）。
    if (rounded_.isNull() || roundedSide_ != side || roundedUrl_ != url_) {
        const QImage scaled = img_.scaled(side, side, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QPoint off((scaled.width() - side) / 2, (scaled.height() - side) / 2);
        QImage out(side, side, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        {
            QPainter rp(&out);
            rp.setRenderHint(QPainter::Antialiasing, true);
            rp.setRenderHint(QPainter::SmoothPixmapTransform, true);
            QPainterPath path;
            path.addRoundedRect(QRectF(0, 0, side, side), 12, 12);
            rp.setClipPath(path);
            rp.drawImage(QPoint(0, 0), scaled, QRect(off, QSize(side, side)));
        }
        rounded_ = out;
        roundedPix_ = QPixmap::fromImage(out);   // 一次性转 pixmap ✓ 之后每帧 drawPixmap 走 GL 纹理 ✓
        roundedSide_ = side;
        roundedUrl_ = url_;
    }
    p.drawPixmap(box.topLeft(), roundedPix_);
    // 1px 半透明白描边（贴在圆角外沿 ✓ 与预圆角图配合 ✓）
    p.setPen(QPen(QColor(255, 255, 255, 70), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(box.adjusted(0, 0, -1, -1), 12, 12);

    if (!title_.isEmpty()) {
        QFont f = p.font();
        f.setPointSizeF(qMax(11.0, side * 0.075));
        f.setBold(true);
        p.setFont(f);
        // 标题/歌手：封面下方（左对齐封面列 ✓ 与 macOS"标题与歌词同左边界"精神一致 ✓）
        const QRect t1(box.left(), box.bottom() + 8, qMin(area.width() - box.left() - 18, side * 2), 26);
        p.setPen(QColor(255, 255, 255, 238));
        p.drawText(t1, Qt::AlignLeft | Qt::AlignVCenter, title_);
        if (!artist_.isEmpty()) {
            f.setBold(false);
            f.setPointSizeF(qMax(9.5, side * 0.062));
            p.setFont(f);
            p.setPen(QColor(200, 210, 225, 220));
            p.drawText(t1.translated(0, 26), Qt::AlignLeft | Qt::AlignVCenter, artist_);
        }
    }
    p.restore();
}
