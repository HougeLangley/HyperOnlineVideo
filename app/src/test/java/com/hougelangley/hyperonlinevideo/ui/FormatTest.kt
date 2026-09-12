package com.hougelangley.hyperonlinevideo.ui

import org.junit.Assert.assertEquals
import org.junit.Test

/** 界面格式化单元测试（B2 拆分出的纯函数） */
class FormatTest {

    @Test
    fun `时长格式化：分秒与时分秒`() {
        assertEquals("", fmtDuration(0))
        assertEquals("", fmtDuration(-5))
        assertEquals("0:07", fmtDuration(7))
        assertEquals("1:05", fmtDuration(65))
        assertEquals("59:59", fmtDuration(3599))
        assertEquals("1:00:00", fmtDuration(3600))
        assertEquals("2:03:04", fmtDuration(7384))
    }

    @Test
    fun `体积格式化：GB MB KB 分界`() {
        assertEquals("0 KB", fmtBytes(0))
        assertEquals("1 KB", fmtBytes(1024))
        assertEquals("1 MB", fmtBytes(1L shl 20))
        assertEquals("1.5 GB", fmtBytes((1.5 * (1L shl 30)).toLong()))
        assertEquals("2.0 GB", fmtBytes(2L shl 30))
    }
}
