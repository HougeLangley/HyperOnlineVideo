#include "LoginDialog.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QNetworkCookie>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#ifdef HOV_WEBENGINE
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QWebEngineView>
#endif

QStringList LoginDialog::knownSites() {
    return { "youtube", "bilibili", "netease", "qqmusic" };
}

QString LoginDialog::cookieFileFor(const QString &site) {
    return QDir::homePath() + "/.config/hov/cookies/" + site + ".txt";
}

QString LoginDialog::loginUrlFor(const QString &site) {
    if (site == "youtube") return "https://accounts.google.com/ServiceLogin?service=youtube";
    if (site == "bilibili") return "https://passport.bilibili.com/login";
    if (site == "netease") return "https://music.163.com/#/login";
    if (site == "qqmusic") return "https://y.qq.com/";
    return "about:blank";
}

bool LoginDialog::available() {
#ifdef HOV_WEBENGINE
    return true;
#else
    return false;
#endif
}

LoginDialog::LoginDialog(const QString &site, QWidget *parent) : QDialog(parent), site_(site) {
    setWindowTitle(QString("登录 %1").arg(site));
    resize(1000, 720);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setText(QString("在下方页面登录；登录态会**自动**保存到 %1（无需手动导出 cookie）")
                         .arg(cookieFileFor(site)));
    root->addWidget(status_);

#ifdef HOV_WEBENGINE
    // 独立 profile：与系统浏览器隔离，cookie 只落在本应用目录
    profile_ = new QWebEngineProfile(QString("hov-%1").arg(site), this);
    profile_->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    profile_->setHttpCacheType(QWebEngineProfile::NoCache);

    view_ = new QWebEngineView(this);
    view_->setPage(new QWebEnginePage(profile_, view_));
    root->addWidget(view_, 1);

    // 收集 cookie：loadAllCookies 会把已有 cookie 逐个通过 cookieAdded 发出来
    connect(profile_->cookieStore(), &QWebEngineCookieStore::cookieAdded, this,
            [this](const QNetworkCookie &c) {
                for (auto &x : cookies_)
                    if (x.name() == c.name() && x.domain() == c.domain()) { x = c; return; }
                cookies_.append(c);
            });
    profile_->cookieStore()->loadAllCookies();

    auto *btns = new QPushButton("立即保存登录态", this);
    connect(btns, &QPushButton::clicked, this, [this] { saveCookies(); });
    root->addWidget(btns);

    view_->load(QUrl(loginUrlFor(site)));

    // 自动保存：每 3 秒看一次 cookie 数量变化
    autoSave_ = new QTimer(this);
    autoSave_->setInterval(3000);
    connect(autoSave_, &QTimer::timeout, this, [this] {
        if (cookies_.size() != lastCount_) saveCookies();
    });
    autoSave_->start();
#else
    status_->setText(QString("此构建不含内置浏览器组件（QtWebEngine 未编入）。\n"
                             "请手动把 %1 的 cookie（Netscape 格式）放到：\n%2\n"
                             "Android 版可用同一份 cookie 文件，格式一致。")
                         .arg(site, cookieFileFor(site)));
    auto *close = new QPushButton("关闭", this);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    root->addWidget(close);
#endif
}

LoginDialog::~LoginDialog() {
#ifdef HOV_WEBENGINE
    saveCookies();   // 关窗前再存一次，避免刚登录就关闭丢失
#endif
}

void LoginDialog::saveCookies() {
#ifdef HOV_WEBENGINE
    if (cookies_.isEmpty()) {
        if (status_) status_->setText(QString("尚未捕获到 cookie（可能页面还没加载完）→ %1").arg(cookieFileFor(site_)));
        return;
    }
    const QString path = cookieFileFor(site_);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (status_) status_->setText(QString("写入失败：%1").arg(path));
        return;
    }
    // Netscape cookie 文件头（与 Android 导出的格式一致，yt-dlp/curl 都能直接吃）
    f.write("# Netscape HTTP Cookie File\n");
    f.write(QString("# 由《聚合视频》桌面端导出  %1\n"
                    "# 站点: %2\n\n")
                .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"), site_)
                .toUtf8());
    int written = 0;
    for (const auto &c : cookies_) {
        // Netscape 第 2 列（includeSubdomains）必须与域名是否带前导点**一致**：
        // Python 的 cookiejar 会断言 domain_specified == initial_dot，不一致直接报
        // "invalid Netscape format cookies file"（实测踩过：yt-dlp 读不了我们导出的文件）。
        // 因此：带点的域 → TRUE；host-only（无点）→ FALSE，且**不要**补点。
        QString domain = c.domain();
        const bool includeSub = domain.startsWith('.');
        if (!includeSub) domain = domain;       // host-only 保持无点
        QString value = QString::fromUtf8(c.value());
        value.replace('\t', ' ');
        QString name = QString::fromUtf8(c.name());
        name.replace('\t', ' ');
        f.write(QString("%1\t%2\t/\t%3\t%4\t%5\t%6\n")
                    .arg(domain, includeSub ? "TRUE" : "FALSE",
                         c.isSecure() ? "TRUE" : "FALSE",
                         QString::number(c.expirationDate().isValid()
                                             ? c.expirationDate().toSecsSinceEpoch()
                                             : 0),
                         name, value)
                    .toUtf8());
        ++written;
    }
    f.close();
    lastCount_ = cookies_.size();
    qInfo() << "登录态已保存:" << path << written << "条 cookie";
    if (status_)
        status_->setText(QString("已保存 %1 条 cookie → %2").arg(written).arg(path));
#endif
}
