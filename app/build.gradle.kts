plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

import java.util.Properties

/** Release 签名：从 ~/keystores/keystore.properties 读取（不入库） */
val keystoreProps = Properties().apply {
    val f = file(System.getProperty("user.home") + "/keystores/keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}

android {
    namespace = "com.hougelangley.hyperonlinevideo"
    // compileSdk 保持 37：**依赖元数据要求**（实测降到 36 会让 checkDebugAarMetadata 失败 ✗）。
    // 真正修掉的是下面 targetSdk：它曾误填 34（比 minSdk 35 还小 ✗）。
    compileSdk = 37

    defaultConfig {
        applicationId = "com.hougelangley.hov"
        minSdk = 35
        targetSdk = 36
        versionCode = 5
        versionName = "1.2.2"

        ndk {
            abiFilters += "arm64-v8a"   // 只带 arm64，砍掉模拟器 ABI 体积
        }
    }

    signingConfigs {
        create("release") {
            if (keystoreProps.isNotEmpty()) {
                storeFile = file(keystoreProps.getProperty("storeFile"))
                storePassword = keystoreProps.getProperty("storePassword")
                keyAlias = keystoreProps.getProperty("keyAlias")
                keyPassword = keystoreProps.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true          // R8 代码混淆与裁剪
            isShrinkResources = true        // 移除未引用资源
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
            signingConfig = if (keystoreProps.isNotEmpty()) signingConfigs.getByName("release") else null
        }
        debug {
            applicationIdSuffix = ""        // debug 与 release 同包名，便于覆盖安装验证
        }
    }

    buildFeatures {
        compose = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true   // yt-dlp/ffmpeg 原生库需解压模式（等效 extractNativeLibs=true）
        }
    }
}

dependencies {
    implementation(project(":core"))          // 共享核心（P1 抽取）
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")   // 单测环境提供真实 JSONObject
    val composeBom = platform("androidx.compose:compose-bom:2026.09.00")
    implementation(composeBom)
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.11.0")
    implementation("io.coil-kt:coil-compose:2.7.0")
    implementation("io.coil-kt:coil:2.7.0")

    // 核心能力（与原 Godot 版一致）
    implementation("io.github.junkfood02.youtubedl-android:library:0.18.1")
    implementation("io.github.junkfood02.youtubedl-android:ffmpeg:0.18.1")
    implementation("io.github.abdallahmehiz:mpv-android-lib:0.1.12")
}
