#pragma once
#include <QNetworkProxy>
#include <QString>

// 国内网络环境适配（设计依据见文档 13-国内网络环境适配方案）
// 原则：分流的责任在代理工具，App 默认跟随系统；仅对"环境变量代理"可显式绕过
class NetPolicy {
public:
    enum Service { Domestic, Overseas };   // Domestic = 网易云/QQ/B站；Overseas = YouTube

    /** 是否存在环境变量代理（形态①：应用可绕过） */
    static bool hasEnvProxy();
    /** 是否存在 TUN 类接口（形态②：应用无法绕过，需用户配规则） */
    static bool hasTunLikeInterface();

    /** 按服务返回应使用的代理；forceDirect=true 时国内服务显式绕过（仅形态①有效） */
    static QNetworkProxy proxyFor(Service s, bool forceDirect);

    /** 探活失败时给出的可执行提示（指名域名 + 规则片段） */
    static QString hintForFailure(const QString &service);
};
