package com.hougelangley.hyperonlinevideo.data

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * 播放队列（音乐 / 视频 / 本地 共用）
 *
 * - 从搜索结果或本地库整列表入队，从指定项开始播放
 * - 播放结束自动续播下一项；支持 顺序 / 单曲循环 / 随机
 * - 队列记录每项的 platform，续播时按各自平台解析（视频走 Repo.resolve，音乐走 Repo.resolveMusic）
 */
object PlayQueue {

    enum class Mode(val label: String) {
        SEQUENTIAL("顺序"),
        REPEAT_ONE("单曲"),
        SHUFFLE("随机"),
        REPEAT_ALL("列表循环"),          // 与 macOS 对齐：顺序 → 单曲 → 随机 → 列表循环
    }

    data class Entry(val item: VideoItem, val platform: String)

    private val _entries = MutableStateFlow<List<Entry>>(emptyList())
    val entries: StateFlow<List<Entry>> = _entries

    private val _index = MutableStateFlow(-1)
    val index: StateFlow<Int> = _index

    private val _mode = MutableStateFlow(Mode.SEQUENTIAL)
    val mode: StateFlow<Mode> = _mode

    val current: Entry? get() = _entries.value.getOrNull(_index.value)
    val size: Int get() = _entries.value.size

    /** 用整个列表建立队列，并从 start 开始播 */
    fun setList(items: List<VideoItem>, platform: String, start: Int) {
        if (items.isEmpty()) return
        _entries.value = items.map { Entry(it, platform) }
        _index.value = start.coerceIn(0, items.size - 1)
    }

    /** 单曲入队 */
    fun setEntry(item: VideoItem, platform: String) {
        _entries.value = listOf(Entry(item, platform))
        _index.value = 0
    }

    fun jumpTo(i: Int) {
        if (i in _entries.value.indices) _index.value = i
    }

    fun cycleMode() {
        _mode.value = when (_mode.value) {
            Mode.SEQUENTIAL -> Mode.REPEAT_ONE
            Mode.REPEAT_ONE -> Mode.SHUFFLE
            Mode.SHUFFLE -> Mode.REPEAT_ALL
            Mode.REPEAT_ALL -> Mode.SEQUENTIAL
        }
    }

    /**
     * 下一项（auto=true 表示"播完自动续播"：单曲循环会返回当前项，手动切歌则跳过）
     * SEQUENTIAL 到尾部返回 null（停止）
     */
    fun peekNext(auto: Boolean): Entry? {
        val list = _entries.value
        if (list.isEmpty()) return null
        val i = _index.value.coerceAtLeast(0)
        return when (_mode.value) {
            Mode.REPEAT_ONE -> if (auto) list.getOrNull(i) else list.getOrNull(i + 1)
            Mode.SHUFFLE -> when {
                list.size == 1 -> if (auto) list[0] else null
                else -> list[randomOtherIndex(list.size, i)]
            }
            Mode.SEQUENTIAL -> list.getOrNull(i + 1)
            Mode.REPEAT_ALL -> if (i + 1 < list.size) list[i + 1]
                                 else if (auto) list[0] else null
        }
    }

    /** 上一项（随机模式同样随机；到头部返回 null） */
    fun peekPrev(): Entry? {
        val list = _entries.value
        if (list.isEmpty()) return null
        val i = _index.value.coerceAtLeast(0)
        return when (_mode.value) {
            Mode.SHUFFLE -> if (list.size == 1) list[0] else list[randomOtherIndex(list.size, i)]
            else -> list.getOrNull(i - 1)
        }
    }

    /** 把队列游标移动到指定项（切歌成功后调用） */
    fun moveTo(entry: Entry) {
        val i = _entries.value.indexOf(entry)
        if (i >= 0) _index.value = i
    }

    fun clear() {
        _entries.value = emptyList()
        _index.value = -1
    }

    private fun randomOtherIndex(size: Int, cur: Int): Int {
        if (size <= 1) return 0
        var r: Int
        do { r = (0 until size).random() } while (r == cur)
        return r
    }
}
