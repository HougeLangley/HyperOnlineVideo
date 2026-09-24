#pragma once
#include <QDialog>
#include <QString>

#include "DownloadManager.h"

class QLabel;
class QTableWidget;

/**
 * 下载面板（Linux）
 *
 * 对齐 macOS「下载面板」（UiActions.swift onDownloads —— 本轮把 NSAlert 文字摘要**升级为带进度的面板** ✓）：
 *   每任务一行：名称 / 状态 / **进度条**（bytesDone/bytesTotal ✓）/ 大小；
 *   按钮：重试失败 / LRU 清理 / 打开下载目录 / 关闭 —— 与 macOS 面板逐项一致。
 *
 * 数据来源：DownloadManager::jobs() ✓ 事件来源：MainWindow 的进度回调转发到 updateJob() ✓
 * （按 job.id 定位行 → 只更新那一行 ✓ 不重建整表 ✓ 减少闪烁）
 */
class DownloadPanel : public QDialog {
    Q_OBJECT
public:
    explicit DownloadPanel(DownloadManager *dl, QWidget *parent = nullptr);

    /** 全量重建（打开时 / 清理后 / 出现未知任务时） */
    void refresh();
    /** 增量更新一行（进度回调；任务不在表里则自动全量刷新 ✓） */
    void updateJob(const DownloadManager::Job &j);

private:
    static QString humanSize(qint64 bytes);
    static QString stateText(DownloadManager::State s);
    int rowOf(const QString &id) const;
    void fillRow(int row, const DownloadManager::Job &j);

    DownloadManager *dl_ = nullptr;
    QLabel *summary_ = nullptr;
    QTableWidget *table_ = nullptr;
};
