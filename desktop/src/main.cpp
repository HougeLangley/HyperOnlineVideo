#include <QApplication>
#include <QDesktopServices>
#include <QMenu>
#include <QUrl>
#include <iostream>
#include <QMainWindow>
#include <QLineEdit>
#include <QListWidget>
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
#include <QTimer>
#include <QDir>
#include <QHash>
#include <QtMath>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "MpvWidget.h"
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
        srcBox_->addItems({"YouTube", "网易云音乐", "QQ音乐", "本地库"});
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
        root->addWidget(topCard);

        auto *split = new QSplitter(Qt::Horizontal, this);
        results_ = new QListWidget(split);
        player_ = new MpvWidget(split);
        // MPRIS：桌面端“后台播放 + 媒体键”的标准通道（无会话总线时自降级）
        mpris_ = new Mpris(player_, this);
        // 在线播放：网页 URL 由 App 层解析成直链（mpv 的 ytdl 钩子已禁用，见 MpvWidget 注释）
        player_->setFocus();   // 快捷键立即生效（否则焦点在搜索框）
        resolver_ = new UrlResolver(this);
        resolver_->setStatusHandler([this](const QString &m) { results_->addItem(m); results_->scrollToBottom(); });
        resolver_->setPlayHandler([this](const UrlResolver::Stream &s) {
            player_->setCoverArt(QString(), QString(), QString());   // 视频：清掉音乐封面
            player_->playResolved(s.videoUrl, s.audioUrl);
            if (!s.subtitles.isEmpty()) player_->addSubtitleTracks(s.subtitles);   // 在线字幕轨（追加，不覆盖本地轨）
        });
        split->addWidget(results_);
        split->addWidget(player_);
        split->setStretchFactor(1, 3);
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

        connect(btnNext, &QPushButton::clicked, this, [this] { queueOrFileNext(); });

        ctl->addWidget(seek_, 1);
        ctl->addWidget(labelTime_);
        ctl->addWidget(new QLabel("\U0001f50a", this));
        ctl->addWidget(vol_);
        root->addLayout(ctl);

        connect(btnPlay_, &QPushButton::clicked, this, [this] { player_->togglePause(); refreshControls(); });
        connect(seek_, &QSlider::sliderReleased, this, [this] {
            const double d = player_->durationSec();
            if (d > 0) player_->seekTo(d * seek_->value() / 1000.0);
        });
        connect(vol_, &QSlider::valueChanged, this, [this](int v) { player_->setVolume(v); });
        ctlTimer_ = new QTimer(this);
        ctlTimer_->setInterval(500);
        connect(ctlTimer_, &QTimer::timeout, this, [this] { refreshControls(); tickProgress(); });
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
            if (player_->eofReached(&eof) && eof) queueOrFileNext();
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
        resolver_->setForceDirect(settings_.boolean("network.forceDirectDomestic", false));   // 国内接口可强制直连
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
                    if (!url.isEmpty()) {
                        const QString lb = QString("%1 — %2").arg(s.name, s.artist);
                        beginPlayback("qq:" + s.mid, lb);      // 自动播放也要立起 playKey_（切档/进度记忆依赖它）
                        lastArtist_ = s.artist; lastAlbum_ = s.album;
                        lastCoverUrl_ = qqCoverUrl(s.albumMid);
                        results_->addItem(QString("正在播放：%1").arg(lb));
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
        check(q.next() == false && q.index() == before, "单曲模式 next() 不移动（交给调用方重播）");
        q.cycleMode(); check(q.mode() == PlayQueue::Mode::Shuffle, "模式切换 → 随机");
        q.next(); check(q.index() != before, "随机模式移动到别的项");
        q.cycleMode(); check(q.mode() == PlayQueue::Mode::Sequential, "模式切换 → 顺序");
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
            check(Subtitles::languageRank("zh-Hans · SRT") == 0, "字幕排序：zh-Hans 最优先");
            check(Subtitles::languageRank("中文（中国）· CC") == 0, "字幕排序：B站中文（中国）最优先");
            check(Subtitles::languageRank("zh-Hant") == 2, "字幕排序：繁体次之");
            check(Subtitles::languageRank("en") == 4, "字幕排序：英文靠后");
            check(Subtitles::languageRank("zh-Hans-en") == 1, "字幕排序：再翻译的简体排原生之后");
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

        SubtitleCue cue{ 10.0, 20.0, "t" };
        check(qAbs(Subtitles::lineProgress(cue, 10.0)) < 0.01, "行内进度：起点为 0");
        check(qAbs(Subtitles::lineProgress(cue, 15.0) - 0.5) < 0.01, "行内进度：中点为 0.5");
        check(qAbs(Subtitles::lineProgress(cue, 20.0) - 1.0) < 0.01, "行内进度：终点为 1");
        check(qAbs(Subtitles::lineProgress(cue, 5.0)) < 0.01
                  && qAbs(Subtitles::lineProgress(cue, 30.0) - 1.0) < 0.01, "行内进度：越界自动夹紧");
        SubtitleCue zero{ 5.0, 5.0, "z" };
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
        qInfo() << "队列自检:" << pass << "/" << total << "通过";
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

        form->addRow("音质上限", quality);
        form->addRow("字幕/歌词字号", fontScale);
        form->addRow("默认字幕延迟", delay);
        form->addRow(QString(), karaoke);
        form->addRow(QString(), probe);
        form->addRow(QString(), direct);
        form->addRow("下载目录", dirRow);

        auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, &dlg);
        form->addRow(btns);
        connect(btns, &QDialogButtonBox::accepted, &dlg, [this, quality, fontScale, delay, karaoke, probe, direct, dirEdit, &dlg] {
            static const QStringList qs{ "standard", "exhigh", "lossless" };
            applySetting("music.qualityCeiling", qs.at(quality->currentIndex()));
            applySetting("subtitle.fontScale", QString::number(fontScale->value()));
            applySetting("subtitle.defaultDelay", QString::number(delay->value()));
            applySetting("subtitle.karaoke", karaoke->isChecked() ? "true" : "false");
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
                                        lastArtist_, lastAlbum_, lastCoverUrl_);
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
                api->songUrl(s.id, effectiveQuality(), [this, api, s, i, attempt, count, queued](const QString &url, const QString &e) {
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

    /** 自动化入口：预选搜索来源（0=YouTube 1=网易云 2=QQ音乐） */
    // 上限跟着下拉项数走（加了「本地库」后仍写死 2 会把第 4 项夹回 QQ音乐 —— 实测踩到）
    void applySource(int n) { if (srcBox_) srcBox_->setCurrentIndex(qBound(0, n, srcBox_->count() - 1)); }

    /** 列表点击的统一入口：按条目类型分发（netease: / qq: / 普通网页 URL） */
    void playItem(const QString &key, const QString &label) {
        beginPlayback(key, label);
        // 与队列同步下标（用户在列表里点哪首，队列就跳到哪首 → ⏭/⏮ 接得上）
        for (int i = 0; i < queue_.size(); ++i) {
            if (queue_.at(i) && queue_.at(i)->key == key) { queue_.jumpTo(i); break; }
        }
        const QString q = queue_.label();
        if (!q.isEmpty()) results_->addItem(QString("[队列] %1").arg(q));
        if (key.startsWith("netease:")) { playNetEase(key.mid(8), label); return; }
        if (key.startsWith("qq:")) { playQQ(key.mid(3), label); return; }
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
    void queueOrFileNext() {
        if (!queue_.isEmpty()) {
            const bool moved = queue_.next();
            queue_.saveTo();
            if (!moved) { if (queue_.current()) playItem(queue_.current()->key, queue_.current()->label); return; }  // 单曲循环
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
            results_->addItem(QString("正在播放：%1").arg(label));
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

    /** 网易云音乐：搜索并播放第一条（自动化入口，也是后续音乐 UI 的基础） */
    void musicSearch(const QString &keyword) {
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
    /** 桌面键盘快捷键：空格播放暂停 / ←→ 快进退 / ↑↓ 音量 / F 全屏 / Esc 退出全屏 */
    void keyPressEvent(QKeyEvent *e) override {
        switch (e->key()) {
        case Qt::Key_Space:  player_->togglePause(); refreshControls(); break;
        case Qt::Key_Left:   seekBy(-5); break;
        case Qt::Key_Right:  seekBy(5); break;
        case Qt::Key_Up:     setVol(player_->volume() + 5); break;
        case Qt::Key_Down:   setVol(player_->volume() - 5); break;
        case Qt::Key_F:      isFullScreen() ? showNormal() : showFullScreen(); break;
        case Qt::Key_S:      player_->toggleSubtitles(); break;   // 字幕/歌词开关
        case Qt::Key_C:      player_->cycleSubtitleTrack(); break;      // 切换字幕轨（含关闭）
        case Qt::Key_Comma:  player_->adjustSubtitleDelay(-0.5); break;  // 字幕提前 0.5s
        case Qt::Key_Period: player_->adjustSubtitleDelay(0.5); break;   // 字幕延后 0.5s
        case Qt::Key_A:      toggleFavoriteCurrent(); break;             // A=收藏当前项（F 已是全屏）
        case Qt::Key_M:      queue_.cycleMode(); results_->addItem(QString("[队列] 模式：%1").arg(queue_.modeLabel())); break;
        case Qt::Key_Q:      cycleQuality(); break;                  // Q=音质（标准/较高/无损，播放中切会回到原进度）
        case Qt::Key_O:      lib_.cycleSort(); showLocalLibrary(); break;   // O=本地库排序（名称/时间/大小）
        case Qt::Key_R:      renameSelectedLocal(); break;           // R=重命名选中的本地库文件
        case Qt::Key_Escape: if (isFullScreen()) showNormal(); break;
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
    QString sessionQuality_;        // 会话内音质档位（空 = 跟随设置；Q 键 / --quality 会改它）
    DownloadManager *dl_ = nullptr;   // 下载管理（并发 2 + 重试）
    LocalLibrary lib_;                   // 本地库（下载目录）
    QHash<QString, QString> coverMap_;   // 播放键 → 封面地址（搜索时顺便记录）
    QHash<QString, QString> titleMap_;   // 播放键 → 标题/艺术家（封面下方文字）
    QString lastStreamUrl_;           // 当前播放的直链（下载按钮用）
    QString lastArtist_, lastAlbum_, lastCoverUrl_;   // 下载时嵌入的音乐标签
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
        if (src == 3) { refreshLocalLibrary(q); return; }   // 本地库：关键词当文件名过滤（可为空）
        if (q.isEmpty()) return;
        if (src == 1) { musicSearch(q); return; }      // 网易云音乐
        if (src == 2) { qqMusicSearch(q); return; }    // QQ音乐
        results_->clear();
        results_->addItem("搜索中…");
        auto *p = new QProcess(this);
        connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, p](int code, QProcess::ExitStatus) {
                    results_->clear();
                    if (code != 0) { results_->addItem("搜索失败（检查网络或 yt-dlp）"); return; }
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
                    }
                    if (results_->count() == 0) { results_->addItem("没有结果"); p->deleteLater(); return; }
                    rebuildQueueFromList();             // 搜索结果整体入队（⏭/⏮ 与自动续播都靠它）
                    if (autoplay_) {                       // 自动化验证：自动播放第一条
                        results_->setCurrentRow(0);
                        resolver_->resolveAndPlay(results_->item(0)->data(Qt::UserRole).toString());
                    }
                    p->deleteLater();
                });
        p->start("yt-dlp", {"-J", "--flat-playlist", "--no-warnings",
                            "--playlist-end", "15", "ytsearch15:" + q});
    }
};

int main(int argc, char **argv) {
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
