// 搬运/新增清单: DownloadPanel（下载面板）+ MainWindow::openDownloadPanel
//   W2 ✓ 用户需求「下载面板要有下载进度显示，要美观」（macOS 端同步升级 ✓ 两端功能一致 ✓）
#include "DownloadPanel.h"
#include "MainWindow.h"

#include <QColor>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kColName  = 0;
constexpr int kColState = 1;
constexpr int kColBar   = 2;
constexpr int kColSize  = 3;
constexpr int kColAct   = 4;      // W4 ✓ 操作列（每行「取消下载」按钮 ✓）
constexpr int kBarWidth = 190;
}   // namespace

DownloadPanel::DownloadPanel(DownloadManager *dl, QWidget *parent) : QDialog(parent), dl_(dl) {
    setWindowFlag(Qt::Window);                 // 独立窗口（与 macOS 面板一致的形态 ✓）
    setMinimumSize(760, 420);
    resize(780, 440);
    setWindowTitle(QStringLiteral("下载"));
    // 面板内局部样式：行高/表格观感 + 进度条圆角（强调色与 UI-2 实测的 #3869D3 一致 ✓）
    setStyleSheet(QStringLiteral(
        "QTableWidget { background: #171717; alternate-background-color: #1C1C1C;"
        "  border: 1px solid #2E2E2E; border-radius: 8px; color: #DDDDDD;"
        "  gridline-color: transparent; selection-background-color: transparent; }"
        "QTableWidget::item { padding: 4px 8px; }"
        "QHeaderView::section { background: #202020; color: #9A9A9A; border: none;"
        "  padding: 6px 8px; font-size: 12px; }"
        "QTableCornerButton::section { background: #202020; border: none; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    summary_ = new QLabel(this);
    summary_->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600; color: #E8E8E8;"));
    root->addWidget(summary_);

    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels({ QStringLiteral("名称"), QStringLiteral("状态"),
                                        QStringLiteral("进度"), QStringLiteral("大小"),
                                        QStringLiteral("操作") });      // W4 ✓ 取消下载 ✓
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(36);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->setSelectionMode(QAbstractItemView::NoSelection);      // 面板只读；动作都在按钮上 ✓
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setWordWrap(false);
    QHeaderView *hh = table_->horizontalHeader();
    hh->setSectionResizeMode(kColName, QHeaderView::Stretch);
    hh->setSectionResizeMode(kColState, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(kColBar, QHeaderView::Fixed);
    hh->setSectionResizeMode(kColSize, QHeaderView::ResizeToContents);
    // W4 ✓ 操作列用**固定宽**（ResizeToContents 下按钮会被裁 ✗ 实测截图："取消下载"右侧被切 ✓）
    hh->setSectionResizeMode(kColAct, QHeaderView::Fixed);
    table_->setColumnWidth(kColAct, 118);
    table_->setColumnWidth(kColBar, kBarWidth);
    hh->setHighlightSections(false);
    root->addWidget(table_, 1);

    auto makeBtn = [this](const QString &text) {
        auto *b = new QPushButton(text, this);
        b->setObjectName("pillBtn");       // R1 ✓ 全圆胶囊（与顶栏/面板按钮同一形态 ✓）
        b->setCursor(Qt::PointingHandCursor);
        // ⚠️ QDialog 会把**第一个**按钮当默认按钮（回车即触发 ✓）→ 会把「重试失败」高亮成主按钮 ✗
        //    且回车误触发有副作用 ✗ → 全部关掉 autoDefault，只让「关闭」做默认（回车=安全动作 ✓）
        b->setAutoDefault(false);
        return b;
    };
    auto *retry = makeBtn(QStringLiteral("重试失败"));
    auto *cancelAllBtn = makeBtn(QStringLiteral("取消全部"));   // W4 ✓ 用户指定按钮 ✓
    auto *clean = makeBtn(QStringLiteral("LRU 清理"));
    auto *open  = makeBtn(QStringLiteral("打开下载目录"));
    auto *close = makeBtn(QStringLiteral("关闭"));
    // ✗ 刻意**不**设默认按钮：设了会把「关闭」渲染成蓝色主按钮（语义误导 ✗ 与 macOS 面板不一致 ✗）
    //    回车无动作、Esc=关闭（QDialog 内建 ✓）——用户 2026-09-23 截图评审后定稿 ✓
    auto *btns = new QHBoxLayout();
    btns->setSpacing(8);
    btns->addWidget(retry);
    btns->addWidget(cancelAllBtn);
    btns->addWidget(clean);
    btns->addWidget(open);
    btns->addStretch(1);
    btns->addWidget(close);
    root->addLayout(btns);

    connect(retry, &QPushButton::clicked, this, [this] {
        dl_->retryFailed();
        refresh();
        summary_->setText(QStringLiteral("已重试全部失败任务（%1 个任务）").arg(dl_->jobs().size()));
    });
    connect(cancelAllBtn, &QPushButton::clicked, this, [this] {
        const int n = dl_->activeCount();
        dl_->cancelAll();                                   // 逐个取消（含**混流中的** ✓）
        refresh();                                          // 条目已被内核移除 → 列表同步刷新 ✓
        summary_->setText(n > 0 ? QStringLiteral("已取消全部 %1 个进行中的任务").arg(n)
                                : QStringLiteral("当前没有进行中的任务"));
    });
    connect(clean, &QPushButton::clicked, this, [this] {
        const qint64 cap = qint64(dl_->maxTotalSizeMb()) * 1024 * 1024;
        if (cap <= 0) { summary_->setText(QStringLiteral("下载上限为 0（不限制）→ 无需清理")); return; }
        const DownloadManager::CleanResult r = dl_->cleanupLru(cap);
        // W5 ✓ 用户口径：点「清理」后**已结束的条目也要从列表消失** ✓
        //   （原来只在“有文件被删”时才同步 ✗ → 默认 2GB 上限下条目永远留着 ✗）
        const int dropped = dl_->dropFinishedJobs();
        refresh();
        summary_->setText(QStringLiteral("清理完成：%1 → %2，删除 %3 个文件%4")
                              .arg(humanSize(r.beforeBytes), humanSize(r.afterBytes))
                              .arg(r.removed.size())
                              .arg(dropped > 0 ? QStringLiteral("；列表清除 %1 条已结束记录").arg(dropped)
                                               : QString()));
    });
    connect(open, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dl_->dir()));
    });
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    refresh();
}

QString DownloadPanel::humanSize(qint64 bytes) {
    if (bytes < 0) return QStringLiteral("-");
    static const char *units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = double(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    return i == 0 ? QStringLiteral("%1 B").arg(bytes)
                  : QStringLiteral("%1 %2").arg(v, 0, 'f', 1).arg(units[i]);
}

QString DownloadPanel::stateText(DownloadManager::State s) {
    switch (s) {
    case DownloadManager::State::Queued:   return QStringLiteral("排队");
    case DownloadManager::State::Running:  return QStringLiteral("下载中");
    case DownloadManager::State::Done:     return QStringLiteral("完成");
    case DownloadManager::State::Failed:   return QStringLiteral("失败");
    case DownloadManager::State::Canceled: return QStringLiteral("已取消");
    }
    return QString();
}

int DownloadPanel::rowOf(const QString &id) const {
    for (int r = 0; r < table_->rowCount(); ++r) {
        const QTableWidgetItem *it = table_->item(r, kColName);
        if (it && it->data(Qt::UserRole).toString() == id) return r;
    }
    return -1;
}

void DownloadPanel::fillRow(int row, const DownloadManager::Job &j) {
    // 名称（显示 title ✓ 与 macOS 面板一致；tooltip 给落盘路径 ✓）
    const QString fileName = QFileInfo(j.filePath).fileName();
    auto *name = new QTableWidgetItem(j.title.isEmpty() ? fileName : j.title);
    name->setData(Qt::UserRole, j.id);
    name->setToolTip(j.filePath.isEmpty() ? j.url : j.filePath);
    name->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->setItem(row, kColName, name);

    // 状态（失败附原因 ✓ / 颜色区分 ✓）
    QString st = stateText(j.state);
    if (j.state == DownloadManager::State::Failed && !j.error.isEmpty())
        st += QStringLiteral("：%1").arg(j.error);
    auto *state = new QTableWidgetItem(st);
    state->setToolTip(j.error);
    state->setTextAlignment(Qt::AlignCenter);
    if (j.state == DownloadManager::State::Failed)
        state->setForeground(QColor("#E0726C"));
    else if (j.state == DownloadManager::State::Done)
        state->setForeground(QColor("#5FBF87"));
    else if (j.state == DownloadManager::State::Running)
        state->setForeground(QColor("#7FA9F0"));
    else
        state->setForeground(QColor("#9A9A9A"));
    table_->setItem(row, kColState, state);

    // 进度条（复用 cell widget ✓ 只改值不重建 ✓）
    const int pct = (j.bytesTotal > 0)
                        ? int(j.bytesDone * 100 / j.bytesTotal)
                        : (j.state == DownloadManager::State::Done ? 100 : 0);
    auto *bar = qobject_cast<QProgressBar *>(table_->cellWidget(row, kColBar));
    if (!bar) {
        bar = new QProgressBar(table_);
        bar->setObjectName("dlBar");
        bar->setRange(0, 100);
        bar->setFixedHeight(16);
        bar->setAlignment(Qt::AlignCenter);
        table_->setCellWidget(row, kColBar, bar);
    }
    bar->setValue(qBound(0, pct, 100));
    // 未知总长时（ffmpeg 混流阶段等 ✓）显示"进行中"而不是假的 0% ✓
    bar->setFormat(j.bytesTotal > 0 || j.state == DownloadManager::State::Done
                       ? QStringLiteral("%1%").arg(pct)
                       : (j.state == DownloadManager::State::Running ? QStringLiteral("进行中")
                                                                    : QStringLiteral("-")));
    const QString chunk = j.state == DownloadManager::State::Failed ? QStringLiteral("#E0726C")
                        : j.state == DownloadManager::State::Done   ? QStringLiteral("#4FBF7F")
                                                                   : QStringLiteral("#3869D3");
    bar->setStyleSheet(QStringLiteral(
                           "QProgressBar#dlBar { border: 1px solid #333333; border-radius: 8px;"
                           "  background: #232323; color: #D8D8D8; font-size: 11px; }"
                           "QProgressBar#dlBar::chunk { border-radius: 7px; background: %1; }")
                           .arg(chunk));

    // 大小（进行中显示 已完成/总长 ✓；未知总长只显示已完成 ✓）
    QString sz;
    if (j.state == DownloadManager::State::Done) sz = humanSize(j.bytesDone);
    else if (j.bytesTotal > 0) sz = QStringLiteral("%1 / %2").arg(humanSize(j.bytesDone), humanSize(j.bytesTotal));
    else if (j.bytesDone > 0)  sz = humanSize(j.bytesDone);
    else                       sz = QStringLiteral("-");
    auto *size = new QTableWidgetItem(sz);
    size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, kColSize, size);

    // W4 ✓ 操作列：仅**排队/下载中**给「取消下载」按钮（已完成/失败/已取消 留空 ✓ 不误删记录 ✗）
    auto *btn = qobject_cast<QPushButton *>(table_->cellWidget(row, kColAct));
    const bool active = (j.state == DownloadManager::State::Queued || j.state == DownloadManager::State::Running);
    if (active) {
        if (!btn) {
            btn = new QPushButton(QStringLiteral("取消下载"), table_);
            btn->setObjectName("pillBtn");                  // R1 ✓ 全圆 ✓
            btn->setCursor(Qt::PointingHandCursor);
            btn->setAutoDefault(false);                     // ✗ 别让它成为回车默认（见本文件顶部警示 ✓）
            btn->setStyleSheet(QStringLiteral("QPushButton { padding: 0px 10px; min-height: 22px; }"));   // R2 ✓ 显式覆写纵向 padding（防裁切 ✓）
            table_->setCellWidget(row, kColAct, btn);
            connect(btn, &QPushButton::clicked, this, [this, btn] {
                // 按钮会随行销毁 ✓（connect 的 sender 是 btn → 析构自动断开 ✓ 无悬垂 ✗）
                const QString jid = btn->property("jobId").toString();
                if (!jid.isEmpty()) dl_->cancel(jid);       // 内核：中断/杀进程 + 删半截文件 + 移除条目 ✓
                refresh();                                  // 列表同步（条目消失 ✓）
            });
        }
        btn->setProperty("jobId", j.id);                    // 每次刷新都更新 id（行会被复用 ✓）
    } else if (btn) {
        table_->setCellWidget(row, kColAct, nullptr);        // 非活动行 → 移除按钮（Qt 负责销毁旧控件 ✓）
    }
}

void DownloadPanel::refresh() {
    const QVector<DownloadManager::Job> jobs = dl_->jobs();
    table_->setRowCount(jobs.size());
    for (int i = 0; i < jobs.size(); ++i) fillRow(i, jobs.at(i));
    const int active = dl_->activeCount();
    summary_->setText(QStringLiteral("下载（%1 个任务%2）")
                          .arg(jobs.size())
                          .arg(active > 0 ? QStringLiteral("，%1 个进行中").arg(active) : QString()));
    setWindowTitle(QStringLiteral("下载（%1 个任务）").arg(jobs.size()));
    std::fprintf(stderr, "[DL] 面板刷新：%d 行（内核 %d 个任务 ✓）\n", table_->rowCount(), int(jobs.size()));
}

void DownloadPanel::updateJob(const DownloadManager::Job &j) {
    // ⚠️ 关键：**先查内核里还在不在** ✓ —— cleanupLru / cancel 会把任务从内核移除 ✗，
    //    而本表仍显示旧行 ✗ → 只看 rowOf() 会误判成"行还在，只是更新" ✗ → 行永远不消失 ✗
    //    （实测：清理后内核剩 0 个任务，表格仍显示 1 行 ✗ 就是这里 ✓）
    if (!dl_->find(j.id)) { refresh(); return; }
    const int row = rowOf(j.id);
    if (row < 0) { refresh(); return; }        // 新任务：整表刷新一次（不频繁 ✓）
    fillRow(row, j);
    const int active = dl_->activeCount();
    summary_->setText(QStringLiteral("下载（%1 个任务%2）")
                          .arg(table_->rowCount())
                          .arg(active > 0 ? QStringLiteral("，%1 个进行中").arg(active) : QString()));
}

// ── MainWindow 侧：打开面板（懒创建 + 复用 ✓ 面板可长期开着看进度 ✓）────────────
void MainWindow::openDownloadPanel() {
    if (!dlPanel_) dlPanel_ = new DownloadPanel(dl_, this);
    dlPanel_->refresh();
    dlPanel_->show();
    dlPanel_->raise();
    dlPanel_->activateWindow();
    std::fprintf(stderr, "[DL] 下载面板已打开（%d 个任务 ✓）\n", int(dl_->jobs().size()));
}
