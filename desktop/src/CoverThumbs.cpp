// ── CoverThumbs.cpp：从 MainWindow.h 搬出的成员实现（文档 52/53/54 ✓ 零行为改动 ✓）──
// 搬运清单: applyCoverFor qqCoverUrl setItemThumb youTubeThumb
#include "MainWindow.h"

QString MainWindow::qqCoverUrl(const QString &albumMid) {
        return albumMid.isEmpty() ? QString()
                                  : QString("https://y.gtimg.cn/music/photo_new/T002R300x300M000%1.jpg").arg(albumMid);
}

void MainWindow::applyCoverFor(const QString &key, const QString &label) {
        const QString t = label.isEmpty() ? titleMap_.value(key) : label;
        const QString cover = coverMap_.value(key);
        if (!cover.isEmpty()) {
            lastCoverUrl_ = cover;
            player_->setCoverArt(cover, titleOf(t), artistOf(t));
            return;
        }
        if (key.startsWith("netease:")) {
            fetchNetEaseCover(key.mid(8), titleOf(t), artistOf(t));
            return;
        }
        player_->setCoverArt(QString(), QString(), QString());   // 其他来源不显示封面
}

void MainWindow::setItemThumb(QListWidgetItem *item, const QString &url) {
        if (!item || url.isEmpty()) return;
        // 标记为"卡片"并写入来源（按缩略图域名判定；macOS 卡片下方也有一行来源）
        item->setData(Qt::UserRole + 12, true);
        item->setData(Qt::UserRole + 13, url);                 // 记缩略图 URL → 完成后按 URL 回填全部同源卡片 ✓
        if (url.contains("ytimg"))                        item->setData(Qt::UserRole + 10, QStringLiteral("YouTube"));
        else if (url.contains("hdslb"))                   item->setData(Qt::UserRole + 10, QStringLiteral("B站"));
        else if (url.contains("qq.com") || url.contains("gtimg")) item->setData(Qt::UserRole + 10, QStringLiteral("QQ音乐"));
        else if (url.contains("126.net"))                 item->setData(Qt::UserRole + 10, QStringLiteral("网易云音乐"));
        if (thumbCache_.contains(url)) {
            const QPixmap pm = thumbCache_.value(url);
            if (!pm.isNull()) item->setIcon(QIcon(pm));
            return;                                            // 占位（在途）或已完成：完成后统一回填 ✓
        }
        thumbCache_.insert(url, QPixmap());                    // 占位，防止重复请求
        if (!thumbNet_) thumbNet_ = new QNetworkAccessManager(this);
        QNetworkRequest req{QUrl(url)};
        req.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (X11; Linux x86_64) Chrome/120.0 Safari/537.36");
        if (url.contains("hdslb")) req.setRawHeader("Referer", "https://www.bilibili.com/");
          else if (url.contains("ytimg")) req.setRawHeader("Referer", "https://www.youtube.com/");
          else if (url.contains("qq.com") || url.contains("gtimg")) req.setRawHeader("Referer", "https://y.qq.com/");
          else if (url.contains("126.net")) req.setRawHeader("Referer", "https://music.163.com/");
        QNetworkReply *r = thumbNet_->get(req);
        connect(r, &QNetworkReply::finished, this, [this, r, url] {
            r->deleteLater();
            if (r->error() != QNetworkReply::NoError) { thumbCache_.remove(url); return; }
            QPixmap pm;
            if (!pm.loadFromData(r->readAll())) { thumbCache_.remove(url); return; }
            pm = pm.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation);  // 卡片 160×90 显示 → 2× 缓存（HiDPI）
            thumbCache_.insert(url, pm);
            ++thumbLoaded_;
            if (thumbLoaded_ == 1 || thumbLoaded_ % 10 == 0)
                qInfo() << "缩略图已加载" << thumbLoaded_ << "张";
            // 回填**所有**持同一 URL 的条目（2026-09-25 ✗ 用户实测"同专辑多曲只出第一张封面"）：
            //   旧逻辑"占位在途"直接 return ✗ → 后续同 URL 条目永远拿不到图标 ✗
            for (int i = 0; i < results_->count(); ++i) {
                auto *it = results_->item(i);
                if (it && it->data(Qt::UserRole + 13).toString() == url) it->setIcon(QIcon(pm));
            }
        });
}

QString MainWindow::youTubeThumb(const QString &id) { return QString("https://i.ytimg.com/vi/%1/mqdefault.jpg").arg(id); }
