#include "HovLog.h"
#include "Ui.h"
// 搬运/新增清单: FavoritesPanel（收藏面板）+ MainWindow::openFavoritesPanel
//   W2 ✓ 用户需求「macOS 有收藏，Linux 端要对齐、功能一致」
//   形态**逐项照搬** macOS Panels.swift FavoritesPanel（560x420 ✓ 标题"收藏（N 项）" ✓
//   行文本"title   [platform]" ✓ 按钮 播放选中/移除选中/清空/关闭 ✓ 双击行=播放 ✓）
#include "FavoritesPanel.h"
#include "MainWindow.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

FavoritesPanel::FavoritesPanel(Favorites *fav, QWidget *parent) : QDialog(parent), fav_(fav) {
    setWindowFlag(Qt::Window);                    // 独立窗口（与 macOS 面板一致 ✓）
    resize(560, 420);                             // macOS 收藏面板 560x420 ✓
    setMinimumSize(460, 320);
    setWindowTitle(QStringLiteral("收藏"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(M::space6, M::space6, M::space6, M::space6);
    root->setSpacing(M::space3);

    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(list_, 1);

    auto *play  = Ui::tonalButton(IconId::PlayArrow, QStringLiteral("播放选中"), this);
    auto *del   = Ui::tonalButton(IconId::RemoveCircle, QStringLiteral("移除选中"), this);
    auto *clear = Ui::tonalButton(IconId::DeleteSweep, QStringLiteral("清空"), this);
    auto *close = Ui::textButton(QStringLiteral("关闭"), this);
    // ✗ 刻意**不**设默认按钮：设了会把「关闭」渲染成蓝色主按钮（语义误导 ✗ 与 macOS 面板不一致 ✗）
    //    回车无动作、Esc=关闭（QDialog 内建 ✓）——用户 2026-09-23 截图评审后定稿 ✓
    auto *btns = new QHBoxLayout();
    btns->setSpacing(M::space2);
    btns->addWidget(play);
    btns->addWidget(del);
    btns->addWidget(clear);
    btns->addStretch(1);
    btns->addWidget(close);
    root->addLayout(btns);

    auto playRow = [this](int row) {
        if (row < 0 || row >= list_->count()) return;
        QListWidgetItem *it = list_->item(row);
        if (play_) play_(it->data(Qt::UserRole).toString(), it->data(Qt::UserRole + 1).toString());
    };
    connect(play, &QPushButton::clicked, this, [this, playRow] { playRow(list_->currentRow()); });
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this, playRow](QListWidgetItem *it) { playRow(list_->row(it)); });   // 双击=播放 ✓
    connect(del, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (row < 0 || row >= list_->count()) return;
        fav_->remove(list_->item(row)->data(Qt::UserRole).toString());
        fav_->save();
        refresh();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        const QVector<Favorites::Fav> all = fav_->items();
        for (const auto &f : all) fav_->remove(f.id);
        fav_->save();
        refresh();
    });
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    refresh();
}

void FavoritesPanel::refresh() {
    const QVector<Favorites::Fav> items = fav_->items();       // macOS：按加入时间倒序 ✓
    list_->clear();
    for (const Favorites::Fav &f : items) {
        // 行文本与 macOS 逐字一致："title   [platform]"（Panels.swift ✓）
        auto *it = new QListWidgetItem(QStringLiteral("%1   [%2]").arg(f.title, f.platform), list_);
        it->setData(Qt::UserRole, f.key);
        it->setData(Qt::UserRole + 1, f.title);
        it->setToolTip(f.key);
    }
    setWindowTitle(QStringLiteral("收藏（%1 项）").arg(items.size()));   // macOS 标题格式 ✓
}

void MainWindow::openFavoritesPanel() {
    if (!favPanel_) {
        favPanel_ = new FavoritesPanel(&favs_, this);
        favPanel_->setPlayHandler([this](const QString &key, const QString &label) { playItem(key, label); });
    }
    favPanel_->refresh();
    favPanel_->show();
    favPanel_->raise();
    favPanel_->activateWindow();
    hovLog("[FAV] 收藏面板已打开（%d 项 ✓）\n", favs_.size());
}
