// ── SearchController.cpp：搜索 / 分页 / 探针（S3+S4 ✓ 2026-09-22）──
// 手法（文档 52 ✓ 升级版）：类声明在 MainWindow.h ✓ → 成员函数可直接在任意 TU 定义 ✓
//   **零 Ctx 注入 ✓ 零接口改动 ✓ 纯剪切 ✓**（函数体逐字搬运 ✓ 只加了 `MainWindow::` 前缀 ✓）
#include "MainWindow.h"
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

// ── UI-4-B ✓ 搜索过滤器：YouTube 的 `sp` 编码（**逐字节照搬 macOS `buildSp`** 见 UiActions.swift:1189 ✓ 不自创 ✗）
static QString youTubeSp(int sort, int duration) {
    QByteArray buf;
    if (sort == 2) { buf.append(char(0x08)); buf.append(char(0x02)); }
    if (sort == 3) { buf.append(char(0x08)); buf.append(char(0x03)); }
    if (duration >= 1 && duration <= 3) {
        buf.append(char(0x12)); buf.append(char(0x02)); buf.append(char(0x18)); buf.append(char(duration));
    }
    if (buf.isEmpty()) return QString();
    return QString::fromLatin1(buf.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

/// UI-4-B ✓ 无筛选 → 既有 `ytsearchN:`（行为零变化 ✓）；有筛选 → 完整 results URL + sp ✓
///   want 由调用方的 `--playlist-end` 控制 ✓ 两条路一致 ✓（与 macOS 同法 ✓）
static QString youTubeSearchArgN(const QString &q, int sort, int duration, int want) {
    const QString sp = youTubeSp(sort, duration);
    if (sp.isEmpty()) return QString("ytsearch%1:%2").arg(want).arg(q);
    return QString("https://www.youtube.com/results?search_query=%1&sp=%2")
               .arg(QString::fromUtf8(QUrl::toPercentEncoding(q)), sp);
}

void MainWindow::qqMusicSearch(const QString &keyword) {
        noteSearchContext(keyword, 3);
        results_->clear();
        results_->addItem(QString("QQ音乐搜索：%1").arg(keyword));
        auto *api = new QQMusicApi(this);
        tuneApi(api);
        api->setStatusHandler([this](const QString &m) { results_->addItem(m); });
        api->search(keyword, 20, [this, api](const QVector<QQMusicApi::Song> &songs) {
            if (songs.isEmpty()) { results_->addItem("QQ音乐：无结果"); api->deleteLater(); return; }
            for (const auto &s : songs) {
                auto *item = new QListWidgetItem(QString("%1 — %2").arg(s.name, s.artist), results_);
                item->setData(Qt::UserRole, "qq:" + s.mid);
                setItemThumb(item, qqThumb(s.albumMid));   // B4：QQ 专辑封面
                coverMap_.insert("qq:" + s.mid, qqCoverUrl(s.albumMid));   // 搜索接口已带 albummid
                titleMap_.insert("qq:" + s.mid, QString("%1 — %2").arg(s.name, s.artist));
            }
            // 逐条尝试（QQ 有版权受限曲目）：先 320k，失败再试 128k
            auto attempt = std::make_shared<std::function<void(int)>>();
            *attempt = [this, api, songs, attempt](int idx) {
                if (idx >= songs.size() || idx >= 5) {
                    results_->addItem("QQ音乐：前 5 条都取不到地址（VIP/版权受限曲目需登录且有会员）");
                    api->deleteLater();
                    QTimer::singleShot(0, this, [attempt] { *attempt = nullptr; });   // C1 断环 ✓（延后：不可在自身执行中自毁 ✗）
                    return;
                }
                const QQMusicApi::Song s = songs.at(idx);
                api->streamUrl(s.mid, effectiveQuality(), [this, api, s, idx, attempt](const QString &url, const QString &err) {
                    Q_UNUSED(err)   // 失败原因由 url 为空表示，这里不额外区分（消除 -Wextra 未使用参数警告）
                    if (!url.isEmpty()) {
                        const QString lb = QString("%1 — %2").arg(s.name, s.artist);
                        MusicPlayInfo info;                                   // 文档 60 ✓ 单一入口
                        info.key = "qq:" + s.mid;
                        info.label = lb;
                        info.url = url;
                        info.artist = s.artist; info.album = s.album;
                        info.coverUrl = qqCoverUrl(s.albumMid);               // 封面来自搜索记录的 albummid
                        info.statusText = QString("正在播放[QQ音乐]：%1").arg(lb);
                        info.fetchLyrics = [this, s, api](const QString &l) { loadQQLyricsFor(s.mid, l, api); };
                        startMusicPlayback(info);
                        QTimer::singleShot(0, this, [attempt] { *attempt = nullptr; });   // C1 断环 ✓（延后：不可在自身执行中自毁 ✗）
                        return;
                    }
                    api->streamUrl(s.mid, "standard", [this, api, s, idx, attempt](const QString &u2, const QString &e2) {
                        if (!u2.isEmpty()) {
                            // 文档 59 对齐 + 文档 60 收编：**与 320k 走同一入口** ✓ → 结构上不可能再不一致 ✓
                            const QString lb = QString("%1 — %2").arg(s.name, s.artist);
                            MusicPlayInfo info;
                            info.key = "qq:" + s.mid;
                            info.label = lb;
                            info.url = u2;
                            info.artist = s.artist; info.album = s.album;
                            info.coverUrl = qqCoverUrl(s.albumMid);
                            info.statusText = QString("正在播放（128k）：%1 — %2").arg(s.name, s.artist);   // 文案保留 ✓
                            info.fetchLyrics = [this, s, api](const QString &l) { loadQQLyricsFor(s.mid, l, api); };
                            startMusicPlayback(info);
                            QTimer::singleShot(0, this, [attempt] { *attempt = nullptr; });   // C1 断环 ✓（延后：不可在自身执行中自毁 ✗）
                            return;
                        }
                        results_->addItem(QString("跳过「%1」：%2").arg(s.name, e2));
                        (*attempt)(idx + 1);
                    });
                });
            };
            rebuildQueueFromList();
            (*attempt)(0);
        });
}

void MainWindow::fsProbePublic(const QString &tag) {
        enterVideoFullscreen();
        if (hoverTimer_) hoverTimer_->stop();
        std::fprintf(stderr, "[FS-PROBE] %s 进全屏: 全屏=%d 卡片隐藏=%d 浮层显示=%d\n",
                     qPrintable(tag), isFullScreen() ? 1 : 0,
                     (masonry_ && !masonry_->isVisible()) ? 1 : 0,
                     (masonryHost_ && masonryHost_->isVisible()) ? 1 : 0);
        setSidebarRevealed(true);
        QTimer::singleShot(600, this, [this, tag] {
            std::fprintf(stderr, "[FS-PROBE] %s 浮出后: 浮层=%dx%d 浮层可见=%d 卡片可见=%d\n",
                         qPrintable(tag), masonryHost_ ? masonryHost_->width() : 0,
                         masonryHost_ ? masonryHost_->height() : 0,
                         (masonryHost_ && masonryHost_->isVisible()) ? 1 : 0,
                         (masonry_ && masonry_->isVisible()) ? 1 : 0);
            QTimer::singleShot(1200, this, [this, tag] {
                setSidebarRevealed(false);
                std::fprintf(stderr, "[FS-PROBE] %s 收起后: 浮层可见=%d\n",
                             qPrintable(tag), (masonryHost_ && masonryHost_->isVisible()) ? 1 : 0);
                exitVideoFullscreen();
                // 关键回归判据：退出后**数据源 results_ 必须仍然隐藏**（它曾被我 show() 出来 → 左侧多出窄缝 ✗）
                std::fprintf(stderr, "[FS-PROBE] %s 退出后: 全屏=%d 卡片可见=%d 数据源可见=%d\n",
                             qPrintable(tag), isFullScreen() ? 1 : 0,
                             (masonry_ && masonry_->isVisible()) ? 1 : 0,
                             (results_ && results_->isVisible()) ? 1 : 0);
                // 再走一轮"进→浮出→退出"，专测"再双击就出问题"（用户实测场景）
                QTimer::singleShot(900, this, [this, tag] {
                    enterVideoFullscreen();
                    if (hoverTimer_) hoverTimer_->stop();
                    setSidebarRevealed(true);
                    QTimer::singleShot(900, this, [this, tag] {
                        setSidebarRevealed(false);
                        exitVideoFullscreen();
                        std::fprintf(stderr, "[FS-PROBE] %s 第二轮退出: 全屏=%d 卡片可见=%d 数据源可见=%d 浮层可见=%d\n",
                                     qPrintable(tag), isFullScreen() ? 1 : 0,
                                     (masonry_ && masonry_->isVisible()) ? 1 : 0,
                                     (results_ && results_->isVisible()) ? 1 : 0,
                                     (masonryHost_ && masonryHost_->isVisible()) ? 1 : 0);
                    });
                });
            });
        });
}

void MainWindow::searchPublic(const QString &kw, int src) {
        if (srcBox_) srcBox_->setCurrentIndex(qBound(0, src, srcBox_->count() - 1));
        if (search_) search_->setText(kw);
        doSearch();
}

void MainWindow::loadMoreForTest(int n) {
        std::fprintf(stderr, "[MORE] 探针启动：模拟拖到底 ×%d\n", n);
        for (int i = 0; i < n; ++i)
            QTimer::singleShot(i * 8000, this, [this, i] {
                QScrollBar *sb = (masonry_ && masonry_->isVisible()) ? masonry_->verticalScrollBar()
                               : (results_ ? results_->verticalScrollBar() : nullptr);
                if (!sb) { std::fprintf(stderr, "[MORE] 探针：找不到滚动条\n"); return; }
                std::fprintf(stderr, "[MORE] 探针第 %d 次：推到底（max=%d 当前=%d 可见=%d）\n",
                             i + 1, sb->maximum(), sb->value(),
                             int(masonry_ && masonry_->isVisible()));
                sb->setValue(sb->maximum());          // ← 等价用户拖到底 → 触发 loadMore
            });
}

void MainWindow::musicSearch(const QString &keyword) {
        noteSearchContext(keyword, 2);
        results_->clear();
        results_->addItem(QString("网易云搜索：%1").arg(keyword));
        auto *api = new NetEaseApi(this);
        tuneApi(api);
        api->setStatusHandler([this](const QString &m) { results_->addItem(m); });
        api->search(keyword, 15, [this, api](const QVector<NetEaseApi::Song> &songs) {
            if (songs.isEmpty()) { results_->addItem("网易云：无结果"); api->deleteLater(); return; }
            for (const auto &s : songs) {
                auto *item = new QListWidgetItem(QString("%1 — %2").arg(s.name, s.artist), results_);
                item->setData(Qt::UserRole, "netease:" + s.id);
                titleMap_.insert("netease:" + s.id, QString("%1 — %2").arg(s.name, s.artist));
            }
            // 封面：搜索接口只给 picId（picUrl=None ✗ 实测 ✓）→ 批量走 song/detail（与 macOS 对齐 ✓）
            //  用途①卡片缩略图（结果列表不再是黑块 ✓）②coverMap_ 预热（播放时单曲封面零额外请求 ✓）
            prefetchNetEaseCovers(songs);
            // 逐条尝试取地址：网易云里有些曲目（混音版/下架曲/版权受限）拿不到直链，
            // 失败就自动换下一条，最多试 5 条 —— 避免"第一条恰好不可播"就整体失败。
            auto attempt = std::make_shared<std::function<void(int)>>();
            *attempt = [this, api, songs, attempt](int idx) {
                if (idx >= songs.size() || idx >= 5) {
                    results_->addItem("网易云：前 5 条都取不到播放地址（可能需要登录）");
                    api->deleteLater();
                    QTimer::singleShot(0, this, [attempt] { *attempt = nullptr; });   // C1 断环 ✓（延后：不可在自身执行中自毁 ✗）
                    return;
                }
                const NetEaseApi::Song s = songs.at(idx);
                api->songUrl(s.id, effectiveQuality(), [this, api, s, idx, attempt](const QString &url, const QString &err) {
                    if (url.isEmpty()) {
                        results_->addItem(QString("跳过「%1」：%2").arg(s.name, err));
                        (*attempt)(idx + 1);
                        return;
                    }
                    const QString lb = QString("%1 — %2").arg(s.name, s.artist);
                    MusicPlayInfo info;                                   // 文档 60 ✓ 单一入口（顺序全在此处定义 ✓）
                    info.key = "netease:" + s.id;
                    info.label = lb;
                    info.url = url;
                    info.artist = s.artist; info.album = s.album;
                    info.coverUrl = coverMap_.value("netease:" + s.id);   // 搜索时已批量预热 ✓ 空则播放链路再按需取（applyCoverFor/fetchNetEaseCover）
                    info.statusText = QString("正在播放：%1").arg(lb);
                    info.fetchLyrics = [this, s, api](const QString &l) { loadLyricsFor(s.id, l, api); };
                    startMusicPlayback(info);
                    QTimer::singleShot(0, this, [attempt] { *attempt = nullptr; });   // C1 断环 ✓（延后：不可在自身执行中自毁 ✗）
                });
            };
            rebuildQueueFromList();
            (*attempt)(0);
        });
}

/** 网易云封面批量预热（搜索结果只给 picId ✗ → song/detail ✓）——
 *  第一页与"加载更多"共用（用户实测：翻页后没有封面 ✗ 2026-09-25 ✓） */
void MainWindow::prefetchNetEaseCovers(const QVector<NetEaseApi::Song> &songs) {
    if (songs.isEmpty()) return;
    auto *api = new NetEaseApi(this);
    tuneApi(api);
    QStringList coverIds;
    for (const auto &so : songs) coverIds << so.id;
    api->coverUrls(coverIds, [this, api](const QHash<QString, QString> &covers) {
        api->deleteLater();
        if (covers.isEmpty()) return;
        for (auto it = covers.constBegin(); it != covers.constEnd(); ++it)
            coverMap_.insert("netease:" + it.key(), it.value());
        for (int i = 0; i < results_->count(); ++i) {
            auto *item = results_->item(i);
            if (!item || !item->icon().isNull()) continue;   // 已有图不重复请求 ✓
            const QString k = item->data(Qt::UserRole).toString();
            if (!k.startsWith("netease:")) continue;
            const QString u = covers.value(k.mid(8));        // "netease:" = 8 字符 ✓
            if (!u.isEmpty()) setItemThumb(item, u);
        }
    });
}

void MainWindow::fetchMorePage(int src, const QString &kw, int page) {
        const int want = 20 * page;                               // 与 macOS 相同的"多取后截尾"
        const int reqId = moreReqId_;
        if (src == 1) {                                           // B站（同步接口）
            const auto list = resolver_->searchBili(kw, want);
            QVector<QPair<QString, QString>> rows, thumbs;
            for (const auto &b : list) {
                QString t = b.title;
                if (!b.author.isEmpty()) t += "  · " + b.author;
                if (!b.duration.isEmpty()) t += "  · " + b.duration;
                rows.append({t, b.url()});
                thumbs.append({b.url(), b.pic});
            }
            finishMore(reqId, appendNewRows(rows, thumbs));
            return;
        }
        if (src == 2) {                                           // 网易云（异步）
            auto *api = new NetEaseApi(this);
            tuneApi(api);
            api->search(kw, want, [this, api, reqId](const QVector<NetEaseApi::Song> &songs) {
                api->deleteLater();
                if (reqId != moreReqId_) return;
                QVector<QPair<QString, QString>> rows;
                for (const auto &so : songs)
                    rows.append({QString("%1 — %2").arg(so.name, so.artist), "netease:" + so.id});
                finishMore(reqId, appendNewRows(rows));
                prefetchNetEaseCovers(songs);                     // 翻页封面（用户实测滑到后面全灰 ✗）
            });
            return;
        }
        if (src == 3) {                                           // QQ音乐（异步；Desktop 协议真翻页 ✓）
            auto *api = new QQMusicApi(this);
            api->search(kw, 20, [this, api, reqId](const QVector<QQMusicApi::Song> &songs) {
                api->deleteLater();
                if (reqId != moreReqId_) return;
                QVector<QPair<QString, QString>> rows, thumbs;
                for (const auto &so : songs) {
                    rows.append({QString("%1 — %2").arg(so.name, so.artist), "qq:" + so.mid});
                    thumbs.append({"qq:" + so.mid, qqThumb(so.albumMid)});        // 翻页封面 ✓
                    coverMap_.insert("qq:" + so.mid, qqCoverUrl(so.albumMid));     // 播放预热 ✓
                }
                finishMore(reqId, appendNewRows(rows, thumbs));
            }, page);                                             // page_num 真翻页 ✓
            return;
        }
        // src == 0：YouTube（异步 QProcess；--playlist-end N + ytsearchN:，与现有搜索结果同一解析逻辑）
        auto *p = new QProcess(this);
        connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, p, reqId](int, QProcess::ExitStatus) {
                    const auto doc = QJsonDocument::fromJson(p->readAllStandardOutput());
                    p->deleteLater();
                    if (reqId != moreReqId_) return;
                    QVector<QPair<QString, QString>> rows, thumbs;
                    for (const auto &v : doc.object().value("entries").toArray()) {
                        const auto o = v.toObject();
                        QString url = o.value("url").toString();
                        if (url.isEmpty()) url = "https://www.youtube.com/watch?v=" + o.value("id").toString();
                        static const char *nonVideo[] = {"/channel/", "/user/", "/c/", "/@",
                                                         "/playlist", "/results", "/feed/", nullptr};
                        bool skip = false;
                        for (int i = 0; nonVideo[i]; ++i) if (url.contains(nonVideo[i])) { skip = true; break; }
                        if (skip) continue;
                        rows.append({o.value("title").toString(), url});
                        thumbs.append({url, youTubeThumb(o.value("id").toString())});
                    }
                    finishMore(reqId, appendNewRows(rows, thumbs));
                });
        p->start("yt-dlp", {"-J", "--flat-playlist", "--no-warnings",
                            "--playlist-end", QString::number(want),
                            youTubeSearchArgN(kw, searchSort_, searchDuration_, want)});   // UI-4-B ② ✓ 分页也带筛选（对齐 macOS ✓ 多取后截尾 ✓）
}





void MainWindow::doSearch() {
        const QString q = search_->text().trimmed();
        const int src = srcBox_ ? srcBox_->currentIndex() : 0;
        if (src == 4) { refreshLocalLibrary(q); return; }   // 本地库：关键词当文件名过滤（可为空）
        if (q.isEmpty()) return;
        if (src == 1) { biliSearch(q); return; }       // B站（官方 search/type 接口）
        if (src == 2) { musicSearch(q); return; }      // 网易云音乐
        if (src == 3) { qqMusicSearch(q); return; }    // QQ音乐
        results_->clear();
        results_->addItem("搜索中…");
        auto *p = new QProcess(this);
        connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, p](int code, QProcess::ExitStatus) {
                    results_->clear();
                    if (code != 0) { results_->addItem("搜索失败（检查网络或 yt-dlp）"); p->deleteLater(); return; }   // 修复：错误路径漏 deleteLater → 每失败一次泄一个 QProcess 对象
                    const auto doc = QJsonDocument::fromJson(p->readAllStandardOutput());
                    for (const auto &v : doc.object().value("entries").toArray()) {
                        const auto o = v.toObject();
                        // 搜索结果里会混入频道 / 播放列表条目（它们没有视频时长）。
                        // 之前把它们的 id 也拼成 watch?v= → 播放时报 "This video is unavailable"。
                        // 改：优先用 yt-dlp 给出的 url 字段，并滤掉非视频形态的地址。
                        QString url = o.value("url").toString();
                        if (url.isEmpty())
                            url = "https://www.youtube.com/watch?v=" + o.value("id").toString();
                        static const char *nonVideo[] = {"/channel/", "/user/", "/c/", "/@",
                                                         "/playlist", "/results", "/feed/", nullptr};
                        bool skip = false;
                        for (int i = 0; nonVideo[i]; ++i) {
                            if (url.contains(nonVideo[i])) { skip = true; break; }
                        }
                        if (skip) continue;
                        auto *item = new QListWidgetItem(o.value("title").toString(), results_);
                        item->setData(Qt::UserRole, url);
                        setItemThumb(item, youTubeThumb(o.value("id").toString()));   // B4：YouTube 缩略图
                    }
                    if (results_->count() == 0) { results_->addItem("没有结果"); p->deleteLater(); return; }
                    rebuildQueueFromList();             // 搜索结果整体入队（⏭/⏮ 与自动续播都靠它）
                    if (autoplay_) {                       // 自动化验证：自动播放第一条
                        results_->setCurrentRow(0);
                        resolver_->resolveAndPlay(results_->item(0)->data(Qt::UserRole).toString());
                    }
                    p->deleteLater();
                });

        noteSearchContext(q, 0);   // YouTube 搜索结果上下文（供滚到底翻页）
        // 说明 ✓：滚动加载（下一段 `ytsearch`）不带筛选 —— 与首屏筛选的差异已在文档 61 记录 ✓
        resolver_->setSearchFilters(searchSort_, searchDuration_);   // B站用（在 biliSearch 前生效 ✓）
        // UI-4-B ✓ 过滤器：排序/时长 → YouTube `sp` 参数（无筛选时行为完全不变 ✓ 零回归）
        const QString ytArg = youTubeSearchArgN(q, searchSort_, searchDuration_, 15);
        std::fprintf(stderr, "[FILTER] sort=%d duration=%d ytArg=%s\n", searchSort_, searchDuration_, ytArg.toUtf8().constData());   // ④ ✓ 与 [DL]/[GLASS] 同通道（实测可见 ✓）
        p->start("yt-dlp", {"-J", "--flat-playlist", "--no-warnings",
                            "--playlist-end", "15", ytArg});
}
