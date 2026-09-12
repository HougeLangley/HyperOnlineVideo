package com.hougelangley.hyperonlinevideo

/**
 * 播放器 ⇄ 后台播放服务 的静态桥。
 * Service 不直接持有 Activity，避免泄漏；Activity 存活期间注册 listener。
 */
object PlaybackController {
    interface Listener {
        /** 暂停/继续切换（来自媒体通知、锁屏、耳机媒体键） */
        fun onToggle()
        /** 下一首/下一集（来自通知按钮、锁屏、耳机媒体键） */
        fun onNext()
        /** 上一首 */
        fun onPrev()
        /** 关闭播放（来自通知「关闭」） */
        fun onStopPlayback()
    }

    @Volatile var listener: Listener? = null
    @Volatile var title: String = ""
    @Volatile var playing: Boolean = false
    /** 前台服务是否已启动（Service start 前不允许发 update） */
    @Volatile var started: Boolean = false
}
