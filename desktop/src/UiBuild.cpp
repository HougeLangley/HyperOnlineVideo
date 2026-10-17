#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

#include "YtRangeProxy.h"

// ── 可流播性预检（Issue #1 ✓ 2026-10-04 修正）──────────────────────────
// 真因：受限出口对 **capped Range** 放行（206），对 **无 Range / bytes=0-** 的开放式 GET 403。
// mpv/ffmpeg 起播用开放式 → 403；旧 4MB 预检只会测 capped → 误报「可播 ✓」。
// 现：预检改为探测「开放式 GET」；播放路径一律走 YtRangeProxy 分块拉取。
static void preflightPlayable(const QString &url) {
    if (!url.startsWith("https://") || !url.contains("googlevideo.com")) return;
    static QNetworkAccessManager nam;
    // ① capped：确认出口至少能拉媒体块
    QNetworkRequest capped{QUrl(url)};
    capped.setRawHeader("Range", "bytes=0-65535");
    capped.setRawHeader("Referer", "https://www.youtube.com/");
    auto *cap = nam.get(capped);
    QObject::connect(cap, &QNetworkReply::finished, cap, [cap, url] {
        const int capCode = cap->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        cap->deleteLater();
        if (capCode != 200 && capCode != 206) {
            qWarning() << "[PREFLIGHT] YouTube rejected even capped Range (HTTP" << capCode
                       << ") — try another exit/proxy";
            return;
        }
        // ② 开放式：模拟 mpv；若 403 则说明必须走本地分块代理（已在 playHandler 启用）
        static QNetworkAccessManager nam2;
        QNetworkRequest open{QUrl(url)};
        open.setRawHeader("Referer", "https://www.youtube.com/");
        auto *op = nam2.get(open);
        QObject::connect(op, &QNetworkReply::finished, op, [op] {
            const int code = op->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (code == 200 || code == 206) {
                qInfo() << "[PREFLIGHT] YouTube open GET playable (HTTP" << code << ")";
            } else {
                qInfo() << "[PREFLIGHT] open GET → HTTP" << code
                        << " (restricted exit) — using local chunked Range proxy";
            }
            op->deleteLater();
        });
    });
}

#include "HovLog.h"
// ── UiBuild.cpp：从 MainWindow.h 搬出的成员实现（文档 52/53/54 ✓ 零行为改动 ✓）──
// 搬运清单: 
#include "MainWindow.h"
#include "DownloadPanel.h"
#include "Icons.h"
#include "Ui.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <memory>     // W2 ✓ 下载面板（进度回调要转发到它；仅用指针不足以调 updateJob ✗ 需完整类型 ✓）




// 播放模式 → 图标 + 提示（与 macOS modeButton 的四档一一对应）
static IconId modeIcon(PlayQueue::Mode m) {
    switch (m) {
    case PlayQueue::Mode::RepeatOne: return IconId::RepeatOne;
    case PlayQueue::Mode::Shuffle:   return IconId::Shuffle;
    case PlayQueue::Mode::RepeatAll: return IconId::Repeat;
    default:                         return IconId::PlaylistPlay;
    }
}
static QString modeTip(PlayQueue::Mode m) {
    switch (m) {
    case PlayQueue::Mode::RepeatOne: return "播放模式：单曲循环（点击切换）";
    case PlayQueue::Mode::Shuffle:   return "播放模式：随机播放（点击切换）";
    case PlayQueue::Mode::RepeatAll: return "播放模式：列表循环（点击切换）";
    default:                         return "播放模式：顺序播放（点击切换）";
    }
}

MainWindow::MainWindow() {
        settings_.load();   // ⚠ 铁律（#208 家族 ✗）：必须在**一切按设置初始化的控件之前**
                            //   历史事故：此前放在构造函数中后段 → 画质盒/自动续播框/启动 cookie 导入
                            //   全部读到**默认值**（用户实测 2026-09-25：设置 1080p，左下角恒显示"自动"✗）
        auto *central = new QWidget(this);
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(M::space4, M::space4, M::space4, M::space4);
        root->setSpacing(M::space3);

        // ── 顶部：搜索行 + 来源芯片行（布局见 design/desktop-design-language.md「主窗口」）──
        auto *topCard = new QFrame(this);
        auto *topCol = new QVBoxLayout(topCard);
        topCol->setContentsMargins(0, 0, 0, 0);
        topCol->setSpacing(M::space3);
        auto *top = new QHBoxLayout();
        top->setSpacing(M::space2);
        topCol->addLayout(top);
        // srcBox_ 仍是来源的唯一数据源（各处按 currentIndex 读写），界面上由芯片代替，下拉框本身不显示
        srcBox_ = new QComboBox(this);
        srcBox_->addItems({"YouTube", "B站", "网易云音乐", "QQ音乐", "本地库"});
        srcBox_->setVisible(false);
        search_ = Ui::searchField("搜索视频或歌曲，回车开始", this);
        auto *filterBtn = Ui::iconButton(IconId::Tune, "搜索过滤器：排序与时长", this);
        connect(filterBtn, &QPushButton::clicked, this, [this] { openFilterDialog(); });
        auto *btn = Ui::filledButton(IconId::Search, "搜索", this);
        auto *dlBtn = Ui::iconButton(IconId::Download, "下载当前播放的直链到 ~/Downloads/hov/", this);
        connect(dlBtn, &QPushButton::clicked, this, [this] { downloadCurrent(); });
        auto *moreBtn = Ui::iconButton(IconId::MoreHoriz, "更多", this);
        {
            auto *menu = new QMenu(moreBtn);
            // 顺序与 macOS「更多」一致：下载面板 / 打开本地文件 / 收藏 / 本地库 / 设置
            connect(Ui::menuAction(menu, IconId::Downloading, "下载面板"), &QAction::triggered, this,
                    [this] { openDownloadPanel(); });
            connect(Ui::menuAction(menu, IconId::FileOpen, "打开本地文件…"), &QAction::triggered, this, [this] {
                const QStringList fs = QFileDialog::getOpenFileNames(this, "打开音视频文件（可多选）", QDir::homePath(),
                    "媒体文件 (*.mp4 *.mkv *.webm *.mov *.mp3 *.flac *.m4a *.aac *.wav *.ogg *.opus);;所有文件 (*)");
                if (!fs.isEmpty()) openFiles(fs);
            });
            connect(Ui::menuAction(menu, IconId::Favorite, "收藏"), &QAction::triggered, this,
                    [this] { openFavoritesPanel(); });
            connect(Ui::menuAction(menu, IconId::Folder, "本地库"), &QAction::triggered, this,
                    [this] { showLocalLibrary(); });
            menu->addSeparator();
            connect(Ui::menuAction(menu, IconId::Settings, "设置…"), &QAction::triggered, this,
                    [this] { openSettingsDialog(); });
            moreBtn->setMenu(menu);
        }
        auto *loginBtn = Ui::iconButton(IconId::AccountCircle, "登录（cookie 保存到 ~/.config/hov/cookies/）", this);
        {
            auto *menu = new QMenu(loginBtn);
            struct SiteItem { const char *site; const char *label; };
            static const SiteItem kSites[] = {
                { "youtube",  "YouTube（Google 账号）" },
                { "bilibili", "哔哩哔哩 B站" },
                { "netease",  "网易云音乐" },
                { "qqmusic",  "QQ音乐" },
            };
            for (const auto &it : kSites) {
                const QString site = it.site;
                connect(Ui::menuAction(menu, IconId::Login, QString::fromUtf8(it.label)), &QAction::triggered, this,
                        [this, site] { openLoginDialog(site); });
            }
            menu->addSeparator();
            // 不依赖内置 WebView 的登录路径（AppImage/Flatpak 也能用；同时绕开 Google 对嵌入式登录的限制）
            connect(Ui::menuAction(menu, IconId::OpenInBrowser, "用系统浏览器登录并导入 cookie"), &QAction::triggered, this, [this] {
                const QString browser = settings_.str("network.cookiesFromBrowser", "");
                if (browser.isEmpty()) {
                    results_->addItem("请先在设置里指定浏览器（network.cookiesFromBrowser，例如 safari / chrome / firefox）");
                    results_->addItem("设置命令：--set network.cookiesFromBrowser=chrome");
                    return;
                }
                results_->addItem(QString("正在从 %1 导入 cookie…（首次可能弹出钥匙串授权）").arg(browser));
                importCookiesFromBrowser(browser);
            });
            connect(Ui::menuAction(menu, IconId::FolderOpen, "打开 cookie 目录"), &QAction::triggered, this, [this] {
                const QString dir = QDir::homePath() + "/.config/hov/cookies";
                QDir().mkpath(dir);
                results_->addItem(QString("cookie 目录：%1").arg(dir));
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
            });
            loginBtn->setMenu(menu);
        }
        top->addWidget(search_, 1);
        top->addWidget(filterBtn);
        top->addWidget(btn);
        top->addSpacing(M::space2);
        top->addWidget(dlBtn);
        top->addWidget(moreBtn);
        top->addWidget(loginBtn);
        {
            auto *chips = new QHBoxLayout();
            chips->setSpacing(M::space2);
            topCol->addLayout(chips);
            struct SourceChip { IconId icon; const char *text; };
            static const SourceChip kChips[] = {   // 顺序必须与 srcBox_ 的条目一致
                { IconId::SmartDisplay, "YouTube" },
                { IconId::LiveTv,       "B站" },
                { IconId::MusicNote,    "网易云音乐" },
                { IconId::LibraryMusic, "QQ音乐" },
                { IconId::Folder,       "本地库" },
            };
            auto *group = new QButtonGroup(this);
            group->setExclusive(true);
            int id = 0;
            for (const auto &c : kChips) {
                auto *chip = Ui::chip(c.icon, QString::fromUtf8(c.text), this);
                group->addButton(chip, id++);
                chips->addWidget(chip);
            }
            chips->addStretch(1);
            if (QAbstractButton *b = group->button(srcBox_->currentIndex())) b->setChecked(true);
            connect(group, &QButtonGroup::idClicked, srcBox_, &QComboBox::setCurrentIndex);
            connect(srcBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), group, [group](int i) {
                if (QAbstractButton *b = group->button(i)) b->setChecked(true);
            });
        }
        topCard_ = topCard;                     // B5：画中画时隐藏
        root->addWidget(topCard);

        auto *split = new QSplitter(Qt::Horizontal, this);
        split->setHandleWidth(M::space3);       // 与区块间距一致
        results_ = new QListWidget(split);
    // v1.2.0：结果区对齐 macOS 的"卡片网格"。
    // 原来用 QListWidget 默认列表模式（图标 16~24px），缩略图虽已抓取但被缩到 48×27，
    // 视觉上退化成"小图标 + 一行字"，与 macOS 大卡片差异明显（用户对比截图指出）。
    results_->setViewMode(QListView::IconMode);       // 卡片布局
    // 卡片高度随标题行数变化 → 行高错落（瀑布感的来源）。
    // 注意：**不要**改成 Flow::TopToBottom —— IconMode 下它是"按视口高度分列"，会横向溢出到看不见的第二列
    //（实测截图踩到：右侧一列文字被截断，比等高网格还差）。行优先 + 可变高度才是稳妥做法。
    results_->setFlow(QListView::LeftToRight);
    results_->setWrapping(true);
    results_->setIconSize(QSize(160, 90));            // 16:9 缩略图
    results_->setResizeMode(QListView::Adjust);       // 改宽度时重排
    results_->setMovement(QListView::Static);         // 禁止拖动重排（不改变既有交互）
    results_->setWordWrap(true);                      // 标题换行
    results_->setUniformItemSizes(false);         // 卡片与状态行高度不同
    results_->setItemDelegate(new ResultCardDelegate(results_));   // 自绘卡片（关键）
    results_->setMouseTracking(true);             // 悬停高亮
    results_->viewport()->setAttribute(Qt::WA_Hover, true);
    results_->setSpacing(M::space1);
    results_->setTextElideMode(Qt::ElideRight);
    results_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    results_->setSelectionMode(QAbstractItemView::SingleSelection);
        player_ = new MpvWidget(split);
        {   // 空状态：没有媒体时在黑色画面中央显示图标与提示，refreshControls() 控制显隐
            idleHint_ = new QWidget(player_);
            idleHint_->setAttribute(Qt::WA_TranslucentBackground);
            auto *ih = new QVBoxLayout(idleHint_);
            ih->setContentsMargins(0, 0, 0, 0);
            ih->setSpacing(M::space3);
            auto *icon = new QLabel(idleHint_);
            icon->setPixmap(Icons::icon(IconId::Movie, Tok::OnScrim).pixmap(QSize(M::iconHero, M::iconHero), devicePixelRatioF()));
            icon->setAlignment(Qt::AlignCenter);
            idleText_ = Ui::label(QString(), Ui::TextRole::OnScrim, idleHint_);
            idleText_->setAlignment(Qt::AlignCenter);
            ih->addWidget(icon);
            ih->addWidget(idleText_);
            auto *center = new QVBoxLayout(player_);
            center->addWidget(idleHint_, 0, Qt::AlignCenter);
        }
        // MPRIS：桌面端“后台播放 + 媒体键”的标准通道（无会话总线时自降级）
        mpris_ = new Mpris(player_, this);
        // 在线播放：网页 URL 由 App 层解析成直链（mpv 的 ytdl 钩子已禁用，见 MpvWidget 注释）
        player_->setFocus();   // 快捷键立即生效（否则焦点在搜索框）
        resolver_ = new UrlResolver(this);
        ytProxy_ = new YtRangeProxy(this);
        resolver_->setStatusHandler([this](const QString &m) { results_->addItem(m); });
        resolver_->setFailHandler([this](const QString &key) {
            // 2026-10-17（与 Android「点下一个视频出错」同一案 ✗ 用户要求挖掘潜在同类 ✗）：
            // 队列驱动的播放解析失败（条目失效/受限 ✗）→ 自动跳过（最多连跳 5 ✓ 有界 ✓ 防风暴 ✓）
            bool inQueue = false;
            for (int i = 0; i < queue_.size(); ++i)
                if (queue_.at(i) && queue_.at(i)->key == key) { inQueue = true; break; }
            if (!inQueue) return;                     // 手点单曲失败：维持原行为（只报状态 ✓）
            if (++skipStreak_ > 5) {
                skipStreak_ = 0;
                results_->addItem("连续 5 条无法播放，已停止（可能网络受限 ✗）");
                return;
            }
            hovLog("[SKIP] 解析失败 → 跳过（第 %d 条）：%s\n", skipStreak_, qPrintable(key.left(80)));
            results_->addItem(QString("跳过无法播放：%1").arg(key.left(70)));
            queueOrFileNext(/*autoAdvance=*/true);    // 前进一条 ✓（新播放成功解析后会复位 ✓）
        });
        resolver_->setPlayHandler([this](const UrlResolver::Stream &s) {
            skipStreak_ = 0;                // 解析成功 → 连跳计数复位 ✓（2026-10-17 ✓）
            player_->setCoverArt(QString(), QString(), QString());   // 视频：清掉音乐封面
            // Issue #1：googlevideo 直链经本机分块代理再交给 mpv（开放式 GET 在受限出口会 403）
            const QString playVideo = ytProxy_ ? ytProxy_->map(s.videoUrl) : s.videoUrl;
            const QString playAudio = (ytProxy_ && !s.audioUrl.isEmpty()) ? ytProxy_->map(s.audioUrl) : s.audioUrl;
            if (playVideo != s.videoUrl)
                hovLog("[YT-PROXY] video via localhost %s\n", qPrintable(playVideo));
            if (!playAudio.isEmpty() && playAudio != s.audioUrl)
                hovLog("[YT-PROXY] audio via localhost %s\n", qPrintable(playAudio));
            player_->playResolved(playVideo, playAudio);
            resolvedAtMs_ = QDateTime::currentMSecsSinceEpoch();
            healTried_ = false;   // 新内容重新允许自愈
            preflightPlayable(s.videoUrl);   // 仍探测原始直链（诊断用；播放已走代理）
    hovLog("[PLAY] 交给 mpv: host=%s len=%lld\n", QUrl(playVideo).host().toUtf8().constData(), (long long)playVideo.size());
              hovLog("[PLAY] 已解析: 视频=%s(%lld) 音频=%s(%lld) 标题=%s\n",
                           QUrl(s.videoUrl).host().toUtf8().constData(), (long long)s.videoUrl.size(),
                           QUrl(s.audioUrl).host().toUtf8().constData(), (long long)s.audioUrl.size(),
                           s.title.toUtf8().constData());
            lastAudioUrl_ = s.audioUrl;      // ③ 下载混流用（音乐单路时为空串，无副作用）——保留**原始**直链
            // ③ **Bug3 修复** ✗→✓：此前这里**只记了音频**，没记主直链 ✗
            //    → 视频正在播时点「下载」，downloadCurrent() 报“当前没有可下载的直链” ✗
            //    （用户 2026-09-23 实测截图 ✓）。macOS 端在 AppDelegate.swift:295 早已记录 ✓ → 两端对齐 ✓
            lastStreamUrl_   = s.videoUrl;   // 下载用原始 CDN URL（下载器可自带 Range）
            lastStreamTitle_ = s.title.isEmpty() ? playLabel_ : s.title;
            lastLRC_.clear();                // ③ 歌词异步到达，先清上一首避免错配
            if (!s.title.isEmpty())
                results_->addItem(QString("正在播放[%1]：%2")
                                      .arg(playPlatform_.isEmpty() ? "在线" : playPlatform_, s.title));
            if (!s.subtitles.isEmpty()) player_->addSubtitleTracks(s.subtitles);   // 在线字幕轨（追加，不覆盖本地轨）
        });
        resolver_->setTracksHandler([this](const QVector<SubtitleTrack> &tracks) {
            if (player_ && !tracks.isEmpty()) player_->addSubtitleTracks(tracks);
        });
        split->addWidget(results_);
        split->addWidget(player_);
        player_->installEventFilter(this);   // v1.2.0：双击画面 = 视频全屏（对齐 macOS/Android）
        split->setStretchFactor(1, 3);
        split_ = split;                         // B5：画中画时隐藏结果列表侧
        // v1.2.0：状态行（对齐 macOS —— 结果区只放卡片，日志式信息单独一行显示）。
        // 之前"使用 cookie / 正在获取字幕轨 / 第 1/15 首"等**混在结果列表里**，既污染结果区又像 bug。
        statusLabel_ = new QLabel(central);
        statusLabel_->setObjectName("hovStatus");
        statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        Ui::setTextRole(statusLabel_, Ui::TextRole::CaptionVariant);
        // ⚠️ W5 ✓ 关键修复：**长文本不再撑大窗口** ✗→✓
        //   现象（用户 2026-09-23 截图）：播放开始后窗口从 1200x720 变 1332x775 ✗（有时更宽/更高 ✗）
        //   根因：QLabel 的 minimumSizeHint 随文本变长 ✗ → 布局最小宽变大 ✗ → Qt 把窗口撑到最小宽 ✗
        //        （高 DPI/大字体下更明显 ✓ 我 VM 字体小 → 只到 539 ✗ 复现不全 ✓ 探针实测 ✓）
        //   修法：水平策略设为 Ignored → 布局**不计**它的最小宽 ✓（可用空间内显示、超长裁切 ✓）
        statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        statusLabel_->setMinimumWidth(0);
        statusLabel_->setVisible(false);
        // v1.2.0：fd 泄漏探针（长时间播放后句柄打满会导致事件分发器创建失败 → abort）。
        // 每 5 分钟往 stderr 打一行；不涨说明正常，持续上涨即可锁定泄漏源。
        {
            auto *fdTimer = new QTimer(this);
            fdTimer->setInterval(5 * 60 * 1000);
            connect(fdTimer, &QTimer::timeout, this, [] {
                const QDir fdDir("/proc/self/fd");
                const int n = fdDir.exists()
                    ? fdDir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size() : -1;
                hovLog("[FD] 当前打开的文件描述符: %d\n", n);
            });
            fdTimer->start();
        }
        // 单点拦截：凡插入**没有 Qt::UserRole（即非结果条目）**的行，都是日志/状态，
        // 自动从结果列表移除并显示到状态行（97 处 results_->addItem 无需逐个改）。
        connect(results_->model(), &QAbstractItemModel::rowsInserted, this,
        [this](const QModelIndex &, int, int) {
            // 延迟到插入完成后再清理；**扫全表而不是用 [first,last] 区间**：
            // 多条消息连续插入时，先前的删除会让区间指向错行 → 漏删（实测用户侧漏了 3 条）。
            QTimer::singleShot(0, this, [this] {
                if (!results_) return;
                for (int row = results_->count() - 1; row >= 0; --row) {
                    QListWidgetItem *it = results_->item(row);
                    if (!it) continue;
                    if (!it->data(Qt::UserRole).toString().isEmpty()) continue;      // 结果条目：保留
                    if (!it->data(Qt::DecorationRole).value<QIcon>().isNull()) continue;
                    if (it->data(Qt::UserRole + 12).toBool()) continue;              // 已标记为卡片：保留
                    const QString t = it->text().trimmed();
                    if (!t.isEmpty() && statusLabel_) {
                        statusLabel_->setText(t);
                        statusLabel_->setVisible(true);
                    }
                    delete results_->takeItem(row);
                }
            });
        });
        // v1.2.0：真·瀑布流结果视图（每列独立流动，对齐"瀑布流"预期）。
        // 设计：**保留 results_ 作为数据源与逻辑层**（全部 addItem/点击/缩略图逻辑零改动），只把它隐藏；
        // 显示由 MasonryView 承担。设置 ui.masonry=false 可一键回退到传统列表视图。
        masonry_ = new MasonryView(split);
        masonry_->setProperty("hov", "panel");
        masonry_->setSource(results_);
        masonry_->onActivate = [this](int row) {                 // 单击卡片 → 与列表点击同一条链路
            if (QListWidgetItem *it = results_->item(row))
                playItem(it->data(Qt::UserRole).toString(), it->text());
        };
        split->insertWidget(1, masonry_);
        startStallWatch();          // 直链过期自愈看门狗（与 Android 同法）
        {
            // 瀑布流触发：任一视图滚到接近底部（<200px）就加载下一页（对齐 macOS 的 loadMoreIfNeeded）
            auto nearBottom = [](QScrollBar *sb) {
                return sb && sb->value() + sb->pageStep() >= sb->maximum() - 200;
            };
            // 诊断（--load-more 之外，正常滚动也会打点，便于无头取证）
            auto trace = [](const char *who, QScrollBar *sb, bool visible) {
                if (sb && sb->value() + sb->pageStep() >= sb->maximum() - 400)
                    hovLog("[MORE] %s 滚动: v=%d max=%d step=%d visible=%d\n",
                                 who, sb->value(), sb->maximum(), sb->pageStep(), int(visible));
            };
            connect(results_->verticalScrollBar(), &QScrollBar::valueChanged, this,
                    [this, nearBottom, trace](int) {
                        trace("列表", results_->verticalScrollBar(), results_->isVisible());
                        if (results_->isVisible() && nearBottom(results_->verticalScrollBar())) loadMore();
                    });
            connect(masonry_->verticalScrollBar(), &QScrollBar::valueChanged, this,
                    [this, nearBottom, trace](int) {
                        trace("瀑布", masonry_->verticalScrollBar(), masonry_->isVisible());
                        if (masonry_->isVisible() && nearBottom(masonry_->verticalScrollBar())) loadMore();
                    });
        }
        {
            QAbstractItemModel *m = results_->model();           // 与列表模型保持同步
            // ⚠ 接收者必须用 masonry_ 而不是 this：MainWindow 析构时子对象销毁顺序不定，
            // results_ 析构中发的 modelReset/rowsRemoved 若投递到已销毁的 masonry_ 就是
            // 悬空调用 → 退出 SEGV（实测：~QListWidget → modelReset → MasonryView::layout
            // → viewport() 空指针）。以 masonry_ 为接收者，其析构时连接自动断开。
            connect(m, &QAbstractItemModel::rowsInserted, masonry_,
                    [this](const QModelIndex &, int, int) {
                        QTimer::singleShot(0, masonry_, [this] { if (masonry_) masonry_->rebuild(); });
                    });
            connect(m, &QAbstractItemModel::rowsRemoved, masonry_, [this] { if (masonry_) masonry_->rebuild(); });
            connect(m, &QAbstractItemModel::modelReset,  masonry_, [this] { if (masonry_) masonry_->rebuild(); });
            connect(m, &QAbstractItemModel::dataChanged, masonry_,
                    [this](const QModelIndex &a, const QModelIndex &b) { if (masonry_) masonry_->refreshRows(a.row(), b.row()); });
        }
        root->addWidget(split, 1);

        // ---- 底部播放条：一个面板两行（布局见 design/desktop-design-language.md「主窗口」）----
        // 行 1：进度条 + 时间
        // 行 2：三栏 —— 左：正在播放（标题 / 来源与状态 / 瞬时状态）；中：播放控制；右：画质 倍速 音量 字幕 画中画 全屏
        auto *ctl = Ui::panel(this);
        ctlPanel_ = ctl;
        auto *ctlBox = new QVBoxLayout(ctl);
        ctlBox->setContentsMargins(M::space4, M::space3, M::space4, M::space3);
        ctlBox->setSpacing(M::space2);

        auto *row1 = new QHBoxLayout();
        row1->setSpacing(M::space3);
        seek_ = new QSlider(Qt::Horizontal, this);
        seek_->setRange(0, 1000);
        labelTime_ = Ui::label("0:00 / 0:00", Ui::TextRole::CaptionVariant, this);
        labelTime_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        labelTime_->setMinimumWidth(labelTime_->fontMetrics().horizontalAdvance("00:00:00 / 00:00:00"));
        row1->addWidget(seek_, 1);
        row1->addWidget(labelTime_);

        auto *row2 = new QGridLayout();
        row2->setHorizontalSpacing(M::space4);
        row2->setColumnStretch(0, 1);
        row2->setColumnStretch(2, 1);

        auto *info = new QVBoxLayout();
        info->setSpacing(0);
        labelInfo_ = Ui::label(QString(), Ui::TextRole::Title, this);
        labelMeta_ = Ui::label(QString(), Ui::TextRole::CaptionVariant, this);
        for (QLabel *l : {labelInfo_, labelMeta_}) {
            l->setTextInteractionFlags(Qt::TextSelectableByMouse);
            l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // 长文本不撑宽窗口（W5）
            l->setMinimumWidth(0);
        }
        statusLabel_->setParent(this);
        info->addWidget(labelInfo_);
        info->addWidget(labelMeta_);
        info->addWidget(statusLabel_);
        info->addStretch(1);
        row2->addLayout(info, 0, 0);

        auto *transport = new QHBoxLayout();
        transport->setSpacing(M::space2);
        modeBtn_ = Ui::iconButton(modeIcon(queue_.mode()), modeTip(queue_.mode()), this);
        connect(modeBtn_, &QPushButton::clicked, this, [this] {
            queue_.cycleMode();
            Ui::setIcon(modeBtn_, modeIcon(queue_.mode()));
            modeBtn_->setToolTip(modeTip(queue_.mode()));
        });
        auto *btnPrev = Ui::iconButton(IconId::SkipPrevious, "上一首", this);
        btnPlay_ = Ui::playButton(this);
        auto *btnNext = Ui::iconButton(IconId::SkipNext, "下一首", this);
        // 连播：播完自动播下一条（与 macOS autoNextBtn / Android 同键）
        autoNextBtn_ = Ui::iconButton(IconId::Autoplay, "连播：播完自动播下一条（按列表顺序）", this,
                                      Ui::IconSize::Standard, true);
        autoNextBtn_->setChecked(settings_.boolean("playback.autoNext", true));
        connect(autoNextBtn_, &QPushButton::clicked, this, [this] {
            const bool on = autoNextBtn_->isChecked();
            applySetting("playback.autoNext", on ? "true" : "false");
            results_->addItem(on ? "连播：开（播完自动下一条）" : "连播：关（播完停止）");
        });
        connect(btnPrev, &QPushButton::clicked, this, [this] { queueOrFilePrev(); });
        connect(btnNext, &QPushButton::clicked, this, [this] { queueOrFileNext(/*autoAdvance=*/false); });
        transport->addWidget(modeBtn_);
        transport->addWidget(btnPrev);
        transport->addWidget(btnPlay_);
        transport->addWidget(btnNext);
        transport->addWidget(autoNextBtn_);
        row2->addLayout(transport, 0, 1, Qt::AlignCenter);

        qualityBox_ = new QComboBox(this);
        {
            const QVector<int> hs = Settings::qualityHeights();
            for (int h : hs) qualityBox_->addItem(Settings::qualityLabelFor(h), h);
            const int curH = int(settings_.number("video.maxHeight", 0));
            const int idx = hs.indexOf(curH);
            qualityBox_->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        Ui::asSelect(qualityBox_);
        qualityBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        qualityBox_->setToolTip("清晰度（与设置面板共用一份档位表 — V 键也可循环切换）");
        connect(qualityBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
            if (qualityBoxMusic_) {                     // 音乐内容：这个下拉是音质档（对齐 macOS B1 ✓ 用户要求 ✓）
                const QString q = qualityBox_->itemData(i).toString();
                applySetting("music.qualityCeiling", q);
                results_->addItem(QString("[音质] 已设为 %1").arg(q == "lossless" ? "无损" : (q == "exhigh" ? "320k" : "128k")));
                if (playKey_.startsWith("netease:") || playKey_.startsWith("qq:"))
                    playItem(playKey_, playLabel_);     // 立即按新音质重取流 ✓（与 macOS 切音质行为一致 ✓）
                return;
            }
            const int h = qualityBox_->itemData(i).toInt();
            applySetting("video.maxHeight", QString::number(h));
            switchVideoQuality(h);                      // 立即用新档位重新解析当前视频 ✓
        });
        // 倍速（**共用一张速度表** ✓ 与 macOS 的 7 档对齐 ✓）
        speedBox_ = new QComboBox(this);
        {
            static const double kSpeeds[] = {0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};
            // 文案与 macOS 一致（1.0x ✓ 1.75x ✓ 而不是 'g' 格式化的 "1" ✗）
            auto fmtSpeed = [](double v) {
                QString t = QString::number(v, 'f', (v == qRound(v)) ? 1 : 2);
                if (t.endsWith('0') && !t.endsWith(QString(".0"))) t.chop(1);
                return t + "x";
            };
            for (double sp : kSpeeds) speedBox_->addItem(fmtSpeed(sp), sp);
            speedBox_->setCurrentIndex(2);              // 1.0x
        }
        Ui::asSelect(speedBox_);
        speedBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        speedBox_->setToolTip("播放倍速");
        connect(speedBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
            const double sp = speedBox_->itemData(i).toDouble();
            player_->setSpeed(sp);
            results_->addItem(QString("倍速: %1x").arg(sp, 0, 'g', 3));
        });
        vol_ = new QSlider(Qt::Horizontal, this);
        vol_->setRange(0, 130);
        vol_->setValue(100);
        vol_->setFixedWidth(M::controlH * 3);
        connect(vol_, &QSlider::valueChanged, this, [this](int v) { player_->setVolume(v); });
        auto *volBtn = Ui::iconButton(IconId::VolumeUp, "静音 / 恢复音量", this);
        {
            auto savedVol = std::make_shared<int>(100);
            connect(volBtn, &QPushButton::clicked, this, [this, savedVol] {
                if (vol_->value() > 0) { *savedVol = vol_->value(); vol_->setValue(0); }
                else vol_->setValue(*savedVol > 0 ? *savedVol : 100);
            });
            connect(vol_, &QSlider::valueChanged, volBtn, [volBtn](int v) {
                Ui::setIcon(volBtn, v == 0 ? IconId::VolumeOff : IconId::VolumeUp);
            });
        }
        auto *subBtn = Ui::iconButton(IconId::Subtitles, "字幕/歌词：关 → 轨1 → 轨2 → …（C 键）", this);
        connect(subBtn, &QPushButton::clicked, this, [this] { if (player_) player_->cycleSubtitleTrack(); });
        auto *pipBtn = Ui::iconButton(IconId::PictureInPictureAlt, "画中画（P 键）", this);
        connect(pipBtn, &QPushButton::clicked, this, [this] { togglePiP(); });
        auto *fsBtn = Ui::iconButton(IconId::Fullscreen, "全屏（双击画面亦可）", this);
        connect(fsBtn, &QPushButton::clicked, this, [this] { toggleVideoFullscreen(); });
        // 铺满没有按钮（与 macOS 一致，只有 Z 键）；fillBtn_ 保持 nullptr，既有空值保护覆盖
        auto *right = new QHBoxLayout();
        right->setSpacing(M::space2);
        right->addStretch(1);
        right->addWidget(qualityBox_);
        right->addWidget(speedBox_);
        right->addWidget(volBtn);
        right->addWidget(vol_);
        right->addWidget(subBtn);
        right->addWidget(pipBtn);
        right->addWidget(fsBtn);
        row2->addLayout(right, 0, 2);

        ctlBox->addLayout(row1);
        ctlBox->addLayout(row2);
        root->addWidget(ctl);

        connect(btnPlay_, &QPushButton::clicked, this, [this] {
            if (player_->paused()) healIfExpired(); // 长时间暂停后按播放：直链可能已过期
            player_->togglePause(); refreshControls();
        });
        connect(seek_, &QSlider::sliderReleased, this, [this] {
            const double d = player_->durationSec();
            if (d > 0) player_->seekTo(d * seek_->value() / 1000.0);
        });
        ctlTimer_ = new QTimer(this);
        ctlTimer_->setInterval(500);
        connect(ctlTimer_, &QTimer::timeout, this, [this] { refreshControls(); tickProgress(); applyFillMode(); });
        ctlTimer_->start();

        mpris_->start();
        // 媒体键的“切歌/停止”仍走既有逻辑（单一来源），不另写一套
        connect(mpris_, &Mpris::requestNext, this, [this] { queueOrFileNext(); });
        connect(mpris_, &Mpris::requestPrevious, this, [this] { queueOrFilePrev(); });
        connect(mpris_, &Mpris::requestStop, this, [this] {
            saveProgressNow();
            player_->stop();
            playKey_.clear();
            results_->addItem("已停止（MPRIS）");
        });

        // 播完自动续播（轮询 eof-reached，与 Android 版做法一致）
        auto *eofTimer = new QTimer(this);
        eofTimer->setInterval(700);
        connect(eofTimer, &QTimer::timeout, this, [this] {
            if (!playlist_.hasMultiple() && queue_.isEmpty()) return;
            int eof = 0;
            // eof 闩锁：在线解析是异步的（yt-dlp 要数秒），期间 mpv 仍报 eof=true →
            // 不加锁会每 700ms 连发 autoNext → yt-dlp/请求风暴（fd/CPU 双重放大，实测队列被瞬间轮询）。
            if (player_->eofReached(&eof) && eof) {
                if (!eofLatch_) { eofLatch_ = true; if (settings_.boolean("playback.autoNext", true)) queueOrFileNext(/*autoAdvance=*/true); }   // 播完自动续播（一次）
            } else {
                eofLatch_ = false;                              // 新媒体起播后解除，下次播完再触发
            }
        });
        eofTimer->start();



        setCentralWidget(central);
        setWindowTitle("聚合视频 · Hyper Online Video");

        connect(btn, &QPushButton::clicked, this, [this] { doSearch(); });
        connect(search_, &QLineEdit::returnPressed, this, [this] { doSearch(); });
        connect(results_, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
            playItem(it->data(Qt::UserRole).toString(), it->text());
        });
        // settings_.load() 已上移至构造函数最前（#208 家族铁律 ✓ 见函数开头注释）
        // W1 ✓ 窗口几何：优先恢复上次关闭时的大小（设置键 ui.windowGeometry ✓ base64 ✓）
        //   ⚠️ 必须放在 settings_.load() **之后**：首版插在前面，读到的永远是空串
        //      → 恢复静默失效（探针实证：save 落盘 88 字节 ✓ 却恢复成 1200x720 ✗）
        {
            const QString g = settings_.str("ui.windowGeometry", "");
            bool restored = false;
            if (!g.isEmpty()) restored = restoreGeometry(QByteArray::fromBase64(g.toLatin1()));
            if (!restored) {
                resize(1200, 720);                                   // 适中默认（用户要求：不要太大也不要太小 ✓）
                if (QScreen *sc = screen()) {                        // 首次启动居中 ✓（用户要求 ✓）
                    const QRect av = sc->availableGeometry();
                    move(av.center().x() - width() / 2, av.center().y() - height() / 2);
                }
            }
            hovLog("[W1] 几何: 键 %d 字符 → %s；当前 %dx%d @(%d,%d)\n",
                         int(g.size()), restored ? "恢复成功 ✓" : "无记忆→默认 1200x720 居中",
                         width(), height(), x(), y());   // 探针式日志 ✓ 自动化可判定 ✓
        }
        // ── 结果区尺寸（v1.2.0）：启动固定"横 2 张 × 纵 4 张"，并记住用户后续调整 ──
        // 之前只设了 setStretchFactor(1,3)，且从不读写 ui.listWidth → 每次启动都是偶然的窄宽度（用户实测）。
        if (split_) {
            const int defW = MasonryView::defaultPanelWidth();
            const int saved = int(settings_.number("ui.listWidth", 0));
            const int wantW = saved > 0 ? saved : defW;
            const int total = qMax(wantW + 360, split_->width() > 0 ? split_->width() : width());
            // 三个子件：results_（数据源，常隐藏）、masonry_、player_；只给两个值时 player_ 分到 0 被收起
            split_->setSizes({0, wantW, total - wantW});
            split_->setCollapsible(split_->indexOf(player_), false);
            // W1 ✗ 已删除"首次运行把窗口高度调到纵向 4 张"的自动放大：
            //   ① 它在构造函数里算 chrome = height() - split_->height()，而此刻布局尚未完成
            //      （实测 split_->height()=30）→ chrome 被算成 690（真实约 150）→ wantH=1374 ✗
            //   ② 更严重：条件只看 ui.listWidth（用户从不拖分隔条时恒为 0）→ **每次启动都会执行**，
            //      把用户记忆的窗口尺寸直接改掉 ✗（与"记住关闭时尺寸"互斥）
            //   ③ 用户口径：打开时"大小适中"（2026-09-23）→ 首启固定 1200x720 + 居中，
            //      此后一切尺寸都由 ui.windowGeometry 记忆
            //   （若日后想要"纵向 4 张"的引导，必须改为**仅在无记忆几何**时、且在布局完成后执行）
            // 只在**用户拖动**（splitterMoved）时保存 → 程序化 setSizes 不会污染记忆
            //（macOS 端踩过：程序化重排把 ui.listWidth 误存成 683pt）
            connect(split_, &QSplitter::splitterMoved, this, [this](int, int) {
                if (!split_ || !masonry_) return;
                const int w = split_->sizes().value(split_->indexOf(masonry_));
                // 值没变就不写：缩略图/卡片重排期 splitterMoved 会密集触发，
                // 无去重时每次都打日志/写盘（用户侧日志 33 秒刷了 217 条 reject）
                if (int(settings_.number("ui.listWidth", 0)) == w) return;
                applySetting("ui.listWidth", QString::number(w));
            });
        }
        // 玻璃质感（ui.glass）：启动即下发；设置面板改动后同样会走到这里
        if (player_) player_->setGlassEnabled(settings_.boolean("ui.glass", true));
        // ── ui.theme：跟随系统 light/dark（Phase 3 保底 ✓ 用户 2026-09-18 指定）──
        applyThemeSettingNow();

        // 窗口级玻璃（ui.glassWindow）：窗口半透明 + 样式表切玻璃模式 → 桌面透出（macOS 整窗毛玻璃的对应物）
        {
            const bool isX11 = QGuiApplication::platformName().contains("xcb");
            // 平台自适应默认：X11 实测「半透明窗口 + QOpenGLWidget」共存正常 ✓；
            // Wayland 下 Qt/KWin 的半透明合成有缺陷（实测 GL 层整层消失 ✗）→ 默认关；
            // 用户在设置里显式设过则一律尊重用户 ✓
            const bool gw = settings_.has("ui.glassWindow") ? settings_.boolean("ui.glassWindow", false) : isX11;
            Theme::setGlass(gw);
            setAttribute(Qt::WA_TranslucentBackground, gw);
            hovLog("[GLASS] 窗口级玻璃=%s 平台=%s 属性生效=%d（Wayland/KDE 可透出桌面；Xvfb 无 ARGB 视觉时不生效属正常）\n",
                         gw ? "开" : "关", qPrintable(QGuiApplication::platformName()),
                         testAttribute(Qt::WA_TranslucentBackground) ? 1 : 0);
        }
        if (masonry_) {                                          // 结果视图选择（ui.masonry=false → 传统列表）
            const bool useMasonry = settings_.boolean("ui.masonry", true);
            masonry_->setVisible(useMasonry);
            results_->setVisible(!useMasonry);
        }
        resolver_->setForceDirect(settings_.boolean("network.forceDirectDomestic", false));   // 国内接口可强制直连
        resolver_->setCookiesFromBrowser(settings_.str("network.cookiesFromBrowser", ""));   // A0：浏览器 cookie（空=关）
        // 下载管理：进度回调写进结果列表，但按 10% 节流，避免刷屏
        dl_ = new DownloadManager(this);
        dl_->setDir(settings_.str("download.dir"));
        dl_->setMaxTotalSizeMb(settings_.number("download.maxSizeMb", 2048));   // 0=不限制；超出按 LRU 清理
        player_->setSubtitleFontScale(settings_.number("subtitle.fontScale", 1.0));
        { const double d0 = settings_.number("subtitle.defaultDelay", 0.0);
          if (qAbs(d0) > 0.001) player_->adjustSubtitleDelay(d0); }
        dl_->setProgressHandler([this](const DownloadManager::Job &j) {
            static QHash<QString, int> lastPct;
            const int pct = (j.bytesTotal > 0) ? int(j.bytesDone * 100 / j.bytesTotal) : -1;
            const int prev = lastPct.value(j.id, -2);
            if (j.state == DownloadManager::State::Done) {
                results_->addItem(QString("下载完成：%1（%2）").arg(j.title, QFileInfo(j.filePath).fileName()));
                results_->addItem(QString("  保存到：%1").arg(j.filePath));
                lastPct.remove(j.id);
            } else if (j.state == DownloadManager::State::Failed) {
                results_->addItem(QString("下载失败：%1（%2）").arg(j.title, j.error));
                lastPct.remove(j.id);
            } else if (j.state == DownloadManager::State::Running && pct >= 0 && pct / 10 != prev / 10) {
                lastPct[j.id] = pct;
                results_->addItem(QString("下载中：%1  %2%").arg(j.title).arg(pct));
            }
            // W2 ✓ 下载面板开着 → 实时刷新那一行（进度条 ✓ 按 id 定位不重建整表 ✓）
            if (dlPanel_) dlPanel_->updateJob(j);
        });
        favs_.load();
        prog_.load();                                        // 进度记忆（--progress-show 也用它）
        if (queue_.loadFrom())
            results_->addItem(QString("[队列] 已恢复上次的 %1 项（%2）")
                                  .arg(queue_.size()).arg(queue_.modeLabel()));
        if (settings_.boolean("ui.showNetworkProbe", true))
            QMetaObject::invokeMethod(this, [this] { probeNetwork(); }, Qt::QueuedConnection);
        connect(results_, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
            playItem(it->data(Qt::UserRole).toString(), it->text());
        });
}
