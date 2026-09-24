// ── VideoFullscreen.cpp：从 MainWindow.h 搬出的成员实现（文档 52/53 手法 ✓ 零行为改动 ✓）──
// 搬运清单: enterVideoFullscreen exitVideoFullscreen pollHover setMasonryFloating setSidebarRevealed
#include "MainWindow.h"

void MainWindow::setMasonryFloating(bool floating) {
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

void MainWindow::setSidebarRevealed(bool on) {
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

void MainWindow::pollHover() {
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

void MainWindow::enterVideoFullscreen() {
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

void MainWindow::exitVideoFullscreen() {
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
