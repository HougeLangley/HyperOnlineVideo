package com.hougelangley.hyperonlinevideo.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test

/** 播放队列单元测试（P1：顺序 / 单曲 / 随机） */
class PlayQueueTest {

    private fun item(i: Int) = VideoItem(
        id = "id$i", title = "曲目$i", url = "https://x/$i",
        durationSec = 100, uploader = "歌手", cover = "",
    )

    @Before
    fun reset() {
        PlayQueue.clear()
        while (PlayQueue.mode.value != PlayQueue.Mode.SEQUENTIAL) PlayQueue.cycleMode()
    }

    @Test
    fun `顺序模式：末尾返回 null 非末尾返回下一项`() {
        PlayQueue.setList(listOf(item(1), item(2), item(3)), "netease", 0)
        assertEquals("曲目2", PlayQueue.peekNext(auto = true)?.item?.title)
        assertNull(PlayQueue.peekPrev())                        // 首项没有上一条
        PlayQueue.jumpTo(2)
        assertNull(PlayQueue.peekNext(auto = true))             // 已到末尾
        assertEquals("曲目2", PlayQueue.peekPrev()?.item?.title) // 上一条存在
    }

    @Test
    fun `单曲循环：自动续播返回自身 手动切歌走下一项`() {
        PlayQueue.setList(listOf(item(1), item(2)), "netease", 0)
        PlayQueue.cycleMode()                              // SEQUENTIAL → REPEAT_ONE
        assertEquals(PlayQueue.Mode.REPEAT_ONE, PlayQueue.mode.value)
        assertEquals("曲目1", PlayQueue.peekNext(auto = true)?.item?.title)
        assertEquals("曲目2", PlayQueue.peekNext(auto = false)?.item?.title)
    }

    @Test
    fun `随机模式：多项时不会返回自身 单项时自动续播返回自身`() {
        PlayQueue.setList(listOf(item(1), item(2), item(3)), "netease", 0)
        PlayQueue.cycleMode(); PlayQueue.cycleMode()        // → SHUFFLE
        assertEquals(PlayQueue.Mode.SHUFFLE, PlayQueue.mode.value)
        val next = PlayQueue.peekNext(auto = true)
        assertNotEquals("曲目1", next?.item?.title)         // 不会随机到自己
        PlayQueue.setList(listOf(item(9)), "netease", 0)
        assertEquals("曲目9", PlayQueue.peekNext(auto = true)?.item?.title)
    }

    @Test
    fun `模式循环切换 与 队列清空`() {
        PlayQueue.cycleMode(); assertEquals(PlayQueue.Mode.REPEAT_ONE, PlayQueue.mode.value)
        PlayQueue.cycleMode(); assertEquals(PlayQueue.Mode.SHUFFLE, PlayQueue.mode.value)
        PlayQueue.cycleMode(); assertEquals(PlayQueue.Mode.SEQUENTIAL, PlayQueue.mode.value)
        PlayQueue.setList(listOf(item(1)), "youtube", 0)
        assertEquals(1, PlayQueue.size)
        PlayQueue.clear()
        assertEquals(0, PlayQueue.size)
        assertNull(PlayQueue.peekNext(auto = true))
    }
}
