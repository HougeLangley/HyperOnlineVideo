#include "DownloadManager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

DownloadManager::DownloadManager(QObject *parent) : QObject(parent) {
    net_ = new QNetworkAccessManager(this);
    QDir().mkpath(defaultDir());
}

DownloadManager::~DownloadManager() {
    cancelAll();
}

QString DownloadManager::defaultDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty()) base = QDir::homePath() + "/下载";
    return base + "/hov";
}

DownloadManager::Job *DownloadManager::mutableFind(const QString &id) {
    for (auto &j : jobs_)
        if (j.id == id) return &j;
    return nullptr;
}

const DownloadManager::Job *DownloadManager::find(const QString &id) const {
    for (const auto &j : jobs_)
        if (j.id == id) return &j;
    return nullptr;
}

int DownloadManager::activeCount() const {
    int n = 0;
    for (const auto &j : jobs_)
        if (j.state == State::Running) ++n;
    return n;
}

QString DownloadManager::enqueue(const QString &url, const QString &title, const QString &fileNameHint,
                                 const QString &artist, const QString &album, const QString &coverUrl) {
    if (url.isEmpty()) return QString();
    Job j;
    j.id = QString("dl%1").arg(++seq_);
    j.url = url;
    j.title = title.isEmpty() ? QFileInfo(QUrl(url).path()).fileName() : title;

    // 文件名：优先用调用方给的提示，否则从 URL 路径猜；都拿不到就用 id
    QString name = fileNameHint;
    if (name.isEmpty()) name = QFileInfo(QUrl(url).path()).fileName();
    if (name.isEmpty()) name = j.id;
    // 简单净化：去掉路径分隔符与常见非法字符
    name.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "_");
    QString dir = this->dir();
    QDir().mkpath(dir);
    j.filePath = dir + "/" + name;
    j.artist = artist;
    j.album = album;
    j.coverUrl = coverUrl;
    // 断点不续传，直接覆盖：避免半截文件被误认为已完成
    if (QFile::exists(j.filePath)) QFile::remove(j.filePath);

    jobs_.append(j);
    if (progress_) progress_(j);
    pump();
    return j.id;
}

void DownloadManager::pump() {
    for (auto &j : jobs_) {
        if (activeCount() >= kMaxConcurrent) break;
        if (j.state == State::Queued) start(j.id);
    }
}

void DownloadManager::start(const QString &id) {
    for (auto &j : jobs_) {
        if (j.id != id) continue;
        j.state = State::Running;
        j.bytesDone = 0;
        j.bytesTotal = -1;
        j.error.clear();
        if (progress_) progress_(j);

        QNetworkRequest req{QUrl(j.url)};
        req.setRawHeader("User-Agent",
                         "Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 (KHTML, like Gecko) "
                         "Chrome/120.0.0.0 Safari/537.36");
        // 音乐 CDN（网易云/QQ）对 Referer 敏感，统一带上可提高成功率
        if (j.url.contains("126.net") || j.url.contains("music.163.com"))
            req.setRawHeader("Referer", "https://music.163.com/");
        else if (j.url.contains("qq.com"))
            req.setRawHeader("Referer", "https://y.qq.com/");

        auto *reply = net_->get(req);
        running_.insert(id, reply);
        auto *file = new QFile(j.filePath, this);
        if (!file->open(QIODevice::WriteOnly)) {
            file->deleteLater();
            running_.remove(id);
            reply->abort();
            reply->deleteLater();
            fail(id, QString("无法写入文件：%1").arg(j.filePath), false);
            return;
        }
        files_.insert(id, file);

        connect(reply, &QNetworkReply::readyRead, this, [this, id, reply] {
            if (auto *f = files_.value(id)) f->write(reply->readAll());
        });
        connect(reply, &QNetworkReply::downloadProgress, this, [this, id](qint64 got, qint64 total) {
            for (auto &j : jobs_) {
                if (j.id != id) continue;
                j.bytesDone = got;
                j.bytesTotal = total;
                if (progress_) progress_(j);
                break;
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this, id, reply] {
            const QNetworkReply::NetworkError err = reply->error();
            const QString errStr = reply->errorString();
            if (auto *f = files_.take(id)) {
                if (auto *j = mutableFind(id)) {
                    f->write(reply->readAll());
                    j->bytesDone = f->size();
                }
                f->close();
                f->deleteLater();
            }
            running_.remove(id);
            reply->deleteLater();

            if (err == QNetworkReply::OperationCanceledError) {
                for (auto &j : jobs_)
                    if (j.id == id && j.state != State::Canceled) { j.state = State::Canceled; if (progress_) progress_(j); }
            } else if (err != QNetworkReply::NoError) {
                fail(id, errStr, true);
            } else {
                for (auto &j : jobs_)
                    if (j.id == id) {
                        j.state = State::Done;
                        if (embedTags(j)) qInfo() << "已嵌入音乐标签:" << j.title << "/" << j.artist;
                        qInfo() << "下载完成:" << j.filePath << j.bytesDone << "字节";
                        if (maxTotalMb_ > 0) cleanupLru(static_cast<qint64>(maxTotalMb_) * 1024 * 1024);
                        if (progress_) progress_(j);
                    }
            }
            pump();
        });
        break;
    }
}

DownloadManager::CleanResult DownloadManager::cleanupLru(qint64 maxBytes) {
    CleanResult res;
    QDir d(dir());
    if (!d.exists() || maxBytes <= 0) return res;      // 0 = 不限制
    static const QStringList exts{"*.mp3", "*.flac", "*.m4a", "*.wav", "*.mp4", "*.mkv", "*.webm", "*.ts", "*.mov"};
    struct Item { QString path; qint64 size; qint64 used; };
    QVector<Item> items;
    for (const QFileInfo &fi : d.entryInfoList(exts, QDir::Files, QDir::Name)) {
        // 正在下载 / 排队中的文件不参与清理
        bool busy = false;
        for (const auto &j : jobs_)
            if (j.filePath == fi.absoluteFilePath()
                && (j.state == State::Queued || j.state == State::Running)) { busy = true; break; }
        if (busy) continue;
        // 只用 mtime 判「最近」：现代文件系统默认 relatime，atime 基本等于"现在"，
        // 拿它排序会导致 LRU 完全失效（实测：4 个文件全被删光）。
        const qint64 mt = fi.lastModified().isValid() ? fi.lastModified().toMSecsSinceEpoch() : 0;
        items.append({ fi.absoluteFilePath(), fi.size(), mt });
        res.beforeBytes += fi.size();
    }
    res.afterBytes = res.beforeBytes;
    if (res.beforeBytes <= maxBytes) return res;
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.used < b.used; });   // 最久未用在前
    const qint64 target = maxBytes * 9 / 10;            // 一次多清一点，避免每次下载都触发
    for (const Item &it : items) {
        if (res.afterBytes <= target) break;
        if (QFile::remove(it.path)) {
            res.afterBytes -= it.size;
            res.removed.append(it.path);
            qInfo() << "LRU 清理:" << QFileInfo(it.path).fileName() << (it.size / 1024 / 1024) << "MB";
        }
    }
    qInfo() << "LRU 清理结果:" << res.beforeBytes / 1024 / 1024 << "MB →" << res.afterBytes / 1024 / 1024
            << "MB（上限" << maxBytes / 1024 / 1024 << "MB，删除" << res.removed.size() << "个）";
    return res;
}

bool DownloadManager::embedTags(Job &j) {
    // 只对音乐（有标题/艺术家）做；视频（yt-dlp 产物）不动
    if (j.title.isEmpty() && j.artist.isEmpty()) return false;
    const QFileInfo fi(j.filePath);
    if (!fi.exists() || fi.size() <= 0) return false;
    const QString ext = fi.suffix();
    const QString tmp = j.filePath + ".tag" + (ext.isEmpty() ? QString() : "." + ext);
    QFile::remove(tmp);

    QStringList a{"-y", "-hide_banner", "-loglevel", "error", "-i", j.filePath};
    const bool cover = !j.coverUrl.isEmpty();
    if (cover) a << "-i" << j.coverUrl;              // ffmpeg 直接读 HTTP 封面
    a << "-map" << "0:a?";
    if (cover) a << "-map" << "1" << "-disposition:v:0" << "attached_pic"
                 << "-metadata:s:v" << "title=Album cover"
                 << "-metadata:s:v" << "comment=Cover (front)";
    a << "-c:a" << "copy";
    a << "-metadata" << ("title=" + j.title);
    if (!j.artist.isEmpty()) a << "-metadata" << ("artist=" + j.artist);
    if (!j.album.isEmpty()) a << "-metadata" << ("album=" + j.album);
    if (ext.toLower() == "mp3") a << "-id3v2_version" << "3";
    a << tmp;

    QProcess p;
    p.start("ffmpeg", a);
    if (!p.waitForStarted(5000) || !p.waitForFinished(30000)) {
        p.kill();
        QFile::remove(tmp);
        qInfo() << "嵌标签失败（ffmpeg 超时/无法启动），保留原文件";
        return false;
    }
    if (p.exitCode() != 0) {
        QFile::remove(tmp);
        qInfo() << "嵌标签失败（ffmpeg 退出码" << p.exitCode() << "），保留原文件";
        return false;
    }
    // 读回校验：标题确实写进去了才替换（避免"标签没写成还把文件弄坏"）
    QProcess probe;
    probe.start("ffprobe", QStringList{"-v", "error", "-show_entries", "format_tags=title",
                                        "-of", "default=nw=1:nk=1", tmp});
    if (!probe.waitForFinished(10000)) {
        probe.kill();
        QFile::remove(tmp);
        return false;
    }
    const QString readBack = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
    if (readBack.isEmpty()) {
        QFile::remove(tmp);
        qInfo() << "嵌标签校验未通过（读回为空），保留原文件";
        return false;
    }
    const QString bak = j.filePath + ".bak";
    QFile::remove(bak);
    if (!QFile::rename(j.filePath, bak)) {          // 先备份
        QFile::remove(tmp);
        return false;
    }
    if (!QFile::rename(tmp, j.filePath)) {          // 失败则回滚
        QFile::rename(bak, j.filePath);
        return false;
    }
    QFile::remove(bak);
    return true;
}

void DownloadManager::fail(const QString &id, const QString &reason, bool retryable) {
    for (auto &j : jobs_) {
        if (j.id != id) continue;
        if (retryable && j.retries < kMaxRetries) {
            j.retries++;
            j.state = State::Queued;
            j.error = QString("%1（将重试 %2/%3）").arg(reason).arg(j.retries).arg(kMaxRetries);
            qWarning() << "下载失败将重试:" << j.url << reason;
            if (progress_) progress_(j);
            // 重试前清掉半截文件
            QFile::remove(j.filePath);
            return;
        }
        j.state = State::Failed;
        j.error = reason;
        qWarning() << "下载失败:" << j.url << reason;
        if (progress_) progress_(j);
        return;
    }
}

void DownloadManager::update(const QString &id) {
    if (auto *j = mutableFind(id))
        if (progress_) progress_(*j);
}

void DownloadManager::retryFailed() {
    for (auto &j : jobs_) {
        if (j.state == State::Failed) {
            j.retries = 0;
            j.error.clear();
            j.state = State::Queued;
            if (progress_) progress_(j);
        }
    }
    pump();
}

void DownloadManager::cancelAll() {
    for (auto it = running_.begin(); it != running_.end(); ++it) it.value()->abort();
    for (auto &j : jobs_)
        if (j.state == State::Queued) j.state = State::Canceled;
}
