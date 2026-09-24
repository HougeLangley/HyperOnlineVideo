// ── UiBuild.cpp：从 MainWindow.h 搬出的成员实现（文档 52/53/54 ✓ 零行为改动 ✓）──
// 搬运清单: 
#include "MainWindow.h"
#include "DownloadPanel.h"     // W2 ✓ 下载面板（进度回调要转发到它；仅用指针不足以调 updateJob ✗ 需完整类型 ✓）




// ── 放大镜图标（UI-1 ✓）：Qt **没有**内置放大镜标准图标 ✗ →
//    优先用系统图标主题 ✓；拿不到就用 QPainter 画一个 ✓（不依赖任何外部资源文件 ✓ 任何主题下都可见 ✓）

// ── 手绘图标（UI-1 ✓ 续）：字形 ⬇ ⋯ 👤 在部分 Linux 字体下渲染成方块/三角 ✗（实测截图 ✓）→
//    全部改用 QPainter 绘制 ✓ **不依赖字体与图标主题** ✓ 任何环境一致 ✓
enum class IconKind { Download, More, User,
                      Fullscreen, Pip, Subtitles,
                      ModeSeq, ModeOne, ModeShuffle, AutoNext, ModeAll };

// 播放模式 → 图标 + 提示（UI-3b ✓ 与 macOS modeButton 的四档一一对应 ✓）
static IconKind modeIconKind(PlayQueue::Mode m) {
    switch (m) {
    case PlayQueue::Mode::RepeatOne: return IconKind::ModeOne;
    case PlayQueue::Mode::Shuffle:   return IconKind::ModeShuffle;
    case PlayQueue::Mode::RepeatAll: return IconKind::ModeAll;
    default:                         return IconKind::ModeSeq;
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
static QIcon drawnIcon(IconKind k, const QColor &c = QColor(0xD8, 0xD8, 0xD8)) {
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(c);
    pen.setWidthF(1.8);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    switch (k) {
    case IconKind::Download:                       // 下载：↓ + 底槽
        p.drawLine(QPointF(14, 7.5), QPointF(14, 16));
        p.drawLine(QPointF(14, 16), QPointF(10.5, 12.5));
        p.drawLine(QPointF(14, 16), QPointF(17.5, 12.5));
        p.drawLine(QPointF(8.5, 20), QPointF(19.5, 20));
        break;
    case IconKind::More:                           // 更多：⋯（三个圆点）
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(9.5, 14), 1.7, 1.7);
        p.drawEllipse(QPointF(14.0, 14), 1.7, 1.7);
        p.drawEllipse(QPointF(18.5, 14), 1.7, 1.7);
        break;
    case IconKind::User:                           // 登录：头 + 肩
        p.drawEllipse(QPointF(14, 10.5), 3.4, 3.4);
        p.drawArc(QRectF(7.5, 15.5, 13, 10), 0, 180 * 16);
        break;
    case IconKind::Fullscreen:                     // 全屏：四角箭头
        p.drawLine(QPointF(8, 13), QPointF(8, 8));  p.drawLine(QPointF(8, 8), QPointF(13, 8));
        p.drawLine(QPointF(20, 15), QPointF(20, 20)); p.drawLine(QPointF(20, 20), QPointF(15, 20));
        break;
    case IconKind::Pip:                            // 画中画：大框 + 右下小框
        p.drawRect(QRectF(7.5, 9, 13, 10));
        p.setBrush(c); p.drawRect(QRectF(14, 14, 6.5, 5));
        p.setBrush(Qt::NoBrush);
        break;
    case IconKind::Subtitles:                      // 字幕：圆角框 + 两行短线
        p.drawRoundedRect(QRectF(7, 10, 14, 9), 2, 2);
        p.drawLine(QPointF(9.5, 13.5), QPointF(15, 13.5));
        p.drawLine(QPointF(9.5, 16), QPointF(18.5, 16));
        break;
    case IconKind::ModeSeq:                        // 顺序：→ + 竖线
        p.drawLine(QPointF(8, 14), QPointF(19, 14));
        p.drawLine(QPointF(15.5, 10.8), QPointF(19, 14));
        p.drawLine(QPointF(15.5, 17.2), QPointF(19, 14));
        p.drawLine(QPointF(8, 10.5), QPointF(8, 17.5));
        break;
    case IconKind::ModeOne:                        // 单曲：↻ + 1
        p.drawArc(QRectF(8.5, 8.5, 11, 11), 40 * 16, 280 * 16);
        p.drawLine(QPointF(12.5, 12.5), QPointF(14, 11.5));
        p.drawLine(QPointF(14, 11.5), QPointF(14, 16.5));
        break;
    case IconKind::ModeShuffle:                    // 随机：交叉箭头
        p.drawLine(QPointF(8, 10.5), QPointF(19, 17.5));
        p.drawLine(QPointF(8, 17.5), QPointF(19, 10.5));
        p.drawLine(QPointF(16.2, 10.0), QPointF(19, 10.5));
        p.drawLine(QPointF(16.2, 18.0), QPointF(19, 17.5));
        break;
    case IconKind::ModeAll:                        // 列表循环：↻ + 双箭头
        p.drawArc(QRectF(8.5, 8.5, 11, 11), 30 * 16, 300 * 16);
        p.drawLine(QPointF(11, 11), QPointF(11, 17));
        break;
    case IconKind::AutoNext:                       // 连播：∞（两个相切圆）
        p.drawEllipse(QPointF(11, 14), 3.2, 3.2);
        p.drawEllipse(QPointF(17, 14), 3.2, 3.2);
        break;
        break;
    }
    p.end();
    return QIcon(pm);
}

static QIcon magnifierIcon(const QColor &c = QColor(0xB4, 0xB4, 0xB4)) {
    QIcon ic = QIcon::fromTheme("edit-find");
    if (!ic.isNull()) return ic;
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(c);
    pen.setWidthF(2.0);
    p.setPen(pen);
    p.drawEllipse(QPointF(12.0, 12.0), 5.5, 5.5);
    p.drawLine(QPointF(16.2, 16.2), QPointF(21.5, 21.5));
    p.end();
    return QIcon(pm);
}

MainWindow::MainWindow() {
        auto *central = new QWidget(this);
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(10, 10, 10, 10);
        root->setSpacing(8);

        auto *topCard = new QFrame(this);
        topCard->setObjectName("card");
        auto *top = new QHBoxLayout(topCard);
        top->setContentsMargins(10, 8, 10, 8);
        top->setSpacing(8);
        // ── 顶栏（UI-1 ✓ 对齐 macOS ✓ 见文档 61）──
        // macOS 原文（Ui.swift:395-397）："只保留 4 个高频按钮：搜索（文字）/ 下载 / 更多▾ / 登录；
        //   其余（下载面板、打开本地文件、收藏、队列、设置）收进"更多"菜单——按钮一多就拥挤"
        // ✗ 已删除：原左侧"聚合视频"标签（macOS 无 ✓）
        srcBox_ = new QComboBox(this);
        srcBox_->addItems({"YouTube", "B站", "网易云音乐", "QQ音乐", "本地库"});
        srcBox_->setToolTip("搜索来源");
        srcBox_->setFixedHeight(28);                        // R1 ✓ 胶囊
        srcBox_->setObjectName("pillBox");
        top->addWidget(srcBox_);
        // UI-4-B ✓ 过滤按钮（与 macOS 同位：来源下拉之后 ✓）
        auto *filterBtn = new QPushButton("过滤", this);
        filterBtn->setObjectName("pillBtn");                     // R1 ✓ 胶囊
        filterBtn->setFixedHeight(28);
        filterBtn->setToolTip("搜索过滤器：排序 相关度/最新/播放最多；时长 全部/短视频/中等/长篇");
        connect(filterBtn, &QPushButton::clicked, this, [this] { openFilterDialog(); });
        top->addWidget(filterBtn);
        search_ = new QLineEdit(this);
        search_->setPlaceholderText("输入关键词后回车（来源见左侧下拉：YouTube / 网易云 / QQ音乐）…");
        search_->setClearButtonEnabled(true);                       // ✓ macOS 的清空 ✕
        search_->setObjectName("searchField");                      // ✓ UI-2 上聚焦蓝边（#4B779F）
        search_->setFixedHeight(28);                          // R1 ✓ 胶囊（两端全圆）
        search_->setObjectName("pillField");
        search_->addAction(magnifierIcon(), QLineEdit::LeadingPosition);   // ✓ 放大镜（有绘制回退 ✓ 不依赖图标主题 ✗）
        auto *btn = new QPushButton("搜索", this);
        btn->setFixedHeight(28);                             // R1 ✓ 胶囊（原圆角 8 ✗）
        btn->setObjectName("primaryBtn");                           // ✓ UI-2 上实心 #3869D3
        // 右侧三件（图标 + 悬浮提示 ✓）：下载 / 更多▾ / 登录
        auto *dlBtn = new QPushButton(this);
        dlBtn->setIcon(drawnIcon(IconKind::Download));
        dlBtn->setObjectName("iconBtn");
        dlBtn->setFixedSize(28, 28);
        dlBtn->setToolTip("下载当前播放的直链到 ~/Downloads/hov/");
        connect(dlBtn, &QPushButton::clicked, this, [this] { downloadCurrent(); });
        auto *moreBtn = new QPushButton(this);
        moreBtn->setIcon(drawnIcon(IconKind::More));
        moreBtn->setObjectName("iconBtn");
        moreBtn->setFixedSize(28, 28);
        moreBtn->setToolTip("更多：打开本地文件 / 本地库 / 设置");
        {
            auto *menu = new QMenu(moreBtn);
            // W2 ✓ 菜单顺序与 macOS「更多」逐项对齐（UiActions.swift:130-136 ✓）：
            //   下载面板 / 打开本地文件 / 收藏 / [播放队列→Linux 用「本地库」承担] / 设置
            menu->addAction("下载面板", this, [this] { openDownloadPanel(); });
            menu->addAction("打开本地文件…", this, [this] {
                const QStringList fs = QFileDialog::getOpenFileNames(this, "打开音视频文件（可多选）", QDir::homePath(),
                    "媒体文件 (*.mp4 *.mkv *.webm *.mov *.mp3 *.flac *.m4a *.aac *.wav *.ogg *.opus);;所有文件 (*)");
                if (!fs.isEmpty()) openFiles(fs);
            });
            menu->addAction("收藏", this, [this] { openFavoritesPanel(); });
            menu->addAction("本地库", this, [this] { showLocalLibrary(); });
            menu->addSeparator();
            menu->addAction("设置…", this, [this] { openSettingsDialog(); });
            moreBtn->setMenu(menu);
        }
        auto *loginBtn = new QPushButton(this);
        loginBtn->setIcon(drawnIcon(IconKind::User));
        loginBtn->setObjectName("iconBtn");
        loginBtn->setFixedSize(28, 28);
        loginBtn->setToolTip("App 内登录（cookie 自动保存到 ~/.config/hov/cookies/）");
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
                menu->addAction(QString::fromUtf8(it.label), this, [this, site] { openLoginDialog(site); });
            }
            menu->addSeparator();
            // A0：不依赖内置 WebView 的登录路径（AppImage/Flatpak 也能用；同时绕开 Google 对嵌入式登录的限制）
            menu->addAction("用系统浏览器登录并导入 cookie", this, [this] {
                const QString browser = settings_.str("network.cookiesFromBrowser", "");
                if (browser.isEmpty()) {
                    results_->addItem("请先在设置里指定浏览器（network.cookiesFromBrowser，例如 safari / chrome / firefox）");
                    results_->addItem("设置命令：--set network.cookiesFromBrowser=chrome");
                    return;
                }
                results_->addItem(QString("正在从 %1 导入 cookie…（首次可能弹出钥匙串授权）").arg(browser));
                importCookiesFromBrowser(browser);
            });
            menu->addAction("打开 cookie 目录", this, [this] {
                const QString dir = QDir::homePath() + "/.config/hov/cookies";
                QDir().mkpath(dir);
                results_->addItem(QString("cookie 目录：%1").arg(dir));
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
            });
            loginBtn->setMenu(menu);
        }
        top->addWidget(search_, 1);
        top->addWidget(btn);
        top->addWidget(dlBtn);
        top->addWidget(moreBtn);
        top->addWidget(loginBtn);
        topCard_ = topCard;                     // B5：画中画时隐藏
        root->addWidget(topCard);

        auto *split = new QSplitter(Qt::Horizontal, this);
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
    results_->setSpacing(4);
    results_->setTextElideMode(Qt::ElideRight);
    results_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    results_->setSelectionMode(QAbstractItemView::SingleSelection);
        player_ = new MpvWidget(split);
        // MPRIS：桌面端“后台播放 + 媒体键”的标准通道（无会话总线时自降级）
        mpris_ = new Mpris(player_, this);
        // 在线播放：网页 URL 由 App 层解析成直链（mpv 的 ytdl 钩子已禁用，见 MpvWidget 注释）
        player_->setFocus();   // 快捷键立即生效（否则焦点在搜索框）
        resolver_ = new UrlResolver(this);
        resolver_->setStatusHandler([this](const QString &m) { results_->addItem(m); });
        resolver_->setPlayHandler([this](const UrlResolver::Stream &s) {
            player_->setCoverArt(QString(), QString(), QString());   // 视频：清掉音乐封面
            player_->playResolved(s.videoUrl, s.audioUrl);
            resolvedAtMs_ = QDateTime::currentMSecsSinceEpoch();
            healTried_ = false;   // 新内容重新允许自愈
            std::fprintf(stderr, "[PLAY] 交给 mpv: host=%s len=%lld\n", QUrl(s.videoUrl).host().toUtf8().constData(), (long long)s.videoUrl.size());
              std::fprintf(stderr, "[PLAY] 已解析: 视频=%s(%lld) 音频=%s(%lld) 标题=%s\n",
                           QUrl(s.videoUrl).host().toUtf8().constData(), (long long)s.videoUrl.size(),
                           QUrl(s.audioUrl).host().toUtf8().constData(), (long long)s.audioUrl.size(),
                           s.title.toUtf8().constData());
            lastAudioUrl_ = s.audioUrl;      // ③ 下载混流用（音乐单路时为空串，无副作用）
            // ③ **Bug3 修复** ✗→✓：此前这里**只记了音频**，没记主直链 ✗
            //    → 视频正在播时点「下载」，downloadCurrent() 报“当前没有可下载的直链” ✗
            //    （用户 2026-09-23 实测截图 ✓）。macOS 端在 AppDelegate.swift:295 早已记录 ✓ → 两端对齐 ✓
            lastStreamUrl_   = s.videoUrl;
            lastStreamTitle_ = s.title.isEmpty() ? playLabel_ : s.title;
            lastLRC_.clear();                // ③ 歌词异步到达，先清上一首避免错配
            if (!s.title.isEmpty())
                results_->addItem(QString("正在播放[%1]：%2")
                                      .arg(playPlatform_.isEmpty() ? "在线" : playPlatform_, s.title));
            if (!s.subtitles.isEmpty()) player_->addSubtitleTracks(s.subtitles);   // 在线字幕轨（追加，不覆盖本地轨）
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
        statusLabel_->setStyleSheet("color:#8b93a3;padding:2px 8px;font-size:12px;");
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
                std::fprintf(stderr, "[FD] 当前打开的文件描述符: %d\n", n);
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
                    std::fprintf(stderr, "[MORE] %s 滚动: v=%d max=%d step=%d visible=%d\n",
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

        // ---- 底部播放控制条（UI-3 ✓ 对齐 macOS：**三行**结构 ✓ 见文档 61）----
        // 行1: [⏮][⏸][⏭]  ——进度条——  时间
        // 行2: [倍速][铺满] [🔊 音量]            （UI-3b 将在此行右侧补 画质/PiP/模式/全屏 ✓）
        // 行3: [来源] 标题 | 说明   时间 | 瞬时状态（原顶栏状态行 ✓ 仅挪位置 ✓ 语义不变）
        auto *ctlBox = new QVBoxLayout();
        ctlBox->setContentsMargins(0, 4, 0, 4);
        ctlBox->setSpacing(4);

        // ── 行1：传输 + 进度 + 时间 ──
        auto *row1 = new QHBoxLayout();
        btnPlay_ = new QPushButton("\u23f8", this);
        btnPlay_->setObjectName("iconBtn");   // R1 ✓ 正圆（与右侧图标一致）
        btnPlay_->setFixedSize(28, 28);
        seek_ = new QSlider(Qt::Horizontal, this);
        seek_->setRange(0, 1000);
        labelTime_ = new QLabel("0:00 / 0:00", this);
        labelTime_->setMinimumWidth(110);
        labelTime_->setAlignment(Qt::AlignCenter);
        auto *btnPrev = new QPushButton("\u23ee", this);
        auto *btnNext = new QPushButton("\u23ed", this);
        btnPrev->setObjectName("iconBtn");   // R1 ✓ 正圆
        btnNext->setObjectName("iconBtn");
        btnPrev->setFixedSize(28, 28);
        btnNext->setFixedSize(28, 28);
        row1->addWidget(btnPrev);
        row1->addWidget(btnPlay_);
        row1->addWidget(btnNext);
        // 播放模式（顺序 → 单曲 → 随机 → 列表 ✓ 与 macOS modeButton 一致 ✓）
        modeBtn_ = new QPushButton(this);
        modeBtn_->setObjectName("iconBtn");
        modeBtn_->setFixedSize(28, 28);
        modeBtn_->setIcon(drawnIcon(modeIconKind(queue_.mode())));
        modeBtn_->setToolTip(modeTip(queue_.mode()));
        connect(modeBtn_, &QPushButton::clicked, this, [this] {
            queue_.cycleMode();
            modeBtn_->setIcon(drawnIcon(modeIconKind(queue_.mode())));
            modeBtn_->setToolTip(modeTip(queue_.mode()));
        });
        row1->addWidget(modeBtn_);
        row1->addWidget(seek_, 1);
        row1->addWidget(labelTime_);
        connect(btnPrev, &QPushButton::clicked, this, [this] { queueOrFilePrev(); });
        connect(btnNext, &QPushButton::clicked, this, [this] { queueOrFileNext(/*autoAdvance=*/false); });

        // ── 行2：画质⌄ / 倍速⌄ / 🔊音量        [全屏] [画中画] [字幕]（对齐 macOS ✓）──
        auto *row2 = new QHBoxLayout();
        // 画质（**共用档位表** ✓ Settings::qualityHeights() ✓ 单一来源 ✓ #122）
        qualityBox_ = new QComboBox(this);
        {
            const QVector<int> hs = Settings::qualityHeights();
            for (int h : hs) qualityBox_->addItem(Settings::qualityLabelFor(h), h);
            const int curH = int(settings_.number("video.maxHeight", 0));
            const int idx = hs.indexOf(curH);
            qualityBox_->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        qualityBox_->setFixedWidth(104);
        qualityBox_->setFixedHeight(28);            // R1 ✓ 胶囊：高 28 + 半径 14
        qualityBox_->setObjectName("pillBox");
        qualityBox_->setToolTip("清晰度（与设置面板共用一份档位表 — V 键也可循环切换）");
        connect(qualityBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
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
        speedBox_->setFixedWidth(80);
        speedBox_->setFixedHeight(28);              // R1 ✓ 胶囊
        speedBox_->setObjectName("pillBox");
        speedBox_->setToolTip("播放倍速");
        connect(speedBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
            const double sp = speedBox_->itemData(i).toDouble();
            player_->setSpeed(sp);
            results_->addItem(QString("倍速: %1x").arg(sp, 0, 'g', 3));
        });
        vol_ = new QSlider(Qt::Horizontal, this);
        vol_->setRange(0, 130);
        vol_->setValue(100);
        vol_->setFixedWidth(120);
        connect(vol_, &QSlider::valueChanged, this, [this](int v) { player_->setVolume(v); });
        row2->addWidget(qualityBox_);
        row2->addWidget(speedBox_);
        row2->addWidget(new QLabel("\U0001f50a", this));
        row2->addWidget(vol_);
        row2->addStretch(1);
        // 右侧三图标：全屏 / 画中画 / 字幕（动作全部既有 ✓ 见文档 61）
        auto *fsBtn = new QPushButton(this);
        fsBtn->setObjectName("iconBtn"); fsBtn->setFixedSize(28, 28);
        fsBtn->setIcon(drawnIcon(IconKind::Fullscreen));
        fsBtn->setToolTip("全屏（双击画面或 ⌃⌘F 亦可）");
        connect(fsBtn, &QPushButton::clicked, this, [this] { toggleVideoFullscreen(); });
        auto *pipBtn = new QPushButton(this);
        pipBtn->setObjectName("iconBtn"); pipBtn->setFixedSize(28, 28);
        pipBtn->setIcon(drawnIcon(IconKind::Pip));
        pipBtn->setToolTip("画中画（P 键）");
        connect(pipBtn, &QPushButton::clicked, this, [this] { togglePiP(); });
        auto *subBtn = new QPushButton(this);
        subBtn->setObjectName("iconBtn"); subBtn->setFixedSize(28, 28);
        subBtn->setIcon(drawnIcon(IconKind::Subtitles));
        subBtn->setToolTip("字幕/歌词：关 → 轨1 → 轨2 → …（C 键）");
        connect(subBtn, &QPushButton::clicked, this, [this] { if (player_) player_->cycleSubtitleTrack(); });
        // ③ UI-4-B ✓ macOS 底栏**没有**铺满按钮（只有 Z 快捷键 ✓ Ui.swift:268）→ 已移除按钮 ✓
        //   功能保留：Z 键 ✓（fillBtn_ 保持 nullptr ✓ 既有空值保护覆盖 ✓）
        connect(fillBtn_, &QPushButton::clicked, this, [this] { toggleFillScreen(); });
        row2->addWidget(fsBtn);
        row2->addWidget(pipBtn);
        row2->addWidget(subBtn);
        row2->addWidget(fillBtn_);
        // 连播（∞）：播完自动播下一条（UI-4 ✓ 与 macOS autoNextBtn / Android 同键 ✓）
        autoNextBtn_ = new QPushButton(this);
        autoNextBtn_->setObjectName("iconBtn");
        autoNextBtn_->setCheckable(true);
        autoNextBtn_->setFixedSize(28, 28);
        autoNextBtn_->setIcon(drawnIcon(IconKind::AutoNext));
        autoNextBtn_->setChecked(settings_.boolean("playback.autoNext", true));
        autoNextBtn_->setToolTip("连播：播完自动播下一条（按列表顺序）");
        connect(autoNextBtn_, &QPushButton::clicked, this, [this] {
            const bool on = autoNextBtn_->isChecked();
            applySetting("playback.autoNext", on ? "true" : "false");
            results_->addItem(on ? "连播：开（播完自动下一条）" : "连播：关（播完停止）");
        });
        row2->addWidget(autoNextBtn_);

        // ── 行3：信息行（正在播放 ✓）+ 瞬时状态（原顶栏状态行 ✓ 仅挪位置）──
        auto *row3 = new QHBoxLayout();
        labelInfo_ = new QLabel(this);
        labelInfo_->setObjectName("hovInfo");
        labelInfo_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        labelInfo_->setStyleSheet("color:#8b93a3;padding:2px 8px;font-size:12px;");
        labelInfo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // 同上 ✓ 不撑窗口 ✓
        labelInfo_->setMinimumWidth(0);
        statusLabel_->setParent(this);
        row3->addWidget(labelInfo_, 1);
        row3->addWidget(statusLabel_, 0);
        row3->addStretch(0);

        ctlBox->addLayout(row1);
        ctlBox->addLayout(row2);
        ctlBox->addLayout(row3);
        root->addLayout(ctlBox);

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
        settings_.load();                                    // 先读设置，后面的组件都按它初始化
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
            std::fprintf(stderr, "[W1] 几何: 键 %d 字符 → %s；当前 %dx%d @(%d,%d)\n",
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
            split_->setSizes({wantW, total - wantW});
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
                if (!split_ || split_->sizes().isEmpty()) return;
                const int w = split_->sizes().at(0);
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
            std::fprintf(stderr, "[GLASS] 窗口级玻璃=%s 平台=%s 属性生效=%d（Wayland/KDE 可透出桌面；Xvfb 无 ARGB 视觉时不生效属正常）\n",
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
