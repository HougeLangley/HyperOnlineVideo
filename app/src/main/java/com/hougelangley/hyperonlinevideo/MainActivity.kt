package com.hougelangley.hyperonlinevideo

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.lifecycle.lifecycleScope
import com.hougelangley.hyperonlinevideo.data.Repo
import com.hougelangley.hyperonlinevideo.ui.HyperApp
import com.hougelangley.hyperonlinevideo.ui.HyperTheme
import com.hougelangley.hyperonlinevideo.ui.ThemeState
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        // 启动页退场：首帧就绪后延迟 450ms 再淡出（平台 SplashScreen，API 31+；minSdk 35 无需兼容库）
        splashScreen.setOnExitAnimationListener { splashView ->
            splashView.animate()
                .alpha(0f)
                .setStartDelay(450)
                .setDuration(300)
                .withEndAction { splashView.remove() }
                .start()
        }
        super.onCreate(savedInstanceState)
        setContent {
            // ⚠️ 这里**不能**先 load()：Settings.init 在下面的 Repo.init 里，且是异步的 ✗
            //（实测：先 load 会读到默认 "auto"，用户持久化的 light/dark 被忽略 ✓）→ 改为 init 完成后 load ✓
            HyperTheme { HyperApp() }
        }
        lifecycleScope.launch {
            Repo.init(applicationContext)
            ThemeState.load()
            // ── 自动化测试入口（**仅 debug 构建生效** ✓ 与桌面端 CLI 探针同思路 ✓）──
            // 用法：adb shell am start -n com.hougelangley.hov/.MainActivity \
            //         --es hov_autotest_url /sdcard/Download/x.mp4 --es hov_autotest_platform local
            // 说明：PlayerActivity 是 android:exported="false"（正确的安全设置 ✓ 不为测试削弱它 ✗）→
            //       所以由**导出的** MainActivity 接收参数并转发 ✓ 且只在 DEBUG 下 ✓
            val isDebuggable = (applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0
            if (isDebuggable) {   // 不依赖 BuildConfig（AGP8 默认不生成 ✓ 用运行时标志更稳 ✓）
                val autoUrl = intent.getStringExtra("hov_autotest_url")
                if (!autoUrl.isNullOrEmpty()) {
                    android.util.Log.i("HOV", "[AUTOTEST] 收到自动化播放请求: $autoUrl")
                    startActivity(android.content.Intent(this@MainActivity, PlayerActivity::class.java).apply {
                        putExtra("url", autoUrl)
                        putExtra("platform", intent.getStringExtra("hov_autotest_platform") ?: "local")
                        putExtra("title", "autotest")
                    })
                }
            }                       // 设置就绪后再读持久化主题 ✓（会触发一次重组 ✓）
        }
        // Android 13+ 通知权限（后台播放的媒体通知需要）
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 101)
        }
    }
}
