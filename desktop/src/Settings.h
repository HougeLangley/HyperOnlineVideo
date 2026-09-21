#pragma once
#include <QString>
#include <QVector>
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
 *   network.cookiesFromBrowser 浏览器名或空                (空)      从系统浏览器读 cookie（A0 登录方案）
 *   video.maxHeight       0 | -1 | 高度                    (0)       视频清晰度上限（0=自动，-1=仅音频）
 */
class Settings {
public:
    static QString filePath();

    void load();
    bool save() const;

    QString str(const QString &key, const QString &def = QString()) const;
    bool boolean(const QString &key, bool def) const;
    double number(const QString &key, double def) const;
    /// 该键是否被显式设置过（平台自适应默认值用：用户没设过才用平台默认）
    bool has(const QString &key) const;
    /// 直接读设置文件里的布尔键（**不依赖实例是否 load** ✓ macOS 上踩过"新建实例是空表"的坑 ✓）
    /// 用途：MpvWidget 构造时就要定 vo（initialize 后不能再改 ✗），此刻拿不到主窗口的 settings_ ✓
    static bool rawBool(const QString &key, bool def);

    /** 校验一项：返回空串表示合法，否则返回错误说明（用于 --set 与设置对话框） */
    static QString validate(const QString &key, const QString &value);

    // ── ④ 视频清晰度档位表：**全工程唯一一处定义**（macOS 端踩过"设置显示 1080p 实际 480p"：
    //    控制条/设置面板/快捷键各持一份表 → 下标错位。此处统一收口，界面只许从这里取。）
    //    0 = 自动（不设上限）；-1 = 仅音频；其余为 height 上限。
    static QVector<int> qualityHeights() { return {0, 2160, 1440, 1080, 720, 480, 360, -1}; }
    static QString qualityLabelFor(int h) {
        if (h == -1) return "仅音频";
        if (h <= 0) return "自动";
        return QString("%1p").arg(h);
    }
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
