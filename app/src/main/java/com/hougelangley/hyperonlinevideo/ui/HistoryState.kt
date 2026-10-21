package com.hougelangley.hyperonlinevideo.ui

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.hougelangley.hyperonlinevideo.data.Settings

/**
 * 搜索历史的 Compose 状态源（2026-10-20 修复）：
 * 之前 UI 直接读 Settings.searchHistory（非 Compose 状态 ✗ 清空/记录后不重组 →
 * "点清空没反应"的假象 + 误判丢失 ✗）。所有写操作走这里，读方订阅 [list] ✓
 */
object HistoryState {
    var list by mutableStateOf(Settings.searchHistory)
        private set

    /** 记录一条（内部含开关判断 ✓）后刷新状态 */
    fun add(q: String) {
        Settings.addSearchHistory(q)
        list = Settings.searchHistory
    }

    /** 清空后刷新状态（UI 立即消失 ✓） */
    fun clear() {
        Settings.clearSearchHistory()
        list = emptyList()
    }

    /** 设置面板开关变化后同步显示（关→隐藏不清数据；开→恢复显示） */
    fun resync() { list = Settings.searchHistory }
}
