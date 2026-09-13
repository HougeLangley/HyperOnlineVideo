package com.hougelangley.hyperonlinevideo.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** 字幕解析单元测试（M14 本地 SRT + M14b 在线 B站/YouTube） */
class SubtitlesTest {

    @Test
    fun `解析标准 SRT：序号 时间 多行文本`() {
        val srt = """
            1
            00:00:01,000 --> 00:00:06,000
            第一行
            第二行

            2
            00:00:07,500 --> 00:00:09,000
            第二条
        """.trimIndent()
        val cues = Subtitles.parseSrt(srt)
        assertEquals(2, cues.size)
        assertEquals(1000L, cues[0].startMs)
        assertEquals(6000L, cues[0].endMs)
        assertEquals("第一行\n第二行", cues[0].text)
        assertEquals(7500L, cues[1].startMs)
    }

    @Test
    fun `兼容点号毫秒 与 去除 HTML 标签 与 乱序排序`() {
        val srt = """
            1
            00:00:10.000 --> 00:00:12.000
            <i>后面</i>

            2
            00:00:02.5 --> 00:00:04.0
            前面
        """.trimIndent()
        val cues = Subtitles.parseSrt(srt)
        assertEquals(2, cues.size)
        assertEquals(2500L, cues[0].startMs)   // 乱序输入 → 按时间排序
        assertEquals(4000L, cues[0].endMs)     // "04.0" = 4000ms
        assertEquals("前面", cues[0].text)
        assertEquals("后面", cues[1].text)      // HTML 标签被剔除
    }

    @Test
    fun `空输入与垃圾输入返回空表`() {
        assertTrue(Subtitles.parseSrt("").isEmpty())
        assertTrue(Subtitles.parseSrt("没有时间轴的一堆文字").isEmpty())
    }

    @Test
    fun `cueAt 命中区间与边界`() {
        val cues = listOf(
            Subtitles.Cue(1000, 2000, "A"),
            Subtitles.Cue(3000, 4000, "B"),
        )
        assertEquals("A", Subtitles.cueAt(cues, 1000)?.text)
        assertEquals("A", Subtitles.cueAt(cues, 1500)?.text)
        assertEquals("A", Subtitles.cueAt(cues, 2000)?.text)
        assertNull(Subtitles.cueAt(cues, 2500))
        assertEquals("B", Subtitles.cueAt(cues, 3001)?.text)
        assertNull(Subtitles.cueAt(cues, 999))
    }

    @Test
    fun `B站 CC 字幕 JSON 解析`() {
        val json = """{"body":[{"from":0.5,"to":2.5,"content":"你好"},{"from":3.0,"to":4.0,"content":""},{"from":5.0,"to":6.0,"content":"再见"}]}"""
        val cues = Subtitles.parseBiliJson(json)
        assertEquals(2, cues.size)              // 空内容被跳过
        assertEquals(500L, cues[0].startMs)
        assertEquals(2500L, cues[0].endMs)
        assertEquals("再见", cues[1].text)
    }

    @Test
    fun `YouTube json3 字幕解析（含换行与缺时长）`() {
        val json = """
        {"events":[
          {"tStartMs":1000,"dDurationMs":2000,"segs":[{"utf8":"Hello "},{"utf8":"World"}]},
          {"tStartMs":5000,"segs":[{"utf8":"no duration"}]},
          {"tStartMs":9000,"dDurationMs":500,"segs":[{"utf8":"\n"}]}
        ]}
        """.trimIndent()
        val cues = Subtitles.parseYoutubeJson(json)
        assertEquals(2, cues.size)               // 纯换行事件被跳过
        assertEquals("Hello World", cues[0].text)
        assertEquals(1000L, cues[0].startMs)
        assertEquals(3000L, cues[0].endMs)
        assertEquals(5000L, cues[1].startMs)
        assertEquals(7500L, cues[1].endMs)       // 缺 dDurationMs → 默认 2500ms
    }

    @Test
    fun `parseAny 自动识别格式与容错`() {
        assertEquals(1, Subtitles.parseAny("""{"body":[{"from":1.0,"to":2.0,"content":"x"}]}""").size)
        assertEquals(1, Subtitles.parseAny("""{"events":[{"tStartMs":0,"dDurationMs":900,"segs":[{"utf8":"y"}]}]}""").size)
        assertTrue(Subtitles.parseAny("完全不是 JSON").isEmpty())
        assertTrue(Subtitles.parseAny("""{"unknown":1}""").isEmpty())
    }
}
