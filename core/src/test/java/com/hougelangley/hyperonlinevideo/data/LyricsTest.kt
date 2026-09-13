package com.hougelangley.hyperonlinevideo.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** 歌词解析单元测试（P3：网易云 lrc + QQ LRC） */
class LyricsTest {

    @Test
    fun `解析 mm-ss-xx 与 mm-ss-xxx 两种时间轴`() {
        val lrc = """
            [00:01.00]第一句
            [00:03.500]第二句
            [01:02:25]冒号毫秒
        """.trimIndent()
        val lines = Lyrics.parseLrc(lrc)
        assertEquals(3, lines.size)
        assertEquals(1000L, lines[0].timeMs)
        assertEquals("第一句", lines[0].text)
        assertEquals(3500L, lines[1].timeMs)      // 三位小数按前三位取
        assertEquals(62_250L, lines[2].timeMs)    // mm:ss:xx（冒号分隔毫秒）
    }

    @Test
    fun `忽略元信息行 空行 与无时间戳行`() {
        val lrc = """
            [ti:歌名]
            [ar:歌手]
            [by:某人]
            [00:05.00]唯一一行
            这是一句没有时间戳的说明
            [00:06.00]
        """.trimIndent()
        val lines = Lyrics.parseLrc(lrc)
        assertEquals(1, lines.size)
        assertEquals("唯一一行", lines[0].text)
    }

    @Test
    fun `乱序输入按时间排序 且空输入返回空表`() {
        val lines = Lyrics.parseLrc("[00:10.00]后\n[00:02.00]前")
        assertEquals(listOf("前", "后"), lines.map { it.text })
        assertTrue(Lyrics.parseLrc("").isEmpty())
        assertTrue(Lyrics.parseLrc("   ").isEmpty())
    }
}
