package com.hougelangley.hyperonlinevideo.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.DownloadDone
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Storage
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.clickable
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.hougelangley.hyperonlinevideo.data.Settings
import coil.compose.AsyncImage
import com.hougelangley.hyperonlinevideo.data.Favorites
import com.hougelangley.hyperonlinevideo.data.LocalFile
import com.hougelangley.hyperonlinevideo.data.Repo
import com.hougelangley.hyperonlinevideo.data.StorageRepo
import com.hougelangley.hyperonlinevideo.data.SearchFilters
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

// ================= 账号 =================

private val ACCOUNTS = listOf(
    "youtube" to "YouTube",
    "bilibili" to "Bilibili",
    "netease" to "网易云音乐",
    "qqmusic" to "QQ音乐",
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AccountsSheet(
    cookieTick: Int,
    onDismiss: () -> Unit,
    onChanged: () -> Unit,
    onLogin: (String) -> Unit,
) {
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(horizontal = 20.dp).padding(bottom = 30.dp)) {
            Text("账号登录", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(16.dp))
            for ((id, label) in ACCOUNTS) {
                val loggedIn = remember(cookieTick) { Repo.hasCookies(id) }
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp),
                ) {
                    Badge(color = if (loggedIn) androidx.compose.ui.graphics.Color(0xFF81C995) else androidx.compose.ui.graphics.Color(0xFF777777))
                    Spacer(Modifier.width(12.dp))
                    Text(
                        "$label  ${if (loggedIn) "已登录" else "未登录"}",
                        style = MaterialTheme.typography.bodyLarge,
                        modifier = Modifier.weight(1f),
                    )
                    if (loggedIn) {
                        OutlinedButton(onClick = {
                            Repo.cookieFile(id).delete()
                            onChanged()
                        }) { Text("退出") }
                    } else {
                        FilledTonalButton(onClick = { onLogin(id) }) { Text("登录") }
                    }
                }
            }
            Spacer(Modifier.height(8.dp))
            Text(
                "登录后可解锁 YouTube 解析、B 站 1080P、网易云/QQ音乐高音质与无损（VIP 账号）；登录页支持边缘滑动返回",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun Badge(color: androidx.compose.ui.graphics.Color) {
    Box(
        modifier = Modifier
            .size(12.dp)
            .clipCircle(color)
    )
}

private fun Modifier.clipCircle(color: androidx.compose.ui.graphics.Color): Modifier =
    this.then(Modifier.background(color = color, shape = CircleShape))

// ================= 本地库 =================

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LibrarySheet(
    onDismiss: () -> Unit,
    /** (当前选中文件, 完整列表) —— 列表用于建立播放队列 */
    onPlay: (LocalFile, List<LocalFile>) -> Unit,
    onStatus: (String) -> Unit,
) {
    var files by remember { mutableStateOf<List<LocalFile>>(emptyList()) }
    var refresh by remember { mutableStateOf(0) }
    var sort by remember { mutableStateOf(StorageRepo.LocalSort.RECENT) }   // M16：排序
    var query by remember { mutableStateOf("") }                     // M16：库内搜索
    var renaming by remember { mutableStateOf<LocalFile?>(null) }    // M16：重命名
    var renameText by remember { mutableStateOf("") }

    LaunchedEffect(refresh, sort, query) {
        files = withContext(Dispatchers.IO) { Repo.listDownloads(sort, query) }
    }

    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(horizontal = 20.dp).padding(bottom = 30.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("本地库", style = MaterialTheme.typography.titleLarge)
                Spacer(Modifier.weight(1f))
                Text(
                    "${files.size} 个文件",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Spacer(Modifier.height(10.dp))
            OutlinedTextField(
                value = query,
                onValueChange = { query = it },
                placeholder = { Text("搜索文件名 / 歌手") },
                leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                StorageRepo.LocalSort.entries.forEach { st ->
                    FilterChip(
                        selected = sort == st,
                        onClick = { sort = st },
                        label = { Text(st.label) },
                    )
                }
            }
            Spacer(Modifier.height(6.dp))
            if (files.isEmpty()) {
                Text(
                    if (query.isBlank()) "暂无下载内容" else "没有匹配的文件",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else {
                LazyColumn(modifier = Modifier.heightIn(max = 460.dp)) {
                    items(files, key = { it.path }) { f ->
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp),
                        ) {
                            Icon(
                                if (f.isAudio) Icons.Filled.PlayArrow else Icons.Filled.DownloadDone,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                            )
                            Spacer(Modifier.width(12.dp))
                            Column(Modifier.weight(1f)) {
                                // 音乐条目：显示"歌名"，副行显示歌手；视频：显示文件名
                                Text(
                                    if (f.isAudio) f.title else f.name,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                    style = MaterialTheme.typography.bodyLarge,
                                )
                                Text(
                                    buildString {
                                        if (f.isAudio && f.artist.isNotEmpty()) append(f.artist).append(" · ")
                                        append(fmtBytes(f.sizeBytes))
                                    },
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                            IconButton(onClick = {
                                renaming = f
                                renameText = f.name.substringBeforeLast('.')
                            }) {
                                Icon(
                                    Icons.Filled.Edit,
                                    contentDescription = "重命名",
                                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                            IconButton(onClick = { onPlay(f, files) }) {
                                Icon(Icons.Filled.PlayArrow, contentDescription = "播放", tint = MaterialTheme.colorScheme.primary)
                            }
                            IconButton(onClick = {
                                val ok = Repo.deleteDownload(f.path)
                                onStatus(if (ok) "已删除: ${f.name.take(20)}" else "删除失败")
                                refresh++
                            }) {
                                Icon(Icons.Filled.Delete, contentDescription = "删除", tint = MaterialTheme.colorScheme.error)
                            }
                        }
                    }
                }
            }
        }
    }

    // 重命名对话框（保留扩展名，外挂字幕联动改名）
    renaming?.let { target ->
        AlertDialog(
            onDismissRequest = { renaming = null },
            title = { Text("重命名") },
            text = {
                Column {
                    Text(
                        "原名：${target.name}",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Spacer(Modifier.height(8.dp))
                    OutlinedTextField(
                        value = renameText,
                        onValueChange = { renameText = it },
                        singleLine = true,
                        label = { Text("新名称（不含扩展名）") },
                    )
                }
            },
            confirmButton = {
                TextButton(onClick = {
                    val newPath = Repo.renameDownload(target.path, renameText)
                    onStatus(if (newPath != null) "已重命名：${renameText.take(20)}" else "重命名失败（同名文件已存在或名称非法）")
                    renaming = null
                    refresh++
                }) { Text("确定") }
            },
            dismissButton = { TextButton(onClick = { renaming = null }) { Text("取消") } },
        )
    }
}

// ================= 存储管理 =================

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun StorageSheet(onDismiss: () -> Unit, onStatus: (String) -> Unit) {
    var tick by remember { mutableStateOf(0) }
    var stats by remember { mutableStateOf<Triple<Int, Long, Long>?>(null) }
    LaunchedEffect(tick) {
        stats = withContext(Dispatchers.IO) { Repo.storageStats() }   // 目录遍历移出主线程
    }

    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(horizontal = 20.dp).padding(bottom = 30.dp)) {
            Text("存储管理", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(12.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Filled.Storage, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                Spacer(Modifier.width(12.dp))
                val st = stats
                Text(
                    if (st == null) "统计中…"
                    else "已下载 ${st.first} 个视频 [${fmtBytes(st.second)} / 上限 ${fmtBytes(Repo.downloadsCapBytes)}]\n运行缓存 ${fmtBytes(st.third)}",
                    style = MaterialTheme.typography.bodyLarge,
                )
            }
            Spacer(Modifier.height(16.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedButton(onClick = {
                    Repo.clearCache()
                    onStatus("缓存已清理")
                    tick++
                }) { Icon(Icons.Filled.Refresh, contentDescription = null, modifier = Modifier.size(18.dp)); Spacer(Modifier.width(6.dp)); Text("清理缓存") }
                OutlinedButton(onClick = {
                    Repo.clearDownloads()
                    onStatus("已清空全部下载")
                    tick++
                }) { Icon(Icons.Filled.Delete, contentDescription = null, modifier = Modifier.size(18.dp)); Spacer(Modifier.width(6.dp)); Text("清空下载") }
            }
            Spacer(Modifier.height(8.dp))
            Text(
                "下载目录达 2GB 上限时自动清理最旧文件",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

// ================= 搜索过滤器 =================

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FilterSheet(
    current: SearchFilters,
    onDismiss: () -> Unit,
    onApply: (SearchFilters) -> Unit,
) {
    var sort by remember { mutableStateOf(current.sort) }
    var duration by remember { mutableStateOf(current.duration) }

    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(horizontal = 20.dp).padding(bottom = 30.dp)) {
            Text("搜索过滤器", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(16.dp))

            Text("排序", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                listOf(1 to "相关度", 2 to "最新", 3 to "播放最多").forEach { (v, label) ->
                    FilterChip(selected = sort == v, onClick = { sort = v }, label = { Text(label) })
                }
            }
            Spacer(Modifier.height(16.dp))

            Text("时长", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                listOf(0 to "全部", 1 to "短视频", 2 to "中等", 3 to "长篇").forEach { (v, label) ->
                    FilterChip(selected = duration == v, onClick = { duration = v }, label = { Text(label) })
                }
            }
            Spacer(Modifier.height(24.dp))

            Button(
                onClick = { onApply(SearchFilters(sort, duration)) },
                modifier = Modifier.fillMaxWidth(),
            ) { Text("应用过滤器") }
            Spacer(Modifier.height(8.dp))
            Text(
                "应用后将以当前关键词重新搜索（双平台均生效）",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

// ================= 设置（M13） =================

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsSheet(
    onDismiss: () -> Unit,
    onOpenStorage: () -> Unit,
    onStatus: (String) -> Unit,
) {
    // 每次进入读取一次，改动即时写回
    var resume by remember { mutableStateOf(Settings.resumePlayback) }
    var autoNext by remember { mutableStateOf(Settings.autoNext) }
    var gestures by remember { mutableStateOf(Settings.gesturesEnabled) }
    var fillScreen by remember { mutableStateOf(Settings.fillScreen) }
    var quality by remember { mutableStateOf(Settings.musicQuality) }
    var wifiOnly by remember { mutableStateOf(Settings.wifiOnlyDownload) }
    val ytdlp by Repo.ytdlpStatus.collectAsStateWithLifecycle()
    val ctx = androidx.compose.ui.platform.LocalContext.current
    val appVersion = remember {
        runCatching { ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName }.getOrNull() ?: "1.0"
    }

    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(
            Modifier
                .padding(horizontal = 20.dp)
                .padding(bottom = 30.dp)
                .verticalScroll(rememberScrollState())
        ) {
            Text("设置", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(12.dp))

            // ---------- 播放 ----------
            SectionTitle("播放")
            SwitchRow("进度记忆（退出后续播）", resume) {
                resume = it; Settings.resumePlayback = it
                onStatus(if (it) "已开启进度记忆" else "已关闭进度记忆")
            }
            SwitchRow("播完自动连播（队列）", autoNext) {
                autoNext = it; Settings.autoNext = it
                onStatus(if (it) "已开启自动连播" else "已关闭自动连播（播完退出）")
            }
            SwitchRow("手势调节亮度 / 音量", gestures) {
                gestures = it; Settings.gesturesEnabled = it
                onStatus(if (it) "已开启手势调节" else "已关闭手势调节")
            }
            SwitchRow("全屏时铺满屏幕", fillScreen) {
                fillScreen = it; Settings.fillScreen = it
                onStatus(if (it) "全屏铺满（无左右黑边，上下少量裁切）" else "全屏保持比例（保留左右黑边）")
            }

            Spacer(Modifier.height(14.dp))

            // ---------- 音乐 ----------
            SectionTitle("音乐音质上限")
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                listOf(
                    Settings.QUALITY_AUTO to "自动最高",
                    Settings.QUALITY_HIGH to "320k",
                    Settings.QUALITY_STANDARD to "128k",
                ).forEach { (v, label) ->
                    FilterChip(
                        selected = quality == v,
                        onClick = {
                            quality = v; Settings.musicQuality = v
                            onStatus("音质上限：$label")
                        },
                        label = { Text(label) },
                    )
                }
            }
            Spacer(Modifier.height(6.dp))
            Text(
                "自动最高会优先取无损（需账号有对应权益），拿不到时自动降级。",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            Spacer(Modifier.height(14.dp))

            // ---------- 下载 ----------
            SectionTitle("下载")
            SwitchRow("仅 WiFi 下载", wifiOnly) {
                wifiOnly = it; Settings.wifiOnlyDownload = it
                onStatus(if (it) "已开启仅 WiFi 下载" else "已关闭仅 WiFi 下载")
            }

            Spacer(Modifier.height(14.dp))

            // ---------- 关于 ----------
            SectionTitle("关于")
            ListItem(
                headlineContent = { Text("存储管理") },
                supportingContent = { Text("查看缓存占用 / 清理下载") },
                leadingContent = { Icon(Icons.Filled.Storage, contentDescription = null) },
                modifier = Modifier.clickable { onOpenStorage() },
            )
            ListItem(
                headlineContent = { Text("yt-dlp") },
                supportingContent = { Text(ytdlp.ifEmpty { "初始化中..." }) },
                leadingContent = { Icon(Icons.Filled.DownloadDone, contentDescription = null) },
            )
            ListItem(
                headlineContent = { Text("聚合视频") },
                supportingContent = { Text("版本 $appVersion") },
                leadingContent = { Icon(Icons.Filled.Refresh, contentDescription = null) },
            )
        }
    }
}

@Composable
private fun SectionTitle(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.titleMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    Spacer(Modifier.height(4.dp))
}

@Composable
private fun SwitchRow(title: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(title, Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge)
        Switch(checked = checked, onCheckedChange = onChange)
    }
}


// ================= 收藏（M19） =================

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FavoritesSheet(
    onDismiss: () -> Unit,
    /** (选中项, 完整收藏列表) —— 列表整体入队 */
    onPlay: (Favorites.Fav, List<Favorites.Fav>) -> Unit,
    onChanged: () -> Unit,
) {
    val favs by Favorites.items.collectAsStateWithLifecycle()
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(
            Modifier
                .padding(horizontal = 20.dp)
                .padding(bottom = 30.dp)
        ) {
            Text("收藏（${favs.size}）", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(10.dp))
            if (favs.isEmpty()) {
                Text(
                    "还没有收藏。在搜索结果里点条目右侧的 ♥ 即可加入。",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                return@Column
            }
            LazyColumn(Modifier.heightIn(max = 520.dp)) {
                items(favs, key = { it.id }) { f ->
                    ListItem(
                        headlineContent = { Text(f.title, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                        supportingContent = {
                            Text(
                                listOf(f.uploader, fmtDur(f.durationSec)).filter { it.isNotBlank() }.joinToString(" · "),
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                            )
                        },
                        leadingContent = {
                            AsyncImage(
                                model = f.cover,
                                contentDescription = null,
                                contentScale = ContentScale.Crop,
                                modifier = Modifier
                                    .size(52.dp)
                                    .clip(RoundedCornerShape(8.dp))
                                    .background(MaterialTheme.colorScheme.surfaceVariant),
                            )
                        },
                        trailingContent = {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                IconButton(onClick = { onPlay(f, favs) }) {
                                    Icon(Icons.Filled.PlayArrow, contentDescription = "播放", tint = MaterialTheme.colorScheme.primary)
                                }
                                IconButton(onClick = { Favorites.remove(f.id); onChanged() }) {
                                    Icon(Icons.Filled.Delete, contentDescription = "移除", tint = MaterialTheme.colorScheme.error)
                                }
                            }
                        },
                    )
                }
            }
        }
    }
}

private fun fmtDur(sec: Int): String =
    if (sec <= 0) "" else "%d:%02d".format(sec / 60, sec % 60)
