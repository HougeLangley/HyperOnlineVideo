package com.hougelangley.hyperonlinevideo.data

import java.io.File

/**
 * 本地外挂字幕（M14）
 *
 * 为什么不用 mpv 的 sub-add：本工程用的 libmpv 构建里 libass 没有可用的 font provider
 * （日志报 "can't find selected font provider"），字幕轨能加载但渲染为空白。
 * 改为在 App 层解析字幕并在 UI 覆盖层渲染 —— 字体走系统，稳定可控。
 *
 * 支持格式：.srt（SubRip）；时间轴形如 00:00:01,000 --> 00:00:06,000
 */
object Subtitles {

    data class Cue(val startMs: Long, val endMs: Long, val text: String)

    /** 找与媒体同名的字幕文件（.srt 优先） */
    fun findFor(mediaPath: String): File? {
        if (mediaPath.isEmpty() || mediaPath.startsWith("http")) return null
        val base = mediaPath.substringBeforeLast('.')
        for (ext in listOf("srt", "ass", "ssa", "vtt")) {
            val f = File("$base.$ext")
            if (f.exists() && f.length() > 0) return f
        }
        return null
    }

    /** 解析 SRT（容错：跳过序号行/空行，支持多行文本） */
    fun parseSrt(text: String): List<Cue> {
        if (text.isBlank()) return emptyList()
        val out = ArrayList<Cue>()
        // 统一换行，按空行切块
        val blocks = text.replace("\r\n", "\n").replace("\r", "\n").split(Regex("\n{2,}"))
        val timeRe = Regex("""(\d{1,2}):(\d{2}):(\d{2})[,.](\d{1,3})\s*-->\s*(\d{1,2}):(\d{2}):(\d{2})[,.](\d{1,3})""")
        for (block in blocks) {
            val lines = block.trim('\n').split('\n')
            val timeLineIdx = lines.indexOfFirst { timeRe.containsMatchIn(it) }
            if (timeLineIdx < 0) continue
            val m = timeRe.find(lines[timeLineIdx]) ?: continue
            fun ms(h: String, mnt: String, s: String, msStr: String): Long {
                val frac = when (msStr.length) {
                    1 -> msStr.toLong() * 100
                    2 -> msStr.toLong() * 10
                    else -> msStr.take(3).toLong()
                }
                return h.toLong() * 3_600_000 + mnt.toLong() * 60_000 + s.toLong() * 1000 + frac
            }
            val start = ms(m.groupValues[1], m.groupValues[2], m.groupValues[3], m.groupValues[4])
            val end = ms(m.groupValues[5], m.groupValues[6], m.groupValues[7], m.groupValues[8])
            val content = lines.drop(timeLineIdx + 1)
                .joinToString("\n")
                .replace(Regex("<[^>]+>"), "")     // 去 HTML 标签
                .trim()
            if (content.isEmpty()) continue
            out.add(Cue(start, end, content))
        }
        return out.sortedBy { it.startMs }
    }

    fun load(file: File): List<Cue> = try {
        parseSrt(file.readText())
    } catch (e: Exception) {
        emptyList()
    }

    /** 当前时间点应显示的字幕（无则 null） */
    fun cueAt(cues: List<Cue>, posMs: Long): Cue? =
        cues.firstOrNull { posMs >= it.startMs && posMs <= it.endMs }

    // ---------- 在线字幕解析（M14b）----------

    /** B 站 CC 字幕 JSON：{"body":[{"from":0.0,"to":1.5,"content":"..."}]} */
    fun parseBiliJson(raw: String): List<Cue> {
        val out = ArrayList<Cue>()
        runCatching {
            val body = org.json.JSONObject(raw).optJSONArray("body") ?: return@runCatching
            for (i in 0 until body.length()) {
                val o = body.optJSONObject(i) ?: continue
                val from = (o.optDouble("from", 0.0) * 1000).toLong()
                val to = (o.optDouble("to", 0.0) * 1000).toLong()
                val content = o.optString("content").trim()
                if (content.isEmpty()) continue
                out.add(Cue(from, if (to > from) to else from + 2500, content))
            }
        }
        return out
    }

    /** YouTube json3 字幕：{"events":[{"tStartMs":0,"dDurationMs":1000,"segs":[{"utf8":"..."}]}]} */
    fun parseYoutubeJson(raw: String): List<Cue> {
        val out = ArrayList<Cue>()
        runCatching {
            val events = org.json.JSONObject(raw).optJSONArray("events") ?: return@runCatching
            for (i in 0 until events.length()) {
                val e = events.optJSONObject(i) ?: continue
                val segs = e.optJSONArray("segs") ?: continue
                val sb = StringBuilder()
                for (k in 0 until segs.length()) {
                    sb.append(segs.optJSONObject(k)?.optString("utf8").orEmpty())
                }
                val text = sb.toString().replace("\n", " ").trim()
                if (text.isEmpty()) continue
                val start = e.optLong("tStartMs", 0L)
                var dur = e.optLong("dDurationMs", 0L)
                if (dur <= 0L) dur = 2500L
                out.add(Cue(start, start + dur, text))
            }
        }
        return out.sortedBy { it.startMs }
    }

    /** 自动识别在线字幕格式（B站 / YouTube） */
    /** 字幕轨与系统语言的匹配打分（越小越优先；>=9 = 不自动选 ✗）——纯函数（可单测 ✓）
     *  label 形态：B站 "中文" / YouTube "Chinese (Simplified)（自动）" / 本地 "zh-Hans · SRT"
     *  修复 2026-09-25 用户实测 ✗：B站轨曾带"（自动翻译）"后缀 → 精确分支全不命中 → rank=9
     *  → 自动选轨静默失效（toast"字幕加载失败或为空"）✗ */
    fun languageRank(label: String, sysTag: String): Int {
        val key = label.substringBefore("·").trim().lowercase()
        if (key.isEmpty()) return 9
        val tag = sysTag.lowercase()
        val lang = tag.substringBefore('-')
        // ① 精确 / 前缀（zh-hans-cn ↔ "zh-hans · SRT" ✓）
        if (tag.isNotEmpty() && (key == tag || key.startsWith(tag) || tag.startsWith(key))) return 0
        // ② 同主语言（"zh" / "zh-cn" 形态；zh 走下面的自然语言分支 ✗）
        if (lang != "zh" && lang.isNotEmpty() && (key == lang || key.startsWith("$lang-") || key.contains(lang))) return 1
        // ③ 中文自然语言标签（B站/YouTube 用自然语言 ✗ 不走语言码 ✗）
        if (lang == "zh") {
            val hans = key.contains("简体") || key.contains("中文（中国）") || key.contains("中文(中国)") ||
                       key.contains("hans") || key.contains("simplified")
            val hant = key.contains("繁體") || key.contains("中文（台") || key.contains("中文(台") ||
                       key.contains("hant") || key.contains("traditional")
            if (hans) return 0
            if (hant) return 2
            if (key.contains("中文") || key.contains("chinese")) return 1   // 裸"中文"/"Chinese" 兜底 ✓（B站 lan_doc 常态 ✗）
        }
        // ④ 英文兜底
        if (key.startsWith("en") || key.contains("english")) return 3
        return 9
    }

    fun parseAny(raw: String): List<Cue> = try {
        val j = org.json.JSONObject(raw)
        when {
            j.has("body") -> parseBiliJson(raw)
            j.has("events") -> parseYoutubeJson(raw)
            else -> emptyList()
        }
    } catch (e: Exception) {
        emptyList()
    }
}
