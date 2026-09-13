package com.hougelangley.hyperonlinevideo.data

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import java.net.HttpURLConnection
import java.net.URL

/**
 * 直连网络路由（音乐平台专用）
 *
 * 背景：网易云 / QQ音乐 的接口与 CDN 对代理、海外出口敏感（登录态 URL 走代理会被判 403），
 * 而 YouTube 等平台又必须走代理。系统级 VPN 会把整个 App 的流量都带走，App 无法把自己从
 * VPN 里"摘出去"；但 App 可以**主动把请求绑定到非 VPN 网络**（Wi-Fi/蜂窝）。
 *
 * 本工具的做法：
 *  - [directNetwork] 找出一个非 VPN、可上网的网络（优先 Wi-Fi > 以太网 > 蜂窝）
 *  - [open] 用该网络打开连接（Java 层 HTTP 生效）
 *  - native 播放器（mpv）无法逐 socket 绑定 → 由 [StreamProxy] 在本机转发
 */
object NetRoute {

    @Volatile private var appContext: Context? = null

    fun init(ctx: Context) {
        appContext = ctx.applicationContext
    }

    private fun cm(): ConnectivityManager? =
        appContext?.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager

    /** 当前是否存在活动的 VPN 网络（仅用于日志/诊断） */
    fun vpnActive(): Boolean {
        val c = cm() ?: return false
        return c.allNetworks.any { n ->
            c.getNetworkCapabilities(n)?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) == true
        }
    }

    /** 选择直连网络：排除 VPN，优先 Wi-Fi > 以太网 > 蜂窝 */
    fun directNetwork(): Network? {
        val c = cm() ?: return null
        val candidates = c.allNetworks.mapNotNull { n ->
            c.getNetworkCapabilities(n)?.let { cap -> n to cap }
        }.filter { (_, cap) ->
            cap.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
                !cap.hasTransport(NetworkCapabilities.TRANSPORT_VPN)
        }
        fun pick(transport: Int) = candidates.firstOrNull { it.second.hasTransport(transport) }?.first
        return pick(NetworkCapabilities.TRANSPORT_WIFI)
            ?: pick(NetworkCapabilities.TRANSPORT_ETHERNET)
            ?: pick(NetworkCapabilities.TRANSPORT_CELLULAR)
            ?: candidates.firstOrNull()?.first
    }

    /**
     * 打开连接：有直连网络时绑定直连网络，否则退回系统默认（例如只剩 VPN 可用时）。
     * timeout 同时设置连接与读取超时。
     */
    fun open(url: String, timeoutMs: Int = 15000): HttpURLConnection {
        val net = try { directNetwork() } catch (e: Exception) { null }
        val conn: java.net.URLConnection = try {
            if (net != null) net.openConnection(URL(url)) else URL(url).openConnection()
        } catch (e: Exception) {
            URL(url).openConnection()   // 直连网络失效时兜底
        }
        return (conn as HttpURLConnection).apply {
            connectTimeout = timeoutMs
            readTimeout = timeoutMs
        }
    }
}
