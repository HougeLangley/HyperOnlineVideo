#pragma once
#include <QStringList>
#include <QString>

/**
 * 播放列表（独立小类，纯逻辑、不依赖界面）
 *
 * 设计说明（P3 经验）：main.cpp 已 200+ 行且含 switch/lambda，往类内部插代码反复踩"锚点漂移"。
 * 后续新增功能一律**先分文件**，主类只加一行成员 + 一行转调。
 *
 * 对应 Android 版：PlayQueue（顺序/单曲/随机）—— 桌面端先做"顺序 + 循环"，
 * 单曲/随机待与共享核心（core）合并后统一实现，避免两套逻辑。
 */
class Playlist {
public:
    void setFiles(const QStringList &files, int startIndex = 0) {
        files_ = files;
        index_ = files_.isEmpty() ? -1 : clamp(startIndex);
    }
    bool isEmpty() const { return files_.isEmpty(); }
    int count() const { return files_.size(); }
    int index() const { return index_; }
    bool hasMultiple() const { return files_.size() > 1; }

    QString current() const { return fileAt(index_); }

    /** 下一首（循环）；只有一首时返回自身，空列表返回空串 */
    QString next() {
        if (files_.isEmpty()) return QString();
        index_ = clamp(index_ + 1);
        return current();
    }
    QString prev() {
        if (files_.isEmpty()) return QString();
        index_ = clamp(index_ - 1);
        return current();
    }
    /** 跳到指定位置（越界自动环绕） */
    QString at(int i) {
        if (files_.isEmpty()) return QString();
        index_ = clamp(i);
        return current();
    }

    /** 界面显示用："文件名（第 n/N 个）" */
    QString displayName() const;

private:
    QStringList files_;
    int index_ = -1;

    int clamp(int i) const {
        const int n = files_.size();
        if (n == 0) return -1;
        return ((i % n) + n) % n;   // 环绕，避免越界与负数
    }
    QString fileAt(int i) const {
        if (i < 0 || i >= files_.size()) return QString();
        return files_.at(i);
    }
};
