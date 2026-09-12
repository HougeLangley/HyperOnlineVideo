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
            HyperTheme { HyperApp() }
        }
        lifecycleScope.launch { Repo.init(applicationContext) }
        // Android 13+ 通知权限（后台播放的媒体通知需要）
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 101)
        }
    }
}
