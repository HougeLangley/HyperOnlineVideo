package com.hougelangley.hyperonlinevideo

import android.app.Activity
import android.os.Bundle
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import android.webkit.CookieManager
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import java.io.File

/** 平台登录页：WebView 官方登录 → CookieManager 抓取 → Netscape 格式落盘（供 yt-dlp 使用） */
class LoginActivity : Activity() {

    companion object {
        private val PLATFORMS = mapOf(
            "youtube" to Triple(
                "https://accounts.google.com/ServiceLogin?service=youtube&continue=https://m.youtube.com",
                listOf(".google.com", ".youtube.com"),
                "YouTube（Google 账号）"
            ),
            "bilibili" to Triple(
                "https://passport.bilibili.com/login",
                listOf(".bilibili.com"),
                "Bilibili"
            ),
            "netease" to Triple(
                "https://music.163.com/#/login",
                listOf(".music.163.com", ".163.com"),
                "网易云音乐"
            ),
            "qqmusic" to Triple(
                // 实际不直接加载此 URL：先用内置选择页让用户选 QQ登录 / 微信登录（见 chooseHtml）
                QQ_LOGIN_URL,
                // 采集探针：QQ音乐的密钥可能挂在不同子域上（qm_keyst / qqmusic_key），
                // 每个探针会返回「该 URL 适用的全部 cookie」（含父域），多探针取并集更稳
                listOf(".qq.com", ".y.qq.com", ".c.y.qq.com", ".u.y.qq.com", ".i.y.qq.com", ".music.qq.com"),
                "QQ音乐"
            )
        )

        // ---- QQ音乐两种登录方式的官方入口（来自 y.qq.com/vip/select_logintype.html 源码）----
        // QQ 与微信账号的音乐资产/付费特权不互通，因此必须让用户自己选对入口
        private const val SURL = "https%3A%2F%2Fy.qq.com%2F"

        /**
         * QQ登录：graph.qq.com（which=Login 才会直接渲染登录界面：手机扫码 + 密码登录）
         * 回调 y.qq.com/portal/wx_redirect.html?login_type=1 写入 qm_keyst
         */
        private const val QQ_LOGIN_URL =
            "https://graph.qq.com/oauth2.0/show?which=Login&display=pc&response_type=code&client_id=100497308" +
                "&redirect_uri=https%3A%2F%2Fy.qq.com%2Fportal%2Fwx_redirect.html%3Flogin_type%3D1%26surl%3D" + SURL +
                "%26use_customer_cb%3D0&scope=get_user_info%2Cget_app_friends"

        /** 微信登录：open.weixin.qq.com 扫码 → 回调 y.qq.com/vip/wx_redirect.html?login_type=2（写 qqmusic_key） */
        private const val WX_LOGIN_URL =
            "https://open.weixin.qq.com/connect/qrconnect?appid=wx48db31d50e334801" +
                "&redirect_uri=https%3A%2F%2Fy.qq.com%2Fvip%2Fwx_redirect.html%3Flogin_type%3D2%26surl%3D" + SURL +
                "&response_type=code&scope=snsapi_login&state=STATE" +
                "&href=https%3A%2F%2Fy.gtimg.cn%2Fmediastyle%2Fyqq%2Fpopup_wechat.css#wechat_redirect"

        /** 内置选择页：QQ登录 / 微信登录（原生 HTML，移动端友好，避免官方 700px 桌面弹窗被裁切） */
        private fun chooseHtml(): String = """
            <!DOCTYPE html><html><head><meta charset="utf-8">
            <meta name="viewport" content="width=device-width, initial-scale=1">
            <style>
              body { margin:0; background:#121212; color:#eee;
                     font-family:-apple-system,'PingFang SC','Noto Sans CJK SC',sans-serif;
                     display:flex; flex-direction:column; align-items:center; justify-content:center;
                     min-height:100vh; box-sizing:border-box; padding:24px; }
              h1 { font-size:20px; font-weight:600; margin:0 0 10px; }
              .tip { color:#9aa0a6; font-size:13px; text-align:center; line-height:1.7; margin:0 0 8px; }
              .tip b { color:#ff8a80; }
              a.btn { display:block; width:100%; max-width:420px; box-sizing:border-box;
                      margin:10px 0; padding:16px 0; text-align:center; border-radius:14px;
                      font-size:17px; color:#fff; text-decoration:none; }
              .qq { background:#12B7F5; }
              .wx { background:#07C160; }
              .sub { color:#7f858b; font-size:12px; margin-top:14px; text-align:center; line-height:1.7; }
            </style></head><body>
              <h1>选择 QQ音乐 登录方式</h1>
              <p class="tip">QQ 与微信账号的音乐资产、付费特权<b>不互通</b><br>请选择你开通会员/购买数字专辑时用的账号</p>
              <a class="btn qq" href="$QQ_LOGIN_URL">QQ 登录</a>
              <a class="btn wx" href="$WX_LOGIN_URL">微信登录（扫码）</a>
              <p class="sub">微信登录为扫码方式：可截图后用微信「扫一扫 → 右上角相册」识别</p>
            </body></html>
        """.trimIndent()
    }

    private lateinit var platform: String
    private lateinit var webView: WebView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        platform = intent.getStringExtra("platform") ?: "bilibili"
        val (loginUrl, domains, displayName) = PLATFORMS[platform] ?: PLATFORMS["bilibili"]!!

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(0xFF121212.toInt())
            fitsSystemWindows = true
            setPadding(0, 96, 0, 0)
        }

        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(0xFF1F1F1F.toInt())
            setPadding(40, 0, 24, 0)
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(64))
        }
        val title = TextView(this).apply {
            text = "登录 $displayName\n成功后自动返回；也可点「完成」或边缘滑动返回"
            setTextColor(0xFFFFFFFF.toInt())
            textSize = 15f
            gravity = android.view.Gravity.CENTER_VERTICAL
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f)
        }
        val doneBtn = Button(this).apply {
            text = "完成"
            textSize = 15f
            setOnClickListener { harvestAndFinish(domains, manual = true) }
        }
        header.addView(title)
        header.addView(doneBtn)
        root.addView(header)

        webView = WebView(this)
        webView.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            builtInZoomControls = true          // 支持双指缩放（桌面版页面字小可放大）
            displayZoomControls = false
            setSupportZoom(true)
            if (platform == "qqmusic") {
                // QQ音乐登录页是 700px 桌面弹窗（QQ登录/微信登录 两个 tab + iframe），
                // 需按桌面宽度渲染再缩放到屏幕，否则右侧内容会被裁掉
                useWideViewPort = true
                loadWithOverviewMode = true
            }
            userAgentString =
                "Mozilla/5.0 (Linux; Android 16; 2509FPN0BC) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Mobile Safari/537.36"
        }
        webView.isFocusable = true
        webView.isFocusableInTouchMode = true
        webView.setOnTouchListener { v, event ->
            v.requestFocus()
            // 仅账号密码型页面自动弹键盘；扫码/一键登录页面不弹（避免遮挡二维码）
            if (event.action == android.view.MotionEvent.ACTION_UP && platform != "qqmusic") {
                (getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager)
                    .showSoftInput(v, InputMethodManager.SHOW_IMPLICIT)
            }
            false
        }
        CookieManager.getInstance().setAcceptCookie(true)
        CookieManager.getInstance().setAcceptThirdPartyCookies(webView, true)
        webView.webViewClient = object : WebViewClient() {
            override fun onPageFinished(view: WebView?, url: String?) {
                view?.requestFocus()
                val cookies = domains.joinToString("; ") {
                    CookieManager.getInstance().getCookie("https://" + it.removePrefix(".")) ?: ""
                }
                val ok = when (platform) {
                    "bilibili" -> cookies.contains("SESSDATA")
                    "netease" -> cookies.contains("MUSIC_U=")
                    // QQ登录与微信登录写入的密钥 cookie 名称不同（qm_keyst / qqmusic_key），两者都算已登录
                    "qqmusic" -> cookies.contains("qm_keyst=") || cookies.contains("qqmusic_key=")
                    else -> cookies.contains("SID=")
                }
                if (ok) harvestAndFinish(domains, manual = false)
            }
        }
        root.addView(webView, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)
        if (platform == "qqmusic") {
            webView.loadDataWithBaseURL("https://y.qq.com/", chooseHtml(), "text/html", "utf-8", null)
        } else {
            webView.loadUrl(loginUrl)
        }
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (::webView.isInitialized && webView.canGoBack()) webView.goBack() else finish()
    }

    private fun harvestAndFinish(domains: List<String>, manual: Boolean) {
        try {
            val cm = CookieManager.getInstance()
            val sb = StringBuilder("# Netscape HTTP Cookie File\n")
            var count = 0
            val seen = HashSet<String>()
            val names = StringBuilder()
            for (domain in domains) {
                val url = "https://" + domain.removePrefix(".")
                val cookieStr = cm.getCookie(url) ?: continue
                for (pair in cookieStr.split("; ")) {
                    val idx = pair.indexOf('=')
                    if (idx <= 0) continue
                    val name = pair.substring(0, idx)
                    val value = pair.substring(idx + 1)
                    if (!seen.add("$domain|$name")) continue
                    sb.append("$domain\tTRUE\t/\tTRUE\t2000000000\t$name\t$value\n")
                    names.append(name).append(' ')
                    count++
                }
            }
            android.util.Log.i("HOV", "cookie 采集[$platform] $count 项: $names")
            if (count == 0) {
                // 不写空文件（否则会覆盖已登录状态，且 hasCookies 只检查存在性）
                if (manual) Toast.makeText(this, "未检测到登录信息，请先完成登录", Toast.LENGTH_SHORT).show()
                return
            }
            val f = File(filesDir, "cookies/$platform.txt")
            f.parentFile?.mkdirs()
            f.writeText(sb.toString())
            cm.flush()
            finish()
        } catch (e: Exception) {
            if (!manual) finish()
        }
    }

    override fun onDestroy() {
        if (::webView.isInitialized) webView.destroy()
        super.onDestroy()
    }
}
