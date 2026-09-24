#pragma once
#include <QDialog>
#include <QString>
#include <functional>

#include "Favorites.h"

class QListWidget;

/**
 * 收藏面板（Linux）
 *
 * 形态**逐项照搬** macOS Panels.swift FavoritesPanel：
 *   - 窗口 560x420 ✓ 标题「收藏（N 项）」✓ 边距/间距 12/10 ✓
 *   - 行文本「title   [platform]」✓ 按加入时间倒序 ✓
 *   - 按钮：播放选中 / 移除选中 / 清空 / 关闭 ✓（另有双击行=播放 ✓）
 *
 * 播放动作由 MainWindow 注入（面板不直接依赖播放实现 ✓ 与既有 Callback 注入同风格 ✓）。
 */
class FavoritesPanel : public QDialog {
    Q_OBJECT
public:
    explicit FavoritesPanel(Favorites *fav, QWidget *parent = nullptr);

    void refresh();
    void setPlayHandler(std::function<void(const QString &key, const QString &label)> h) {
        play_ = std::move(h);
    }

private:
    Favorites *fav_ = nullptr;
    QListWidget *list_ = nullptr;
    std::function<void(const QString &, const QString &)> play_;
};
