package com.hougelangley.hyperonlinevideo.data

/** 搜索结果条目 */
data class VideoItem(
    val id: String,
    val title: String,
    val url: String,
    val durationSec: Int,
    val uploader: String,
    val cover: String,
    val album: String = "",      // 音乐平台专用（视频为空）
) {
    /** 是否音乐条目（netease / qqmusic） */
    val isMusic: Boolean get() = id.startsWith("ne:") || id.startsWith("qq:")
}

/** 可播放/下载格式 */
data class MediaFormat(
    val url: String,
    val label: String,     // 清晰度描述
    val ext: String,
    val sizeBytes: Long,
)

/** 清晰度选项（YouTube=直链变体可直接切；B站=qn 需重新解析） */
data class QualityOption(
    val id: String,      // YouTube: format_id；B站: qn 字符串
    val label: String,   // 显示名，如 "1080p60" / "1080P"
    val url: String,     // YouTube 变体直链；B站为空
    val qn: Int,         // B站清晰度编号；YouTube 为 -1
) : java.io.Serializable

/** 在线字幕轨（B站 CC / YouTube 人工·自动字幕） */
data class SubTrack(
    val label: String,     // 显示名，如 "中文（中国）" / "English (auto)"
    val url: String,       // 字幕内容地址（B站 JSON / YouTube json3）
    val lang: String = "",
    val auto: Boolean = false,
) : java.io.Serializable

/** 解析结果 */
data class VideoDetail(
    val title: String,
    val uploader: String,
    val durationSec: Double,
    val cover: String,
    val webpageUrl: String,
    val formats: List<MediaFormat>,
    val audioUrl: String,  // YouTube 分离音轨（合流为空）
    val qualities: List<QualityOption> = emptyList(),
    val currentQualityId: String = "",
    val subtitleTracks: List<SubTrack> = emptyList(),   // 在线字幕（M14b）
)

/** 下载任务状态 */
data class DownloadTask(
    val id: String,
    val title: String,
    val progress: Float,   // 0..1
    val etaSec: Long,
    val state: State,
    val error: String = "",              // 失败原因（用于展示与重试）
) {
    enum class State { QUEUED, RUNNING, DONE, FAILED, CANCELED }
}

/** 本地库文件 */
data class LocalFile(
    val name: String,
    val path: String,
    val sizeBytes: Long,
    val mtime: Long,
) {
    /** 文件名解析出的歌手（下载命名规范：`歌手 - 歌名.ext`） */
    val artist: String get() = if (name.contains(" - ")) name.substringBefore(" - ") else ""

    /** 文件名解析出的标题（去掉扩展名） */
    val title: String get() =
        (if (name.contains(" - ")) name.substringAfter(" - ") else name).substringBeforeLast('.')

    /** 是否音频（决定卡片显示“歌手”还是“文件名”） */
    val isAudio: Boolean
        get() = path.substringAfterLast('.', "").lowercase() in
            setOf("mp3", "flac", "m4a", "aac", "wav", "ogg", "opus", "ape", "wma")
}

/** 搜索过滤器：sort 1=相关度 2=最新 3=播放最多；duration 0=全部 1=短 2=中 3=长 */
data class SearchFilters(
    val sort: Int = 1,
    val duration: Int = 0,
)

const val SEARCH_PAGE_SIZE = 20
