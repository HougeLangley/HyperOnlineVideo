// ── MainWindow.cpp：从 MainWindow.h 搬出的成员实现（文档 52/53/54 ✓ 零行为改动 ✓）──
// 搬运清单: applyFillMode applySetting applySettingInner applySource applySubsUrl applySubtitleDelay applySubtitleTrackIndex artistOf biliSearchPublic cleanupDownloads clearProgress cycleQuality cycleVideoQuality demoSearch downloadUrl dumpProgress effectiveVideoHeight fetchNetEaseCover finishMore fmtClock fmtTime followSourceFor healIfExpired importCookiesFromBrowser isWebUrl openFiles openLoginDialog openSettingsDialog playCurrent playNextFile playPrevFile qualityLabel queueOrFilePrev rebuildQueueFromList runQueueSelfTest saveProgressNow selfTest setAutoplay setResumeEnabled setStatusLine setVol titleOf toggleFillScreen togglePiPPublic toggleVideoFullscreen ttlMs tuneApi
#include "MainWindow.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QButtonGroup>
#include <QRadioButton>
#include <QDialogButtonBox>

void MainWindow::runQueueSelfTest() {
        SelfTest::Ctx ctx;
        ctx.results       = results_;
        ctx.player        = player_;
        ctx.fillApplied   = &lastFillApplied_;
        ctx.applyFillMode = [this](bool on) { applyFillMode(on); };
        ctx.isFullScreen  = [this] { return isFullScreen(); };
        ctx.enterFullscreen = [this] { enterVideoFullscreen(); };
        ctx.exitFullscreen  = [this] { exitVideoFullscreen(); };
        SelfTest::runQueueSelfTest(ctx);
}

void MainWindow::openLoginDialog(const QString &site) {
        if (!LoginDialog::available()) {
            results_->addItem("当前构建不含登录组件（QtWebEngine 未编入，常见于 Flatpak 沙箱）");
            results_->addItem(QString("可手动放置 cookie 到：%1").arg(LoginDialog::cookieFileFor(site)));
        }
        LoginDialog dlg(site, this);
        dlg.exec();
        results_->addItem(QString("登录窗口已关闭；cookie 文件：%1").arg(LoginDialog::cookieFileFor(site)));
}

void MainWindow::openSettingsDialog() {
        SettingsDialog::Ctx ctx;
        ctx.settings  = &settings_;
        ctx.results   = results_;
        ctx.player    = player_;
        ctx.applySetting        = [this](const QString &k, const QString &v) { return applySetting(k, v); };
        ctx.applyThemeSettingNow = [this] { applyThemeSettingNow(); };
        ctx.setStatusLine       = [this](const QString &s) { setStatusLine(s); };
        ctx.currentVideoHeight  = [this] { return effectiveVideoHeight(); };   // 面板初值=当前有效档位 ✓
        SettingsDialog::open(this, ctx);
}

bool MainWindow::applySetting(const QString &key, const QString &value) {
        const bool ch = applySettingInner(key, value);
        // 与网络解析相关的设置改动后要立刻生效（不只等重启）
        if (key.startsWith("network.")) {
            resolver_->setForceDirect(settings_.boolean("network.forceDirectDomestic", false));
            resolver_->setCookiesFromBrowser(settings_.str("network.cookiesFromBrowser", ""));
        }
        return ch;
}

bool MainWindow::applySettingInner(const QString &key, const QString &value) {
        const QString err = Settings::validate(key, value);      // 非法值与未知键一律拒绝
        if (!err.isEmpty()) {
            results_->addItem(QString("[设置] 拒绝 %1=%2：%3").arg(key, value, err));
            std::cerr << "[reject] " << key.toStdString() << "=" << value.toStdString()
                      << "  → " << err.toStdString() << std::endl;
            return false;
        }
        const bool changed = settings_.set(key, value);
        settings_.save();
        if (key == "subtitle.fontScale") player_->setSubtitleFontScale(settings_.number(key, 1.0));
        else if (key == "subtitle.defaultDelay") player_->adjustSubtitleDelay(settings_.number(key, 0.0));
        else if (key == "download.dir") dl_->setDir(settings_.str(key));
        else if (key == "video.maxHeight") {
            // 设置面板保存画质 = 权威来源 ✗：清掉会话覆盖，让 effectiveVideoHeight() 落到设置值 ✓
            //   （面板初值已按"当前有效档位"显示 → 同值写回时用户无感；改值则立即生效 ✓ 无静默变化 ✗）
            sessionVideoHeight_ = 0;
            if (resolver_) resolver_->setMaxHeight(effectiveVideoHeight());
            syncQualityBox();                       // 左下角画质盒立即跟随 ✓（用户主诉 bug ✗）
        }
        else if (key == "network.forceDirectDomestic" && dl_) { /* 下一次创建 API 时生效 */ }
        results_->addItem(QString("[设置] %1 = %2%3").arg(key, value, changed ? "" : "（值未变化）"));
        return changed;
}


void MainWindow::downloadUrl(const QString &url, const QString &name) {
        dl_->enqueue(url, name.isEmpty() ? url.section('/', -1) : name, name);
        results_->addItem(QString("已加入下载队列：%1").arg(url.left(80)));
}

void MainWindow::saveProgressNow() {
        if (!player_ || playKey_.isEmpty()) return;
        const double pos = player_->positionSec(), dur = player_->durationSec();
        if (dur < 1.0 || pos < ProgressStore::kRecordMin) return;
        if (!settings_.boolean("playback.rememberProgress", true)) return;
        if (prog_.remember(playKey_, pos, dur, playLabel_)) prog_.save();
}

QString MainWindow::dumpProgress() const {
        QString out = QString("---- 进度记忆 (%1) ----\n").arg(ProgressStore::filePath());
        const auto list = prog_.items();
        if (list.isEmpty()) out += "(空)\n";
        for (const auto &p : list) {
            out += QString("%1  %2 / %3  %4\n")
                       .arg(p.first,
                            fmtClock(p.second.pos), fmtClock(p.second.dur),
                            p.second.title.isEmpty() ? QString("(无标题)") : p.second.title);
        }
        return out;
}

DownloadManager::CleanResult MainWindow::cleanupDownloads(qint64 maxBytes) {
        if (!dl_) return DownloadManager::CleanResult{};
        const auto r = dl_->cleanupLru(maxBytes);
        // W5 ✓ 与面板「LRU 清理」按钮**同行为**：CLI 也把已结束条目从列表清掉 ✓
        //   （用户口径：点清理后已下载内容要从下载面板消失 ✓ 两条入口必须一致 ✓）
        dl_->dropFinishedJobs();
        return r;
}

void MainWindow::applySubsUrl(const QString &url) {
        if (url.isEmpty()) return;
        results_->addItem(QString("正在获取在线字幕：%1").arg(url));
        // 传视频页地址就按站点取字幕（B站 CC / YouTube），传字幕文件地址就直接取
        resolver_->fetchOnlineSubs(url, [this](const QVector<SubtitleTrack> &tracks, const QString &err) {
            if (!err.isEmpty()) { results_->addItem("在线字幕：" + err); return; }
            player_->addSubtitleTracks(tracks);
            for (const auto &t : tracks)
                results_->addItem(QString("在线字幕已加载：%1（%2 行）").arg(t.label).arg(t.cues.size()));
        });
}

QString MainWindow::fmtClock(double s) {
        const int t = static_cast<int>(s < 0 ? 0 : s);
        return QString("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QLatin1Char('0'));
}

void MainWindow::queueOrFilePrev() {
        if (!queue_.isEmpty()) {
            queue_.prev();
            if (queue_.current()) playItem(queue_.current()->key, queue_.current()->label);
            return;
        }
        playPrevFile();
}

void MainWindow::rebuildQueueFromList() {
        QVector<QueueEntry> entries;
        for (int i = 0; i < results_->count(); ++i) {
            QListWidgetItem *it = results_->item(i);
            const QString key = it->data(Qt::UserRole).toString();
            if (key.isEmpty()) continue;   // 状态行没有 key
            entries.append({ key, it->text() });
        }
        if (!entries.isEmpty()) {
            queue_.setList(entries, 0);
            queue_.saveTo();   // 队列变化即落盘（重启可恢复）
            results_->addItem(QString("[队列] 已建立 %1 项（%2）").arg(queue_.size()).arg(queue_.modeLabel()));
        }
}

QString MainWindow::titleOf(const QString &label) {
        const int sep = label.indexOf(QString::fromUtf8(" \xe2\x80\x94 "));
        return sep > 0 ? label.left(sep) : label;
}

QString MainWindow::artistOf(const QString &label) {
        const int sep = label.indexOf(QString::fromUtf8(" \xe2\x80\x94 "));
        return sep > 0 ? label.mid(sep + 3) : QString();
}

QString MainWindow::qualityLabel(const QString &q) {
        if (q == "standard") return QStringLiteral("标准 128k");
        if (q == "lossless") return QStringLiteral("无损 FLAC");
        return QStringLiteral("较高 320k");
}

void MainWindow::cycleQuality() {
        static const QStringList order{"standard", "exhigh", "lossless"};
        int i = order.indexOf(effectiveQuality());
        if (i < 0) i = 1;
        switchQualityTo(order.at((i + 1) % order.size()));
}

void MainWindow::fetchNetEaseCover(const QString &id, const QString &title, const QString &artist) {
        auto *cv = new NetEaseApi(this);
        tuneApi(cv);
        cv->coverUrl(id, [this, cv, title, artist](const QString &url) {
            cv->deleteLater();
            lastCoverUrl_ = url;
            player_->setCoverArt(url, title, artist);
            if (!url.isEmpty()) results_->addItem(QString("封面已加载：%1").arg(title));
        });
}

int MainWindow::effectiveVideoHeight() const {
        if (sessionVideoHeight_ != 0) return sessionVideoHeight_;
        return static_cast<int>(settings_.number("video.maxHeight", 0));
}

void MainWindow::setQualityBoxForMusic(bool music) {
        if (!qualityBox_ || qualityBoxMusic_ == music) return;
        qualityBoxMusic_ = music;
        const QSignalBlocker b(qualityBox_);            // 重建内容不触发业务信号 ✗
        qualityBox_->clear();
        if (music) {
            static const struct { const char *id; const char *label; } kQ[] = {
                {"lossless", "无损"}, {"exhigh", "320k"}, {"standard", "128k"}};
            for (const auto &q : kQ) qualityBox_->addItem(q.label, QString(q.id));
            const int idx = qualityBox_->findData(settings_.str("music.qualityCeiling", "exhigh"));
            qualityBox_->setCurrentIndex(idx < 0 ? 1 : idx);
            qualityBox_->setToolTip("音质上限（听歌时；与设置面板共用 music.qualityCeiling ✓ V 键也可循环 ✓）");
        } else {
            const QVector<int> hs = Settings::qualityHeights();
            for (int h : hs) qualityBox_->addItem(Settings::qualityLabelFor(h), h);
            const int idx = Settings::qualityHeights().indexOf(effectiveVideoHeight());
            qualityBox_->setCurrentIndex(idx < 0 ? 0 : idx);
            qualityBox_->setToolTip("清晰度（与设置面板共用一份档位表 — V 键也可循环切换）");
        }
}

void MainWindow::syncQualityBox() {
        if (!qualityBox_) return;
        const int idx = Settings::qualityHeights().indexOf(effectiveVideoHeight());
        if (idx < 0 || idx == qualityBox_->currentIndex()) return;
        const QSignalBlocker b(qualityBox_);        // 防递归：indexChanged 会写设置 + 重新解析 ✗
        qualityBox_->setCurrentIndex(idx);
}

void MainWindow::applyFillMode(bool force) {
        if (!player_ || !player_->renderReady()) return;   // mpv 未就绪一律不动（早期同步调用会挂住 UI）
        const bool want = isFullScreen() && fillScreenSetting();
        if (!force && want == lastFillApplied_) return;
        lastFillApplied_ = want;
        player_->setPanscanAsync(want ? 1.0 : 0.0);        // 异步下发，绝不阻塞 GUI 线程
        const double actual = player_->panscan();
        qInfo().noquote() << QString("[SELFTEST] 全屏铺满：期望=%1 panscan回读=%2（全屏=%3 设置=%4）")
                                 .arg(want ? "开" : "关").arg(actual, 0, 'f', 2)
                                 .arg(isFullScreen() ? "是" : "否")
                                 .arg(fillScreenSetting() ? "开" : "关");
        results_->addItem(want ? QString("铺满模式（裁切黑边，回读 panscan=%1）").arg(actual, 0, 'f', 1)
                               : QString("保持比例（panscan=0）"));
        if (fillBtn_) fillBtn_->setChecked(fillScreenSetting());
}

void MainWindow::toggleFillScreen() {
        if (!videoFull_ && !isFullScreen()) { enterVideoFullscreen(); return; }
        const bool now = settings_.boolean("video.fillScreen", true);
        settings_.set("video.fillScreen", !now);
        applyFillMode(true);
}

void MainWindow::cycleVideoQuality() {
        if (qualityBoxMusic_) {                                         // 音乐内容 → V 键循环音质（对齐 macOS ✓）
            static const QStringList qOrder{"lossless", "exhigh", "standard"};
            const QString curQ = settings_.str("music.qualityCeiling", "exhigh");
            int qi = qOrder.indexOf(curQ);
            if (qi < 0) qi = 1;
            applySetting("music.qualityCeiling", qOrder.at((qi + 1) % qOrder.size()));
            setQualityBoxForMusic(true);                                // 重建下拉（选中同步 ✓）
            results_->addItem(QString("[音质] 已切到 %1").arg(qOrder.at((qi + 1) % qOrder.size()) == "lossless" ? "无损"
                                                              : (qOrder.at((qi + 1) % qOrder.size()) == "exhigh" ? "320k" : "128k")));
            if (playKey_.startsWith("netease:") || playKey_.startsWith("qq:")) playItem(playKey_, playLabel_);
            return;
        }
        static const QVector<int> order = Settings::qualityHeights();   // ④ 单一档位表（含 2160/1440）
        const int cur = effectiveVideoHeight();
        int i = order.indexOf(cur);
        if (i < 0) i = 0;
        switchVideoQuality(order.at((i + 1) % order.size()));
}

void MainWindow::followSourceFor(const QString &service) {
        QString name;
        if (service == "youtube") name = "YouTube";
        else if (service == "bilibili") name = "B站";
        else if (service == "netease") name = "网易云音乐";
        else if (service == "qqmusic") name = "QQ音乐";
        playPlatform_ = name;
        if (name.isEmpty() || !srcBox_) return;
        const int idx = srcBox_->findText(name);
        if (idx >= 0 && idx != srcBox_->currentIndex()) {
            srcBox_->setCurrentIndex(idx);
            qInfo() << "搜索源已跟随内容切到:" << name;
        }
}

QString MainWindow::importCookiesFromBrowser(const QString &browser) {
        QString summary;
        const bool ok = CookieImport::importFromBrowser(
            browser, "https://www.bilibili.com/video/BV1GJ411x7h7", &summary);
        const QString msg = ok ? QString("cookie 导入完成：%1").arg(summary)
                               : QString("cookie 导入失败：%1").arg(summary);
        if (results_) results_->addItem(msg);
        std::cout << msg.toStdString() << std::endl;   // CLI 直接可读
        return msg;
}

void MainWindow::openFiles(const QStringList &files) {
        // 单个网页地址：走在线解析（与搜索结果同一条路径 —— 解析直链 + 抓在线字幕）
        // 之前无论传什么都当本地文件直接 playResolved，导致 --open <网址> 把网页当媒体流，
        // 在线字幕自然一条也拿不到（实测踩到，见踩坑 #66）
        if (files.size() == 1 && isWebUrl(files.first())) { playItem(files.first(), files.first()); return; }
        playlist_.setFiles(files);
        playCurrent();
}

void MainWindow::setVol(int v) {
        v = qBound(0, v, 130);
        player_->setVolume(v);
        vol_->setValue(v);
}

// ── UI-4-B ✓ 搜索过滤器（与 macOS searchSort/searchDuration、Android SearchFilters 完全同语义 ✓）
//    面板形态**逐项照搬** macOS `UiActions.swift:794-850`（垂直单选 ✓ 18/22 边距 ✓ spacing 10 ✓
//    带范围的时长文案 ✓ Enter 默认按钮 ✓ 居中 180/179 ✓）—— 见文档 61 §二十一 ✓
void MainWindow::applySearchFilters(int sort, int duration) {
    searchSort_     = qBound(1, sort, 3);
    searchDuration_ = qBound(0, duration, 3);
    if (resolver_) resolver_->setSearchFilters(searchSort_, searchDuration_);
    const QString sN = UrlResolver::sortNames().value(searchSort_ - 1);
    const QString dN = UrlResolver::durationNames().value(searchDuration_);
    std::fprintf(stderr, "[过滤器] 已应用 排序=%d（%s） 时长=%d（%s）\n",
                 searchSort_, sN.toUtf8().constData(), searchDuration_, dN.toUtf8().constData());
    setStatusLine(QStringLiteral("搜索过滤器：排序=%1 · 时长=%2").arg(sN, dN));
    // macOS 行为 ✓（UiActions.swift:793）：应用后**若搜索框里有词就立即按新条件重搜** —— 用户预期"点了就生效"
    const QString q = search_ ? search_->text().trimmed() : QString();
    if (!q.isEmpty()) doSearch();
}

void MainWindow::openFilterDialog() {
    std::fprintf(stderr, "[过滤器] 打开面板（当前 排序=%d 时长=%d）\n", searchSort_, searchDuration_);   // 与 macOS 同文案 ✓
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("搜索过滤器"));
    dlg.setFixedWidth(360);                                  // macOS 面板 360 宽 ✓（UiActions.swift:795）
    dlg.setMinimumHeight(330);                               // macOS 面板 330 高 ✓
    auto *root = new QVBoxLayout(&dlg);
    root->setContentsMargins(22, 18, 22, 18);                // macOS insets 18/22/18/22 ✓
    root->setSpacing(10);                                    // macOS spacing 10 ✓
    auto header = [&](const QString &t) {
        auto *l = new QLabel(t, &dlg);
        l->setStyleSheet("font-size: 13px; font-weight: 600;");   // macOS 13px semibold ✓
        root->addWidget(l);
    };
    header(QStringLiteral("排序（对 YouTube / B站生效）"));      // macOS 原文 ✓
    auto *sortGrp = new QButtonGroup(&dlg);
    const QStringList sn = UrlResolver::sortNames();
    for (int i = 0; i < sn.size(); ++i) {
        auto *b = new QRadioButton(sn.at(i), &dlg);
        b->setChecked(searchSort_ == i + 1);
        sortGrp->addButton(b, i + 1);
        root->addWidget(b);                                  // **垂直** ✓ 与 macOS 一致 ✓
    }
    header(QStringLiteral("时长"));
    auto *durGrp = new QButtonGroup(&dlg);
    const QStringList dn = UrlResolver::durationLabels();     // 带范围 ✓ macOS 原文 ✓
    for (int i = 0; i < dn.size(); ++i) {
        auto *b = new QRadioButton(dn.at(i), &dlg);
        b->setChecked(searchDuration_ == i);
        durGrp->addButton(b, i);
        root->addWidget(b);
    }
    root->addSpacing(6);
    auto *row = new QHBoxLayout();
    auto *apply = new QPushButton(QStringLiteral("应用过滤器"), &dlg);
    apply->setObjectName("primaryBtn");                      // 主按钮（蓝实心 ✓ 与 macOS 的蓝色默认按钮一致 ✓）
    apply->setDefault(true);                                 // Enter 即应用 ✓（macOS keyEquivalent "\r" ✓）
    auto *cancel = new QPushButton(QStringLiteral("取消"), &dlg);
    row->addWidget(apply);
    row->addWidget(cancel);
    row->addStretch(1);
    root->addLayout(row);
    root->addStretch(1);
    QObject::connect(apply, &QPushButton::clicked, &dlg, [this, &dlg, sortGrp, durGrp] {
        applySearchFilters(sortGrp->checkedId(), durGrp->checkedId());
        dlg.accept();
    });
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    {   // 居中于主窗 ✓（照搬 macOS：f.midX-180 / f.midY-179 ✓ UiActions.swift:845）
        const QRect f = frameGeometry();
        dlg.move(f.center().x() - 180, f.center().y() - 179);
    }
    dlg.exec();
}

void MainWindow::setStatusLine(const QString &t) {
        if (statusLabel_) { statusLabel_->setText(t); statusLabel_->setVisible(true); }
}

void MainWindow::finishMore(int reqId, int added) {
        if (reqId != moreReqId_) return;
        moreLoading_ = false;
        if (added == 0) {                                          // 本页没有新内容 → 到底（不设人为上限）
            moreHasMore_ = false;
            setStatusLine(QString("已加载全部 %1 条结果").arg(moreSeen_.size()));
            std::fprintf(stderr, "[MORE] 到底：共 %d 条\n", int(moreSeen_.size()));
        } else {
            ++morePage_;
            rebuildQueueFromList();                               // 追加后重建播放队列（⏭ 与自动续播依赖它）
            setStatusLine(QString("已加载 %1 条（继续下拉可加载更多）").arg(moreSeen_.size()));
            std::fprintf(stderr, "[MORE] 追加 %d 条，累计 %d 条（第 %d 页）\n", added, int(moreSeen_.size()), morePage_);
        }
}

qint64 MainWindow::ttlMs() const {
        const QString svc = UrlResolver::serviceOf(resolutionKey_);
        if (svc == "youtube")  return 4LL * 60 * 60 * 1000;
        if (svc == "bilibili") return 2LL * 60 * 60 * 1000;
        if (svc == "netease" || svc == "qqmusic") return 30LL * 60 * 1000;
        return std::numeric_limits<qint64>::max();
}

void MainWindow::healIfExpired(bool force) {
        if (healTried_ || resolutionKey_.isEmpty()) return;
        const qint64 age = QDateTime::currentMSecsSinceEpoch() - resolvedAtMs_;
        if (!force && (resolvedAtMs_ == 0 || age < ttlMs())) return;
        healTried_ = true;
        qInfo() << "[HEAL] 直链已" << age / 1000 << "秒（> TTL" << ttlMs() / 1000 << "秒）→ 重新解析";
        std::fprintf(stderr, "[HEAL] 直链过期 %llds → 重新解析并回原位\n", static_cast<long long>(age / 1000));
        setStatusLine("直链已过期，正在重新解析…");
        switchVideoQuality(effectiveVideoHeight());      // 内含重新解析 + pendingResume_ 回原进度
}

void MainWindow::playCurrent() {
        const QString f = playlist_.current();
        if (f.isEmpty()) return;
        results_->addItem("正在播放: " + playlist_.displayName());
        beginPlayback(f, playlist_.displayName());
        player_->playResolved(f, QString());
        refreshControls();
}

QString MainWindow::fmtTime(double sec) {
        if (sec < 0 || !qIsFinite(sec)) sec = 0;
        const int t = (int)sec;
        return QString("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QChar('0'));
}

void MainWindow::playPrevFile() { playlist_.prev(); playCurrent(); }

void MainWindow::playNextFile() { playlist_.next(); playCurrent(); }

void MainWindow::selfTest(const QString &f) { player_->playResolved(f, QString()); }

bool MainWindow::isWebUrl(const QString &s) { return s.startsWith("http://") || s.startsWith("https://"); }

void MainWindow::applySubtitleDelay(double sec) { player_->adjustSubtitleDelay(sec); }

void MainWindow::applySubtitleTrackIndex(int n) { player_->setPreferredSubtitleTrack(n); }

void MainWindow::togglePiPPublic() { togglePiP(); }

void MainWindow::biliSearchPublic(const QString &k, bool play) { biliSearch(k, play); }

void MainWindow::toggleVideoFullscreen() { videoFull_ ? exitVideoFullscreen() : enterVideoFullscreen(); }

void MainWindow::setResumeEnabled(bool b) { resumeEnabled_ = b; }

void MainWindow::clearProgress() { prog_.clear(); prog_.save(); }

void MainWindow::applySource(int n) { if (srcBox_) srcBox_->setCurrentIndex(qBound(0, n, srcBox_->count() - 1)); }

void MainWindow::setAutoplay(bool v) { autoplay_ = v; }

void MainWindow::demoSearch(const QString &q) { search_->setText(q); doSearch(); }
