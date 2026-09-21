#include <QApplication>
#include <QDesktopServices>
#include <QMenu>
#include <QUrl>
#include <iostream>
#include <QMainWindow>
#include <QLineEdit>
#include <QListWidget>
#include <QFontMetrics>
#include <QPainterPath>
#include <QPainter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QPushButton>
#include <QFileDialog>
#include <QSplitter>
#include <QVBoxLayout>
#include <QLabel>
#include <QProcess>
#include <memory>
#include <QFileInfo>
#include <QSlider>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QEvent>
#include <QTimer>
#include <QThread>   // 自检轮询等待快照刷新（QThread::msleep）
#include <QDir>
#include <QHash>
#include <QtMath>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "MasonryView.h"
#include <cmath>
#include <limits>
#include <QScrollBar>
#include "MpvWidget.h"
#include "ColorTheme.h"
#include "ThemeWatcher.h"
#ifdef HOV_HAVE_KWINDOWSYSTEM
#include <KWindowEffects>
#endif
#include "NetPolicy.h"
#include "Playlist.h"
#include "NetEaseApi.h"
#include "DownloadManager.h"
#include "Favorites.h"
#include "PlayQueue.h"
#include "QQMusicApi.h"
#include "LoginDialog.h"
#include "Settings.h"
#include "Theme.h"
#include "ProgressStore.h"
#include "Mpris.h"
#include "LocalLibrary.h"
#include "CookieImport.h"
#include <QInputDialog>
#include <QDateTime>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include "UrlResolver.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

// 《聚合视频》Linux 桌面版 MVP
// 搜索走 yt-dlp（与 Android 版同款引擎）；后续 P2 后半段改为调用 KMP 共享核心
// ── 结果卡片绘制（对齐 macOS 的 ResultGrid）───────────────────────────────
// 为什么**必须自绘**：QListWidget 的 IconMode + 全局深色样式表组合下，默认 delegate 会
// 把 item 文字压没、缩略图还被裁成半截（用户对比截图实测：只有半截图、没有标题/来源）。
// 自绘后完全可控：圆角缩略图 + 标题 + 来源标签，悬停/选中状态也自己做。
class ResultCardDelegate : public QStyledItemDelegate {
public:
    // 字体与度量在**构造时建一次**：避免每帧新建 QFont/QFontMetrics（既省 CPU，
    // 也避免字体子系统被反复触碰 —— 与句柄泄漏排查方向一致）
    explicit ResultCardDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {
        fTitle_ = QApplication::font(); fTitle_.setPixelSize(12); fTitle_.setWeight(QFont::DemiBold);
        fMeta_  = QApplication::font(); fMeta_.setPixelSize(11);
        fmTitle_ = new QFontMetrics(fTitle_);
        fmMeta_  = new QFontMetrics(fMeta_);
    }
    ~ResultCardDelegate() override { delete fmTitle_; delete fmMeta_; }

    static constexpr int kCardW = 186;      // 单列最小宽（决定两列阈值）
    static constexpr int kCardH = 138;      // 兼容常量（不再用于固定高度）
    static constexpr int kRowH  = 24;       // 状态行（"共 N 条结果"等）
    static constexpr int kPad   = 5;
    static constexpr int kLineH = 16;       // 标题行高
    static constexpr int kMetaH = 15;       // 元信息行高
    static constexpr int kMaxTitleLines = 2;

    /** 卡片判定：显式标记（见 setItemThumb）或已拿到缩略图 */
    static bool isCard(const QModelIndex &idx) {
        return idx.data(Qt::UserRole + 12).toBool()
            || !idx.data(Qt::DecorationRole).value<QIcon>().isNull();
    }
    QFont fTitle_, fMeta_;
    QFontMetrics *fmTitle_ = nullptr;
    QFontMetrics *fmMeta_ = nullptr;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &idx) const override {
        if (!isCard(idx)) return QSize(qMax(kCardW, viewWidth() - 24), kRowH);
        const int w = cardWidth();
        // 高度按内容算 → 标题长的卡片更高 → 列内错落 = 瀑布流
        return QSize(w, kPad + thumbHeight(w) + 4
                        + titleLines(titleOf(idx), w) * kLineH + 1 + kMetaH + kPad);
    }
    // 列数由"**最小可读列宽**"决定，而不是由固定阈值决定。
    // 之前用 kCardW(186)*2+18=390 当阈值 → 面板 330px 时只有 1 列 = 单列满宽列表（用户实测"没有瀑布流"）。
    // 现在：列宽不低于 140px 就尽量多排列 → 窄面板也能出 2~3 列瀑布。
    static constexpr int kMinColW = 140;
    int colsFor(int vw) const { return qBound(1, vw / (kMinColW + 8), 4); }
    int cardWidth() const {
        const int vw = viewWidth() - 24;
        const int cols = colsFor(vw);
        return (vw - (cols - 1) * 8) / cols;
    }
    int thumbHeight(int w) const { const int iw = w - kPad * 2; return qRound(iw * 9.0 / 16.0); }
    static QString titleOf(const QModelIndex &idx) {
        const QString t = idx.data(Qt::DisplayRole).toString();
        const int sep = t.indexOf(" · ");
        return sep > 0 ? t.left(sep) : t;
    }
    /** 标题占几行（1~2 行，按词换行测量） */
    int titleLines(const QString &t, int w) const {
        const QRect r = fmTitle_->boundingRect(QRect(0, 0, qMax(20, w - kPad * 2), 4000),
                                              Qt::TextWordWrap, t);
        return qBound(1, (r.height() + kLineH - 1) / kLineH, kMaxTitleLines);
    }
    /** 视图可用宽度（delegate 的 parent 就是结果列表） */
    int viewWidth() const {
        if (auto *v = qobject_cast<const QListView *>(parent())) return qMax(200, v->viewport()->width());
        return 392;
    }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setRenderHint(QPainter::SmoothPixmapTransform, true);
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered  = opt.state & QStyle::State_MouseOver;
        const QString text = idx.data(Qt::DisplayRole).toString();

        if (!isCard(idx)) {                              // ── 状态行：单行暗色
            QFont f = opt.font; f.setPixelSize(12);
            p->setFont(f);
            p->setPen(QColor(Theme::textDim()));
            p->drawText(opt.rect.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter,
                        QFontMetrics(f).elidedText(text, Qt::ElideRight, opt.rect.width() - 16));
            p->restore();
            return;
        }

        // ── 卡片底（悬停/选中）
        const QRect r = opt.rect.adjusted(2, 2, -2, -2);
        QColor bg(255, 255, 255, 12);
        const bool dkCard = Theme::isDark();
        if (selected)      bg = QColor(88, 136, 216, 135);
        else if (hovered)  bg = dkCard ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 22);
        p->setPen(Qt::NoPen);
        p->setBrush(bg);
        p->drawRoundedRect(r, 9, 9);

        // ── 缩略图（16:9，圆角裁剪，cover 填充不裁半截）
        const int pad = 5;
        const int iw = r.width() - pad * 2;
        const QRect ir(r.left() + pad, r.top() + pad, iw, qRound(iw * 9.0 / 16.0));
        QPainterPath clip;
        clip.addRoundedRect(ir, 6, 6);
        p->setClipPath(clip);
        const QIcon ic = idx.data(Qt::DecorationRole).value<QIcon>();
        if (ic.isNull()) {
            p->fillRect(ir, dkCard ? QColor(255, 255, 255, 16) : QColor(0, 0, 0, 14));   // 占位底色（跟随主题）
        } else {
            // 完整显示（fit），不裁切 —— 与瀑布流视图保持一致（用户要求缩略图必须完整）
            const QSize nat = ic.availableSizes().value(0);
            const QPixmap src = nat.isValid() ? ic.pixmap(nat) : ic.pixmap(ir.size());
            const QPixmap sc = src.scaled(ir.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p->drawPixmap(QRect(ir.center().x() - sc.width() / 2, ir.center().y() - sc.height() / 2,
                                sc.width(), sc.height()), sc);
        }
        p->setClipping(false);

        // ── 标题（最多两行，自动换行 → 卡片高度不同 → 瀑布流错落感）
        QString title = text, sub;
        const int sep = text.indexOf(" · ");
        if (sep > 0) { title = text.left(sep); sub = text.mid(sep + 3); }
        const QString src = idx.data(Qt::UserRole + 10).toString();
        const QString bottom = src.isEmpty() ? sub
                             : (sub.isEmpty() ? src : src + " · " + sub);

        p->setFont(fTitle_);
        p->setPen(selected ? QColor(255, 255, 255) : QColor(Theme::text()));
        const QRect tr(r.left() + pad, ir.bottom() + 4, iw, kLineH * kMaxTitleLines);
        p->drawText(tr, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, title);

        // ── 元信息一行：来源 · 作者/时长（暗色小字，单行省略）
        p->setFont(fMeta_);
        p->setPen(QColor(Theme::textDim()));
        const QRect br(r.left() + pad, tr.top() + kLineH * titleLines(title, r.width()) + 1, iw, kMetaH);
        p->drawText(br, Qt::AlignLeft | Qt::AlignVCenter,
                    fmMeta_->elidedText(bottom, Qt::ElideRight, br.width()));
        p->restore();
    }
};

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        auto *central = new QWidget(this);
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(10, 10, 10, 10);
        root->setSpacing(8);

        auto *topCard = new QFrame(this);
        topCard->setObjectName("card");
        auto *top = new QHBoxLayout(topCard);
        top->setContentsMargins(10, 8, 10, 8);
        top->setSpacing(8);
        top->addWidget(new QLabel("聚合视频", this));
        srcBox_ = new QComboBox(this);
        srcBox_->addItems({"YouTube", "B站", "网易云音乐", "QQ音乐", "本地库"});
        srcBox_->setToolTip("搜索来源");
        top->addWidget(srcBox_);
        search_ = new QLineEdit(this);
        search_->setPlaceholderText("输入关键词后回车（来源见左侧下拉：YouTube / 网易云 / QQ音乐）…");
        auto *btn = new QPushButton("搜索", this);
        auto *openBtn = new QPushButton("打开本地文件", this);
        // 登录：下拉菜单四站点（YouTube / B站 / 网易云 / QQ音乐）
        auto *loginBtn = new QPushButton("登录 ▾", this);
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
        auto *setBtn = new QPushButton("设置", this);
        connect(setBtn, &QPushButton::clicked, this, [this] { openSettingsDialog(); });
        auto *dlBtn = new QPushButton("下载", this);
        dlBtn->setToolTip("下载当前播放的直链到 ~/Downloads/hov/");
        connect(dlBtn, &QPushButton::clicked, this, [this] { downloadCurrent(); });
        connect(openBtn, &QPushButton::clicked, this, [this] {
            const QStringList fs = QFileDialog::getOpenFileNames(this, "打开音视频文件（可多选）", QDir::homePath(),
                "媒体文件 (*.mp4 *.mkv *.webm *.mov *.mp3 *.flac *.m4a *.aac *.wav *.ogg *.opus);;所有文件 (*)");
            if (!fs.isEmpty()) openFiles(fs);
        });
        top->addWidget(search_, 1);
        top->addWidget(btn);
        top->addWidget(openBtn);
        top->addWidget(dlBtn);
        top->addWidget(setBtn);
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
        root->insertWidget(root->indexOf(topCard) + 1, statusLabel_);
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

        // ---- 底部播放控制条（播放/暂停 · 进度 · 时间 · 音量）----
        auto *ctl = new QHBoxLayout();
        btnPlay_ = new QPushButton("\u23f8", this);
        btnPlay_->setFixedWidth(46);
        seek_ = new QSlider(Qt::Horizontal, this);
        seek_->setRange(0, 1000);
        labelTime_ = new QLabel("0:00 / 0:00", this);
        labelTime_->setMinimumWidth(110);
        labelTime_->setAlignment(Qt::AlignCenter);
        vol_ = new QSlider(Qt::Horizontal, this);
        vol_->setRange(0, 130);
        vol_->setValue(100);
        vol_->setFixedWidth(120);
        auto *btnPrev = new QPushButton("\u23ee", this);
        auto *btnNext = new QPushButton("\u23ed", this);
        btnPrev->setFixedWidth(38);
        btnNext->setFixedWidth(38);
        ctl->addWidget(btnPrev);
        connect(btnPrev, &QPushButton::clicked, this, [this] { queueOrFilePrev(); });

        ctl->addWidget(btnPlay_);
        ctl->addWidget(btnNext);
        // 倍速按钮：点击循环 0.5x → 0.75x → 1.0x → 1.25x → 1.5x → 2.0x
        speedBtn_ = new QPushButton("1.0x", this);
        speedBtn_->setFixedWidth(56);
        ctl->addWidget(speedBtn_);
        // B7：全屏铺满开关（与 Android 的「全屏时铺满」同键同语义）
        fillBtn_ = new QPushButton("铺满", this);
        fillBtn_->setCheckable(true);
        fillBtn_->setFixedWidth(58);
        fillBtn_->setChecked(settings_.boolean("video.fillScreen", true));
        fillBtn_->setToolTip("视频全屏 + 铺满（裁切左右黑边）；非全屏时点击会先进入视频全屏 — 快捷键 Z");
        ctl->addWidget(fillBtn_);
        connect(fillBtn_, &QPushButton::clicked, this, [this] { toggleFillScreen(); });
        connect(speedBtn_, &QPushButton::clicked, this, [this] {
            static const double kSpeeds[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
            const double cur = player_->speed();
            int idx = 0;
            for (int i = 0; i < 6; ++i) if (qFuzzyCompare(kSpeeds[i], cur)) idx = i;
            const double next = kSpeeds[(idx + 1) % 6];
            player_->setSpeed(next);
            speedBtn_->setText(QString("%1x").arg(next, 0, 'g', 3));
            results_->addItem(QString("倍速: %1x").arg(next, 0, 'g', 3));
        });

        connect(btnNext, &QPushButton::clicked, this, [this] { queueOrFileNext(/*autoAdvance=*/false); });   // ⏭ 手动切歌

        ctl->addWidget(seek_, 1);
        ctl->addWidget(labelTime_);
        ctl->addWidget(new QLabel("\U0001f50a", this));
        ctl->addWidget(vol_);
        root->addLayout(ctl);

        connect(btnPlay_, &QPushButton::clicked, this, [this] {
            if (player_->paused()) healIfExpired();   // 长时间暂停后按播放：直链可能已过期
            player_->togglePause(); refreshControls();
        });
        connect(seek_, &QSlider::sliderReleased, this, [this] {
            const double d = player_->durationSec();
            if (d > 0) player_->seekTo(d * seek_->value() / 1000.0);
        });
        connect(vol_, &QSlider::valueChanged, this, [this](int v) { player_->setVolume(v); });
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
                if (!eofLatch_) { eofLatch_ = true; queueOrFileNext(/*autoAdvance=*/true); }   // 播完自动续播（一次）
            } else {
                eofLatch_ = false;                              // 新媒体起播后解除，下次播完再触发
            }
        });
        eofTimer->start();



        setCentralWidget(central);
        resize(1200, 720);
        setWindowTitle("聚合视频 · Hyper Online Video");

        connect(btn, &QPushButton::clicked, this, [this] { doSearch(); });
        connect(search_, &QLineEdit::returnPressed, this, [this] { doSearch(); });
        connect(results_, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
            playItem(it->data(Qt::UserRole).toString(), it->text());
        });
        settings_.load();                                    // 先读设置，后面的组件都按它初始化
        // ── 结果区尺寸（v1.2.0）：启动固定"横 2 张 × 纵 4 张"，并记住用户后续调整 ──
        // 之前只设了 setStretchFactor(1,3)，且从不读写 ui.listWidth → 每次启动都是偶然的窄宽度（用户实测）。
        if (split_) {
            const int defW = MasonryView::defaultPanelWidth();
            const int saved = int(settings_.number("ui.listWidth", 0));
            const int wantW = saved > 0 ? saved : defW;
            const int total = qMax(wantW + 360, split_->width() > 0 ? split_->width() : width());
            split_->setSizes({wantW, total - wantW});
            if (saved <= 0) {                       // 首次运行：顺带把窗口高度调到"纵向 4 张"
                const int chrome = qMax(0, height() - split_->height());
                const int wantH = MasonryView::defaultPanelHeight() + chrome;
                if (wantH > height()) resize(qMax(width(), wantW + 720), wantH);
            }
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
        // gpu-next 守卫回调：本机无硬件 GL 时 gpu-next 会全黑（实测 ✗）→ 提示并按本次会话切回 ✓
        if (player_) player_->onGpuNextUnusable = [this] {
            setStatusLine(QString::fromUtf8("gpu-next 在本机不可用（画面全黑），已切回 libmpv —— 下次播放生效；如需永久关闭请在设置里取消勾选"));
        };
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

    bool autoplay_ = false;
    bool eofLatch_ = false;   // 播完续播闩锁：eof 期间只触发一次（见 eofTimer 注释）
    QLineEdit *search_;
    QComboBox *srcBox_ = nullptr;   // 搜索来源：YouTube / 网易云 / QQ音乐
    QListWidget *results_;
    MpvWidget *player_;

    void demoSearch(const QString &q) { search_->setText(q); doSearch(); }
    void setAutoplay(bool v) { autoplay_ = v; }
    /** 打开本地音视频文件（对应 Android 版本地库；不依赖网络，可离线验证） */
    /** 打开多个文件（播放列表；--open 支持多参数） */
    /** 设置倍速（供 --speed 参数与按钮共用） */
    void applySpeed(double sp) {
        player_->setSpeed(sp);
        if (speedBtn_) speedBtn_->setText(QString("%1x").arg(sp, 0, 'g', 3));
        results_->addItem(QString("倍速: %1x").arg(sp, 0, 'g', 3));
    }
    /** QQ音乐：搜索并播放第一条（自动化入口；取流需登录态，匿名会明确提示无权限） */
    void qqMusicSearch(const QString &keyword) {
        noteSearchContext(keyword, 3);
        results_->clear();
        results_->addItem(QString("QQ音乐搜索：%1").arg(keyword));
        auto *api = new QQMusicApi(this);
        tuneApi(api);
        api->setStatusHandler([this](const QString &m) { results_->addItem(m); });
        api->search(keyword, 15, [this, api](const QVector<QQMusicApi::Song> &songs) {
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
                    return;
                }
                const QQMusicApi::Song s = songs.at(idx);
                api->streamUrl(s.mid, effectiveQuality(), [this, api, s, idx, attempt](const QString &url, const QString &err) {
                    Q_UNUSED(err)   // 失败原因由 url 为空表示，这里不额外区分（消除 -Wextra 未使用参数警告）
                    if (!url.isEmpty()) {
                        const QString lb = QString("%1 — %2").arg(s.name, s.artist);
                        beginPlayback("qq:" + s.mid, lb);      // 自动播放也要立起 playKey_（切档/进度记忆依赖它）
                        lastArtist_ = s.artist; lastAlbum_ = s.album;
                        lastCoverUrl_ = qqCoverUrl(s.albumMid);
                        results_->addItem(QString("正在播放[QQ音乐]：%1").arg(lb));
                        applyCoverFor("qq:" + s.mid, lb);      // 封面来自搜索记录的 albummid
                        lastStreamUrl_ = url; lastStreamTitle_ = QString("%1 — %2").arg(s.name, s.artist);
                        player_->playResolved(url, QString());
                        loadQQLyricsFor(s.mid, QString("%1 — %2").arg(s.name, s.artist), api);
                        return;
                    }
                    api->streamUrl(s.mid, "standard", [this, api, s, idx, attempt](const QString &u2, const QString &e2) {
                        if (!u2.isEmpty()) {
                            results_->addItem(QString("正在播放（128k）：%1 — %2").arg(s.name, s.artist));
                            player_->playResolved(u2, QString());
                            api->deleteLater();
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

    /** 取网易云歌词并合并翻译后交给字幕层（匿名接口即可，实测部分曲目 40+ 行） */
    void loadLyricsFor(const QString &id, const QString &label, NetEaseApi *api) {
        api->lyricFull(id, [this, api, label](const QString &lrc, const QString &trans, const QString &yrc) {
            lastLRC_ = lrc;                  // ③ 供下载写 .lrc 侧车
            // 有逐字歌词就用它（真·逐字高亮）；否则退回行级 + 行内插值近似
            QVector<SubtitleCue> cues = yrc.isEmpty() ? Subtitles::parse(lrc, "lrc")
                                                      : Subtitles::parseYrc(yrc);
            if (!yrc.isEmpty()) results_->addItem("歌词带逐字时间（逐字高亮）");
            const QVector<SubtitleCue> tc = Subtitles::parse(trans, "lrc");
            cues = Subtitles::mergeBilingual(cues, tc);
            if (cues.isEmpty()) {
                results_->addItem("该曲目没有歌词");
            } else {
                player_->setSubtitleCues(cues, QString("歌词：%1").arg(label),
                                              settings_.boolean("subtitle.karaoke", true));   // true=歌词面板模式
                results_->addItem(QString("已加载歌词 %1 行%2")
                                      .arg(cues.size())
                                      .arg(tc.isEmpty() ? "" : "（含翻译）"));
            }
            api->deleteLater();
        });
    }

    /** 队列 / 收藏逻辑自检（--queue-selftest）：纯逻辑，不触网、不动真实收藏文件 */
    void runQueueSelfTest() {
        int pass = 0, total = 0;
        auto check = [&](bool ok, const QString &name) {
            total++; if (ok) pass++;
            results_->addItem(QString("%1  %2").arg(ok ? "PASS" : "FAIL", name));
            if (!ok) std::cerr << "[FAIL] " << name.toStdString() << std::endl;   // 无人值守时也能定位
        };
        PlayQueue q;
        q.setList({ {"a", "A"}, {"b", "B"}, {"c", "C"} }, 0);
        check(q.size() == 3 && q.index() == 0, "入队 3 项且从第 0 项开始");
        check(q.current() && q.current()->key == "a", "当前项为 a");
        q.next();  check(q.index() == 1, "顺序模式下一首 → 索引 1");
        q.next(); q.next(); check(q.index() == 0, "末尾环绕回第 0 项");
        q.prev();  check(q.index() == 2, "上一首从第 0 项环绕到末项");
        q.cycleMode(); check(q.mode() == PlayQueue::Mode::RepeatOne, "模式切换 → 单曲");
        const int before = q.index();
        check(q.next() == true && q.index() != before, "单曲模式：手动切歌仍前进（与 macOS/Android 一致）");
            const int rep = q.index();
            check(q.autoNext() == true && q.index() == rep, "单曲模式：自动续播不移动（调用方重播本曲）");
            q.cycleMode(); q.cycleMode();                       // → 列表循环
            check(q.mode() == PlayQueue::Mode::RepeatAll, "模式：四档循环到列表循环");
            q.jumpTo(q.size() - 1);
            check(q.autoNext() == true && q.index() == 0, "列表循环：到底自动回到第一项");
            check(q.next() == true, "手动切歌：列表循环下可前进");
            // 持久化往返：四种模式都必须原样保存/加载（本轮真实 bug：RepeatAll 存了但加载丢）
            {
                const QString tp = QDir::tempPath() + "/hov-mode-roundtrip.json";
                for (auto m2 : { PlayQueue::Mode::Sequential, PlayQueue::Mode::RepeatOne,
                                 PlayQueue::Mode::Shuffle, PlayQueue::Mode::RepeatAll }) {
                    PlayQueue a; a.setList({ {"x","X"}, {"y","Y"} }, 0);
                    while (a.mode() != m2) a.cycleMode();
                    a.saveToPath(tp);
                    PlayQueue b; b.loadFromPath(tp);
                    check(b.mode() == m2, QString("持久化往返：模式 %1 保持").arg(static_cast<int>(m2)));
                }
                QFile::remove(tp);
            }
        // 接在上面的"列表循环"块之后（此时模式 = RepeatAll），把四档整圈走完
        q.cycleMode(); check(q.mode() == PlayQueue::Mode::Sequential, "模式切换：列表循环 → 顺序");
        q.cycleMode(); check(q.mode() == PlayQueue::Mode::RepeatOne, "模式切换：顺序 → 单曲");
        q.cycleMode(); check(q.mode() == PlayQueue::Mode::Shuffle, "模式切换：单曲 → 随机");
        const int beforeRand = q.index();
        q.next(); check(q.index() != beforeRand, "随机模式移动到别的项");
        q.cycleMode(); check(q.mode() == PlayQueue::Mode::RepeatAll, "模式切换：随机 → 列表循环（整圈闭合）");
        q.jumpTo(99); check(q.index() >= 0 && q.index() < q.size(), "越界 jumpTo 被忽略且索引合法");
        // 逐字高亮的纯函数：行内进度
        // 本地库：排序 / 过滤 / 重命名守卫（临时目录，测完自清）
        {
            const QString td = QDir::tempPath() + "/hov-lib-selftest";
            QDir(td).removeRecursively();
            QDir().mkpath(td);
            auto writeFile = [](const QString &p, int kb) {
                QFile f(p);
                if (f.open(QIODevice::WriteOnly)) { f.write(QByteArray(kb * 1024, 'x')); f.close(); }
            };
            auto setMtime = [](const QString &p, QDate d) {
                QFile f(p);
                if (f.open(QIODevice::ReadOnly)) {
                    f.setFileTime(QDateTime(d, QTime(0, 0)), QFileDevice::FileModificationTime);
                    f.close();
                }
            };
            writeFile(td + "/a.flac", 3);                       // 最大
            writeFile(td + "/b.mp3", 2);
            writeFile(td + "/x.mp3", 1);                        // 重命名测试用
            writeFile(td + "/y.mp3", 1);                        // 重命名测试用（冲突目标）
            writeFile(td + "/c.txt", 1);                        // 非媒体：应被忽略
            setMtime(td + "/x.mp3", QDate(2025, 1, 1));
            setMtime(td + "/y.mp3", QDate(2025, 1, 2));
            setMtime(td + "/a.flac", QDate(2026, 1, 1));
            setMtime(td + "/b.mp3", QDate(2026, 6, 1));         // 最新

            LocalLibrary lib(td);
            lib.setSort(LocalLibrary::Sort::Name);
            const auto byName = lib.scan();
            check(byName.size() == 4, "本地库：只列媒体文件（忽略 .txt）");
            check(byName.size() == 4 && byName[0].name == "a.flac", "本地库：按名称排序");
            lib.setSort(LocalLibrary::Sort::Date);
            const auto byDate = lib.scan();
            check(byDate.size() == 4 && byDate[0].name == "b.mp3", "本地库：按时间排序（新→旧）");
            lib.setSort(LocalLibrary::Sort::Size);
            const auto bySize = lib.scan();
            check(bySize.size() == 4 && bySize[0].name == "a.flac" && bySize[3].size == 1024,
                  "本地库：按大小排序（大→小）");
            check(lib.scan("b").size() == 1, "本地库：文件名过滤生效");
            check(lib.rename(td + "/x.mp3", "bad/name").contains("路径"), "本地库：重命名拒绝路径分隔符");
            check(!lib.rename(td + "/x.mp3", "y").isEmpty(), "本地库：重命名拒绝已存在的目标");
            check(lib.rename(td + "/y.mp3", "新的名字").isEmpty() && QFile::exists(td + "/新的名字.mp3"),
                  "本地库：重命名成功且保留扩展名");
            check(!lib.rename(td + "/y.mp3", "x").isEmpty(), "本地库：原文件不存在时明确报错");
            QDir(td).removeRecursively();
            check(!QDir(td).exists(), "本地库：测试目录已清理");
        }

        // 在线字幕：语言优先级（修复"英文轨排在中文前"）+ B站 CC JSON 格式
        {
            {
                // 断言必须**环境自适应** ✗：系统语言是中文时中文优先 ✓；POSIX/英文系统时英文优先 ✓
                //（VM 的 LANG 为空 → QLocale 判为英文 ✓ → 写死"中文优先"的断言会假红 ✗ 实测踩到）
                const QStringList hints = Subtitles::systemLanguageHints();
                const bool sysZh = !hints.isEmpty() && hints.first().startsWith("zh");
                if (sysZh) {
                    check(Subtitles::languageRank("zh-Hans · SRT") < Subtitles::languageRank("en · SRT"),
                          "字幕排序：中文系统 → 简体排在英文之前");
                    check(Subtitles::languageRank("中文（中国）· CC") <= Subtitles::languageRank("zh-Hans · SRT"),
                          "字幕排序：B站中文（中国）不劣于 zh-Hans");
                    check(Subtitles::languageRank("zh-Hans · SRT") < Subtitles::languageRank("zh-Hant · SRT"),
                          "字幕排序：简体排在繁体之前");
                    check(Subtitles::languageRank("zh-Hant · SRT") < Subtitles::languageRank("ja · SRT"),
                          "字幕排序：中文（含繁体）排在其它语种之前");
                } else {
                    check(Subtitles::languageRank("en · SRT") < Subtitles::languageRank("zh-Hans · SRT"),
                          "字幕排序：非中文系统 → 英文排在中文之前");
                }
                check(Subtitles::languageRank("zh-Hans-en") >= 0, "字幕排序：复合语言标签不崩溃");
                check(!hints.isEmpty() && !Subtitles::subLangsForSystem().isEmpty(),
                      "字幕语言：能读到系统语言偏好并生成 --sub-langs");
            }
            check(Subtitles::languageRank("en") < Subtitles::languageRank("ja"),
                  "字幕排序：英文优先于其他语种");
            const QString biliJson =
                "{\"body\":[{\"from\":1.5,\"to\":4.0,\"content\":\"第一句\"},"
                "{\"from\":4.2,\"to\":7.0,\"content\":\"第二句\"}]}";
            const QVector<SubtitleCue> bc = Subtitles::parseJson(biliJson);
            check(bc.size() == 2 && qAbs(bc.value(1).start - 4.2) < 0.001
                      && bc.value(0).text == "第一句",
                  "B站 CC JSON：from/to/content 解析正确");
        }

        // A0 跨端登录：cookie 导入的纯逻辑（域名映射 / 按站点切分 / 隐私过滤 / 格式修正）
        {
            check(CookieImport::siteOfDomain("music.163.com") == "netease", "cookie：网易云域名映射");
            check(CookieImport::siteOfDomain(".bilibili.com") == "bilibili", "cookie：带点域名也认得 B站");
            check(CookieImport::siteOfDomain("www.youtube.com") == "youtube", "cookie：YouTube 域名映射");
            check(CookieImport::siteOfDomain("y.qq.com") == "qqmusic", "cookie：QQ音乐域名映射");
            check(CookieImport::siteOfDomain("example.com").isEmpty(), "cookie：无关域名不映射");
            const QString combined =
                "# Netscape HTTP Cookie File\n"
                ".bilibili.com\tTRUE\t/\tFALSE\t0\tSESSDATA\tabc\n"
                "music.163.com\tFALSE\t/\tFALSE\t0\tMUSIC_U\tdef\n"
                ".y.qq.com\tTRUE\t/\tFALSE\t0\tqqmusic_key\tghi\n"
                ".youtube.com\tTRUE\t/\tTRUE\t0\tSID\tjkl\n"
                ".example.com\tTRUE\t/\tFALSE\t0\tTRACK\tzzz\n";
            const QHash<QString, QString> by = CookieImport::splitBySite(combined);
            const QHash<QString, int> cnt = CookieImport::countsOf(by);
            check(by.size() == 4, "cookie：按 4 个站点切分（无关域名被丢弃）");
            check(cnt.value("bilibili") == 1 && cnt.value("netease") == 1 && cnt.value("qqmusic") == 1
                      && cnt.value("youtube") == 1, "cookie：各站点条数正确");
            check(!by.value("bilibili").contains("example.com"), "cookie：无关域名不进文件（隐私）");
            check(by.value("netease").contains("music.163.com\tFALSE\t"), "cookie：includeSubdomains 与域名一致");
            check(by.value("bilibili").contains(".bilibili.com\tTRUE\t"), "cookie：带点域名写 TRUE");
            check(by.value("bilibili").startsWith("# Netscape HTTP Cookie File"), "cookie：带标准文件头");
            check(Settings::validate("network.cookiesFromBrowser", "chrome").isEmpty(), "设置：浏览器名合法");
            check(Settings::validate("network.cookiesFromBrowser", "notabrowser").isEmpty() == false,
                  "设置：非法浏览器名被拒");
            check(Settings::validate("network.cookiesFromBrowser", "").isEmpty(), "设置：留空=关闭合法");
            // 参数组装：浏览器模式要把 --cookies-from-browser 与站点文件都带上（文件不存在也要带，yt-dlp 才会导出）
            {
                UrlResolver r0;
                const QStringList off = r0.cookieArgsFor("https://music.163.com/song?id=1");
                check(!off.contains("--cookies-from-browser"), "cookie 参数：关闭时不带浏览器参数");
                UrlResolver r1;
                r1.setCookiesFromBrowser("chrome");
                const QStringList on = r1.cookieArgsFor("https://music.163.com/song?id=1");
                check(on.contains("--cookies-from-browser") && on.contains("chrome"), "cookie 参数：开启时带浏览器名");
                check(on.contains("--cookies") && on.value(on.indexOf("--cookies") + 1).endsWith("netease.txt"),
                      "cookie 参数：带上站点文件（用于导出）");
                check(r1.cookieArgsFor("https://example.com/x").size() == 2,
                      "cookie 参数：未知站点只带浏览器参数（不写文件）");
            }
        }

        // B1 清晰度（纯函数 + 设置校验）
        {
            check(UrlResolver::formatArgsFor(0) == QStringList{"-f", "bv*+ba/b"}, "清晰度：自动档保持原行为");
            check(UrlResolver::formatArgsFor(-1) == QStringList{"-f", "ba/b"}, "清晰度：仅音频档");
            const QStringList a720 = UrlResolver::formatArgsFor(720);
            check(a720.size() == 2 && a720.at(1).contains("height<=720"), "清晰度：720 档带上限");
            check(a720.at(1).contains("+ba/b[height<="), "清晰度：音频部分不设 height 过滤（否则整条失配）");
            check(a720.at(1).endsWith("/bv*+ba/b"), "清晰度：拿不到该高度时有兜底");
            check(UrlResolver::qualityLabel(0) == "自动" && UrlResolver::qualityLabel(-1) == "仅音频"
                      && UrlResolver::qualityLabel(1080) == "1080p", "清晰度：档位标签");
            check(Settings::validate("video.maxHeight", "0").isEmpty()
                      && Settings::validate("video.maxHeight", "-1").isEmpty()
                      && Settings::validate("video.maxHeight", "1080").isEmpty(), "设置：清晰度合法值通过");
            check(!Settings::validate("video.maxHeight", "99").isEmpty(), "设置：清晰度 99 被拒");
        }

        // 进度记忆（仅内存，不碰真文件：不调用 load/save）
        {
            ProgressStore ps;
            ps.remember("k", 3.0, 100.0, "t");
            check(!ps.has("k"), "进度：不足 5 秒不记录");
            ps.remember("k", 30.0, 100.0, "t");
            check(qAbs(ps.resumePos("k") - 30.0) < 0.001, "进度：30/100 秒可续播");
            ps.remember("k", 10.0, 100.0, "t");
            check(ps.has("k") && ps.resumePos("k") == 0.0, "进度：10 秒保留记录但不续播");
            ps.remember("k", 92.0, 100.0, "t");
            check(!ps.has("k"), "进度：距结尾 15 秒内视为已看完并清除");
            ps.remember("k2", 20.0, 100.0, "t");
            check(qAbs(ps.resumePos("k2") - 20.0) < 0.001 && ps.size() == 1, "进度：条目互相独立");
        }

        // 逐字歌词解析（YRC / QRC）—— 在线接口对已登录用户返回 yrc；这里用固定样本验证解析与字级时间
        {
            const QString yrcText = "[12340,3200](0,240,0)你(240,260,0)好(500,300,0)啊";
            const QVector<SubtitleCue> y = Subtitles::parseYrc(yrcText);
            check(y.size() == 1, "YRC：解析出 1 行");
            check(!y.isEmpty() && qAbs(y[0].start - 12.34) < 0.001 && qAbs(y[0].end - 15.54) < 0.001,
                  "YRC：行起止时间正确（12.34~15.54s）");
            check(!y.isEmpty() && y[0].words.size() == 3, "YRC：得到 3 个字级时间戳");
            check(!y.isEmpty() && y[0].words.size() == 3 && y[0].words[1].text == "好"
                      && qAbs(y[0].words[1].start - 12.58) < 0.001,
                  "YRC：第 2 字的时间与文本正确");
            check(!y.isEmpty() && y[0].text == "你好啊", "YRC：正文拼接正确");
            const QString qrcText =
                "<QrcInfos><LyricInfo LyricContent=\"[1000,2000]逐(0,500)字(500,500)高(1000,500)亮(1500,500)\"/></QrcInfos>";
            const QVector<SubtitleCue> qq = Subtitles::parseQrc(qrcText);
            check(qq.size() == 1 && qq[0].words.size() == 4 && qq[0].text == "逐字高亮",
                  "QRC：XML/LyricContent 解析出 4 个字级时间戳");
        }

        SubtitleCue cue{ 10.0, 20.0, "t", {} };   // 第 4 个成员 words 显式空初始化
        check(qAbs(Subtitles::lineProgress(cue, 10.0)) < 0.01, "行内进度：起点为 0");
        check(qAbs(Subtitles::lineProgress(cue, 15.0) - 0.5) < 0.01, "行内进度：中点为 0.5");
        check(qAbs(Subtitles::lineProgress(cue, 20.0) - 1.0) < 0.01, "行内进度：终点为 1");
        check(qAbs(Subtitles::lineProgress(cue, 5.0)) < 0.01
                  && qAbs(Subtitles::lineProgress(cue, 30.0) - 1.0) < 0.01, "行内进度：越界自动夹紧");
        SubtitleCue zero{ 5.0, 5.0, "z", {} };
        check(qAbs(Subtitles::lineProgress(zero, 6.0) - 1.0) < 0.01, "行内进度：零时长视为已唱完");
        check(!q.label().isEmpty(), "队列标签非空（形如 第 2/3 首 · 顺序）");
        // 持久化往返：独立对象 + 临时文件（不碰真实队列文件）
        const QString tmp = QDir::tempPath() + "/hov-queue-selftest.json";
        PlayQueue q2;
        q2.setList({ {"x", "X"}, {"y", "Y"} }, 1);
        q2.cycleMode();
        const bool saved = q2.saveToPath(tmp);
        PlayQueue q3;
        const bool loaded = q3.loadFromPath(tmp);
        check(saved && loaded, "队列持久化：保存 + 读取成功");
        check(q3.size() == 2 && q3.index() == 1, "队列持久化：条目数与下标一致");
        check(q3.mode() == PlayQueue::Mode::RepeatOne, "队列持久化：模式一致");
        check(q3.at(1) && q3.at(1)->key == "y" && q3.at(1)->label == "Y", "队列持久化：条目内容一致");
        QFile::remove(tmp);
        Favorites fav;
        Favorites::Fav f; f.id = "netease:1"; f.key = f.id; f.title = "T";
        check(fav.add(f) && fav.contains("netease:1"), "收藏：加入后命中");
        check(!fav.add(f), "收藏：重复加入返回 false");
        check(fav.remove("netease:1") && !fav.contains("netease:1"), "收藏：移除生效");
        // ---- B7 全屏铺满：真 mpv 属性往返 + 门控逻辑 ----
        if (player_ && player_->renderReady()) {
            settings_.set("video.fillScreen", true);
            applyFillMode(true);                       // 自检时窗口非全屏 → 门控应为"不铺满"
            check(std::abs(player_->panscan()) < 0.01, "全屏铺满：非全屏不下发 panscan（保持比例）");
            check(!lastFillApplied_, "全屏铺满：状态机记为未铺满");
            // applyFillMode 走异步下发（mpv_set_property_async），与下面的同步 set 可能乱序落盘 →
            // 先等异步 panscan=0 落地再下发 1.0（此前偶发 FAIL 的真因；快照 5Hz 刷新还需轮询回读）。
            QThread::msleep(300);
            player_->setPanscan(1.0);
            auto waitPanscan = [&](double want) {
                for (int i = 0; i < 40; ++i) {
                    if (std::abs(player_->panscan() - want) < 0.01) return true;
                    QThread::msleep(50);
                }
                return false;
            };
            check(waitPanscan(1.0), "全屏铺满：panscan=1 下发并回读成功（真 mpv）");
            player_->setPanscan(0.0);
            check(waitPanscan(0.0), "全屏铺满：panscan=0 恢复保持比例");
            // v1.2.0 视频全屏：进入收起面板/退出还原（对齐 macOS/Android；Linux 原先完全没有）
            enterVideoFullscreen();
            check(isFullScreen(), "视频全屏：进入后窗口为全屏");
            check(results_ && !results_->isVisible(), "视频全屏：进入后结果面板已收起");
            exitVideoFullscreen();
            check(!isFullScreen(), "视频全屏：退出后窗口还原");
            check(results_ && results_->isVisible(), "视频全屏：退出后结果面板已恢复");
        }
        // ── 第四阶段扩测：纯函数与配置契约（不触网、不写用户设置、不碰 mpv）──
        {
            // ④ 唯一档位表：控制条/快捷键/设置面板共用的一张表
            const QVector<int> hs = Settings::qualityHeights();
            check(hs.size() == 8, "档位表：共 8 档（含 2160/1440）");
            check(hs.contains(2160) && hs.contains(1440), "档位表：包含 2160 与 1440");
            check(hs.first() == 0 && hs.last() == -1, "档位表：首档=自动、末档=仅音频");
            check(hs.indexOf(2160) < hs.indexOf(1080), "档位表：2160 在 1080 之前（从高到低）");
            check(Settings::qualityLabelFor(2160) == "2160p" && Settings::qualityLabelFor(0) == "自动"
                      && Settings::qualityLabelFor(-1) == "仅音频", "档位标签：与表格一致");
            // 清晰度格式串：只限制视频部分；高档位也要带上限
            const QStringList a2160 = UrlResolver::formatArgsFor(2160);
            check(a2160.size() == 2 && a2160.at(1).contains("height<=2160"), "清晰度：2160 档带上限");
            check(a2160.at(1).contains("+ba/b[height<=2160]") && a2160.at(1).endsWith("/bv*+ba/b"),
                  "清晰度：2160 档音频不设限且有兜底");
            // 来源识别与直链判定（下载/播放都依赖）
            check(UrlResolver::serviceOf("https://www.bilibili.com/video/BV1xx") == "bilibili",
                  "来源识别：B 站");
            check(UrlResolver::serviceOf("https://y.qq.com/n/ryqq/songDetail/xxx") == "qqmusic",
                  "来源识别：QQ 音乐");
            check(UrlResolver::isDirectMedia("https://x.com/a.mp4") && !UrlResolver::isDirectMedia("https://www.bilibili.com/video/BV1"),
                  "直链判定：媒体直链 vs 网页地址");
            check(UrlResolver::configDir().endsWith("/.config/hov"), "配置目录：~/.config/hov");
            // 字幕/歌词解析健壮性
            check(Subtitles::parse(QString(), "lrc").isEmpty(), "字幕解析：空输入返回空");
            check(Subtitles::parseYrc("{}").isEmpty(), "逐字歌词：垃圾输入不崩溃、返回空");
            // 设置校验契约
            check(Settings::validate("video.fillScreen", "true").isEmpty()
                      && Settings::validate("video.fillScreen", "maybe").isEmpty() == false,
                  "设置校验：布尔值严格判定");
            check(Settings::validate("music.qualityCeiling", "lossless").isEmpty()
                      && !Settings::validate("music.qualityCeiling", "hd").isEmpty(),
                  "设置校验：音质三档");
            check(!Settings::validate("video.noSuchKey", "1").isEmpty(), "设置校验：未知键被拒");
            check(ProgressStore::kRecordMin < ProgressStore::kResumeMin && ProgressStore::kEndGuard > 0,
                  "进度阈值：记录 < 续播，且看完守卫为正");
        }
        // ── 玻璃质感（ColorTheme）纯逻辑断言：主色提取 / 磨砂底 / 渐变 ──
        {
            QImage solid(64, 64, QImage::Format_RGB32);
            solid.fill(QColor(30, 90, 200));
            bool ok = false;
            const QColor d = ColorTheme::dominantColor(solid, &ok);
            check(ok, "玻璃：纯色封面取色成功");
            check(qAbs(d.hsvHueF() - QColor(30, 90, 200).hsvHueF()) < 0.06, "玻璃：主色色相与封面一致");
            check(d.hsvSaturationF() >= 0.449 && d.valueF() >= 0.339 && d.valueF() <= 0.781,
                  "玻璃：饱和/明度被夹到观感范围（≥0.45 / 0.34~0.78）");
            QImage gray(64, 64, QImage::Format_RGB32);
            gray.fill(QColor(128, 128, 128));
            bool ok2 = true;
            ColorTheme::dominantColor(gray, &ok2);
            check(!ok2, "玻璃：纯灰封面取色失败 → 回退兜底色");
            check(ColorTheme::neutral() == QColor(0x24, 0x2B, 0x38), "玻璃：兜底色为中性深蓝灰");
            const ColorTheme t = ColorTheme::make(solid);
            check(t.valid() && !t.backdrop().isNull() && !t.gradient().isNull(), "玻璃：主题构造出磨砂底与渐变");
            check(t.gradient().width() == 1 && t.gradient().height() == 256, "玻璃：渐变为 1×256（拉伸铺满用）");
            check(t.tintDark().valueF() < t.tint().valueF(), "玻璃：渐变底部比顶部暗（有纵深）");
            QImage uni(32, 32, QImage::Format_ARGB32_Premultiplied);
            uni.fill(QColor(10, 20, 30));
            const QImage bl = ColorTheme::boxBlur(uni, 4);
            check(bl.pixelColor(16, 16) == QColor(10, 20, 30), "玻璃：模糊均匀色不变（不产生色偏）");
            const ColorTheme same = ColorTheme::make(solid);
            check(same.tint() == t.tint(), "玻璃：同一封面结果稳定（可安全缓存）");
        }
        // 字幕字号基准（与 macOS 同：画面高 3.5%，下限 14px）—— 曾经是 height/16，比 macOS 大近 1.8 倍
        check(SubtitleOverlay::fontPxFor(700, 1.0) == 19, "字幕字号：700px 画面 → 19px（2.8%，向 macOS 观感靠拢）");
        check(SubtitleOverlay::fontPxFor(200, 1.0) == 13, "字幕字号：小画面命中 13px 下限");
        check(SubtitleOverlay::fontPxFor(700, 2.0) == 39, "字幕字号：用户倍数 2.0 生效");
        check(SubtitleOverlay::fontPxFor(700, 0.5) == 13, "字幕字号：缩到 0.5 时仍不低于下限 13px");

        qInfo() << "队列自检:" << pass << "/" << total << "通过";
        std::cerr << "[SELFTEST] 队列自检: " << pass << "/" << total << " 通过" << std::endl;   // 无人值守（CI/SSH）时也能读到汇总
        results_->addItem(QString("自检结果：%1/%2 通过").arg(pass).arg(total));
    }

    /** 登录窗口：App 内登录并把 cookie 自动落盘（无 WebEngine 时给出降级提示） */
    void openLoginDialog(const QString &site) {
        if (!LoginDialog::available()) {
            results_->addItem("当前构建不含登录组件（QtWebEngine 未编入，常见于 Flatpak 沙箱）");
            results_->addItem(QString("可手动放置 cookie 到：%1").arg(LoginDialog::cookieFileFor(site)));
        }
        LoginDialog dlg(site, this);
        dlg.exec();
        results_->addItem(QString("登录窗口已关闭；cookie 文件：%1").arg(LoginDialog::cookieFileFor(site)));
    }

    /** 设置窗口（用户可见入口）：改动即时应用并落盘 */
    void openSettingsDialog() {
        QDialog dlg(this);
        dlg.setWindowTitle("设置");
        dlg.setMinimumWidth(520);
        auto *form = new QFormLayout(&dlg);

        auto *quality = new QComboBox(&dlg);
        quality->addItems({ "standard（标准 128k）", "exhigh（较高 320k）", "lossless（无损，需会员）" });
        quality->setCurrentIndex(qBound(0, QStringList{ "standard", "exhigh", "lossless" }
                                               .indexOf(settings_.str("music.qualityCeiling", "exhigh")), 1));

        auto *fontScale = new QDoubleSpinBox(&dlg);
        fontScale->setRange(0.5, 2.0);
        fontScale->setSingleStep(0.25);
        fontScale->setValue(settings_.number("subtitle.fontScale", 1.0));

        auto *delay = new QDoubleSpinBox(&dlg);
        delay->setRange(-5.0, 5.0);
        delay->setSingleStep(0.5);
        delay->setSuffix(" 秒");
        delay->setValue(settings_.number("subtitle.defaultDelay", 0.0));

        auto *karaoke = new QCheckBox("歌词用卡拉OK面板（居中 + 逐字高亮）", &dlg);
        karaoke->setChecked(settings_.boolean("subtitle.karaoke", true));

        auto *probe = new QCheckBox("启动时做网络探活", &dlg);
        probe->setChecked(settings_.boolean("ui.showNetworkProbe", true));

        // 观感三项（键早已存在 ✓ 之前设置界面点不到 ✗ —— 规则 #5 一致性清单本轮补齐 ✓）
        auto *gpuNextCheck = new QCheckBox("视频用 gpu-next 渲染（libplacebo：缩放/去色带/色调映射更好；需重启）", &dlg);
        gpuNextCheck->setChecked(Settings::rawBool("video.gpuNext", false));
        auto *glassCheck = new QCheckBox("玻璃质感（封面磨砂底 + 主题色渐变）", &dlg);
        glassCheck->setChecked(settings_.boolean("ui.glass", true));
        auto *glassWinCheck = new QCheckBox("窗口级透明（桌面透出；Wayland 下可能隐藏视频画面，谨慎开）", &dlg);
        glassWinCheck->setChecked(settings_.boolean("ui.glassWindow", QGuiApplication::platformName().contains("xcb")));
        auto *hoverCheck = new QCheckBox("全屏时鼠标贴左边缘浮出结果侧栏", &dlg);
        hoverCheck->setChecked(settings_.boolean("ui.hoverReveal", true));

        auto *direct = new QCheckBox("国内接口强制直连（音乐/B站，忽略代理）", &dlg);
        direct->setChecked(settings_.boolean("network.forceDirectDomestic", false));

        auto *dirEdit = new QLineEdit(settings_.str("download.dir", DownloadManager::defaultDir()), &dlg);
        auto *dirRow = new QWidget(&dlg);
        auto *dirLay = new QHBoxLayout(dirRow);
        dirLay->setContentsMargins(0, 0, 0, 0);
        dirLay->addWidget(dirEdit, 1);
        auto *browse = new QPushButton("浏览…", dirRow);
        dirLay->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [this, dirEdit] {
            const QString d = QFileDialog::getExistingDirectory(this, "选择下载目录", dirEdit->text());
            if (!d.isEmpty()) dirEdit->setText(d);
        });

        auto *themeBox = new QComboBox(&dlg);
        themeBox->addItem("跟随系统（浅色 / 深色）", "auto");
        themeBox->addItem("浅色", "light");
        themeBox->addItem("深色", "dark");
        {
            const QString cur = settings_.str("ui.theme", "auto").trimmed().toLower();
            const int idx = themeBox->findData(cur);
            themeBox->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        form->addRow("应用主题", themeBox);
        form->addRow("音质上限", quality);
        form->addRow("字幕/歌词字号", fontScale);
        form->addRow("默认字幕延迟", delay);
        form->addRow(QString(), karaoke);
        form->addRow(QString(), gpuNextCheck);
        form->addRow(QString(), glassCheck);
        form->addRow(QString(), glassWinCheck);
        form->addRow(QString(), hoverCheck);
        form->addRow(QString(), probe);
        form->addRow(QString(), direct);
        form->addRow("下载目录", dirRow);

        auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, &dlg);
        form->addRow(btns);
        connect(btns, &QDialogButtonBox::accepted, &dlg, [this, quality, fontScale, delay, karaoke, probe, direct, dirEdit, themeBox, glassCheck, glassWinCheck, hoverCheck, gpuNextCheck, &dlg] {
            static const QStringList qs{ "standard", "exhigh", "lossless" };
            applySetting("music.qualityCeiling", qs.at(quality->currentIndex()));
            applySetting("ui.theme", themeBox->currentData().toString());
            applyThemeSettingNow();                                  // 立即生效（无需重启 ✓）
            applySetting("subtitle.fontScale", QString::number(fontScale->value()));
            applySetting("subtitle.defaultDelay", QString::number(delay->value()));
            applySetting("subtitle.karaoke", karaoke->isChecked() ? "true" : "false");
            const bool gpuNextWas = Settings::rawBool("video.gpuNext", false);
            applySetting("video.gpuNext", gpuNextCheck->isChecked() ? "true" : "false");
            if (gpuNextWas != gpuNextCheck->isChecked())
                setStatusLine(QString("渲染后端已设为 %1 —— 重新打开应用后生效")
                                  .arg(gpuNextCheck->isChecked() ? "gpu-next" : "libmpv"));
            applySetting("ui.glass", glassCheck->isChecked() ? "true" : "false");
            if (player_) player_->setGlassEnabled(glassCheck->isChecked());          // 立即生效 ✓
            const bool gwWas = settings_.boolean("ui.glassWindow", false);
            applySetting("ui.glassWindow", glassWinCheck->isChecked() ? "true" : "false");
            if (gwWas != glassWinCheck->isChecked())
                setStatusLine(QString("窗口级透明已设为 %1 —— 重新打开应用后生效")
                                  .arg(glassWinCheck->isChecked() ? "开" : "关"));   // 切透明要重建窗口 → 提示重启 ✓
            applySetting("ui.hoverReveal", hoverCheck->isChecked() ? "true" : "false");  // 轮询里实时读 ✓ 立即生效
            applySetting("ui.showNetworkProbe", probe->isChecked() ? "true" : "false");
            applySetting("network.forceDirectDomestic", direct->isChecked() ? "true" : "false");
            applySetting("download.dir", dirEdit->text().trimmed());
            results_->addItem("设置已保存");
            dlg.accept();
        });
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        dlg.exec();
    }

    /** 命令行设置一项：写入 settings.json，并尽量**立即生效**（能热更新的就热更新） */
    bool applySetting(const QString &key, const QString &value) {
        const bool ch = applySettingInner(key, value);
        // 与网络解析相关的设置改动后要立刻生效（不只等重启）
        if (key.startsWith("network.")) {
            resolver_->setForceDirect(settings_.boolean("network.forceDirectDomestic", false));
            resolver_->setCookiesFromBrowser(settings_.str("network.cookiesFromBrowser", ""));
        }
        return ch;
    }
    bool applySettingInner(const QString &key, const QString &value) {
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
        else if (key == "network.forceDirectDomestic" && dl_) { /* 下一次创建 API 时生效 */ }
        results_->addItem(QString("[设置] %1 = %2%3").arg(key, value, changed ? "" : "（值未变化）"));
        return changed;
    }
    QString dumpSettings() const { return settings_.dump(); }

    /** 按设置给国内 API 对象设定代理策略（强制直连 / 跟随系统） */
    template <class T> void tuneApi(T *api) {
        if (api) api->setForceDirect(settings_.boolean("network.forceDirectDomestic", false));
    }

    /** 下载当前播放的直链（按钮 / 命令行共用） */
    void downloadCurrent() {
        if (lastStreamUrl_.isEmpty()) {
            results_->addItem("下载：当前没有可下载的直链（先播放一首歌/一个视频）");
            return;
        }
        QString name = lastStreamTitle_;
        const QString ext = lastStreamUrl_.contains(".flac") ? "flac"
                          : lastStreamUrl_.contains(".m4a") ? "m4a" : "mp3";
        if (name.isEmpty()) name = QString("hov-download.%1").arg(ext);
        else name = name + "." + ext;
        // 音乐标签：标题/艺术家/专辑（+ 封面，当前大多来自 QQ 搜索的 albummid）
        const QString id = dl_->enqueue(lastStreamUrl_, lastStreamTitle_, name,
                                        lastArtist_, lastAlbum_, lastCoverUrl_,
                                        lastAudioUrl_, lastLRC_);   // ③ 带独立音轨（混流）与歌词（.lrc 侧车）
        results_->addItem(QString("已加入下载队列：%1（并发上限 2，失败自动重试 2 次）").arg(lastStreamTitle_));
        Q_UNUSED(id);
    }

    /** 命令行：下载某个直链（--download <URL> [文件名]） */
    void downloadUrl(const QString &url, const QString &name) {
        dl_->enqueue(url, name.isEmpty() ? url.section('/', -1) : name, name);
        results_->addItem(QString("已加入下载队列：%1").arg(url.left(80)));
    }

    /** 命令行：搜索并下载第一首可播放的（--download-music <关键词>） */
    void downloadMusic(const QString &keyword, int count = 1) {
        results_->addItem(QString("下载（网易云）：%1").arg(keyword));
        auto *api = new NetEaseApi(this);
        tuneApi(api);
        api->setStatusHandler([this](const QString &m) { results_->addItem(m); });
        api->search(keyword, 10, [this, api, count](const QVector<NetEaseApi::Song> &songs) {
            if (songs.isEmpty()) { results_->addItem("无结果"); api->deleteLater(); return; }
            auto attempt = std::make_shared<std::function<void(int)>>();
            auto queued = std::make_shared<int>(0);
            *attempt = [this, api, songs, attempt, count, queued](int i) {
                if (i >= songs.size() || (*queued) >= count) {
                    if (*queued == 0) results_->addItem("前几首都没有可下载地址");
                    else results_->addItem(QString("已入队 %1 首（并发上限 2）").arg(*queued));
                    api->deleteLater();
                    return;
                }
                const NetEaseApi::Song s = songs.at(i);
                api->songUrl(s.id, effectiveQuality(), [this, s, i, attempt, queued](const QString &url, const QString &e) {
                    if (url.isEmpty()) { results_->addItem(QString("跳过「%1」：%2").arg(s.name, e)); (*attempt)(i + 1); return; }
                    const QString label = QString("%1 — %2").arg(s.name, s.artist);
                    lastStreamUrl_ = url;
                    lastStreamTitle_ = label;
                    lastArtist_ = s.artist;
                    lastAlbum_ = s.album;
                    lastCoverUrl_.clear();     // 网易云封面需额外详情请求，批量下载暂不嵌封面
                    downloadCurrent();          // 复用同一套命名与落盘逻辑
                    (*queued)++;
                    (*attempt)(i + 1);          // 继续下一首（达到 count 时由头部收尾）
                });
            };
            (*attempt)(0);
        });
    }

    /** 自动化入口：预选搜索来源（0=YouTube 1=B站 2=网易云 3=QQ音乐 4=本地库） */
    // 上限跟着下拉项数走（加了「本地库」后仍写死 2 会把第 4 项夹回 QQ音乐 —— 实测踩到）
    void applySource(int n) { if (srcBox_) srcBox_->setCurrentIndex(qBound(0, n, srcBox_->count() - 1)); }

    /** 列表点击的统一入口：按条目类型分发（netease: / qq: / 普通网页 URL） */
    void playItem(const QString &key, const QString &label) {
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

    // ---------------- 进度记忆 ----------------
    // 设计要点（与 Android 版对齐）：
    //   * 键用「解析标识」而不是解析后的直链 —— 直链会过期，存了下次也放不了
    //   * 续播是**加载完再 seek**：mpv 刚 playResolved 时 duration 还是 0，立刻 seek 会被丢掉
    /** 播放任何内容前调用：结掉上一项的进度，并安排好本项的续播位置 */
    void beginPlayback(const QString &key, const QString &label) {
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

    /** 500ms 定时器：执行待办的续播 seek + 记录/落盘进度（带节流） */
    void tickProgress() {
        if (!player_ || player_->durationSec() < 1.0) {
            if (mpris_) mpris_->updateStatus();       // 无内容时状态为 Stopped
            return;
        }
        if (mpris_) {
            if (!metaSent_) {                          // 时长就绪后补报元数据（一次/首）
                mpris_->updateMeta(playTitle_, playArtist_, player_->durationSec());
                metaSent_ = true;
            }
            mpris_->updateStatus();
            mpris_->setNavAvailable(!queue_.isEmpty(), !queue_.isEmpty());
        }
        if (pendingResume_ > 0.5) {
            if (player_->positionSec() < 1.0) {          // 媒体已就绪（还没开始走）
                const double p = pendingResume_;
                pendingResume_ = 0.0;
                player_->seekTo(p);
                results_->addItem(QString("已跳转到 %1").arg(fmtClock(p)));
            } else if (player_->positionSec() > pendingResume_ + 5.0) {
                pendingResume_ = 0.0;                    // 太晚了（已播到后面），放弃续播
            }
        }
        if (playKey_.isEmpty()) return;
        if (!settings_.boolean("playback.rememberProgress", true)) return;
        const double pos = player_->positionSec(), dur = player_->durationSec();
        if (pos < ProgressStore::kRecordMin) return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastProgRemember_ < 5000) return;
        lastProgRemember_ = now;
        prog_.remember(playKey_, pos, dur, playLabel_);
        if (now - lastProgSave_ >= 30000) {              // 落盘节流：30 秒一次
            lastProgSave_ = now;
            prog_.save();
        }
    }

    /** 立即把当前进度落盘（退出 / 切歌 / 停止时调） */
    void saveProgressNow() {
        if (!player_ || playKey_.isEmpty()) return;
        const double pos = player_->positionSec(), dur = player_->durationSec();
        if (dur < 1.0 || pos < ProgressStore::kRecordMin) return;
        if (!settings_.boolean("playback.rememberProgress", true)) return;
        if (prog_.remember(playKey_, pos, dur, playLabel_)) prog_.save();
    }

    /** --progress-show：列出进度记录（倒序） */
    QString dumpProgress() const {
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
    void clearProgress() { prog_.clear(); prog_.save(); }
    // ---------------- 本地库（下载目录） ----------------
    /** 列出下载目录里的媒体文件；filter 为文件名过滤 */
    void refreshLocalLibrary(const QString &filter = QString()) {
        lib_.setDir(dl_ ? dl_->dir() : QString());
        const auto list = lib_.scan(filter);
        results_->clear();
        results_->addItem(QString("本地库：%1").arg(lib_.dir()));
        results_->addItem(QString("排序：%1%2").arg(LocalLibrary::sortLabel(lib_.sort()),
                                                    filter.isEmpty() ? QString()
                                                                     : QString("，过滤「%1」").arg(filter)));
        results_->addItem("双击播放 · O 键切换排序 · R 键重命名");
        qint64 total = 0;
        for (const auto &e : list) {
            total += e.size;
            auto *it = new QListWidgetItem(QString("%1   %2   %3")
                                               .arg(e.name, LocalLibrary::formatSize(e.size),
                                                    QDateTime::fromMSecsSinceEpoch(e.mtime).toString("MM-dd HH:mm")),
                                           results_);
            it->setData(Qt::UserRole, e.path);
        }
        results_->addItem(QString("共 %1 个文件，合计 %2").arg(list.size()).arg(LocalLibrary::formatSize(total)));
        qInfo() << "本地库:" << list.size() << "个文件," << LocalLibrary::formatSize(total);
    }
    void showLocalLibrary() { refreshLocalLibrary(search_ ? search_->text().trimmed() : QString()); }

    /** R 键：重命名选中的本地库文件（保持扩展名；冲突/非法字符都有明确提示） */
    /** presetName 非空时不弹对话框（供 --rename-selected 自动化用，与界面走同一条逻辑） */
    void renameSelectedLocal(const QString &presetName = QString()) {
        QListWidgetItem *it = results_->currentItem();
        const bool itIsFile = it && !it->data(Qt::UserRole).toString().isEmpty();
        if (!itIsFile && !presetName.isEmpty()) {
            // 自动化：取第一个真正的媒体项 —— 列表前几行是标题/提示行，UserRole 为空
            for (int i = 0; i < results_->count(); ++i) {
                QListWidgetItem *cand = results_->item(i);
                if (!cand->data(Qt::UserRole).toString().isEmpty()) { it = cand; break; }
            }
        }
        const QString path = it ? it->data(Qt::UserRole).toString() : QString();
        qInfo() << "重命名：选中项=" << (it ? it->text().left(40) : QString("(无)")) << " 路径=" << path;
        if (path.isEmpty() || !QFile::exists(path)) {
            qInfo() << "重命名：没有可重命名的选中项";
            results_->addItem("重命名：请先在本地库列表里选中一个文件（来源选「本地库」）");
            return;
        }
        const QFileInfo fi(path);
        bool ok = false;
        QString newName;
        if (presetName.isEmpty()) {
            newName = QInputDialog::getText(this, "重命名", QString("新名称（.%1 可省略）").arg(fi.suffix()),
                                            QLineEdit::Normal, fi.completeBaseName(), &ok);
        } else {
            newName = presetName;   // 走预设名时 ok 必须置真，否则会被当成"已取消"（实测踩到）
            ok = true;
        }
        if (!ok || newName.trimmed().isEmpty()) {
            results_->addItem("重命名已取消");
            return;
        }
        const QString err = lib_.rename(path, newName);
        qInfo() << "重命名结果:" << (err.isEmpty() ? QString("成功") : err) << " 新名=" << newName.trimmed();
        if (err.isEmpty()) {
            results_->addItem(QString("已重命名为：%1").arg(newName.trimmed()));
            refreshLocalLibrary(search_->text().trimmed());
        } else {
            results_->addItem("重命名失败：" + err);
        }
    }

    int downloadMaxTotalMb() const { return dl_ ? dl_->maxTotalSizeMb() : 2048; }
    DownloadManager::CleanResult cleanupDownloads(qint64 maxBytes) {
        return dl_ ? dl_->cleanupLru(maxBytes) : DownloadManager::CleanResult{};
    }
    void setResumeEnabled(bool b) { resumeEnabled_ = b; }

    /** --subs-url：取在线字幕并挂到当前播放上（自动化验证整条链路用，也方便手工挂字幕） */
    void applySubsUrl(const QString &url) {
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
    static QString fmtClock(double s) {
        const int t = static_cast<int>(s < 0 ? 0 : s);
        return QString("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QLatin1Char('0'));
    }

    /** ⏭/播完续播：有队列走队列，否则走本地文件播放列表 */
    void queueOrFileNext(bool autoAdvance = false) {
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
    void queueOrFilePrev() {
        if (!queue_.isEmpty()) {
            queue_.prev();
            if (queue_.current()) playItem(queue_.current()->key, queue_.current()->label);
            return;
        }
        playPrevFile();
    }

    /** 收藏当前播放项（F 键 / 命令行） */
    void toggleFavoriteCurrent() {
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

    /** 用当前搜索结果建立队列（各来源搜索完都调它） */
    void rebuildQueueFromList() {
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

    /** 播放网易云歌曲：取地址 → 播放 → 顺带取歌词（原文 + 翻译合并为双语） */
    void playNetEase(const QString &id, const QString &label) {
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

    /** QQ 专辑封面地址（不需额外请求：搜索接口已带 albummid） */
    static QString qqCoverUrl(const QString &albumMid) {
        return albumMid.isEmpty() ? QString()
                                  : QString("https://y.gtimg.cn/music/photo_new/T002R300x300M000%1.jpg").arg(albumMid);
    }
    /** 「歌名 — 歌手」是本项目中音乐标题的统一约定（_MOC 文档与队列/收藏都用它） */
    static QString titleOf(const QString &label) {
        const int sep = label.indexOf(QString::fromUtf8(" \xe2\x80\x94 "));
        return sep > 0 ? label.left(sep) : label;
    }
    static QString artistOf(const QString &label) {
        const int sep = label.indexOf(QString::fromUtf8(" \xe2\x80\x94 "));
        return sep > 0 ? label.mid(sep + 3) : QString();
    }

    /** 音乐播放时挂封面：搜索里记过的直接用，网易云没记过就按 id 取详情 */
    void applyCoverFor(const QString &key, const QString &label) {
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

    // ---------------- 播放中切音质 ----------------
    /** 当前有效档位：会话档位（若有）再受设置里的上限裁剪 */
    QString effectiveQuality() const {
        return settings_.effectiveQuality(sessionQuality_.isEmpty() ? QStringLiteral("lossless") : sessionQuality_);
    }
    static QString qualityLabel(const QString &q) {
        if (q == "standard") return QStringLiteral("标准 128k");
        if (q == "lossless") return QStringLiteral("无损 FLAC");
        return QStringLiteral("较高 320k");
    }

    /** 切到指定档位；正在放音乐时**重新取流并回到原进度**（视频不适用：由 yt-dlp 按清晰度选流） */
    void switchQualityTo(const QString &q) {
        static const QStringList ok{"standard", "exhigh", "lossless"};
        if (!ok.contains(q)) {
            results_->addItem(QString("[音质] 未知档位：%1（可用 %2）").arg(q, ok.join(" / ")));
            return;
        }
        sessionQuality_ = q;
        const QString eff = effectiveQuality();
        qInfo() << "[音质] 切档：目标=" << q << " 会话档位=" << sessionQuality_ << " 有效档位=" << eff
                << " 当前播放键=" << playKey_ << " 时长=" << (player_ ? player_->durationSec() : -1.0);
        results_->addItem(QString("[音质] 目标 %1，有效档位 %2%3")
                              .arg(qualityLabel(q), qualityLabel(eff),
                                   eff == q ? QString() : QString("（受设置里的上限裁剪）")));
        const bool music = playKey_.startsWith("netease:") || playKey_.startsWith("qq:");
        if (!music) {
            results_->addItem("音质切换仅对网易云 / QQ音乐 有效");
            return;
        }
        if (!player_ || player_->durationSec() < 1.0) return;      // 没在放：只改档位
        pendingResume_ = player_->positionSec();                   // 复用"就绪后 seek"的机制回到原进度
        results_->addItem(QString("重新取流（%1），完成后回到 %2").arg(qualityLabel(eff), fmtClock(pendingResume_)));
        if (playKey_.startsWith("netease:")) playNetEase(playKey_.mid(8), playLabel_);
        else playQQ(playKey_.mid(3), playLabel_);
    }

    /** Q 键：在 标准 → 较高 → 无损 之间循环 */
    void cycleQuality() {
        static const QStringList order{"standard", "exhigh", "lossless"};
        int i = order.indexOf(effectiveQuality());
        if (i < 0) i = 1;
        switchQualityTo(order.at((i + 1) % order.size()));
    }

    /** 取网易云封面（走独立实例，避免与歌词请求争同一个对象的生命周期） */
    void fetchNetEaseCover(const QString &id, const QString &title, const QString &artist) {
        auto *cv = new NetEaseApi(this);
        tuneApi(cv);
        cv->coverUrl(id, [this, cv, title, artist](const QString &url) {
            cv->deleteLater();
            lastCoverUrl_ = url;
            player_->setCoverArt(url, title, artist);
            if (!url.isEmpty()) results_->addItem(QString("封面已加载：%1").arg(title));
        });
    }

    /** 取 QQ音乐歌词（LRC 明文 + 翻译）并合并后交给字幕层 */
    void loadQQLyricsFor(const QString &mid, const QString &label, QQMusicApi *api) {
        api->lyric(mid, [this, api, label](const QString &lrc, const QString &trans) {
            lastLRC_ = lrc;                  // ③ 同上（QQ 音乐）
            QVector<SubtitleCue> cues = Subtitles::parse(lrc, "lrc");
            const QVector<SubtitleCue> tc = Subtitles::parse(trans, "lrc");
            cues = Subtitles::mergeBilingual(cues, tc);
            if (cues.isEmpty()) {
                results_->addItem("该曲目没有歌词");
            } else {
                player_->setSubtitleCues(cues, QString("歌词：%1").arg(label),
                                              settings_.boolean("subtitle.karaoke", true));   // true=歌词面板模式
                results_->addItem(QString("已加载歌词 %1 行%2")
                                      .arg(cues.size())
                                      .arg(tc.isEmpty() ? "" : "（含翻译）"));
            }
            api->deleteLater();
        });
    }

    /** 播放 QQ音乐歌曲：逐档降级取流（VIP 曲目匿名会返回 104003，明确提示） */
    void playQQ(const QString &mid, const QString &label) {
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

    /** 播放内容的平台标签与"搜索源跟随"（用户反馈：源显示 YouTube 却在放 B 站 —— 下拉是搜索源，容易误读） */
    /** 当前有效视频清晰度（会话值优先，其次设置） */
    int effectiveVideoHeight() const {
        if (sessionVideoHeight_ != 0) return sessionVideoHeight_;
        return static_cast<int>(settings_.number("video.maxHeight", 0));
    }

    /** 切清晰度：重新解析并回到原进度（视频专属；与"切音质"同一思路） */
    void switchVideoQuality(int h) {
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

    /** B5 画中画：小窗 + 置顶 + 隐藏界面（不重建播放器，播放不中断）
     *  说明：Wayland 下"置顶"由合成器决定（Qt 只能表达意图），故同时把界面收成纯播放窗口。 */
    // ---- B7 全屏铺满（与 Android/macOS 同键 video.fillScreen）----
    /** 期望：全屏 && 设置开启 → panscan=1（裁切填满、去左右黑边）；否则 0（保持比例） */
    void applyFillMode(bool force = false) {
        if (!player_ || !player_->renderReady()) return;   // mpv 未就绪一律不动（早期同步调用会挂住 UI）
        const bool want = isFullScreen() && settings_.boolean("video.fillScreen", true);
        if (!force && want == lastFillApplied_) return;
        lastFillApplied_ = want;
        player_->setPanscanAsync(want ? 1.0 : 0.0);        // 异步下发，绝不阻塞 GUI 线程
        const double actual = player_->panscan();
        qInfo().noquote() << QString("[SELFTEST] 全屏铺满：期望=%1 panscan回读=%2（全屏=%3 设置=%4）")
                                 .arg(want ? "开" : "关").arg(actual, 0, 'f', 2)
                                 .arg(isFullScreen() ? "是" : "否")
                                 .arg(settings_.boolean("video.fillScreen", true) ? "开" : "关");
        results_->addItem(want ? QString("铺满模式（裁切黑边，回读 panscan=%1）").arg(actual, 0, 'f', 1)
                               : QString("保持比例（panscan=0）"));
        if (fillBtn_) fillBtn_->setChecked(settings_.boolean("video.fillScreen", true));
    }

    // ── 视频全屏（v1.2.0：对齐 macOS VideoFullscreen / Android 沉浸式）──────────────
    // 用户实测（Arch Linux）：双击画面无反应、点「铺满」在非全屏时毫无效果。
    // 根因两条：① 全项目从无 mouseDoubleClickEvent → 双击从未绑定任何行为；
    //          ② applyFillMode 的门控是 isFullScreen() && 设置 → 非全屏时点它什么都不做。
    // 更深一层：Linux 端根本没有"视频全屏"模式（窗口全屏时工具栏/结果区仍在），macOS/Android 都有。
    bool videoFull_ = false;              // 视频全屏中
    bool vfStatusWasVisible_ = false;     // 进入前的状态行可见性（退出时要还原，不能强显）
    QList<int> vfSplitSizes_;             // 进入前的分隔器尺寸（退出还原 → 配合结果区宽度记忆）

    /** 只留画面：收起/恢复工具栏、结果面板、状态行与所有按钮/滑块/标签（复用 PiP 的既有惯例） */
    void setPlayerOnlyVisible(bool only) {
        if (topCard_) only ? topCard_->hide() : topCard_->show();
        // ⚠️ results_ 是 masonry_ 的**数据源**，必须永远隐藏 —— 退出全屏时不能 show() 它
        //（否则屏幕左侧多出一条窄缝般的旧列表 —— 用户截图实测）
        if (results_) results_->hide();
        // ⚠️ 关键修复：**可见的结果区是 masonry_**（自绘卡片视图），results_ 只是它的数据源（平时就隐藏）。
        //    原来只藏 results_ → 全屏后卡片网格照样在屏幕上（用户截图实测）✗ → 这里必须一起处理。
        if (masonry_) only ? masonry_->hide() : masonry_->show();
        if (statusLabel_) {
            if (only) { vfStatusWasVisible_ = statusLabel_->isVisible(); statusLabel_->hide(); }
        }
        for (auto *w : findChildren<QWidget *>())
            if (qobject_cast<QPushButton *>(w) || qobject_cast<QSlider *>(w)
                || qobject_cast<QLabel *>(w) || qobject_cast<QLineEdit *>(w))
                only ? w->hide() : w->show();
        // 上面循环会强制显示所有 QLabel，状态行要按进入前的可见性还原（默认本就隐藏）
        if (!only && statusLabel_ && !vfStatusWasVisible_) statusLabel_->hide();
    }

    // ── 全屏浮出侧栏（对齐 macOS：轮询鼠标位置 + 浮层显隐；只重挂 masonry_，不动布局）──────
    /** 进入视频全屏时把 masonry_ 移进浮层框；退出时移回分隔器（顺序/尺寸都不变） */
    void setMasonryFloating(bool floating) {
        if (!masonry_ || !split_) return;
        if (floating) {
            if (!masonryHost_) {
                masonryHost_ = new QFrame(this);
                masonryHost_->setObjectName("card");
                auto *lay = new QVBoxLayout(masonryHost_);
                lay->setContentsMargins(6, 6, 6, 6);
            }
            masonry_->setParent(masonryHost_);
            masonryHost_->layout()->addWidget(masonry_);
            masonryHost_->hide();
        } else {
            if (masonryHost_) {
                masonry_->setParent(split_);
                split_->insertWidget(0, masonry_);          // 与初始一致：第 0 位（results_ 是数据源，不占位）
                masonryHost_->hide();
                // ⚠️ 还原尺寸必须**按当前子控件数**构造列表：插入后数量与原 setSizes 的列表长度不符 →
                //    结果区会被挤成 ~90px 窄缝（用户截图实测）。这里逐下标赋值，剩余宽度留给播放器。
                const int total = qMax(1, split_->width());
                const int keepW = qBound(240, vfMasonryWidth_, qMax(240, total - 120));
                QList<int> sizes;
                for (int i = 0; i < split_->count(); ++i) sizes << (split_->widget(i) == masonry_ ? keepW : 0);
                for (int i = split_->count() - 1; i >= 0; --i)
                    if (split_->widget(i) != masonry_ && split_->widget(i) != results_) {
                        sizes[i] = qMax(0, total - keepW);      // 播放器吃掉其余
                        break;
                    }
                split_->setSizes(sizes);
                std::fprintf(stderr, "[VFULL] 退出还原：子控件=%d masonry宽=%d 播放器宽=%d\n",
                             split_->count(), keepW, qMax(0, total - keepW));
            }
            masonry_->show();
        }
    }

    /** 浮出/收起侧栏（含控制条） */
    void setSidebarRevealed(bool on) {
        if (!masonryHost_) return;
        if (on) {
            // 用**窗口当前**尺寸（全屏切换后 rect() 才更新；探针实测过 1334 > 窗口高 1000 的越界）
            // 全屏切换是异步的（height() 还没更新）→ 用**屏幕**尺寸兜底，避免浮层越界（探针实测过 1334 > 1000）
            const QSize scr = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->size() : QSize(1280, 720);
            const int winW = isFullScreen() ? scr.width() : (width() > 0 ? width() : scr.width());
            const int winH = isFullScreen() ? scr.height() : (height() > 0 ? height() : scr.height());
            const int w = qBound(240, MasonryView::defaultPanelWidth(), qMax(240, winW - 80));
            const int h = qMax(200, winH - 40);
            masonryHost_->setGeometry(8, 20, w, h);
            masonryHost_->raise();
            masonryHost_->show();
            if (masonry_) masonry_->show();     // ⚠️ 必须显式显示：它被 setPlayerOnlyVisible(true) 藏过（探针实测踩到）
            sidebarRevealed_ = true;
            // 控制条（按钮/滑块）浮出 —— 但**排除顶栏卡里的控件**（顶栏在全屏时始终隐藏）
            for (auto *w2 : findChildren<QWidget *>())
                if (qobject_cast<QPushButton *>(w2) || qobject_cast<QSlider *>(w2) || qobject_cast<QLineEdit *>(w2)) {
                    if (topCard_ && topCard_->isAncestorOf(w2)) continue;
                    w2->show();
                }
            std::fprintf(stderr, "[FS-PROBE] 侧栏浮出 w=%d h=%d 卡片可见=%d\n", w, h, (masonry_ && masonry_->isVisible()) ? 1 : 0);
        } else {
            masonryHost_->hide();
            if (masonry_) masonry_->hide();     // 浮层收起 → 卡片也要藏（否则留在画面上）
            sidebarRevealed_ = false;
            if (videoFull_) {                   // 全屏时控制条一起收回（同样排除顶栏）
                for (auto *w2 : findChildren<QWidget *>())
                    if (qobject_cast<QPushButton *>(w2) || qobject_cast<QSlider *>(w2) || qobject_cast<QLineEdit *>(w2)) {
                        if (topCard_ && topCard_->isAncestorOf(w2)) continue;
                        w2->hide();
                    }
            }
        }
    }

    /** 悬停轮询：鼠标贴左边缘 12px 浮出侧栏；贴底边 12px 浮出控制条；离开 400ms 收起 */
    void pollHover() {
        if (!videoFull_ || !settings_.boolean("ui.hoverReveal", true)) return;
        const QPoint p = mapFromGlobal(QCursor::pos());
        const bool inWindow = rect().contains(p);
        const bool nearLeft = inWindow && p.x() <= kEdgeBand;
        const bool hoverPanel = masonryHost_ && masonryHost_->isVisible() && masonryHost_->geometry().contains(p);
        const bool nearBottom = inWindow && p.y() >= rect().height() - kEdgeBand;
        const bool shouldShow = nearLeft || hoverPanel || nearBottom;
        if (shouldShow) {
            hideDeadline_ = 0;                                  // 取消待收起
            if (!masonryHost_ || !masonryHost_->isVisible()) setSidebarRevealed(true);
        } else if (masonryHost_ && masonryHost_->isVisible()) {
            if (hideDeadline_ == 0) hideDeadline_ = QDateTime::currentMSecsSinceEpoch() + 400;
            else if (QDateTime::currentMSecsSinceEpoch() >= hideDeadline_) setSidebarRevealed(false);
        }
    }

    /// ui.theme 的**唯一入口**（构造时 + 设置里改动时都走这里 ✓ 避免两处重复 ✗）
    void applyThemeSettingNow()
    {
        const QString t = settings_.str("ui.theme", "auto").trimmed().toLower();
        Theme::Mode forced = Theme::Mode::Dark;
        if (t == "auto") {
            if (!themeWatcher_) {
                themeWatcher_ = new ThemeWatcher(this);
                connect(themeWatcher_, &ThemeWatcher::modeChanged, this,
                        [](Theme::Mode m, const QString &src) {
                            Theme::setMode(m);
                            std::fprintf(stderr, "[THEME] 应用模式=%s（来源=%s）\n",
                                         m == Theme::Mode::Light ? "light" : "dark", qPrintable(src));
                        });
            }
            themeWatcher_->start();          // start() 会立刻 emit 一次 → setMode ✓
        } else if (Theme::parseMode(t, &forced)) {
            if (themeWatcher_) themeWatcher_->stop();   // 用户显式指定 → 不再跟随系统 ✓
            Theme::setMode(forced);
            std::fprintf(stderr, "[THEME] 用户指定 ui.theme=%s（不跟随系统）\n", qPrintable(t));
        }
    }

    void enterVideoFullscreen() {
        if (videoFull_) return;
        videoFull_ = true;
        vfSplitSizes_ = split_ ? split_->sizes() : QList<int>{};
        // 记下进入前的结果区真实宽度（退出要按它还原；两元素 setSizes 在插入后子控件数变化时会错乱 ✗）
        vfMasonryWidth_ = masonry_ ? qMax(240, masonry_->width()) : MasonryView::defaultPanelWidth();
        setPlayerOnlyVisible(true);
        setMasonryFloating(true);                 // 结果区改为浮层（不动布局）
        showFullScreen();
        applyFillMode(true);
        if (!hoverTimer_) {                       // 200ms 轮询鼠标（与 macOS 的 0.15s 同法）
            hoverTimer_ = new QTimer(this);
            connect(hoverTimer_, &QTimer::timeout, this, [this] { pollHover(); });
        }
        if (settings_.boolean("ui.hoverReveal", true)) hoverTimer_->start(200);
        qInfo().noquote() << QString("[VFULL] 进入视频全屏（面板已收起；鼠标贴左边缘浮出列表，双击 / Esc / F 退出）");
        std::fprintf(stderr, "[VFULL] 进入视频全屏 窗口全屏=%d 结果面板隐藏=%d\n",
                     isFullScreen() ? 1 : 0, (results_ && !results_->isVisible()) ? 1 : 0);
    }

    void exitVideoFullscreen() {
        if (!videoFull_) return;
        videoFull_ = false;
        if (hoverTimer_) hoverTimer_->stop();
        setMasonryFloating(false);                // 结果区还原进分隔器
        setPlayerOnlyVisible(false);
        if (split_ && vfSplitSizes_.size() == 2) split_->setSizes(vfSplitSizes_);   // 还原分栏（结果区宽度）
        if (isFullScreen()) showNormal();
        applyFillMode(true);
        qInfo().noquote() << QString("[VFULL] 退出视频全屏（面板已恢复）");
        std::fprintf(stderr, "[VFULL] 退出视频全屏 窗口全屏=%d 结果面板可见=%d\n",
                     isFullScreen() ? 1 : 0, (results_ && results_->isVisible()) ? 1 : 0);
    }

    void toggleVideoFullscreen() { videoFull_ ? exitVideoFullscreen() : enterVideoFullscreen(); }

    /** Z 键 / 铺满按钮：非全屏时**先给出视频全屏效果**（用户直觉），全屏后再切铺满开关 */
    void toggleFillScreen() {
        if (!videoFull_ && !isFullScreen()) { enterVideoFullscreen(); return; }
        const bool now = settings_.boolean("video.fillScreen", true);
        settings_.set("video.fillScreen", !now);
        applyFillMode(true);
    }

    /** 析构时先摘掉瀑布流的数据源：teardown 时 results_（QListWidget）先于 masonry_ 销毁，
     *  其模型析构发出 modelReset/rowsRemoved → 触发 masonry_->rebuild() 回读半销毁对象 →
     *  退出时段错误（coredump 实证：~QListWidget → modelReset → MasonryView::layout → viewport()）。 */
    ~MainWindow() override {
        if (masonry_) masonry_->setSource(nullptr);
    }

    void togglePiP() {
        pipActive_ = !pipActive_;
        if (pipActive_) {
            pipSavedGeometry_ = geometry();
            if (topCard_) topCard_->hide();
            if (results_) results_->hide();
            // 控制条没有单一容器，直接收起窗口里所有按钮/滑块/标签 —— 画中画只留画面
            for (auto *w : findChildren<QWidget *>())
                if (qobject_cast<QPushButton *>(w) || qobject_cast<QSlider *>(w)
                    || qobject_cast<QLabel *>(w) || qobject_cast<QLineEdit *>(w))
                    w->hide();
            // 注意：对**可见窗口**改 window flag 会让窗口被隐藏，必须再 show()（Qt 文档明说）
            setWindowFlag(Qt::WindowStaysOnTopHint, true);
            show();
            resize(480, 270);
            qInfo() << "[SELFTEST] 已进入画中画（480x270，界面已收起；置顶由合成器决定）";
        } else {
            if (topCard_) topCard_->show();
            if (results_) results_->show();
            for (auto *w : findChildren<QWidget *>())
                if (qobject_cast<QPushButton *>(w) || qobject_cast<QSlider *>(w)
                    || qobject_cast<QLabel *>(w) || qobject_cast<QLineEdit *>(w))
                    w->show();
            setWindowFlag(Qt::WindowStaysOnTopHint, false);
            show();                                            // 同上：改 flag 后要再显示
            if (!pipSavedGeometry_.isNull()) setGeometry(pipSavedGeometry_);
            qInfo() << "[SELFTEST] 已退出画中画";
        }
    }

    /** V 键：自动 → 1080 → 720 → 480 → 360 → 仅音频 循环 */
    void cycleVideoQuality() {
        static const QVector<int> order = Settings::qualityHeights();   // ④ 单一档位表（含 2160/1440）
        const int cur = effectiveVideoHeight();
        int i = order.indexOf(cur);
        if (i < 0) i = 0;
        switchVideoQuality(order.at((i + 1) % order.size()));
    }

    // ---------------- B4：列表缩略图 ----------------
    // 内存缓存 + 异步下载 + **按条目指针守卫**（行可能在后续搜索里重排，避免错图）
    QHash<QString, QPixmap> thumbCache_;
    QNetworkAccessManager *thumbNet_ = nullptr;
    int thumbLoaded_ = 0;

    void setItemThumb(QListWidgetItem *item, const QString &url) {
        if (!item || url.isEmpty()) return;
        // 标记为"卡片"并写入来源（按缩略图域名判定；macOS 卡片下方也有一行来源）
        item->setData(Qt::UserRole + 12, true);
        if (url.contains("ytimg"))          item->setData(Qt::UserRole + 10, QStringLiteral("YouTube"));
        else if (url.contains("hdslb"))     item->setData(Qt::UserRole + 10, QStringLiteral("B站"));
        else if (url.contains("qq.com"))    item->setData(Qt::UserRole + 10, QStringLiteral("QQ音乐"));
        else if (url.contains("126.net"))   item->setData(Qt::UserRole + 10, QStringLiteral("网易云音乐"));
        if (thumbCache_.contains(url)) {                       // 命中缓存
            const QPixmap pm = thumbCache_.value(url);
            if (!pm.isNull()) item->setIcon(QIcon(pm));
            return;
        }
        if (thumbCache_.contains(url)) return;
        thumbCache_.insert(url, QPixmap());                    // 占位，防止重复请求
        if (!thumbNet_) thumbNet_ = new QNetworkAccessManager(this);
        QNetworkRequest req{QUrl(url)};
        req.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (X11; Linux x86_64) Chrome/120.0 Safari/537.36");
        if (url.contains("hdslb")) req.setRawHeader("Referer", "https://www.bilibili.com/");
          else if (url.contains("ytimg")) req.setRawHeader("Referer", "https://www.youtube.com/");
          else if (url.contains("qq.com")) req.setRawHeader("Referer", "https://y.qq.com/");
          else if (url.contains("126.net")) req.setRawHeader("Referer", "https://music.163.com/");
        QNetworkReply *r = thumbNet_->get(req);
        connect(r, &QNetworkReply::finished, this, [this, r, url, item] {
            r->deleteLater();
            if (r->error() != QNetworkReply::NoError) { thumbCache_.remove(url); return; }
            QPixmap pm;
            if (!pm.loadFromData(r->readAll())) { thumbCache_.remove(url); return; }
            pm = pm.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation);  // 卡片 160×90 显示 → 2× 缓存（HiDPI）
            thumbCache_.insert(url, pm);
            ++thumbLoaded_;
            if (thumbLoaded_ == 1 || thumbLoaded_ % 10 == 0)
                qInfo() << "缩略图已加载" << thumbLoaded_ << "张";
            // 搜索可能已经清空列表 → 反查条目是否还在（不依赖 QPointer）
            if (item && results_->row(item) >= 0) item->setIcon(QIcon(pm));
        });
    }

    /** 在线缩略图地址：YouTube 由 id 推出；B站用返回的 pic；QQ 用 albummid */
    static QString youTubeThumb(const QString &id) { return QString("https://i.ytimg.com/vi/%1/mqdefault.jpg").arg(id); }
    static QString qqThumb(const QString &albumMid) { return albumMid.isEmpty() ? QString()
        : QString("https://y.gtimg.cn/music/photo_new/T002R300x300M000%1.jpg").arg(albumMid); }

    void followSourceFor(const QString &service) {
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

    /** B站搜索（官方接口；结果点击后走统一的在线解析 + CC 字幕路径） */
    void biliSearchPublic(const QString &k, bool play) { biliSearch(k, play); }
    /** A0：从系统浏览器导入 cookie（登录菜单与 --import-cookies CLI 共用同一条实现）
     *  产物包（AppImage/Flatpak）没有 WebEngine，这条路径就是它们的"登录"方式。 */
    QString importCookiesFromBrowser(const QString &browser) {
        QString summary;
        const bool ok = CookieImport::importFromBrowser(
            browser, "https://www.bilibili.com/video/BV1GJ411x7h7", &summary);
        const QString msg = ok ? QString("cookie 导入完成：%1").arg(summary)
                               : QString("cookie 导入失败：%1").arg(summary);
        if (results_) results_->addItem(msg);
        std::cout << msg.toStdString() << std::endl;   // CLI 直接可读
        return msg;
    }
    void togglePiPPublic() { togglePiP(); }
    void showFullScreenPublic() { enterVideoFullscreen(); }   // CLI --fullscreen：与双击/F 同一语义

    /// CLI 探针 --fs-probe：进视频全屏 → 强制浮出侧栏 → 打印可判定状态 → 收起 → 退出
    /// （与 macOS 的 --fs-probe 同名同义；探针期间**停掉鼠标轮询**，否则真实鼠标会覆盖强制状态）
    void fsProbePublic(const QString &tag) {
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
    void setFillScreenPublic(bool on) { settings_.set("video.fillScreen", on); applyFillMode(true); }
    /** 自动化：按来源搜索（0=YouTube 1=B站 2=网易云 3=QQ） */
    void searchPublic(const QString &kw, int src) {
        if (srcBox_) srcBox_->setCurrentIndex(qBound(0, src, srcBox_->count() - 1));
        if (search_) search_->setText(kw);
        doSearch();
    }
    /** 自动化探针：连续触发 n 次"加载更多"（每隔 6 秒一次）——等价用户反复拖到底。
     *  与项目里既有的 --queue-selftest / --dump-frame 等同属自动化入口，可脚本化验收。 */
    void loadMoreForTest(int n) {
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

    /** 连播压测探针：--play-storm <次数> [间隔毫秒]（配合 --yt/--bili 搜索使用）。
     *  等价用户快速连点搜索结果：每次都走完整 解析→在线字幕→播放 链路。
     *  fd 泄漏排查的复现入口，与 --load-more / --queue-selftest 同属自动化探针。 */
    void playStormForTest(int n, int intervalMs) {
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

    void biliSearch(const QString &keyword, bool autoPlayFirst = false) {
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

    /** 网易云音乐：搜索并播放第一条（自动化入口，也是后续音乐 UI 的基础） */
    void musicSearch(const QString &keyword) {
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
            // 逐条尝试取地址：网易云里有些曲目（混音版/下架曲/版权受限）拿不到直链，
            // 失败就自动换下一条，最多试 5 条 —— 避免"第一条恰好不可播"就整体失败。
            auto attempt = std::make_shared<std::function<void(int)>>();
            *attempt = [this, api, songs, attempt](int idx) {
                if (idx >= songs.size() || idx >= 5) {
                    results_->addItem("网易云：前 5 条都取不到播放地址（可能需要登录）");
                    api->deleteLater();
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
                    beginPlayback("netease:" + s.id, lb);   // 同上：自动播放也要立起 playKey_
                    lastArtist_ = s.artist; lastAlbum_ = s.album;
                    lastCoverUrl_ = coverMap_.value("netease:" + s.id);   // 网易云搜索不带封面，播放时另取
                    results_->addItem(QString("正在播放：%1").arg(lb));
                    applyCoverFor("netease:" + s.id, lb);
                    lastStreamUrl_ = url; lastStreamTitle_ = QString("%1 — %2").arg(s.name, s.artist);
                    player_->playResolved(url, QString());
                    loadLyricsFor(s.id, QString("%1 — %2").arg(s.name, s.artist), api);
                });
            };
            rebuildQueueFromList();
            (*attempt)(0);
        });
    }

    /** 自动化入口：指定字幕轨序号（-1=关闭）与字幕延迟（秒） */
    void applySubtitleTrackIndex(int n) { player_->setPreferredSubtitleTrack(n); }
    void applySubtitleDelay(double sec) { player_->adjustSubtitleDelay(sec); }

    void openFiles(const QStringList &files) {
        // 单个网页地址：走在线解析（与搜索结果同一条路径 —— 解析直链 + 抓在线字幕）
        // 之前无论传什么都当本地文件直接 playResolved，导致 --open <网址> 把网页当媒体流，
        // 在线字幕自然一条也拿不到（实测踩到，见踩坑 #66）
        if (files.size() == 1 && isWebUrl(files.first())) { playItem(files.first(), files.first()); return; }
        playlist_.setFiles(files);
        playCurrent();
    }
    static bool isWebUrl(const QString &s) { return s.startsWith("http://") || s.startsWith("https://"); }
    void openLocal(const QString &f) {
        if (isWebUrl(f)) { playItem(f, f); return; }        // 同上：手动输入的网址也走在线路径
        results_->addItem("正在播放本地文件: " + QFileInfo(f).fileName());
        beginPlayback(f, QFileInfo(f).fileName());
        player_->playResolved(f, QString());
    }
    void selfTest(const QString &f) { player_->playResolved(f, QString()); }

protected:
    /** 视频全屏：双击画面 / 单击已在前端处理（mpv 不抢鼠标事件） */
    /// 双击画面 → 视频全屏（窗口级兜底：即便 GL 子控件吞了事件，冒泡到窗口也能生效）
    void mouseDoubleClickEvent(QMouseEvent *e) override {
        if (player_ && player_->isVisible() && player_->geometry().contains(e->position().toPoint())) {
            toggleVideoFullscreen();
            e->accept();
            return;
        }
        QMainWindow::mouseDoubleClickEvent(e);
    }

    /// 玻璃：首次显示时向合成器申请模糊（KWin Wayland 的 org_kde_kwin_blur 与 X11 原子都经此转发）
    void showEvent(QShowEvent *e) override {
        QMainWindow::showEvent(e);
#ifdef HOV_HAVE_KWINDOWSYSTEM
        if (!blurRequested_) {
            blurRequested_ = true;
            const bool on = testAttribute(Qt::WA_TranslucentBackground);   // 与窗口属性同源，不再各自判断
            // KF6 的签名是 QWindow*（KF5 是 WId）—— 用 windowHandle() 才能同时兼容
            KWindowEffects::enableBlurBehind(windowHandle(), on);
            // 有些合成器要等窗口映射完成才接受模糊区域 → 300ms 后再申一次（幂等）
            QTimer::singleShot(300, this, [this, on] {
                KWindowEffects::enableBlurBehind(windowHandle(), on);
            });
            std::fprintf(stderr, "[GLASS] 已向合成器申请窗口模糊=%d（KWindowEffects；映射后再申一次）\n", on ? 1 : 0);
        }
#endif
    }

    bool eventFilter(QObject *o, QEvent *e) override {
        if (o == player_ && e->type() == QEvent::MouseButtonDblClick) { toggleVideoFullscreen(); return true; }
        return QMainWindow::eventFilter(o, e);
    }

    /** 桌面键盘快捷键：空格播放暂停 / ←→ 快进退 / ↑↓ 音量 / F 全屏 / Esc 退出全屏 */
    void keyPressEvent(QKeyEvent *e) override {
        switch (e->key()) {
        case Qt::Key_Space:  player_->togglePause(); refreshControls(); break;
        case Qt::Key_Left:   seekBy(-5); break;
        case Qt::Key_Right:  seekBy(5); break;
        case Qt::Key_Up:     setVol(player_->volume() + 5); break;
        case Qt::Key_Down:   setVol(player_->volume() - 5); break;
        case Qt::Key_F:      toggleVideoFullscreen(); break;                    // F=视频全屏（收起面板）
        case Qt::Key_Z:      toggleFillScreen(); break;                    // Z=全屏铺满开关
        case Qt::Key_S:      player_->toggleSubtitles(); break;   // 字幕/歌词开关
        case Qt::Key_C:      player_->cycleSubtitleTrack(); break;      // 切换字幕轨（含关闭）
        case Qt::Key_Comma:  player_->adjustSubtitleDelay(-0.5); break;  // 字幕提前 0.5s
        case Qt::Key_Period: player_->adjustSubtitleDelay(0.5); break;   // 字幕延后 0.5s
        case Qt::Key_A:      toggleFavoriteCurrent(); break;             // A=收藏当前项（F 已是全屏）
        case Qt::Key_M:      queue_.cycleMode(); results_->addItem(QString("[队列] 模式：%1").arg(queue_.modeLabel())); break;
        case Qt::Key_Q:      cycleQuality(); break;                  // Q=音质（标准/较高/无损，播放中切会回到原进度）
        case Qt::Key_V:      cycleVideoQuality(); break;                  // V=视频清晰度（自动/1080/720/480/360/仅音频）
        case Qt::Key_P:      togglePiP(); break;                          // P=画中画（小窗+收起界面）
        case Qt::Key_O:      lib_.cycleSort(); showLocalLibrary(); break;   // O=本地库排序（名称/时间/大小）
        case Qt::Key_R:      renameSelectedLocal(); break;           // R=重命名选中的本地库文件
        case Qt::Key_Escape:                                            // Esc=退出视频全屏
            if (videoFull_) {
                exitVideoFullscreen();
            } else if (isFullScreen()) {
                showNormal();
            }
            break;
        default: QMainWindow::keyPressEvent(e); return;
        }
        e->accept();
    }

    void seekBy(double d) {
        const double dur = player_->durationSec();
        if (dur <= 0) return;
        player_->seekTo(qBound(0.0, player_->positionSec() + d, dur - 0.5));
        refreshControls();
    }
    void setVol(int v) {
        v = qBound(0, v, 130);
        player_->setVolume(v);
        vol_->setValue(v);
    }

private:
    Playlist playlist_;
    UrlResolver *resolver_ = nullptr;
    Settings settings_;      // 设置（~/.config/hov/settings.json）
    PlayQueue queue_;        // 播放队列（顺序/单曲/随机）
    ProgressStore prog_;     // 进度记忆（~/.config/hov/progress.json）
    QString playKey_;        // 当前播放项的解析标识（进度记忆的键）
    QString playLabel_;      // 标题（仅用于展示）
    QString playTitle_;      // MPRIS 用：标题
    QString playArtist_;     // MPRIS 用：艺术家
    bool metaSent_ = false;  // 元数据是否已随时长报过
    Mpris *mpris_ = nullptr; // 桌面媒体键/锁屏控件接口
    double pendingResume_ = 0.0;    // 待续播位置（等媒体加载完再 seek）
    qint64 lastProgRemember_ = 0;   // 记录节流（毫秒）
    qint64 lastProgSave_ = 0;       // 落盘节流（毫秒）
    bool resumeEnabled_ = true;     // --no-resume 时关闭（自动化测试用）
    QString resolutionKey_;         // 最近一次在线视频的页面地址（切清晰度要重新解析它）
    QString sessionQuality_;        // 会话内音质档位（空 = 跟随设置；Q 键 / --quality 会改它）
    QString playPlatform_;          // 当前播放内容所属平台（状态栏 / 列表提示显示）
    QFrame *topCard_ = nullptr;     // B5：画中画时隐藏
    QLabel *statusLabel_ = nullptr;  // v1.2.0：状态行（结果区只放卡片）
    MasonryView *masonry_ = nullptr;  // v1.2.0：真·瀑布流结果视图（results_ 作为数据源被隐藏）

    // ── 搜索结果分页（用户所说的"瀑布流"：滚到底自动加载下一页）────────────
    // 完全对齐 macOS/Android 三处约定：每页 **20** 条、**无开关**（始终开启）、页数上限 **YouTube 6 / 其它 5**。
    // 翻页策略也与 macOS 相同："多取后截尾" —— 按 20*page 条去请求，只追加没见过的条目（URL 去重）。
    QString moreKw_;
    int  moreSrc_ = -1;
    int  morePage_ = 1;
    bool moreHasMore_ = true;
    bool moreLoading_ = false;
    int  moreReqId_ = 0;            // 代次：换词/换源后旧回调一律丢弃（防竞态）
    QSet<QString> moreSeen_;        // 已出现过的条目 URL
    static int maxPageFor(int src) { return src == 0 ? 6 : 5; }   // 对齐 macOS
    void noteSearchContext(const QString &kw, int src) {           // 每个搜索入口都要调用
        std::fprintf(stderr, "[MORE] 会话建立: kw=%s src=%d\n", qPrintable(kw), src);
        moreKw_ = kw; moreSrc_ = src; morePage_ = 1; moreHasMore_ = true;
        moreLoading_ = false; moreSeen_.clear(); ++moreReqId_;
    }
    void setStatusLine(const QString &t) {
        if (statusLabel_) { statusLabel_->setText(t); statusLabel_->setVisible(true); }
    }
    void syncSeenFromList() {                                      // 懒同步：把列表已有 URL 记为"已见"
        if (!results_) return;
        for (int i = 0; i < results_->count(); ++i)
            if (auto *it = results_->item(i)) {
                const QString u = it->data(Qt::UserRole).toString();
                if (!u.isEmpty()) moreSeen_.insert(u);
            }
    }
    /** 追加"没见过"的条目；返回新增条数（对齐 macOS 的"多取后截尾"） */
    int appendNewRows(const QVector<QPair<QString, QString>> &rows,
                      const QVector<QPair<QString, QString>> &thumbs = {}) {
        int added = 0;
        for (int i = 0; i < rows.size(); ++i) {
            const QString url = rows[i].second;
            if (url.isEmpty()) continue;
            if (rows[i].first.trimmed().isEmpty()) continue;   // yt-dlp 偶有无标题条目 → 不要建成空卡片
            if (moreSeen_.contains(url)) continue;
            moreSeen_.insert(url);
            auto *it = new QListWidgetItem(rows[i].first, results_);
            it->setData(Qt::UserRole, url);
            if (i < thumbs.size() && !thumbs[i].second.isEmpty()) setItemThumb(it, thumbs[i].second);
            ++added;
        }
        return added;
    }
    void finishMore(int reqId, int added) {
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
    void fetchMorePage(int src, const QString &kw, int page) {
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
            });
            return;
        }
        if (src == 3) {                                           // QQ音乐（异步）
            auto *api = new QQMusicApi(this);
            api->search(kw, want, [this, api, reqId](const QVector<QQMusicApi::Song> &songs) {
                api->deleteLater();
                if (reqId != moreReqId_) return;
                QVector<QPair<QString, QString>> rows;
                for (const auto &so : songs)
                    rows.append({QString("%1 — %2").arg(so.name, so.artist), "qq:" + so.mid});
                finishMore(reqId, appendNewRows(rows));
            });
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
                            "ytsearch" + QString::number(want) + ":" + kw});
    }
    /** 滚到底触发：与 macOS loadMoreIfNeeded 等价 */
    void loadMore() {
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

    QSplitter *split_ = nullptr;    // B5：画中画时隐藏
    bool pipActive_ = false;        // B5：画中画状态
    bool blurRequested_ = false;    // 玻璃：是否已向合成器申请过模糊（只申请一次）
    ThemeWatcher *themeWatcher_ = nullptr;   // ui.theme=auto 时跟随系统 light/dark
    // ── 全屏浮出侧栏（ui.hoverReveal）──
    static constexpr int kEdgeBand = 12;   // 边缘感应带宽（像素，唯一真相）
    QFrame *masonryHost_ = nullptr;        // 浮层框（进入全屏时 masonry_ 挂进来）
    QTimer *hoverTimer_ = nullptr;         // 鼠标轮询（仅全屏时运行）
    qint64 hideDeadline_ = 0;              // 离开边缘后的收起时刻（400ms 防抖）
    bool sidebarRevealed_ = false;         // 浮层是否展开
    int vfMasonryWidth_ = 0;               // 进入全屏前的结果区宽度（退出按它还原）
    // 初值必须与【非全屏】的初始状态一致（false）。设 true 会导致启动后第一拍就下发一次，
    // 而那一拍恰好撞上 mpv 打开视频文件的窗口期，同步的 mpv_get_property 会阻塞 GUI 线程 -> 播放卡死。
    bool lastFillApplied_ = false;  // B7：上次下发的铺满状态
    QPushButton *fillBtn_ = nullptr;// B7：控制条「铺满」开关
    QRect pipSavedGeometry_;
    int sessionVideoHeight_ = 0;    // 会话内视频清晰度（0=自动，-1=仅音频；V 键 / --video-quality 会改）
    DownloadManager *dl_ = nullptr;   // 下载管理（并发 2 + 重试）
    LocalLibrary lib_;                   // 本地库（下载目录）
    QHash<QString, QString> coverMap_;   // 播放键 → 封面地址（搜索时顺便记录）
    QHash<QString, QString> titleMap_;   // 播放键 → 标题/艺术家（封面下方文字）
    QString lastStreamUrl_;           // 当前播放的直链（下载按钮用）
    QString lastArtist_, lastAlbum_, lastCoverUrl_;   // 下载时嵌入的音乐标签
    QString lastAudioUrl_;            // ③ 当前视频的独立音轨直链（下载时用 ffmpeg 混流，否则下载无声）

    // ── 直链过期自愈（与 Android 端同法；macOS 端同样需要）──────────────────
    // 现象：暂停很久后继续播 → 播一会儿停住 → 手动**切清晰度**才能继续（用户实测）。
    // 根因：解析出的直链有有效期（googlevideo / B站 数小时失效）；切档会重新解析 → 与观察吻合。
    qint64 resolvedAtMs_ = 0;      // 本直链解析时刻
    bool   healTried_ = false;     // 每条内容最多自愈一次（防抖）
    double lastPos_ = -1;          // 停滞检测：上次进度
    qint64 lastPosAtMs_ = 0;
    /** 保守 TTL（按来源；本地文件与未知来源视为不过期） */
    qint64 ttlMs() const {
        const QString svc = UrlResolver::serviceOf(resolutionKey_);
        if (svc == "youtube")  return 4LL * 60 * 60 * 1000;
        if (svc == "bilibili") return 2LL * 60 * 60 * 1000;
        if (svc == "netease" || svc == "qqmusic") return 30LL * 60 * 1000;
        return std::numeric_limits<qint64>::max();
    }
    /** 过期自愈：走"切清晰度"同一条链路（重新解析 + 用 pendingResume_ 回到原位置） */
    void healIfExpired(bool force = false) {
        if (healTried_ || resolutionKey_.isEmpty()) return;
        const qint64 age = QDateTime::currentMSecsSinceEpoch() - resolvedAtMs_;
        if (!force && (resolvedAtMs_ == 0 || age < ttlMs())) return;
        healTried_ = true;
        qInfo() << "[HEAL] 直链已" << age / 1000 << "秒（> TTL" << ttlMs() / 1000 << "秒）→ 重新解析";
        std::fprintf(stderr, "[HEAL] 直链过期 %llds → 重新解析并回原位\n", static_cast<long long>(age / 1000));
        setStatusLine("直链已过期，正在重新解析…");
        switchVideoQuality(effectiveVideoHeight());      // 内含重新解析 + pendingResume_ 回原进度
    }
    /** 停滞看门狗：播放中且进度 15 秒不推进（非暂停）→ 自愈一次 */
    void startStallWatch() {
        auto *t = new QTimer(this);
        t->setInterval(5000);
        connect(t, &QTimer::timeout, this, [this] {
            if (!player_ || player_->paused()) return;
            const double pos = player_->positionSec();
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (pos >= 0 && std::abs(pos - lastPos_) > 0.05) { lastPos_ = pos; lastPosAtMs_ = now; return; }
            if (lastPosAtMs_ > 0 && now - lastPosAtMs_ > 15000) {
                std::fprintf(stderr, "[HEAL] 进度停滞 15s（pos=%.1f）→ 尝试自愈\n", pos);
                lastPosAtMs_ = now;                       // 避免反复触发（healTried_ 兜底）
                healIfExpired(true);
            }
        });
        t->start();
    }
    QString lastLRC_;                 // ③ 当前曲目的歌词原文（下载时写同名 .lrc 侧车）
    QString lastStreamTitle_;
    Favorites favs_;         // 收藏（~/.config/hov/favorites.json）

    /** 播放列表当前项 */
    void playCurrent() {
        const QString f = playlist_.current();
        if (f.isEmpty()) return;
        results_->addItem("正在播放: " + playlist_.displayName());
        beginPlayback(f, playlist_.displayName());
        player_->playResolved(f, QString());
        refreshControls();
    }
    void playNextFile() { playlist_.next(); playCurrent(); }
    void playPrevFile() { playlist_.prev(); playCurrent(); }

    QPushButton *speedBtn_ = nullptr;
    QPushButton *btnPlay_ = nullptr;
    QSlider *seek_ = nullptr;
    QSlider *vol_ = nullptr;
    QLabel *labelTime_ = nullptr;
    QTimer *ctlTimer_ = nullptr;

    static QString fmtTime(double sec) {
        if (sec < 0 || !qIsFinite(sec)) sec = 0;
        const int t = (int)sec;
        return QString("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QChar('0'));
    }

    /** 定时刷新播放控件（用户拖动进度条时不打断） */
    void refreshControls() {
        const double pos = player_->positionSec(), dur = player_->durationSec();
        if (!seek_->isSliderDown() && dur > 0) {
            seek_->blockSignals(true);
            seek_->setValue((int)(pos / dur * 1000));
            seek_->blockSignals(false);
        }
        labelTime_->setText(fmtTime(pos) + " / " + fmtTime(dur));
        btnPlay_->setText(player_->paused() ? "\u25b6" : "\u23f8");
    }

    // 启动探活：判断国内音乐接口是否可达；失败时给出"指名域名 + 规则片段"的可执行提示
    void probeNetwork() {
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

    void doSearch() {
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
        p->start("yt-dlp", {"-J", "--flat-playlist", "--no-warnings",
                            "--playlist-end", "15", "ytsearch15:" + q});
    }
};

// 数值 locale 必须是 C：Qt/Chromium/mpv/ffmpeg 内部都按 C 解析小数点。
// 中文系统常见 LC_NUMERIC=zh_CN.UTF-8 —— 某些库（尤其 QtWebEngine 的 Chromium）会在**初始化阶段**
// 直接段错误。这里用 init-priority 构造函数尽量早地设定（比 main() 更早），main() 内再兜一次。
#include <clocale>
#include <unistd.h>   // execv（locale 自重执行修复）
#include <cstdlib>
#include <vector>
#include <cstring>
__attribute__((constructor(101)))
static void hov_force_c_numeric_locale() {
    std::setlocale(LC_NUMERIC, "C");

    // 与启动器同源的双保险：禁 GLib 事件分发器（Qt 6.10+ 的该回归会在线程里 glib abort）
    qputenv("QT_NO_GLIB", "1");
}

#ifdef __linux__
// HOV_QPA=<平台>：手动指定 Qt 平台插件（排障旁路，如 HOV_QPA=xcb 强制走 XWayland）。
// 正常不需要：Wayland 下 libmpv 逐帧渲染的 sync_file fence fd 泄漏（virgl 驱动每帧一个，
// ≈视频帧率 +30~40/s，数小时打满 fd 上限 → 事件分发器建管道失败 abort）已由
// MpvWidget 的 glFenceSync 记账 shim 根治（见 MpvWidget.h“glFenceSync 泄漏防护”注释，
// 上游 mpv f74adc4 亦已修、将随 0.42 发布）。原生 Wayland 可放心使用。
#endif

int main(int argc, char **argv) {
#ifdef __linux__
    // ── libmpv 的数值 locale 崩溃修复（Arch 端实测复现）────────────────────────────
    // 现象：系统 LC_NUMERIC 非 C（如中文环境 zh_CN.UTF-8）时，**任何**子命令都直接段错误（rc=139），
    //       连 --show-settings 也一样；报错为 libmpv 自带的
    //       "Non-C locale detected. This is not supported. Call 'setlocale(LC_NUMERIC, \"C\");'"
    // 原因：libmpv 在**初始化阶段**就检查数值 locale，此时进程内的 setlocale 已经来不及
    //       （构造函数/达 main 都太晚，实测均无效）→ 只能用正确的环境**重新 exec 自己**。
    // 处理：仅把 LC_NUMERIC 设为 C（其它类别不动 → 界面语言照旧），并加标记位防止无限递归。
    // ⚠ 判定必须看**环境变量**而不是 setlocale 当前值：进程启动时 C 库的 locale
    // 恒为 "C"（环境还没 apply），用 setlocale(LC_NUMERIC, nullptr) 判断永远为假 ——
    // 旧写法因此从不 re-exec，而 QApplication 构造时会 setlocale(LC_ALL, "") 把
    // LC_NUMERIC 重新拉回环境值（如 zh_CN.UTF-8）→ libmpv 初始化直接 abort
    //（本轮实测：LC_NUMERIC=en_US.UTF-8 直启 hov-qt 必 core dump）。
    // 按 POSIX 优先级取生效值：LC_ALL > LC_NUMERIC > LANG。
    {
        auto effectiveNumeric = []() -> const char * {
            if (const char *v = std::getenv("LC_ALL"); v && *v) return v;
            if (const char *v = std::getenv("LC_NUMERIC"); v && *v) return v;
            if (const char *v = std::getenv("LANG"); v && *v) return v;
            return "C";
        };
        const char *cur = effectiveNumeric();
        const bool isC = std::strcmp(cur, "C") == 0 || std::strcmp(cur, "POSIX") == 0;
        if (!isC && std::getenv("HOV_LOCALE_REEXEC") == nullptr) {
            // LC_ALL 优先级高于 LC_NUMERIC：脏的话必须先清掉（置空=按 unset 处理），
            // 否则只设 LC_NUMERIC 不生效（启动器同款处理，见 hov-qt-launcher.sh.in）。
            if (const char *v = std::getenv("LC_ALL"); v && *v) setenv("LC_ALL", "", 1);
            setenv("LC_NUMERIC", "C", 1);
            setenv("HOV_LOCALE_REEXEC", "1", 1);
            std::vector<char *> av;
            av.reserve(static_cast<size_t>(argc) + 1);
            for (int i = 0; i < argc; ++i) av.push_back(argv[i]);
            av.push_back(nullptr);
            execv("/proc/self/exe", av.data());      // 失败则继续跑（保持原行为，不会更糟）
        }
    }
#endif

#ifdef __linux__
    // HOV_QPA=<平台>：手动指定 Qt 平台插件（见上方注释）；用户显式设了 QT_QPA_PLATFORM 时尊重用户。
    if (std::getenv("QT_QPA_PLATFORM") == nullptr) {
        if (const char *q = std::getenv("HOV_QPA"); q && *q) qputenv("QT_QPA_PLATFORM", q);
    }
#endif

    // 保留用户环境的语言（LC_CTYPE 等），只把**数值**格式统一成 C（见上方构造函数注释）
    std::setlocale(LC_ALL, "");
    std::setlocale(LC_NUMERIC, "C");

    // 纯命令行维护动作（导入 cookie / 清理下载 / 进度查询 / 设置读写）不需要图形界面。
    // 在服务器、SSH、CI 等没有显示环境的地方，Qt 会因为连不上显示而直接 abort；
    // 这里提前把这类调用切到 offscreen 平台，让脚本/产物包也能用（用户桌面上仍用真实平台）。
    {
        static const QStringList kHeadlessCmds{
            "--import-cookies", "--cleanup-downloads", "--progress-show", "--progress-clear",
            "--show-settings", "--set", "--queue-selftest"};
        bool headlessCmd = false;
        for (int i = 1; i < argc; ++i)
            if (kHeadlessCmds.contains(QString::fromLocal8Bit(argv[i]))) { headlessCmd = true; break; }
        const bool hasDisplay = qEnvironmentVariableIsSet("WAYLAND_DISPLAY")
                                || qEnvironmentVariableIsSet("DISPLAY");
        if (headlessCmd && !hasDisplay && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    Theme::apply(app);   // 统一深色主题（主窗口/对话框/播放器同一套观感）
    MainWindow w;
    w.show();
    // 自动化验证：--demo "<关键词>" 启动即搜索（供无人值守截图/回归用）
    const auto args = app.arguments();
    const int di = args.indexOf("--demo");
    if (args.contains("--autoplay")) w.setAutoplay(true);
    const int si = args.indexOf("--speed");
    if (si > 0 && si + 1 < args.size()) {
        bool ok = false;
        const double sp = args[si + 1].toDouble(&ok);
        if (ok && sp >= 0.25 && sp <= 4.0)
            QMetaObject::invokeMethod(&w, [&w, sp] { w.applySpeed(sp); }, Qt::QueuedConnection);
    }
    const int oi = args.indexOf("--open");
    if (oi > 0 && oi + 1 < args.size()) {
        QStringList files;
        for (int i = oi + 1; i < args.size(); ++i) {
            if (args[i].startsWith("--")) break;
            files << args[i];
        }
        QMetaObject::invokeMethod(&w, [&w, files] { w.openFiles(files); }, Qt::QueuedConnection);
    }
    // 自动化参数：--sub-track N（字幕轨序号，-1=关闭）/ --sub-delay SEC（字幕延迟）
    // 说明：无头 VM 里键盘注入不可靠，故提供与 --speed 同风格的命令行入口
    {
        const int ai = args.indexOf("--sub-track");
        if (ai > 0 && ai + 1 < args.size()) {
            bool ok = false;
            const int n = args[ai + 1].toInt(&ok);
            if (ok) QMetaObject::invokeMethod(&w, [&w, n] { w.applySubtitleTrackIndex(n); }, Qt::QueuedConnection);
        }
        const int di = args.indexOf("--sub-delay");
        if (di > 0 && di + 1 < args.size()) {
            bool ok = false;
            const double v = args[di + 1].toDouble(&ok);
            if (ok) QMetaObject::invokeMethod(&w, [&w, v] { w.applySubtitleDelay(v); }, Qt::QueuedConnection);
        }
    }
    // 登录窗口：--login <站点>（youtube/bilibili/netease/qqmusic）
    {
        const int li = args.indexOf("--login");
        if (li > 0) {
            const QString site = (li + 1 < args.size() && !args[li + 1].startsWith("--"))
                                     ? args[li + 1] : QString("youtube");
            QMetaObject::invokeMethod(&w, [&w, site] { w.openLoginDialog(site); }, Qt::QueuedConnection);
        }
    }

    // 设置窗口：--settings 打开（人工验证用）
    if (args.contains("--settings"))
        QMetaObject::invokeMethod(&w, [&w] { w.openSettingsDialog(); }, Qt::QueuedConnection);

    // 进度记忆：--progress-show（列出）/ --progress-clear（清空）—— 纯查询/维护，完事就退出
    // 下载目录 LRU 清理：--cleanup-downloads（纯维护操作，执行完退出）
    // A0：--import-cookies <浏览器> —— 与 macOS 端同名同语义；产物包（无 WebEngine）的登录路径
    if (const int ci = args.indexOf("--import-cookies"); ci > 0) {
        const QString browser = (ci + 1 < args.size() && !args[ci + 1].startsWith("--"))
                                    ? args[ci + 1] : QString("firefox");
        const QString msg = w.importCookiesFromBrowser(browser);
        return msg.startsWith("cookie 导入完成") ? 0 : 1;
    }
    if (args.contains("--cleanup-downloads")) {
        const int mb = args.indexOf("--max-total-mb");
        const qint64 limit = ((mb > 0 && mb + 1 < args.size()) ? args[mb + 1].toLongLong()
                                                               : static_cast<qint64>(w.downloadMaxTotalMb()))
                             * 1024 * 1024;
        const auto r = w.cleanupDownloads(limit);
        std::cout << "---- 下载目录 LRU 清理 ----\n"
                  << "清理前: " << r.beforeBytes / 1024 / 1024 << " MB\n"
                  << "清理后: " << r.afterBytes / 1024 / 1024 << " MB（上限 " << limit / 1024 / 1024 << " MB）\n"
                  << "删除 " << r.removed.size() << " 个文件\n";
        for (const QString &p : r.removed) std::cout << "  - " << p.toStdString() << std::endl;
        return 0;
    }
    if (args.contains("--progress-show") || args.contains("--progress-clear")) {
        if (args.contains("--progress-clear")) {
            w.clearProgress();
            std::cout << "(进度记录已清空: " << ProgressStore::filePath().toStdString() << ")" << std::endl;
        } else {
            std::cout << w.dumpProgress().toStdString();
        }
        return 0;
    }
    if (args.contains("--no-resume")) w.setResumeEnabled(false);   // 自动化测试用：不做续播
    if (args.contains("--local")) {                                 // 打开本地库（下载目录）
        QMetaObject::invokeMethod(&w, [&w] { w.applySource(3); w.showLocalLibrary(); }, Qt::QueuedConnection);
    }
    // 本地库重命名（自动化：--rename-selected <新名>，与界面 R 键同一条逻辑）
    if (const int ri = args.indexOf("--rename-selected"); ri > 0 && ri + 1 < args.size()) {
        const QString nm = args[ri + 1];
        qInfo() << "[CLI] --rename-selected" << nm << "（排队执行）";
        QMetaObject::invokeMethod(&w, [&w, nm] {
            qInfo() << "[CLI] 执行重命名";
            w.renameSelectedLocal(nm);
        }, Qt::QueuedConnection);
    }
    // 音质：--quality <standard|exhigh|lossless> 立即设档位（起播就用它）；
    //       --cycle-quality-after <秒> 播放 N 秒后执行一次「Q 键的循环切档」——
    //       与用户按 Q 完全同一条代码路径，便于自动化验证"播放中切音质"
    const int qi = args.indexOf("--quality");
    const QString qWant = (qi > 0 && qi + 1 < args.size()) ? args[qi + 1] : QString();
    qInfo() << "[CLI] --quality 参数=" << qWant << " （空=未指定）";
    if (!qWant.isEmpty())
        QMetaObject::invokeMethod(&w, [&w, qWant] { w.switchQualityTo(qWant); }, Qt::QueuedConnection);
    if (const int si = args.indexOf("--cycle-quality-after"); si > 0 && si + 1 < args.size()) {
        bool ok = false;
        const int secs = args[si + 1].toInt(&ok);
        if (ok && secs > 0) QTimer::singleShot(secs * 1000, &w, [&w] { w.cycleQuality(); });
    }
    // 在线字幕直取：--subs-url <地址>（srt/vtt/json，B站 CC JSON 也能直接给）
    if (const int si = args.indexOf("--subs-url"); si > 0 && si + 1 < args.size()) {
        const QString u = args[si + 1];
        QMetaObject::invokeMethod(&w, [&w, u] { w.applySubsUrl(u); }, Qt::QueuedConnection);
    }
    // 退出前把进度落盘（关窗口 / aboutToQuit 都会走到）
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &w, [&w] { w.saveProgressNow(); });

    // 设置：--set key=value（可重复）/ --show-settings
    {
        const QStringList all = args;
        bool anySet = false;
        for (int i = 1; i + 1 < all.size(); ++i) {
            if (all[i] == "--set") {
                const QString kv = all[i + 1];
                const int eq = kv.indexOf('=');
                if (eq > 0) {
                    const QString k = kv.left(eq), v = kv.mid(eq + 1);
                    const bool changed = w.applySetting(k, v);
                    const bool valid = Settings::validate(k, v).isEmpty();
                    std::cout << (valid ? (changed ? "[set]    " : "[nochange] ") : "[reject] ")
                              << k.toStdString() << " = " << v.toStdString() << std::endl;
                    anySet = true;
                }
                ++i;
            }
        }
        // 纯配置用法（只带 --set / --show-settings，没有别的动作参数）→ 执行完就退出，
        // 否则 GUI 应用会常驻，脚本里会一直挂着（实测踩过）。
        static const char *kActions[] = { "--open", "--music", "--qqmusic", "--download", "--download-music",
                                          "--demo", "--selftest", "--queue-selftest", "--settings",
                                          "--subs-url", nullptr };
        bool hasAction = false;
        for (int i = 0; kActions[i]; ++i)
            if (args.contains(kActions[i])) { hasAction = true; break; }
        const bool query = args.contains("--show-settings");
        if (query || (anySet && !hasAction)) {
            if (query)
                std::cout << "---- settings (" << Settings::filePath().toStdString() << ") ----\n"
                          << w.dumpSettings().toStdString() << std::endl;
            else
                std::cout << "(设置已写入 " << Settings::filePath().toStdString() << "；重启后完全生效)" << std::endl;
            return 0;
        }
    }

    // --exit-after <秒>：自动化用，N 秒后自动退出（避免无头环境下进程常驻）
    if (const int ei = args.indexOf("--exit-after"); ei > 0 && ei + 1 < args.size()) {
        bool ok = false;
        const int secs = args[ei + 1].toInt(&ok);
        if (ok && secs > 0)
            QTimer::singleShot(secs * 1000, &app, &QCoreApplication::quit);
    }

    // 下载：--download <URL> [文件名] / --download-music <关键词>
    {
        const int di = args.indexOf("--download");
        if (di > 0 && di + 1 < args.size()) {
            const QString url = args[di + 1];
            const QString nm = (di + 2 < args.size() && !args[di + 2].startsWith("--")) ? args[di + 2] : QString();
            QMetaObject::invokeMethod(&w, [&w, url, nm] { w.downloadUrl(url, nm); }, Qt::QueuedConnection);
        }
          int count = 1;
        const int ci = args.indexOf("--download-count");
        if (ci > 0 && ci + 1 < args.size()) {
            bool ok = false; const int n = args[ci + 1].toInt(&ok);
            if (ok && n > 0 && n <= 20) count = n;
        }
        const int dmi = args.indexOf("--download-music");
        if (dmi > 0 && dmi + 1 < args.size())
            QMetaObject::invokeMethod(&w, [&w, kw = args[dmi + 1], count] { w.downloadMusic(kw, count); }, Qt::QueuedConnection);
    }

    // 自检：--queue-selftest 跑播放队列与收藏的逻辑断言
    if (args.contains("--queue-selftest"))
        QMetaObject::invokeMethod(&w, [&w] { w.runQueueSelfTest(); }, Qt::QueuedConnection);

    // 界面：--source N 预选搜索来源（0=YouTube 1=网易云 2=QQ音乐；无头环境无法点击下拉框）
    {
        const int si2 = args.indexOf("--source");
        if (si2 > 0 && si2 + 1 < args.size()) {
            bool ok = false;
            const int n = args[si2 + 1].toInt(&ok);
            if (ok) QMetaObject::invokeMethod(&w, [&w, n] { w.applySource(n); }, Qt::QueuedConnection);
        }
    }
    // 音乐：--music <关键词> → 网易云搜索并播放第一条
    {
        // B1：--video-quality <auto|audio|360|480|720|1080…> 与 --cycle-video-quality-after <秒>
    if (const int vi = args.indexOf("--video-quality"); vi > 0 && vi + 1 < args.size()) {
        const QString v = args[vi + 1].toLower();
        int h = 0;
        if (v == "auto") h = 0;
        else if (v == "audio" || v == "audio-only") h = -1;
        else h = v.split('p').first().toInt();
        // 必须**同步**应用：--open 的解析是队列调用，排队的话档位会晚于解析生效（实测踩过）
        w.switchVideoQuality(h);
    }
    if (const int ci2 = args.indexOf("--cycle-video-quality-after"); ci2 > 0 && ci2 + 1 < args.size()) {
        bool ok = false; const int secs = args[ci2 + 1].toInt(&ok);
        if (ok && secs > 0) QTimer::singleShot(secs * 1000, &w, [&w] { w.cycleVideoQuality(); });
    }
    // --fill-screen / --no-fill-screen / --fullscreen / --fullscreen-after <秒>（B7，自动化验证用）
    // 注意：这些动作要等窗口/mpv 就绪后再做（早期同步调用会与 mpv 初始化竞争，实测会挂住）
    if (app.arguments().contains("--fill-screen")) QTimer::singleShot(900, &w, [&w] { w.setFillScreenPublic(true); });
    if (app.arguments().contains("--no-fill-screen")) QTimer::singleShot(900, &w, [&w] { w.setFillScreenPublic(false); });
    if (app.arguments().contains("--fullscreen")) QTimer::singleShot(1500, &w, [&w] {
        qInfo().noquote() << "[CLI] 进入全屏（--fullscreen 触发）"; w.showFullScreenPublic(); });
    if (const int fi = args.indexOf("--fullscreen-after"); fi > 0 && fi + 1 < args.size()) {
        bool ok = false; const int secs = args[fi + 1].toInt(&ok);
        if (ok && secs > 0) QTimer::singleShot(secs * 1000, &w, [&w] {
            qInfo().noquote() << "[CLI] 进入全屏（--fullscreen-after 触发）"; w.showFullScreenPublic(); });
    }
    // --pip-after <秒>：N 秒后进入画中画（自动化验证用）
    if (const int pi = args.indexOf("--pip-after"); pi > 0 && pi + 1 < args.size()) {
        bool ok = false; const int secs = args[pi + 1].toInt(&ok);
        if (ok && secs > 0) QTimer::singleShot(secs * 1000, &w, [&w] { w.togglePiPPublic(); });
    }
    // --yt <关键词>：按来源搜索（自动化；默认 YouTube）
    // CLI 探针：--fs-probe [标签]（验证"全屏浮出侧栏"的显隐状态机，可脚本化验收）
    if (const int fpi = args.indexOf("--fs-probe"); fpi > 0) {
        const QString tag = (fpi + 1 < args.size() && !args[fpi + 1].startsWith("--")) ? args[fpi + 1] : QString("default");
        QTimer::singleShot(12000, &w, [&w, tag] { w.fsProbePublic(tag); });   // 12s：留足搜索出卡片的时间
    }
    if (const int lmi = args.indexOf("--load-more"); lmi > 0 && lmi + 1 < args.size()) {
        const int n = qBound(1, args[lmi + 1].toInt(), 20);
        QMetaObject::invokeMethod(&w, [&w, n] { w.loadMoreForTest(n); }, Qt::QueuedConnection);
    }
    if (const int yi = args.indexOf("--yt"); yi > 0 && yi + 1 < args.size()) {
        QMetaObject::invokeMethod(&w, [&w, kw = args[yi + 1]] { w.searchPublic(kw, 0); }, Qt::QueuedConnection);
    }
    // --play-storm <次数> [间隔毫秒]：连播压测（复现"快速连点"场景，fd 泄漏排查用）
    if (const int psi = args.indexOf("--play-storm"); psi > 0 && psi + 1 < args.size()) {
        const int n = qBound(1, args[psi + 1].toInt(), 500);
        const int iv = (psi + 2 < args.size() && !args[psi + 2].startsWith("--"))
                           ? qMax(500, args[psi + 2].toInt()) : 3000;
        QMetaObject::invokeMethod(&w, [&w, n, iv] { w.playStormForTest(n, iv); }, Qt::QueuedConnection);
    }
    const int bi = args.indexOf("--bili");
        if (bi > 0 && bi + 1 < args.size()) {
            QMetaObject::invokeMethod(&w, [&w, kw = args[bi + 1]] { w.applySource(1); w.biliSearchPublic(kw, true); },
                                      Qt::QueuedConnection);
        }
        const int mi = args.indexOf("--music");
        if (mi > 0 && mi + 1 < args.size())
            QMetaObject::invokeMethod(&w, [&w, kw = args[mi + 1]] { w.musicSearch(kw); }, Qt::QueuedConnection);
    }
    // 音乐：--qqmusic <关键词> → QQ音乐搜索并播放第一条
    {
        const int qi = args.indexOf("--qqmusic");
        if (qi > 0 && qi + 1 < args.size())
            QMetaObject::invokeMethod(&w, [&w, kw = args[qi + 1]] { w.qqMusicSearch(kw); }, Qt::QueuedConnection);
    }
    if (args.contains("--selftest")) {   // 隔离测试：只验证 playResolved + mpv 渲染链路
        const int si = args.indexOf("--selftest");
        const QString f = (si + 1 < args.size()) ? args[si + 1] : "/tmp/hovtest.mp4";
        QMetaObject::invokeMethod(&w, [&w, f] { w.selfTest(f); }, Qt::QueuedConnection);
    }
    if (di > 0 && di + 1 < args.size()) {
        QMetaObject::invokeMethod(&w, [&w, q = args[di + 1]] { w.demoSearch(q); }, Qt::QueuedConnection);
    }
    return app.exec();
}
