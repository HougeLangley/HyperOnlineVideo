#pragma once
#include <QObject>
#include <QString>
#include <functional>

#include "Subtitles.h"   // SubtitleTrack

class QNetworkAccessManager;

/**
 * 在线播放解析器（桌面端「接 core」第一步：在线播放）
 *
 * 背景：mpv 内置的 ytdl 钩子会对任意 URL fork yt-dlp，实测会崩溃，因此在 MpvWidget 里已关闭
 * （ytdl=no）。关闭后网页 URL 无法直接播放 —— 本类在 App 层调用 yt-dlp 解析出真实媒体直链，
 * 再交给 mpv，与 Android 版「App 自己解析直链」的做法保持一致（ADR-002：视频流 + 音频流分开）。
 *
 * 与 Android 版的对应关系：
 *   - Android：解析在 KMP core（BiliApi / MusicApi / Repo.kt）
 *   - 桌面端：本类先覆盖 YouTube / B站（yt-dlp 路线）；音乐平台 API 后续接入
 *
 * Cookie：读取 ~/.config/hov/cookies/<站点>.txt（Netscape 格式，与 Android 版导出的格式一致）
 */
class UrlResolver : public QObject {
    Q_OBJECT
public:
    // UI-4-B ✓ 搜索过滤器（与 macOS searchSort/searchDuration、Android SearchFilters 同语义 ✓）
    //   排序：1=相关度 2=最新 3=播放最多 ✓   时长：0=全部 1=短视频 2=中等 3=长篇 ✓
    void setSearchFilters(int sort, int duration) { searchSort_ = sort; searchDuration_ = duration; }
    /// B站搜索 URL 的过滤参数（供自检/自动化回读 ✓ 唯一真相 ✓）
    QString biliFilterQuery() const {
        QString q;
        if (searchSort_ >= 1 && searchSort_ <= 3) {
            const char *order = searchSort_ == 2 ? "pubdate" : (searchSort_ == 3 ? "click" : "totalrank");
            q += QString("&order=%1").arg(order);
        }
        if (searchDuration_ >= 1 && searchDuration_ <= 3)
            q += QString("&duration=%1").arg(searchDuration_ == 3 ? 4 : searchDuration_);   // 3→4 是 B站怪癖 ✓（与 macOS/Android 一致 ✓）
        return q;
    }
    static QStringList sortNames() { return {"相关度", "最新", "播放最多"}; }
    static QStringList durationNames() { return {"全部", "短视频", "中等", "长篇"}; }
    /// 带时间范围的时长文案（macOS 原文 ✓ UiActions.swift:825 ✓ 面板专用 ✓）
    static QStringList durationLabels() {
        return {"全部", "短视频（<10 分钟）", "中等（10~30 分钟）", "长篇（>30 分钟）"};
    }
    struct Stream {
        QString videoUrl;
        QString audioUrl;
        QString title;
        QVector<SubtitleTrack> subtitles;   // 在线字幕轨（已由 yt-dlp 落到本地文件）
    };

    explicit UrlResolver(QObject *parent = nullptr);

    /** 配置目录（cookie 与后续设置文件都放这里） */
    static QString configDir();
    /** 按站点返回 cookie 文件路径；不存在则返回空 */
    static QString cookieFileFor(const QString &pageUrl);
    /** 站点标识：youtube / bilibili / netease / qqmusic / 空 */
    static QString serviceOf(const QString &pageUrl);
    /** B站搜索结果条目 */
    struct BiliVideo {
        QString bvid, title, author, duration, pic;   // pic：缩略图（//i0.hdslb.com/…）
        QString url() const { return "https://www.bilibili.com/video/" + bvid; }
    };
    /** B站搜索（官方 search/type 接口；带 cookie 更稳，yt-dlp 的 bilisearch 会被 412 拦） */
    QVector<BiliVideo> searchBili(const QString &keyword, int pageSize = 20);

    /** 是否已是可直接播放的媒体地址（本地文件或 CDN 直链） */
    static bool isDirectMedia(const QString &url);

    void setPlayHandler(std::function<void(const Stream &)> h) { play_ = std::move(h); }
    void setStatusHandler(std::function<void(const QString &)> h) { status_ = std::move(h); }
    /** 国内服务是否显式绕过代理（设置项 network.forceDirectDomestic，与音乐 API 一致） */
    void setForceDirect(bool b) { forceDirect_ = b; }
    /** 视频清晰度上限（0=自动，-1=仅音频，否则高度上限） */
    void setMaxHeight(int h) { maxHeight_ = h; }
    int maxHeight() const { return maxHeight_; }
    /** 档位 → yt-dlp 的 -f 参数（**纯函数**，可单测） */
    static QStringList formatArgsFor(int maxHeight);
    /** 档位标签 */
    static QString qualityLabel(int h);

    /** 站点 cookie 文件 → Cookie 头（音乐 API 与 B站搜索共用） */
    QString cookieHeaderFor(const QString &site) const;
    /** 从系统浏览器读 cookie（A0 登录方案；空 = 关闭）。开启后 yt-dlp 会把合并 jar 写回站点 cookie 文件 */
    void setCookiesFromBrowser(const QString &b) { cookiesFromBrowser_ = b.trimmed(); }
    QString cookiesFromBrowser() const { return cookiesFromBrowser_; }
    /** 站点 cookie 文件路径（不判断存在性——浏览器模式下即使文件还不存在也要传给 yt-dlp 以便导出） */
    static QString cookiePathFor(const QString &pageUrl);
    /** 组装 cookie 相关参数（浏览器模式 + 站点文件） */
    QStringList cookieArgsFor(const QString &pageUrl) const;

    /** 解析并播放；已经是直链时直接交给播放器 */
    void resolveAndPlay(const QString &pageUrl);

private:
    int searchSort_ = 1;        // UI-4-B ✓ 会话态（与 macOS 同：不落盘 ✓）
    int searchDuration_ = 0;
    /** 在线字幕总入口：按站点选路线（B站走官方 CC API，YouTube 走 yt-dlp） */
    void fetchSubtitles(const QString &pageUrl, const QString &service, Stream result);
    /** B站 CC：x/web-interface/view → x/player/v2 → subtitle_url（内存轨，不落盘） */
    void fetchBiliCc(const QString &pageUrl, std::function<void(QVector<SubtitleTrack>, QString)> done);
    /** yt-dlp 路线：抓字幕到缓存目录后落地成文件轨；done(tracks, 错误说明) */
    void fetchByYtDlp(const QString &pageUrl, std::function<void(QVector<SubtitleTrack>, QString)> done);
    /** 按 NetPolicy 设置本类网络请求的代理（国内服务可强制直连） */
    void applyProxy();
    std::function<void(const Stream &)> play_;
    std::function<void(const QString &)> status_;
    QNetworkAccessManager *net_ = nullptr;
    bool forceDirect_ = false;
    QString cookiesFromBrowser_;
    int maxHeight_ = 0;

public:
    /**
     * 直接取一个字幕地址并解析成字幕轨（--subs-url）。
     * 用途：自动化测试中把「在线取字幕 → 解析 → 轨道 → 渲染」整条链路跑通（不依赖媒体是否能播）。
     */
    void fetchSubtitleUrl(const QString &url,
                          std::function<void(const QVector<SubtitleTrack> &, const QString &)> done);

    /**
     * 单独取在线字幕（不播放）：给视频页地址就走对应站点路线，给字幕文件地址就直接取。
     * 用途：`--subs-url <视频页|字幕地址>`，以及自动化里分别验证两条链路。
     */
    void fetchOnlineSubs(const QString &urlOrPage,
                         std::function<void(const QVector<SubtitleTrack> &, const QString &)> done);
};
