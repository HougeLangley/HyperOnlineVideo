#pragma once
#include <QString>
#include <QStringList>
#include <QVariantMap>

/**
 * 设置（桌面端；与 Android core/data/Settings.kt 对齐的语义子集）
 *
 * 落盘 ~/.config/hov/settings.json。所有读取都有默认值，文件不存在/损坏都能正常启动。
 * 覆盖项（括号内为默认）：
 *   music.qualityCeiling  lossless | exhigh | standard   (exhigh)   音质上限
 *   subtitle.fontScale    0.5 ~ 2.0                       (1.0)     字幕/歌词字号倍数
 *   subtitle.defaultDelay -5.0 ~ 5.0 秒                   (0.0)     默认字幕延迟
 *   subtitle.karaoke      true | false                    (true)    歌词是否用卡拉OK面板
 *   download.dir          路径                            (空=默认 ~/Downloads/hov)
 *   download.maxSizeMb    0 ~ 1048576                     (2048)    下载目录总量上限（MB，0=不限制）
 *   network.forceDirectDomestic true | false              (false)   国内接口强制直连
 *   ui.showNetworkProbe   true | false                    (true)    启动时探活
 *   playback.rememberProgress true | false                (true)    进度记忆（续播 + 落盘）
 */
class Settings {
public:
    static QString filePath();

    void load();
    bool save() const;

    QString str(const QString &key, const QString &def = QString()) const;
    bool boolean(const QString &key, bool def) const;
    double number(const QString &key, double def) const;

    /** 校验一项：返回空串表示合法，否则返回错误说明（用于 --set 与设置对话框） */
    static QString validate(const QString &key, const QString &value);
    /** 已知键列表（供对话框与提示使用） */
    static QStringList knownKeys();

    /** 设置一项（字符串/布尔/数字都走这里）；返回是否发生了变更 */
    bool set(const QString &key, const QVariant &value);

    QVariantMap all() const { return values_; }
    /** 形如 "music.qualityCeiling=exhigh" 的多行文本，供 --show-settings 打印 */
    QString dump() const;

    /** 音质档位归一化：把上限值映射到某个 API 支持的档位 */
    QString effectiveQuality(const QString &requested) const;

private:
    QVariantMap values_;
};
