#include <cstdio>
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
#include <QSet>
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
                                 const QString &artist, const QString &album, const QString &coverUrl,
                               const QString &audioUrl, const QString &lyrics) {
    if (url.isEmpty()) return QString();
    Job j;
    j.id = QString("dl%1").arg(++seq_);
    j.url = url;
    j.audioUrl = audioUrl;
    j.lyrics = lyrics;
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
        applyHeaders(req, j.url);

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
                        if (!j.audioUrl.isEmpty()) {
                            // ③ 视频任务：yt-dlp 把音视频拆成两路，这里先混流再报完成（否则下载下来**没有声音**）
                            j.state = State::Running;
                            if (progress_) progress_(j);
                            startAudioPhase(j.id);          // ② 先把音轨下到本地，再本地混流
                        } else {
                            finishJob(j);
                        }
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
    // W4 ✓ Bug3 修复：文件被清掉后，**列表里的对应条目也要消失** ✗→✓
    //   （用户实测：点“清理”后已完成的内容仍留在下载面板 ✗ 因为原来只删文件、不动作业列表 ✗）
    //   顺带把“文件已不存在”的结束态作业（失败/已取消等）也一并清出列表 ✓
    if (!res.removed.isEmpty()) {
        const QSet<QString> gone(res.removed.begin(), res.removed.end());
        for (int i = jobs_.size() - 1; i >= 0; --i) {
            const Job &j = jobs_.at(i);
            if (j.state == State::Running || j.state == State::Queued) continue;   // 进行中的绝不碰 ✓
            const bool fileGone = j.filePath.isEmpty() || !QFile::exists(j.filePath);
            if (gone.contains(j.filePath) || fileGone) jobs_.remove(i);
        }
        std::fprintf(stderr, "[LRU] 列表同步：清理后剩 %d 个条目（进行中的保留 ✓）\n", int(jobs_.size()));
    }
    return res;
}

// ③ 收尾：写 .lrc 侧车 + 只对音频嵌标签 + 置完成（视频**绝不**走 embedTags）
void DownloadManager::finishJob(Job &j) {
    if (writeLyrics(j)) qInfo() << "已写出歌词侧车:" << (j.filePath + ".lrc");
    const QString suffix = QFileInfo(j.filePath).suffix().toLower();
    static const QStringList kVideoSuffix{ "mp4", "mkv", "webm", "mov", "flv", "ts", "m4v" };
    if (kVideoSuffix.contains(suffix)) {
        qInfo() << "跳过音乐标签嵌入（视频文件）:" << suffix;   // macOS 端踩过：把视频当音频处理会毁文件
    } else if (embedTags(j)) {
        qInfo() << "已嵌入音乐标签:" << j.title << "/" << j.artist;
    }
    j.state = State::Done;
    qInfo() << "下载完成:" << j.filePath << j.bytesDone << "字节"
            << (j.error.isEmpty() ? "" : QString("（警告：%1）").arg(j.error));
    // ⚠️ W4 修正：cleanupLru 现在会把“已删文件”的条目从 jobs_ **移除** ✗ →
    //    之后再用引用 `j` 就是**悬垂引用**（UB ✗）。所以：
    //    ① 先做**值拷贝**快照 ✓ ② 通知面板“完成”（真实数据 ✓）
    //    ③ 再清理（可能把条目移除 ✓）④ 清理后**再发一次**同一个快照 →
    //       面板按 id 找不到该条目就整表刷新 → 行消失 ✓（用户 Bug3 的期望 ✓）
    const Job snapshot = j;
    if (progress_) progress_(snapshot);
    if (maxTotalMb_ > 0) cleanupLru(static_cast<qint64>(maxTotalMb_) * 1024 * 1024);
    if (progress_) progress_(snapshot);
}

// 统一请求头：UA + 各站 Referer（B 站/YouTube 的 CDN 不认空 Referer，会 403 —— macOS 端实测踩过）
void DownloadManager::applyHeaders(QNetworkRequest &req, const QString &url) {
    req.setRawHeader("User-Agent",
                     "Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 (KHTML, like Gecko) "
                     "Chrome/120.0.0.0 Safari/537.36");
    if (url.contains("bilivideo") || url.contains("bilibili.com"))
        req.setRawHeader("Referer", "https://www.bilibili.com/");
    else if (url.contains("googlevideo") || url.contains("youtube.com"))
        req.setRawHeader("Referer", "https://www.youtube.com/");
    else if (url.contains("126.net") || url.contains("music.163.com"))
        req.setRawHeader("Referer", "https://music.163.com/");
    else if (url.contains("qq.com"))
        req.setRawHeader("Referer", "https://y.qq.com/");
}

// ② 第 2 阶段：独立音轨先下到本地临时文件，再由 ffmpeg **本地**混流。
//    原来是让 ffmpeg 自己去拉音轨直链 —— 没有请求头（B 站 403）、没有并发、进度不可见，
//    用户实测"下载很慢"。改成两路都用本下载器（带请求头 + 进度 + 失败可退避）。
void DownloadManager::startAudioPhase(const QString &id) {
    Job *jp = mutableFind(id);
    if (!jp) return;
    const QString au = jp->audioUrl;
    if (au.isEmpty()) { startMux(id); return; }
    jp->videoBytes = jp->bytesDone;              // 视频段字节数（音轨进度以它为基数叠加）
    jp->audioPath = jp->filePath + ".audio.part";
    QNetworkRequest req{QUrl(au)};
    applyHeaders(req, au);
    auto *reply = net_->get(req);
    auto *f = new QFile(jp->audioPath, this);
    if (!f->open(QIODevice::WriteOnly)) {
        f->deleteLater();
        reply->abort(); reply->deleteLater();
        jp->audioPath.clear();                   // 退回到"让 ffmpeg 自己拉"的老路径（保底）
        std::fprintf(stderr, "[DL] 音轨临时文件不可写，退回 ffmpeg 直连拉流\n");
        startMux(id);
        return;
    }
    std::fprintf(stderr, "[DL] 开始下载独立音轨（第 2 路）: %s\n", au.left(70).toUtf8().constData());
    connect(reply, &QNetworkReply::readyRead, f, [f, reply] { f->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, id](qint64 got, qint64 total) {
        for (auto &j : jobs_) {
            if (j.id != id) continue;
            j.bytesDone = j.videoBytes + got;
            j.bytesTotal = total > 0 ? j.videoBytes + total : j.bytesTotal;
            if (progress_) progress_(j);
            break;
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, reply, f] {
        const auto err = reply->error();
        f->write(reply->readAll());
        f->close(); f->deleteLater(); reply->deleteLater();
        Job *j = mutableFind(id);
        if (!j) return;
        const bool ok = (err == QNetworkReply::NoError) && QFileInfo(j->audioPath).size() > 0;
        if (!ok) {
            std::fprintf(stderr, "[DL] 音轨下载失败（%d），退回让 ffmpeg 直连拉流\n", int(err));
            QFile::remove(j->audioPath);
            j->audioPath.clear();                    // 保底路径：ffmpeg 用远端 URL
        }
        startMux(id);
    });
}

// ③ 混流：ffmpeg -c copy（不重编码，秒级完成）把独立音轨并进视频，成功则替换原文件
void DownloadManager::startMux(const QString &id) {
    Job *jp = mutableFind(id);
    if (!jp) return;
    const QString video = jp->filePath;
    // ② 第 2 阶段：本地音轨优先（已下好）；没有则退回让 ffmpeg 直连拉远端（保底不失败）
    const bool hasLocal = !jp->audioPath.isEmpty() && QFile::exists(jp->audioPath);
    const QString audio = hasLocal ? jp->audioPath : jp->audioUrl;
    const QString out = video + ".muxing.mp4";
    QStringList args{ "-y", "-hide_banner", "-loglevel", "error", "-i", video, "-i", audio,
                      "-c", "copy", "-movflags", "+faststart", out };
    auto *p = new QProcess(this);
    muxProcs_.insert(id, p);                 // W4 ✓ 存句柄：取消要能杀掉混流进程 ✗（原来没存 → 取消不掉 ✓）
    std::fprintf(stderr, "[MUX] 开始混流（视频 + %s）: %s\n", hasLocal ? "本地音轨" : "远端音轨直连", video.toUtf8().constData());
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, p, id, video, out, hasLocal](int code, QProcess::ExitStatus) {
                p->deleteLater();
                muxProcs_.remove(id);            // W4 ✓ 正常结束就摘掉句柄（与 cancel() 的 take 互不干扰 ✓）
                Job *j = mutableFind(id);
                if (!j) return;                  // 已被取消（条目已移除）→ 到此为止 ✓
                const bool ok = (code == 0) && QFile::exists(out) && QFileInfo(out).size() > 0;
                if (ok && QFile::remove(video) && QFile::rename(out, video)) {
                    j->bytesDone = QFileInfo(video).size();
                    qInfo() << "[MUX] 混流完成:" << video << j->bytesDone << "字节";
                } else {
                    j->error = "音轨合并失败（视频已保留，可能无声）";
                    qInfo() << "[MUX] 混流失败 rc=" << code << "→ 保留原视频（可能无声音）";
                    QFile::remove(out);
                }
                if (hasLocal) QFile::remove(j->audioPath);      // ② 混完就删临时音轨，不占空间
                finishJob(*j);
            });
    p->start("ffmpeg", args);
    if (!p->waitForStarted(5000)) {
        p->deleteLater();
        muxProcs_.remove(id);
        jp->error = "无法启动 ffmpeg（音轨未合并，视频可能无声）";
        qInfo() << "[MUX] ffmpeg 启动失败（未安装？）";
        finishJob(*jp);
    }
}

// ③ 歌词侧车：与媒体同名 .lrc（UTF-8），多数播放器可直接显示
bool DownloadManager::writeLyrics(const Job &j) {
    if (j.lyrics.trimmed().isEmpty()) return false;
    QFile f(j.filePath + ".lrc");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(j.lyrics.toUtf8());
    f.close();
    return true;
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
    if (cover) {
        // 封面 CDN 可能校验 Referer（实测偶发 403 ✗）→ 按域名带上（放对应输入之前 ✓）
        const QString cref = (j.coverUrl.contains("gtimg") || j.coverUrl.contains("qq.com"))
                                 ? QStringLiteral("https://y.qq.com/")
                                 : QStringLiteral("https://music.163.com/");
        a << "-headers" << QString("Referer: %1\r\nUser-Agent: Mozilla/5.0\r\n").arg(cref);
        a << "-i" << j.coverUrl;                      // ffmpeg 直接读 HTTP 封面
    }
    a << "-map" << "0:a?";
    if (cover) a << "-map" << "1" << "-disposition:v:0" << "attached_pic"
                 << "-metadata:s:v" << "title=Album cover"
                 << "-metadata:s:v" << "comment=Cover (front)";
    a << "-c:a" << "copy";
    if (cover) a << "-c:v" << "mjpeg";              // 显式 mjpeg：避免隐式转码（swscale 全图 rgb24 ✗）且跨 ffmpeg 版本稳定 ✓
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
    // 封面读回校验（2026-09-25 ✗）：旧逻辑只查 title → 封面丢了也当成功 ✓ → 用户拿到无封面文件 ✗
    if (cover) {
        QProcess cprobe;
        cprobe.start("ffprobe", QStringList{"-v", "error", "-select_streams", "v",
                                             "-show_entries", "stream=codec_name",
                                             "-of", "default=nw=1:nk=1", tmp});
        if (!cprobe.waitForFinished(10000) || cprobe.readAllStandardOutput().trimmed().isEmpty()) {
            QFile::remove(tmp);
            qInfo() << "嵌标签失败（封面流未写入 ✗），保留原文件";
            return false;
        }
    }
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
        if (j.state == State::Canceled) return;          // W4 ✓ 取消后的 abort 会走到这里 ✗ 绝不能当成“失败重试” ✗
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

int DownloadManager::dropFinishedJobs() {
    int n = 0;
    for (int i = jobs_.size() - 1; i >= 0; --i) {
        const State st = jobs_.at(i).state;
        if (st == State::Done || st == State::Failed || st == State::Canceled) { jobs_.remove(i); ++n; }
    }
    if (n > 0) std::fprintf(stderr, "[LRU] 清理列表：移除 %d 条已结束记录（剩 %d ✓）\n", n, int(jobs_.size()));
    return n;
}

void DownloadManager::cancelAll() {
    // W4 ✓ 改用“逐个 cancel” ✓：排队的 / 下载中的 / **混流中的** 都能停（旧实现只 abort 网络、且不杀 ffmpeg ✗）
    QVector<QString> ids;
    for (const Job &j : jobs_)
        if (j.state == State::Queued || j.state == State::Running) ids.append(j.id);
    for (const QString &id : ids) cancel(id);
    std::fprintf(stderr, "[DL] 取消全部：%d 个任务\n", int(ids.size()));
}

bool DownloadManager::cancel(const QString &id) {
    Job *j = mutableFind(id);
    if (!j) return false;
    if (j->state != State::Queued && j->state != State::Running) return false;   // 已完成/失败/已取消：不动 ✓
    // ① 网络阶段：中断回复 ✓（finished 回调里有 OperationCanceledError 分支 ✓ 且 fail() 已加 Canceled 护栏 ✓）
    if (QNetworkReply *r = running_.take(id)) {
        r->abort();
        r->deleteLater();
    }
    if (QFile *f = files_.take(id)) { f->close(); f->deleteLater(); }
    // ② 混流阶段：杀掉 ffmpeg ✓ 并清掉中间产物 ✓
    if (QProcess *p = muxProcs_.take(id)) {
        p->kill();
        p->waitForFinished(1500);
        std::fprintf(stderr, "[MUX] 已终止混流进程: %s\n", qPrintable(id));
    }
    // ③ 清半截文件（视频本体 / 独立音轨临时文件 / 混流中间产物 ✓）
    const QString video = j->filePath;
    const QString audio = j->audioPath;
    QFile::remove(video);
    if (!audio.isEmpty()) QFile::remove(audio);
    QFile::remove(video + ".muxing.mp4");
    // ④ 从列表里**直接移除**条目 ✓（与 Android “取消即划走”一致 ✓ 避免列表里堆一堆已取消 ✗）
    const QString title = j->title;
    for (int i = 0; i < jobs_.size(); ++i)
        if (jobs_[i].id == id) { jobs_.remove(i); break; }
    std::fprintf(stderr, "[DL] 已取消下载：%s\n", title.toUtf8().constData());
    if (progress_) { Job dummy; dummy.id = id; dummy.title = title; dummy.state = State::Canceled; progress_(dummy); }
    pump();
    return true;
}
