// ── LibraryActions.cpp：从 MainWindow.h 搬出的成员实现（文档 52/53 手法 ✓ 零行为改动 ✓）──
// 搬运清单: downloadMusic openLocal refreshLocalLibrary renameSelectedLocal
#include "MainWindow.h"
#include <QTimer>

void MainWindow::downloadMusic(const QString &keyword, int count) {
        results_->addItem(QString("下载（网易云）：%1").arg(keyword));
        auto *api = new NetEaseApi(this);
        tuneApi(api);
        api->setStatusHandler([this](const QString &m) { results_->addItem(m); });
        api->search(keyword, 10, [this, api, count](const QVector<NetEaseApi::Song> &songs) {
            if (songs.isEmpty()) { results_->addItem("无结果"); api->deleteLater(); return; }
            // 封面批量预热（用户实测 2026-09-25：下载的音乐没有内嵌封面 ✗）——
            //   网易云搜索只给 picId ✗ → song/detail 批量取 ✓ 写入 coverMap_，逐首下载时查用 ✓
            QStringList coverIds;
            for (const auto &s : songs) coverIds << s.id;
            api->coverUrls(coverIds, [this, api, songs, count](const QHash<QString, QString> &covers) {
                for (auto it = covers.constBegin(); it != covers.constEnd(); ++it)
                    coverMap_.insert("netease:" + it.key(), it.value());
                auto attempt = std::make_shared<std::function<void(int)>>();
                auto queued = std::make_shared<int>(0);
                *attempt = [this, api, songs, attempt, count, queued](int i) {
                    if (i >= songs.size() || (*queued) >= count) {
                        if (*queued == 0) results_->addItem("前几首都没有可下载地址");
                        else results_->addItem(QString("已入队 %1 首（并发上限 2）").arg(*queued));
                        api->deleteLater();
                        QTimer::singleShot(0, this, [attempt] { *attempt = nullptr; });   // C1 断环 ✓（延后：不可在自身执行中自毁 ✗）
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
                        lastCoverUrl_ = coverMap_.value("netease:" + s.id);   // 封面（预热结果 ✓ 空则无封面但其余标签照嵌 ✓）
                        downloadCurrent();          // 复用同一套命名与落盘逻辑
                        (*queued)++;
                        (*attempt)(i + 1);          // 继续下一首（达到 count 时由头部收尾）
                    });
                };
                (*attempt)(0);
            });
        });
}

void MainWindow::refreshLocalLibrary(const QString &filter) {
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

void MainWindow::renameSelectedLocal(const QString &presetName) {
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

void MainWindow::openLocal(const QString &f) {
        if (isWebUrl(f)) { playItem(f, f); return; }        // 同上：手动输入的网址也走在线路径
        results_->addItem("正在播放本地文件: " + QFileInfo(f).fileName());
        beginPlayback(f, QFileInfo(f).fileName());
        player_->playResolved(f, QString());
}

void MainWindow::showLocalLibrary() { refreshLocalLibrary(search_ ? search_->text().trimmed() : QString()); }
