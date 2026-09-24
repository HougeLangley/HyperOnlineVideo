// ── Events.cpp：从 MainWindow.h 搬出的成员实现（文档 52/53/54 ✓ 零行为改动 ✓）──
// 搬运清单: eventFilter keyPressEvent mouseDoubleClickEvent showEvent
#include "MainWindow.h"

void MainWindow::closeEvent(QCloseEvent *e) {
    // W1 ✓ 保存窗口几何：用户调整过的大小/位置 → 下次启动恢复（用户要求"关闭时的窗口大小要能保持" ✓）
    //   ⚠️ 画中画/全屏态下**不保存**：那是程序化改的尺寸（PiP 固定 480x270），存下去会把用户正常尺寸污染掉
    //      （与 macOS 端同理；macOS 早有"程序化重排把 ui.listWidth 误存"的历史教训 ✓）
    if (pipActive_ || isFullScreen()) {
        std::fprintf(stderr, "[W1] 跳过保存窗口几何（画中画=%d 全屏=%d ✓）\n", int(pipActive_), int(isFullScreen()));
    } else {
        const QByteArray g = saveGeometry();
        // ⚠️ Settings::set() 只改**内存** values_ ✓ 落盘必须显式 save() ✗（首版漏了 → 日志打了但文件里仍是空 ✗
        //    参照 applySettingInner 的既有写法 ✓）。值未变化时 set() 返回 false → 不写盘 ✓ 避免无谓刷盘。
        if (settings_.set("ui.windowGeometry", QString::fromLatin1(g.toBase64())))
            settings_.save();
        std::fprintf(stderr, "[W1] 保存窗口几何 %dx%d @(%d,%d)（%d 字节 base64，已落盘 ✓）\n",
                     width(), height(), x(), y(), int(g.size()));   // 探针式日志 ✓ 自动化可判定 ✓
    }
    QMainWindow::closeEvent(e);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *e) {
        if (player_ && player_->isVisible() && player_->geometry().contains(e->position().toPoint())) {
            toggleVideoFullscreen();
            e->accept();
            return;
        }
        QMainWindow::mouseDoubleClickEvent(e);
}

void MainWindow::showEvent(QShowEvent *e) {
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

bool MainWindow::eventFilter(QObject *o, QEvent *e) {
        if (o == player_ && e->type() == QEvent::MouseButtonDblClick) { toggleVideoFullscreen(); return true; }
        return QMainWindow::eventFilter(o, e);
}

void MainWindow::keyPressEvent(QKeyEvent *e) {
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
