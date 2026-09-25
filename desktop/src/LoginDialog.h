#pragma once
#include <QDialog>
#include <QVector>

class QLabel;
class QNetworkCookie;
class QTimer;

#ifdef HOV_WEBENGINE
class QWebEngineView;
class QWebEngineProfile;
#endif

/**
 * App 内登录（桌面端）
 *
 * 与 Android 版同一目标：**不让用户手动导出 cookie** —— 在应用内打开站点登录页，
 * 登录成功后把 cookie 自动写成 Netscape 格式（与 Android 导出的完全同格式），
 * 供 NetEaseApi / QQMusicApi / UrlResolver(yt-dlp) 直接读取。
 *
 * 依赖 QtWebEngine（库/安装包构建都有）。Flatpak 的 org.kde.Platform 运行时**不含**
 * QtWebEngine，因此那里编译时不会定义 HOV_WEBENGINE，调用方需给出降级提示
 * （可手动放置 cookie 文件到 ~/.config/hov/cookies/）。
 *
 * 自动保存：每 3 秒检查一次 cookie 变化，有变化就落盘 —— 用户登录后无需额外点击。
 */
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    /** site: youtube / bilibili / netease / qqmusic */
    explicit LoginDialog(const QString &site, QWidget *parent = nullptr);
    ~LoginDialog() override;

    static QString cookieFileFor(const QString &site);
    static QString loginUrlFor(const QString &site);
    /** 是否具备内置浏览器组件（编译期决定） */
    static bool available();

private:
    void saveCookies();

    QString site_;
    QLabel *status_ = nullptr;
    QTimer *autoSave_ = nullptr;
    int lastCount_ = -1;
#ifdef HOV_WEBENGINE
    QWebEngineView *view_ = nullptr;
    QWebEngineProfile *profile_ = nullptr;
    QVector<QNetworkCookie> cookies_;
    /** 析构守卫：profile 析构期间 Chromium 仍可能回调 cookieAdded（实测 SEGV ✗），
     *  回调先检查本标志并直接返回（避坑 #268 ✓）。 */
    bool closing_ = false;
#endif
};
