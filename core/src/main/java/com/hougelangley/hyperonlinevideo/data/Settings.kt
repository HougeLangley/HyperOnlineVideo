package com.hougelangley.hyperonlinevideo.data

import android.content.Context
import android.content.SharedPreferences

/**
 * 应用设置（SharedPreferences 持久化）
 *
 * 目前覆盖：
 *  - 播放：进度记忆续播 / 播完自动连播 / 手势调节（亮度·音量）
 *  - 音乐：音质上限（自动最高 / 320k / 128k）
 *  - 下载：仅 WiFi 下载
 */
object Settings {

    const val VIDEO_CAP_AUTO = 0           // 视频清晰度上限"自动"（=平台默认解析 ✓）
    const val QUALITY_AUTO = "auto"        // 无损 → 320k → 128k
    const val QUALITY_HIGH = "high"        // 320k → 128k
    const val QUALITY_STANDARD = "standard" // 128k

    private const val PREF = "hov_settings"
    private const val K_RESUME = "resume_playback"
    private const val K_AUTO_NEXT = "auto_next"
    private const val K_GESTURES = "gestures"
    private const val K_QUALITY = "music_quality"
    private const val K_VIDEO_MAX_HEIGHT = "video_max_height"   // 视频清晰度上限（0=自动；与桌面 video.maxHeight 同语义 ✓）
    private const val K_WIFI_ONLY = "wifi_only_download"
    private const val K_FILL_SCREEN = "fill_screen_fullscreen"
    private const val K_THEME = "ui_theme"            // auto=跟随系统 / dark / light（与另两端同键语义 ✓）
    private const val K_SUB_AUTO = "subtitle_auto_show"   // 默认自动显示字幕（按系统语言选轨 ✓ 用户可关 ✓）
    private const val K_SUB_SCALE = "subtitle_font_scale" // 字幕字号缩放（与 Linux/macOS 的 subtitle.fontScale 同语义 ✓）

    @Volatile private var sp: SharedPreferences? = null

    fun init(ctx: Context) {
        if (sp == null) sp = ctx.applicationContext.getSharedPreferences(PREF, Context.MODE_PRIVATE)
    }

    /** 字幕默认显示：开启后按**系统语言**自动选轨并显示（用户 2026-09-18 要求 ✓） */
    var subtitleAutoShow: Boolean
        get() = sp?.getBoolean(K_SUB_AUTO, true) ?: true
        set(v) { sp?.edit()?.putBoolean(K_SUB_AUTO, v)?.apply() }

    /**
     * 字幕字号缩放：0.5 ~ 2.0（默认 1.0）。
     * 与 Linux/macOS 的 `subtitle.fontScale` **同语义同范围**（那边也是 0.5~2.0 ✓
     * 见 desktop/src/Settings.cpp 的校验 ✓）—— Android 字幕走 App 层 TextView（libass 渲染不出字 ✗），
     * 故在 PlayerActivity 里以 `17f × scale` 应用（基准 17 不变 ✓ 只加缩放 ✓）。
     */
    var subtitleFontScale: Float
        get() = (sp?.getFloat(K_SUB_SCALE, 1.0f) ?: 1.0f).coerceIn(0.5f, 2.0f)
        set(v) { sp?.edit()?.putFloat(K_SUB_SCALE, v.coerceIn(0.5f, 2.0f))?.apply() }

    /** 应用主题：auto=跟随系统 light/dark（用户 2026-09-18 要求）/ dark / light */
    var theme: String
        get() = sp?.getString(K_THEME, "auto") ?: "auto"
        set(v) { sp?.edit()?.putString(K_THEME, v)?.apply() }

    /** 进度记忆：退出/重进续播 */
    var resumePlayback: Boolean
        get() = sp?.getBoolean(K_RESUME, true) ?: true
        set(v) { sp?.edit()?.putBoolean(K_RESUME, v)?.apply() }

    /** 播完自动连播下一项（关闭则播完退出播放器） */
    var autoNext: Boolean
        get() = sp?.getBoolean(K_AUTO_NEXT, true) ?: true
        set(v) { sp?.edit()?.putBoolean(K_AUTO_NEXT, v)?.apply() }

    /** 手势调节：左半屏亮度 / 右半屏音量 */
    var gesturesEnabled: Boolean
        get() = sp?.getBoolean(K_GESTURES, true) ?: true
        set(v) { sp?.edit()?.putBoolean(K_GESTURES, v)?.apply() }

    /** 音乐音质上限 */
    var musicQuality: String
        get() = sp?.getString(K_QUALITY, QUALITY_AUTO) ?: QUALITY_AUTO
        set(v) { sp?.edit()?.putString(K_QUALITY, v)?.apply() }

    /** 仅 WiFi（含以太网）下载 */
    var wifiOnlyDownload: Boolean
        get() = sp?.getBoolean(K_WIFI_ONLY, false) ?: false
        set(v) { sp?.edit()?.putBoolean(K_WIFI_ONLY, v)?.apply() }

    /**
     * 全屏时铺满屏幕（裁切掉比例差导致的左右黑边）。
     * 手机屏幕多为 20:9，而视频多为 16:9 → 保持比例时左右会各留一条黑边；
     * 开启后 mpv 用 panscan 裁切填满（画面上下少量裁切）。
     */
    var fillScreen: Boolean
        get() = sp?.getBoolean(K_FILL_SCREEN, true) ?: true
        set(v) { sp?.edit()?.putBoolean(K_FILL_SCREEN, v)?.apply() }

    /** 视频清晰度上限（0=自动=平台默认；1080/720/480/360 等=解析时选不超过它的最高档 ✓ 仅降不升 ✓） */
    var videoMaxHeight: Int
        get() = sp?.getInt(K_VIDEO_MAX_HEIGHT, 0) ?: 0
        set(v) { sp?.edit()?.putInt(K_VIDEO_MAX_HEIGHT, v)?.apply() }

    fun qualityLabel(): String = when (musicQuality) {
        QUALITY_HIGH -> "320k"
        QUALITY_STANDARD -> "128k"
        else -> "自动最高"
    }
}
