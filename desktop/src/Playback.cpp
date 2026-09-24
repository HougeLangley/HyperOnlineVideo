// ── Playback.cpp：从 MainWindow.h 搬出的成员实现（文档 52/53/54 ✓ 零行为改动 ✓）──
// 搬运清单: applySpeed effectiveQuality refreshControls seekBy setPlayerOnlyVisible startStallWatch switchQualityTo tickProgress togglePiP
#include "MainWindow.h"
#include <QAbstractButton>

void MainWindow::applySpeed(double sp) {
        player_->setSpeed(sp);
        if (speedBtn_) speedBtn_->setText(QString("%1x").arg(sp, 0, 'g', 3));
        results_->addItem(QString("倍速: %1x").arg(sp, 0, 'g', 3));
}

void MainWindow::tickProgress() {
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

QString MainWindow::effectiveQuality() const {
        return settings_.effectiveQuality(sessionQuality_.isEmpty() ? QStringLiteral("lossless") : sessionQuality_);
}

void MainWindow::switchQualityTo(const QString &q) {
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

void MainWindow::setPlayerOnlyVisible(bool only) {
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
        // ⚠️ W5 ✓ 隐藏清单**漏了 QComboBox** ✗→✓（用户截图实测：PiP 里"自动/1.0x"两个下拉还在 ✗，
        //    且它们的最小宽把 PiP 夹到 539x270 ✗ —— 代码请求的是 480x270 ✓）
        //    顺带用 QAbstractButton 覆盖 QCheckBox/QToolButton 等 ✓（原来只认 QPushButton ✗）
        for (auto *w : findChildren<QWidget *>())
            if (qobject_cast<QAbstractButton *>(w) || qobject_cast<QSlider *>(w)
                || qobject_cast<QLabel *>(w) || qobject_cast<QLineEdit *>(w)
                || qobject_cast<QComboBox *>(w))
                only ? w->hide() : w->show();
        // 上面循环会强制显示所有 QLabel，状态行要按进入前的可见性还原（默认本就隐藏）
        if (!only && statusLabel_ && !vfStatusWasVisible_) statusLabel_->hide();
}

void MainWindow::togglePiP() {
        pipActive_ = !pipActive_;
        if (pipActive_) {
            pipSavedGeometry_ = geometry();
            // ⚠️ 必须复用**全屏那套** `setPlayerOnlyVisible()` ✗→✓：
            //   可见的结果区是 `masonry_` ✓，`results_` 只是它的**数据源**（平时就隐藏 ✗）。
            //   旧写法只 `results_->hide()` ✗、退出时又 `results_->show()` ✗✓
            //   → 把数据源列表显示出来 ✗ → 它与瀑布流同时在 splitter 里 → 左侧“两个卡片网格/卡片被裁” ✗
            //   （用户 2026-09-23 截图实测；全屏路径踩过同一个坑并已修 ✓ 见 setPlayerOnlyVisible 注释 ✓）
            setPlayerOnlyVisible(true);
            // ⚠️ W5 ✓ 进 PiP 前**显式放开窗口最小尺寸** ✗→✓
            //   实测（openbox + 1.2.1-14）：布局最小尺寸已降到 20x58 ✓ 但 resize(480,270) 仍被夹在 539x270 ✗
            //   （Qt 会把 WM_NORMAL_HINTS 的 PMinSize 一起报给 WM ✗ → 小窗被拦 ✗）→ 这里显式设小 ✓
            pipSavedMinSize_ = minimumSize();
            setMinimumSize(160, 90);
            setWindowFlag(Qt::WindowStaysOnTopHint, true);
            show();
            resize(480, 270);
            qInfo() << "[SELFTEST] 已进入画中画（480x270，界面已收起；置顶由合成器决定）";
        } else {
            setPlayerOnlyVisible(false);
            setWindowFlag(Qt::WindowStaysOnTopHint, false);
            show();                                            // 同上：改 flag 后要再显示
            setMinimumSize(pipSavedMinSize_);                   // W5 ✓ 还原原最小尺寸（避免窗口能被拖到不可用 ✓）
            if (!pipSavedGeometry_.isNull()) setGeometry(pipSavedGeometry_);
            qInfo() << "[SELFTEST] 已退出画中画";
        }
}

void MainWindow::seekBy(double d) {
        const double dur = player_->durationSec();
        if (dur <= 0) return;
        player_->seekTo(qBound(0.0, player_->positionSec() + d, dur - 0.5));
        refreshControls();
}

void MainWindow::startStallWatch() {
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

void MainWindow::refreshControls() {
        const double pos = player_->positionSec(), dur = player_->durationSec();
        if (!seek_->isSliderDown() && dur > 0) {
            seek_->blockSignals(true);
            seek_->setValue((int)(pos / dur * 1000));
            seek_->blockSignals(false);
        }
        labelTime_->setText(fmtTime(pos) + " / " + fmtTime(dur));
        // UI-3 ✓ 底栏信息行：[来源] 标题 | 说明   时间   [状态]（与 macOS 同形态 ✓）
        if (labelInfo_) {
            QString src;
            if (playKey_.startsWith("youtube:"))       src = "YouTube";
            else if (playKey_.startsWith("bilibili:")) src = "B站";
            else if (playKey_.startsWith("netease:"))  src = "网易云";
            else if (playKey_.startsWith("qq:"))       src = "QQ音乐";
            else if (!playKey_.isEmpty())              src = "本地";
            const QString who = playLabel_.isEmpty() ? QString("（未在播放）") : playLabel_;
            const QString state = playLabel_.isEmpty() ? QString()
                                  : (player_->paused() ? "已暂停" : "播放中");
            labelInfo_->setText(QString("%1%2    %3 / %4    %5")
                                    .arg(src.isEmpty() ? QString() : "[" + src + "] ")
                                    .arg(who, fmtTime(pos), fmtTime(dur), state));
        }
        btnPlay_->setText(player_->paused() ? "\u25b6" : "\u23f8");
}
