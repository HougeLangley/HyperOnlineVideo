package com.hougelangley.hyperonlinevideo.data

import android.content.Context
import android.content.SharedPreferences

/** 播放进度记忆（key = 网页地址 / 本地文件路径） */
object PlaybackHistory {

    private lateinit var prefs: SharedPreferences

    fun init(context: Context) {
        prefs = context.applicationContext.getSharedPreferences("playback_history", Context.MODE_PRIVATE)
    }

    /** 上次播放位置（秒）；无记录返回 0 */
    fun positionSec(key: String): Double {
        if (key.isEmpty() || !::prefs.isInitialized) return 0.0
        return prefs.getFloat("pos_$key", 0f).toDouble()
    }

    fun save(key: String, posSec: Double, title: String) {
        if (key.isEmpty() || !::prefs.isInitialized) return
        prefs.edit()
            .putFloat("pos_$key", posSec.toFloat())
            .putString("title_$key", title)
            .putLong("at_$key", System.currentTimeMillis())
            .apply()
    }

    fun clear(key: String) {
        if (key.isEmpty() || !::prefs.isInitialized) return
        prefs.edit().remove("pos_$key").remove("title_$key").remove("at_$key").apply()
    }
}
