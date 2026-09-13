plugins {
    id("com.android.library")
}

android {
    namespace = "com.hougelangley.hyperonlinevideo.core"
    compileSdk = 37

    defaultConfig {
        minSdk = 35
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    // 共享核心：平台无关逻辑（数据模型 / 字幕与歌词解析 / 播放队列）
    // 后续 P1 会把搜索、yt-dlp 调用、音乐平台签名、下载队列也迁进来；
    // 再往后加 linuxX64 / macosArm64 目标即可供桌面端复用（KMP 化）
    api("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.2")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
}
