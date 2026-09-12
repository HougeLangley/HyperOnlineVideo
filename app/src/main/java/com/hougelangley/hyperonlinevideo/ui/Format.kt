package com.hougelangley.hyperonlinevideo.ui

/**
 * 界面格式化纯函数（B2 拆分：从 HyperApp.kt 搬出，同包顶层函数 → 所有引用点零改动）。
 * 纯 Kotlin、无 Android 依赖，可直接单元测试。
 */

/** 时长：<1 小时 -> m:ss；>=1 小时 -> h:mm:ss；<=0 -> 空串 */
fun fmtDuration(sec: Int): String {
    if (sec <= 0) return ""
    val h = sec / 3600; val m = (sec % 3600) / 60; val s = sec % 60
    return if (h > 0) "%d:%02d:%02d".format(h, m, s) else "%d:%02d".format(m, s)
}

/** 体积：GB（1 位小数）/ MB / KB（整数） */
fun fmtBytes(b: Long): String = when {
    b >= 1L shl 30 -> "%.1f GB".format(b / 1073741824.0)
    b >= 1L shl 20 -> "%.0f MB".format(b / 1048576.0)
    else -> "%.0f KB".format(b / 1024.0)
}
