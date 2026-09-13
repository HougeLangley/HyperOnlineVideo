#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

class QNetworkAccessManager;

/**
 * QQ音乐 API（桌面端）—— 路线与 Android 版 core/data/MusicApi.kt 的 QQMusicApi 对齐
 *
 * 两条链路（2026-09-12 实测）：
 *   - 搜索：`c.y.qq.com/soso/fcgi-bin/client_search_cp`（y.qq.com 网页版在用的接口）
 *           **匿名可用** —— 实测返回真实歌曲与 songmid；
 *           而 `u.y.qq.com/cgi-bin/musicu.fcg` 的 DoSearchForQQMusicMobile 匿名时
 *           `item_song` 恒为空（返回体里直接给了登录页 feedbackURL）。
 *   - 取流：`u.y.qq.com/cgi-bin/musicu.fcg` 的 `vkey.GetVkeyServer/CgiGetVkey`，
 *           **必须登录**（匿名 result=104003 无权限，与项目既有结论一致）。
 *           VIP 鉴权参数 authst 取自 cookie 里的 qm_keyst / qqmusic_key。
 *
 * Cookie：~/.config/hov/cookies/qqmusic.txt（Netscape 格式，与 Android 版导出的一致）
 */
class QQMusicApi : public QObject {
    Q_OBJECT
public:
    struct Song {
        QString mid;
        QString name;
        QString artist;
        QString album;
        QString albumMid;      // 专辑 mid：封面 URL = y.gtimg.cn/music/photo_new/T002R300x300M000<albumMid>.jpg
        int durationSec = 0;
    };

    explicit QQMusicApi(QObject *parent = nullptr);

    static QString cookieFile();
    static bool hasCookie();

    void setStatusHandler(std::function<void(const QString &)> h) { status_ = std::move(h); }
    /** 国内接口强制直连（设置页 network.forceDirectDomestic）：给本对象的网络管理器禁用代理 */
    void setForceDirect(bool on);

    /** 关键词搜索（网页版接口，匿名可用） */
    void search(const QString &keyword, int limit, std::function<void(const QVector<Song> &)> done);

    /** 取歌词（LRC 明文 + 翻译；接口匿名可用，实测 84 行带时间标签）
     *  接口：c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?format=json&nobase64=1 */
    void lyric(const QString &mid, std::function<void(const QString &lrc, const QString &trans)> done);

    /** 取播放地址；tier = lossless / exhigh / standard。返回空 url 时 err 说明原因 */
    void streamUrl(const QString &mid, const QString &tier,
                   std::function<void(const QString &url, const QString &err)> done);

private:
    struct Cookie {
        QString header;        // 完整 Cookie 头
        QString uin;           // cookie 原值（QQ 登录可能形如 o123456）
        QString uinNumeric;    // 纯数字形式（部分场景服务端只认数字）
        QString musickey;      // qm_keyst / qqmusic_key
    };
    Cookie readCookie() const;
    void vkey(const QString &mid, const QString &filename, const Cookie &ck, bool anon,
              std::function<void(const QString &url, int result, const QString &err)> done);

    QNetworkAccessManager *net_ = nullptr;
    std::function<void(const QString &)> status_;
};
