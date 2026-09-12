package com.hougelangley.hyperonlinevideo

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.drawable.Icon
import android.media.MediaMetadata
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.os.IBinder

/**
 * 后台播放前台服务：媒体通知（MediaStyle）+ 锁屏/耳机媒体键控制（MediaSession）。
 * 播放本体在 PlayerActivity 的 mpv 实例中；本服务只负责"活着 + 可控制 + 可回到 App"。
 */
class PlaybackService : Service() {

    companion object {
        private const val ACTION_START = "com.hougelangley.hov.play.START"
        private const val ACTION_UPDATE = "com.hougelangley.hov.play.UPDATE"
        private const val ACTION_TOGGLE = "com.hougelangley.hov.play.TOGGLE"
        private const val ACTION_NEXT = "com.hougelangley.hov.play.NEXT"
        private const val ACTION_PREV = "com.hougelangley.hov.play.PREV"
        private const val ACTION_STOP = "com.hougelangley.hov.play.STOP"
        private const val CHANNEL_ID = "playback"
        private const val NOTIFY_ID = 1001

        /** 播放开始时调用（进入前台服务 + 通知） */
        fun start(ctx: Context, title: String) {
            val i = Intent(ctx, PlaybackService::class.java)
                .setAction(ACTION_START)
                .putExtra("title", title)
            ctx.startForegroundService(i)
        }

        /** 播放状态变化时调用（刷新通知与媒体会话） */
        fun update(ctx: Context, title: String, playing: Boolean) {
            if (!PlaybackController.started) return
            val i = Intent(ctx, PlaybackService::class.java)
                .setAction(ACTION_UPDATE)
                .putExtra("title", title)
                .putExtra("playing", playing)
            runCatching { ctx.startService(i) }
        }

        /** 播放器退出时调用（停止前台服务与通知） */
        fun stop(ctx: Context) {
            runCatching { ctx.stopService(Intent(ctx, PlaybackService::class.java)) }
        }
    }

    private var session: MediaSession? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "播放控制", NotificationManager.IMPORTANCE_LOW).apply {
                setShowBadge(false)
            }
        )
        session = MediaSession(this, "hov-playback").apply {
            setCallback(object : MediaSession.Callback() {
                override fun onPlay() {
                    if (!PlaybackController.playing) PlaybackController.listener?.onToggle()
                    else if (PlaybackController.listener == null) stopSelf()
                }

                override fun onPause() {
                    if (PlaybackController.playing) PlaybackController.listener?.onToggle()
                    else if (PlaybackController.listener == null) stopSelf()
                }

                override fun onStop() {
                    val l = PlaybackController.listener
                    if (l != null) l.onStopPlayback() else stopSelf()
                }

                override fun onSkipToNext() {
                    PlaybackController.listener?.onNext()
                }

                override fun onSkipToPrevious() {
                    PlaybackController.listener?.onPrev()
                }
            })
            isActive = true
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                val title = intent.getStringExtra("title").orEmpty()
                PlaybackController.started = true
                PlaybackController.title = title
                PlaybackController.playing = true
                startForeground(
                    NOTIFY_ID,
                    buildNotification(title, true),
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK
                )
                updateSession(title, true)
            }
            ACTION_UPDATE -> {
                val title = intent.getStringExtra("title").orEmpty()
                val playing = intent.getBooleanExtra("playing", true)
                PlaybackController.title = title
                PlaybackController.playing = playing
                getSystemService(NotificationManager::class.java)
                    .notify(NOTIFY_ID, buildNotification(title, playing))
                updateSession(title, playing)
            }
            ACTION_TOGGLE -> {
                val l = PlaybackController.listener
                if (l != null) l.onToggle() else stopSelf()
            }
            ACTION_STOP -> {
                val l = PlaybackController.listener
                if (l != null) l.onStopPlayback() else stopSelf()
            }
            ACTION_NEXT -> PlaybackController.listener?.onNext()
            ACTION_PREV -> PlaybackController.listener?.onPrev()
        }
        return START_NOT_STICKY
    }

    private fun updateSession(title: String, playing: Boolean) {
        session?.let { s ->
            s.setMetadata(
                MediaMetadata.Builder()
                    .putString(MediaMetadata.METADATA_KEY_TITLE, title)
                    .putString(MediaMetadata.METADATA_KEY_ARTIST, "聚合视频")
                    .build()
            )
            s.setPlaybackState(
                PlaybackState.Builder()
                    .setState(
                        if (playing) PlaybackState.STATE_PLAYING else PlaybackState.STATE_PAUSED,
                        PlaybackState.PLAYBACK_POSITION_UNKNOWN,
                        if (playing) 1f else 0f
                    )
                    .setActions(
                        PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE or
                            PlaybackState.ACTION_PLAY_PAUSE or PlaybackState.ACTION_STOP or
                            PlaybackState.ACTION_SKIP_TO_NEXT or PlaybackState.ACTION_SKIP_TO_PREVIOUS
                    )
                    .build()
            )
        }
    }

    private fun buildNotification(title: String, playing: Boolean): Notification {
        val open = PendingIntent.getActivity(
            this, 0,
            Intent(this, PlayerActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val togglePi = PendingIntent.getService(
            this, 1,
            Intent(this, PlaybackService::class.java).setAction(ACTION_TOGGLE),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val stopPi = PendingIntent.getService(
            this, 2,
            Intent(this, PlaybackService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val toggleIcon = Icon.createWithResource(
            this,
            if (playing) android.R.drawable.ic_media_pause else android.R.drawable.ic_media_play
        )
        val stopIcon = Icon.createWithResource(this, android.R.drawable.ic_menu_close_clear_cancel)
        val prevPi = PendingIntent.getService(
            this, 3,
            Intent(this, PlaybackService::class.java).setAction(ACTION_PREV),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val nextPi = PendingIntent.getService(
            this, 4,
            Intent(this, PlaybackService::class.java).setAction(ACTION_NEXT),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val prevIcon = Icon.createWithResource(this, android.R.drawable.ic_media_previous)
        val nextIcon = Icon.createWithResource(this, android.R.drawable.ic_media_next)
        session?.setSessionActivity(open)
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle(title.ifEmpty { "正在播放" })
            .setContentText("聚合视频 · 后台播放")
            .setContentIntent(open)
            .setOngoing(playing)
            .setOnlyAlertOnce(true)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .addAction(Notification.Action.Builder(prevIcon, "上一首", prevPi).build())
            .addAction(Notification.Action.Builder(toggleIcon, if (playing) "暂停" else "继续", togglePi).build())
            .addAction(Notification.Action.Builder(nextIcon, "下一首", nextPi).build())
            .addAction(Notification.Action.Builder(stopIcon, "关闭", stopPi).build())
            .setStyle(
                Notification.MediaStyle()
                    .setMediaSession(session?.sessionToken)
                    .setShowActionsInCompactView(0, 1, 2)
            )
            .build()
    }

    override fun onDestroy() {
        session?.isActive = false
        session?.release()
        session = null
        PlaybackController.started = false
        super.onDestroy()
    }
}
