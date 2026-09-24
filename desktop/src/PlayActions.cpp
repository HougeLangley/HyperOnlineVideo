// ── PlayActions.cpp：从 MainWindow.h 搬出的成员实现（文档 52/53/54 ✓ 零行为改动 ✓）──
// 搬运清单: beginPlayback biliSearch downloadCurrent loadMore playItem playNetEase playQQ playStormForTest probeNetwork queueOrFileNext switchVideoQuality toggleFavoriteCurrent
#include "MainWindow.h"

void MainWindow::downloadCurrent() {
        if (lastStreamUrl_.isEmpty()) {
            results_->addItem("下载：当前没有可下载的直链（先播放一首歌/一个视频）");
            return;
        }
        QString name = lastStreamTitle_;
        // 扩展名与 **macOS 完全一致**（UiActions.swift onDownload ✓）：
        //   在线视频（YouTube/B站）→ mp4 ✓；音乐按 URL 判 flac/m4a/mp3 ✓
        //   ✗ 旧实现只有 flac/m4a/else→mp3 → 视频会被存成 .mp3（用户拿到的后缀是错的 ✗）
        const bool isVideo = playPlatform_ == "YouTube" || playPlatform_ == "B站" || !lastAudioUrl_.isEmpty();
        const QString ext = isVideo ? QStringLiteral("mp4")
                          : lastStreamUrl_.contains(".flac") ? QStringLiteral("flac")
                          : lastStreamUrl_.contains(".m4a")  ? QStringLiteral("m4a")
                                                             : QStringLiteral("mp3");
        if (name.isEmpty()) name = QString("hov-download.%1").arg(ext);
        else name = name + "." + ext;
        // 音乐标签：标题/艺术家/专辑（+ 封面，当前大多来自 QQ 搜索的 albummid）
        const QString id = dl_->enqueue(lastStreamUrl_, lastStreamTitle_, name,
                                        lastArtist_, lastAlbum_, lastCoverUrl_,
                                        lastAudioUrl_, lastLRC_);   // ③ 带独立音轨（混流）与歌词（.lrc 侧车）
        results_->addItem(QString("已加入下载队列：%1（并发上限 2，失败自动重试 2 次）").arg(lastStreamTitle_));
        Q_UNUSED(id);
}

void MainWindow::playItem(const QString &key, const QString &label) {
        std::fprintf(stderr, "[PLAY] 点击/请求: %s\n", qPrintable(key.left(110)));
        beginPlayback(key, label);
        // 与队列同步下标（用户在列表里点哪首，队列就跳到哪首 → ⏭/⏮ 接得上）
        for (int i = 0; i < queue_.size(); ++i) {
            if (queue_.at(i) && queue_.at(i)->key == key) { queue_.jumpTo(i); break; }
        }
        const QString q = queue_.label();
        if (!q.isEmpty()) results_->addItem(QString("[队列] %1").arg(q));
        if (key.startsWith("netease:")) { followSourceFor("netease"); playNetEase(key.mid(8), label); return; }
        if (key.startsWith("qq:")) { followSourceFor("qqmusic"); playQQ(key.mid(3), label); return; }
        followSourceFor(UrlResolver::serviceOf(key));     // YouTube / B站：让下拉与内容一致
        resolutionKey_ = key;                            // 记住页面地址：切清晰度要重新解析它
        resolver_->setMaxHeight(effectiveVideoHeight());  // B1：按当前档位解析
        resolver_->resolveAndPlay(key);   // YouTube / B站 等网页地址
}

void MainWindow::beginPlayback(const QString &key, const QString &label) {
        saveProgressNow();                       // 切换前先把上一项落盘
        playKey_ = key;
        playLabel_ = label;
        lastProgRemember_ = lastProgSave_ = 0;
        pendingResume_ = 0.0;
        // 元数据：标题/艺术家（约定用「 — 」分隔）留给时长就绪后一起报；先通知状态变化
        const int sep = label.indexOf(QString::fromUtf8(" \xe2\x80\x94 "));
        playTitle_ = sep > 0 ? label.left(sep) : label;
        playArtist_ = sep > 0 ? label.mid(sep + 3) : QString();
        metaSent_ = false;
        if (mpris_) mpris_->updateStatus();
        if (!resumeEnabled_ || !settings_.boolean("playback.rememberProgress", true)) return;
        const double p = prog_.resumePos(key);
        if (p > 0.5) {
            pendingResume_ = p;
            results_->addItem(QString("该内容有观看记录，将从 %1 继续（从头看请按 Home 或重新点击）").arg(fmtClock(p)));
        }
}

void MainWindow::startMusicPlayback(const MusicPlayInfo &info) {
    // 唯一一份"音乐起播设置" ✓（文档 60）：此前三处各抄一份 ✗ → 已出现"128k 少 6 项"事故 ✓
    beginPlayback(info.key, info.label);          // ① saveProgressNow + playKey_/playLabel_ + 进度簿记复位 + MPRIS + 续播查询
    lastArtist_ = info.artist;                    // ② 元数据
    lastAlbum_  = info.album;
    lastCoverUrl_ = info.coverUrl;                // ③ 封面
    results_->addItem(info.statusText);           // ④ 结果行（文案由调用方给 ✓ 不改 ✗）
    applyCoverFor(info.key, info.label);          // ⑤ 封面下发
    lastStreamUrl_ = info.url;                    // ⑥ 直链记录（切档/下载依赖 ✓）
    lastStreamTitle_ = info.label;
    lastAudioUrl_.clear();                        // ⑥✗→✓ 清掉可能的上一个**视频音轨**（否则音乐下载会走混流 ✗）
    //   成因："视频→音乐"切换时 lastAudioUrl_ 不被清 ✗ → 音乐下载会拿旧视频音轨去 ffmpeg 混流 ✗
    //   （U2 单一入口的好处：此清零点只需一处 ✓）
    player_->playResolved(info.url, QString());    // ⑦ 起播
    if (info.fetchLyrics) info.fetchLyrics(info.label);   // ⑧ 歌词（**api 所有权归歌词回调** ✓）
}

void MainWindow::queueOrFileNext(bool autoAdvance) {
        if (!queue_.isEmpty()) {
            // 手动切歌（⏭）用 next()：总是前进（单曲循环也前进）；
            // 播完自动续播用 autoNext()：单曲=重播本曲、顺序=到底停止、列表循环=回到开头。
            const bool moved = autoAdvance ? queue_.autoNext() : queue_.next();
            queue_.saveTo();
            if (!moved) {
                if (autoAdvance) return;                       // 顺序模式到底：不动作（由调用方收尾）
                if (queue_.current()) playItem(queue_.current()->key, queue_.current()->label);
                return;
            }
            if (queue_.current()) playItem(queue_.current()->key, queue_.current()->label);
            return;
        }
        playNextFile();
}

void MainWindow::toggleFavoriteCurrent() {
        const QueueEntry *e = queue_.current();
        if (!e) { results_->addItem("收藏：当前没有可收藏的条目"); return; }
        if (favs_.contains(e->key)) {
            favs_.remove(e->key);
            favs_.save();
            results_->addItem(QString("已取消收藏：%1（现 %2 项）").arg(e->label).arg(favs_.size()));
        } else {
            Favorites::Fav f;
            f.id = e->key;
            f.key = e->key;
            f.title = e->label;
            f.platform = e->key.section(':', 0, 0);
            f.addedAt = QDateTime::currentMSecsSinceEpoch();
            favs_.add(f);
            favs_.save();
            results_->addItem(QString("已收藏：%1（现 %2 项）").arg(e->label).arg(favs_.size()));
        }
}

void MainWindow::playNetEase(const QString &id, const QString &label) {
        auto *api = new NetEaseApi(this);
        tuneApi(api);
        api->setStatusHandler([this](const QString &m) { results_->addItem(m); });
        api->songUrl(id, effectiveQuality(), [this, api, id, label](const QString &url, const QString &err) {
            if (url.isEmpty()) {
                results_->addItem(QString("取播放地址失败：%1").arg(err));
                api->deleteLater();
                return;
            }
            results_->addItem(QString("正在播放[%1]：%2").arg(playPlatform_.isEmpty() ? "网易云音乐" : playPlatform_, label));
            applyCoverFor("netease:" + id, label);            // 专辑封面（详情接口）
            lastStreamUrl_ = url; lastStreamTitle_ = label;   // 供下载按钮使用
            player_->playResolved(url, QString());
            loadLyricsFor(id, label, api);   // 顺带取歌词并交给字幕层显示
        });
}

void MainWindow::playQQ(const QString &mid, const QString &label) {
        auto *api = new QQMusicApi(this);
        tuneApi(api);
        api->setStatusHandler([this](const QString &m) { results_->addItem(m); });
        api->streamUrl(mid, effectiveQuality(), [this, api, mid, label](const QString &url, const QString &err) {
            if (!url.isEmpty()) {
                results_->addItem(QString("正在播放：%1").arg(label));
                applyCoverFor("qq:" + mid, label);
                player_->playResolved(url, QString());
                loadQQLyricsFor(mid, label, api);      // 顺带取歌词（与网易云同一字幕层）
                return;
            }
            api->streamUrl(mid, "standard", [this, api, mid, label, err](const QString &u2, const QString &e2) {
                if (!u2.isEmpty()) {
                    results_->addItem(QString("正在播放（128k）：%1").arg(label));
                    applyCoverFor("qq:" + mid, label);
                    player_->playResolved(u2, QString());
                } else {
                    results_->addItem(QString("「%1」播放失败：%2").arg(label, e2.isEmpty() ? err : e2));
                }
                api->deleteLater();
            });
        });
}

void MainWindow::switchVideoQuality(int h) {
        sessionVideoHeight_ = h;
        const QString label = UrlResolver::qualityLabel(h);
        results_->addItem(QString("[清晰度] 已切到 %1").arg(label));
        qInfo() << "[清晰度] 已切到" << label << "（会话档位）";     // 同时进 stderr：自动化可判定
        if (!resolutionKey_.startsWith("http") || UrlResolver::serviceOf(resolutionKey_).isEmpty()) {
            results_->addItem("当前不是在线视频，下次播放生效");
            return;
        }
        if (!player_ || player_->durationSec() < 1.0) return;
        const double pos = player_->positionSec();
        pendingResume_ = pos;                       // 复用"就绪后 seek"机制
        results_->addItem(QString("重新解析（%1），完成后回到 %2").arg(label, fmtClock(pos)));
        qInfo() << "[清晰度] 重新解析" << label << "→ 回到" << fmtClock(pos) << "秒";
        resolver_->setMaxHeight(h);
        resolver_->resolveAndPlay(resolutionKey_);
}

void MainWindow::playStormForTest(int n, int intervalMs) {
        std::fprintf(stderr, "[STORM] 探针启动：连播 %d 次，间隔 %dms\n", n, intervalMs);
        auto *t = new QTimer(this);
        t->setInterval(qMax(500, intervalMs));
        auto fired = std::make_shared<int>(0);
        connect(t, &QTimer::timeout, this, [this, t, n, fired] {
            if (*fired >= n) { t->stop(); std::fprintf(stderr, "[STORM] 探针结束\n"); return; }
            QStringList urls;
            for (int i = 0; results_ && i < results_->count(); ++i) {
                const QString u = results_->item(i)->data(Qt::UserRole).toString();
                if (!u.isEmpty()) urls << u;
            }
            if (urls.isEmpty()) return;                      // 搜索是异步的：结果未回就等下一拍
            const QString u = urls.at((*fired) % urls.size());
            ++(*fired);
            std::fprintf(stderr, "[STORM] 第 %d/%d 次连播: %s\n", *fired, n, qPrintable(u.left(80)));
            playItem(u, QString());
        });
        t->start();
}

void MainWindow::biliSearch(const QString &keyword, bool autoPlayFirst) {
        noteSearchContext(keyword, 1);
        results_->clear();
        results_->addItem(QString("B站搜索：%1").arg(keyword));
        const QVector<UrlResolver::BiliVideo> list = resolver_->searchBili(keyword);
        if (list.isEmpty()) {
            results_->addItem("B站：无结果（接口可能被限流，稍后再试）");
            return;
        }
        for (const auto &b : list) {
            QString text = b.title;
            if (!b.author.isEmpty()) text += "  · " + b.author;
            if (!b.duration.isEmpty()) text += "  · " + b.duration;
            auto *item = new QListWidgetItem(text, results_);
            item->setData(Qt::UserRole, b.url());
            setItemThumb(item, b.pic);                      // B4：B站缩略图
        }
        results_->addItem(QString("共 %1 条结果（双击播放，自动带 CC 字幕）").arg(list.size()));
        if (autoPlayFirst) playItem(list.first().url(), list.first().title);
}

void MainWindow::loadMore() {
        std::fprintf(stderr, "[MORE] loadMore 触发: kw=%s src=%d page=%d loading=%d hasMore=%d\n",
                     qPrintable(moreKw_), moreSrc_, morePage_, int(moreLoading_), int(moreHasMore_));
        if (moreLoading_ || !moreHasMore_ || moreKw_.isEmpty() || moreSrc_ < 0) return;
        if (moreSrc_ == 4) return;                                 // 本地库不分页
        if (morePage_ >= maxPageFor(moreSrc_)) {                   // 与 macOS 相同的页数上限
            moreHasMore_ = false;
            setStatusLine(QString("已到分页上限（%1 页，与 macOS/Android 一致）").arg(maxPageFor(moreSrc_)));
            std::fprintf(stderr, "[MORE] 到达上限 %d 页\n", maxPageFor(moreSrc_));
            return;
        }
        syncSeenFromList();
        moreLoading_ = true;
        setStatusLine(QString("正在加载更多（已 %1 条）…").arg(moreSeen_.size()));
        std::fprintf(stderr, "[MORE] 请求第 %d 页（%s）\n", morePage_ + 1, qPrintable(moreKw_));
        fetchMorePage(moreSrc_, moreKw_, morePage_ + 1);
}

void MainWindow::probeNetwork() {
        auto *mgr = new QNetworkAccessManager(this);
        QNetworkRequest req(QUrl("https://music.163.com/api/search/get/web?csrf_token=&s=test&type=1&offset=0&limit=1"));
        req.setRawHeader("Range", "bytes=0-1");
        req.setTransferTimeout(8000);
        auto *rep = mgr->get(req);
        connect(rep, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError e) {
            results_->addItem(QString("探活网络错误码: %1").arg(int(e)));
        });
        connect(rep, &QNetworkReply::finished, this, [this, rep, mgr] {
            const int code = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qInfo() << "网络探活[国内音乐接口]: HTTP" << code
                    << "| env代理:" << NetPolicy::hasEnvProxy()
                    << "| TUN类接口:" << NetPolicy::hasTunLikeInterface();
            if (code == 200 || code == 206) {
                results_->addItem("网络正常：国内音乐接口可达");
                setWindowTitle("聚合视频 · Hyper Online Video — 网络正常");
            } else {
                results_->addItem(QString("⚠ 国内音乐接口异常（HTTP %1）").arg(code));
                results_->addItem(NetPolicy::hintForFailure("netease"));
            }
            rep->deleteLater();
            mgr->deleteLater();
        });
}
