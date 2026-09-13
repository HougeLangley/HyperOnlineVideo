#include "NetPolicy.h"
#include <QDir>
#include <QStringList>

bool NetPolicy::hasEnvProxy() {
    for (const char *k : {"http_proxy", "https_proxy", "all_proxy",
                          "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY"}) {
        if (!qEnvironmentVariableIsEmpty(k)) return true;
    }
    return false;
}

bool NetPolicy::hasTunLikeInterface() {
    const QStringList names = QDir("/sys/class/net").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &n : names) {
        if (n.startsWith("tun") || n.startsWith("utun") || n.startsWith("wg") ||
            n.contains("clash") || n.contains("sing") || n.contains("mihomo")) return true;
    }
    return false;
}

QNetworkProxy NetPolicy::proxyFor(Service s, bool forceDirect) {
    // 国内服务 + 用户显式要求直连 → NoProxy（可绕过环境变量代理；对 TUN/fake-IP 无效）
    if (s == Domestic && forceDirect) return QNetworkProxy(QNetworkProxy::NoProxy);
    // 其余一律跟随系统（让用户的代理规则生效）
    return QNetworkProxy::DefaultProxy;
}

QString NetPolicy::hintForFailure(const QString &service) {
    const bool proxied = hasEnvProxy() || hasTunLikeInterface();
    if (!proxied) return QString("网络不可达：请检查网络连接（%1）").arg(service);
    if (service == "youtube")
        return "YouTube 需要代理：请确认代理已开启且 youtube.com / googlevideo.com 走代理";
    return QString("检测到代理，但 %1 的域名被走了代理 → 请在代理规则中把以下域名设为直连：\n"
                   "music.163.com / 126.net / y.qq.com / qqmusic.qq.com / gtimg.cn / "
                   "bilibili.com / bilivideo.com / hdslb.com").arg(service);
}
