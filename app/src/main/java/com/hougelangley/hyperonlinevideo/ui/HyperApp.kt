package com.hougelangley.hyperonlinevideo.ui

import android.content.Context
import android.content.Intent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.slideInVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.FavoriteBorder
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Storage
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.*
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.*
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.clickable
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import coil.compose.AsyncImage
import com.hougelangley.hyperonlinevideo.LoginActivity
import com.hougelangley.hyperonlinevideo.PlayerActivity
import com.hougelangley.hyperonlinevideo.data.Favorites
import com.hougelangley.hyperonlinevideo.data.DownloadTask
import com.hougelangley.hyperonlinevideo.data.PlayQueue
import com.hougelangley.hyperonlinevideo.data.Repo
import com.hougelangley.hyperonlinevideo.data.SEARCH_PAGE_SIZE
import com.hougelangley.hyperonlinevideo.data.SearchFilters
import com.hougelangley.hyperonlinevideo.data.VideoDetail
import com.hougelangley.hyperonlinevideo.data.VideoItem
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.UUID

private val PLATFORMS = listOf(
    "youtube" to "YouTube",
    "bilibili" to "Bilibili",
    "netease" to "网易云音乐",
    "qqmusic" to "QQ音乐",
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HyperApp() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var platform by remember { mutableStateOf("youtube") }
    var query by remember { mutableStateOf("") }
    var status by remember { mutableStateOf("") }
    var results by remember { mutableStateOf<List<VideoItem>>(emptyList()) }
    var searching by remember { mutableStateOf(false) }
    var loadingMore by remember { mutableStateOf(false) }
    var hasMore by remember { mutableStateOf(false) }
    var page by remember { mutableStateOf(1) }
    var filters by remember { mutableStateOf(SearchFilters()) }
    // 批量下载：长按进入多选
    var selecting by remember { mutableStateOf(false) }
    var selectedIds by remember { mutableStateOf(setOf<String>()) }
    // 收藏 id 集合（驱动行内 ♥ 状态）
    var favIds by remember { mutableStateOf(Favorites.items.value.map { it.id }.toSet()) }
    var searchSeq by remember { mutableStateOf(0) }        // 搜索世代号：作废在途请求（切平台/新搜索）
    var resultQuery by remember { mutableStateOf("") }     // 当前结果集对应的关键词（loadMore 用它翻页）
    val listState = rememberLazyListState()
    var sheet by remember { mutableStateOf<String?>(null) }
    var cookieTick by remember { mutableStateOf(0) }   // 登录态刷新信号
    val downloads by Repo.downloads.collectAsStateWithLifecycle()
    val ytdlpStatus by Repo.ytdlpStatus.collectAsStateWithLifecycle()

    // 回到前台（例如从登录页返回）时刷新登录态
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val obs = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) cookieTick++
        }
        lifecycleOwner.lifecycle.addObserver(obs)
        onDispose { lifecycleOwner.lifecycle.removeObserver(obs) }
    }

    // 入场动画
    var heroVisible by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { heroVisible = true }
    val heroAlpha by animateFloatAsState(if (heroVisible) 1f else 0f, tween(700), label = "hero")

    fun doSearch() {
        if (query.isBlank() || searching) return
        val q = query.trim()
        val seq = ++searchSeq
        searching = true
        page = 1
        status = "正在搜索 $platform: $q ..."
        scope.launch {
            try {
                val r = Repo.search(platform, q, 1, filters)
                if (seq != searchSeq) return@launch      // 已被新搜索/切平台取代，丢弃
                results = r
                resultQuery = q
                hasMore = r.size >= SEARCH_PAGE_SIZE
                status = "找到 ${r.size} 条结果" + if (hasMore) "（下滑加载更多）" else ""
            } catch (e: Exception) {
                if (seq == searchSeq) {
                    status = "搜索失败: ${e.message?.take(120)}"
                    hasMore = false
                }
            } finally {
                if (seq == searchSeq) searching = false
            }
        }
    }

    fun loadMore() {
        if (loadingMore || !hasMore || searching || resultQuery.isBlank()) return
        val seq = searchSeq
        val q = resultQuery
        val nextPage = page + 1
        loadingMore = true
        scope.launch {
            try {
                val next = Repo.search(platform, q, nextPage, filters)
                if (seq != searchSeq) return@launch      // 结果集已更换（切平台/新搜索），丢弃
                val seen = results.map { it.id }.toHashSet()
                val fresh = next.filter { it.id !in seen }
                results = results + fresh
                page = nextPage
                hasMore = next.size >= SEARCH_PAGE_SIZE && fresh.isNotEmpty()
                status = "已加载 ${results.size} 条" + if (hasMore) "（继续下滑）" else "（到底了）"
            } catch (e: Exception) {
                if (seq == searchSeq) {
                    status = "加载更多失败: ${e.message?.take(80)}"
                    hasMore = false
                }
            } finally {
                loadingMore = false                        // 无条件复位，防状态卡死
            }
        }
    }

    // 无限滚动：列表接近底部自动加载下一页
    LaunchedEffect(listState, results.size, hasMore, loadingMore) {
        snapshotFlow { listState.layoutInfo.visibleItemsInfo.lastOrNull()?.index ?: -1 }
            .collect { last ->
                if (hasMore && !loadingMore && last >= results.size - 3) {
                    loadMore()
                }
            }
    }

    fun doPlay(item: VideoItem, plat: String = platform, setQueue: Boolean = true) {
        // 入队：以当前结果列表为队列，从该项开始（队列供"下一首/自动连播"使用）
        if (setQueue) {
            val qi = results.indexOfFirst { it.id == item.id && it.url == item.url }
            if (qi >= 0) PlayQueue.setList(results, plat, qi) else PlayQueue.setEntry(item, plat)
        }
        scope.launch {
            status = "正在解析: ${item.title.take(30)}"
            try {
                val d: VideoDetail = if (item.isMusic) {
                    Repo.resolveMusic(item, plat)
                } else {
                    Repo.resolve(item.url, plat)
                }
                if (d.formats.isEmpty()) { status = "无可用播放格式"; return@launch }
                status = if (d.formats.size > 1) {
                    "多段视频：仅播放第 1 段（共 ${d.formats.size} 段）"
                } else {
                    "播放: ${d.title.take(30)}"
                }
                val it = Intent(context, PlayerActivity::class.java).apply {
                    putExtra("url", d.formats[0].url)
                    putExtra("audioUrl", d.audioUrl)
                    putExtra("title", d.title)
                    putExtra("platform", plat)
                    putExtra("key", item.url)                        // 进度记忆键（网页地址）
                    putExtra("qualities", ArrayList(d.qualities))    // 清晰度选项
                    putExtra("currentQualityId", d.currentQualityId)
                    putExtra("cover", d.cover)                       // 音乐封面（播放器展示）
                    putExtra("album", item.album)
                    putExtra("songId", item.id)                      // 音乐歌曲 id（播放器内切音质用）
                    putExtra("qualityLabel", d.formats.firstOrNull()?.label.orEmpty())  // 当前音质（音乐）
                    putExtra("subtitles", ArrayList(d.subtitleTracks))  // 在线字幕轨（B站 CC / YouTube）
                }
                context.startActivity(it)
            } catch (e: Exception) {
                status = "解析失败: ${e.message?.take(120)}"
                android.util.Log.w("HOV", "解析失败: ${e.message}")
            }
        }
    }


    fun doDownload(item: VideoItem) {
        val taskId = UUID.randomUUID().toString()
        if (item.isMusic) {
            Repo.startDownload(
                taskId = taskId,
                url = item.id,                       // 音乐用 id（ne:xxx / qq:mid）
                title = item.title,
                platform = platform,
                artist = item.uploader,
                album = item.album,
                coverUrl = item.cover,
            )
        } else {
            Repo.startDownload(taskId, item.url, item.title, platform)
        }
        status = "已开始下载: ${item.title.take(30)}"
    }

    /** 批量下载：把选中的条目全部加入下载队列（队列内部限并发） */
    fun downloadSelected() {
        val items = results.filter { selectedIds.contains(it.id) }
        if (items.isEmpty()) {
            status = "未选择任何条目"
            return
        }
        items.forEach { doDownload(it) }
        status = "已加入下载队列：${items.size} 项（并发 2）"
        selecting = false
        selectedIds = emptySet()
    }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        floatingActionButton = {
            FloatingActionButton(
                onClick = { sheet = "menu" },
                containerColor = MaterialTheme.colorScheme.primaryContainer,
                contentColor = MaterialTheme.colorScheme.onPrimaryContainer,
            ) { Icon(Icons.Filled.Add, contentDescription = "菜单") }
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 20.dp),
        ) {
            Spacer(Modifier.height(28.dp))

            // ===== Hero 标题（入场动画）=====
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .alpha(heroAlpha),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text(
                    "Hyper Online Video",
                    style = MaterialTheme.typography.headlineLarge,
                    color = MaterialTheme.colorScheme.onBackground,
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    "YouTube · Bilibili  聚合 · 无广告 · 可下载",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.primary,
                )
            }

            Spacer(Modifier.height(20.dp))

            // ===== 平台选择 =====
            LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                items(PLATFORMS) { (id, label) ->
                    FilterChip(
                        selected = platform == id,
                        onClick = {
                            if (platform != id) {
                                // 切平台：作废在途请求 + 清空旧结果，有词则自动重搜
                                platform = id
                                searchSeq++
                                results = emptyList()
                                resultQuery = ""
                                hasMore = false
                                page = 1
                                if (query.isNotBlank()) doSearch()
                                else status = "已切换到 $id，输入关键词搜索"
                            }
                        },
                        label = { Text(label) },
                    )
                }
            }

            Spacer(Modifier.height(12.dp))

            // ===== 搜索栏（标准输入框，键盘原生支持）=====
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = query,
                    onValueChange = { query = it },
                    modifier = Modifier.weight(1f),
                    placeholder = { Text("搜索视频…") },
                    singleLine = true,
                    shape = MaterialTheme.shapes.extraLarge,
                    keyboardOptions = KeyboardOptions(
                        imeAction = ImeAction.Search,
                        autoCorrectEnabled = false,   // 去掉 AUTO_CORRECT 位：WeType 对该位进入候选面板异常路径（IME diff 实证）
                    ),
                    keyboardActions = KeyboardActions(onSearch = { doSearch() }),
                )
                Spacer(Modifier.width(10.dp))
                FilledIconButton(
                    onClick = { doSearch() },
                    modifier = Modifier.size(52.dp),
                ) {
                    if (searching) CircularProgressIndicator(Modifier.size(22.dp), strokeWidth = 2.dp)
                    else Icon(Icons.Filled.Search, contentDescription = "搜索")
                }
            }

            Spacer(Modifier.height(10.dp))
            Text(
                status.ifEmpty { "yt-dlp $ytdlpStatus" },
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.primary,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )

            // ===== 批量下载动作条 =====
            AnimatedVisibility(visible = selecting) {
                Row(
                    Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        "已选 ${selectedIds.size} 项",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.weight(1f),
                    )
                    TextButton(onClick = {
                        selectedIds = results.map { it.id }.toSet()   // 全选当前结果
                    }) { Text("全选") }
                    TextButton(onClick = { downloadSelected() }) { Text("下载所选") }
                    TextButton(onClick = { selecting = false; selectedIds = emptySet() }) { Text("取消") }
                }
            }

            Spacer(Modifier.height(10.dp))

            // ===== 下载进度卡片 =====
            AnimatedVisibility(
                visible = downloads.isNotEmpty(),
                enter = fadeIn() + slideInVertically { it / 2 },
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    downloads.forEach { task ->
                        DownloadCard(
                            task = task,
                            onCancel = { Repo.cancelDownload(task.id); Repo.dismissDownload(task.id) },
                            onRetry = { Repo.retryDownload(task.id) },
                        )
                    }
                }
            }

            Spacer(Modifier.height(4.dp))

            // ===== 结果列表 =====
            LazyColumn(
                state = listState,
                modifier = Modifier.fillMaxSize(),
                verticalArrangement = Arrangement.spacedBy(10.dp),
                contentPadding = PaddingValues(bottom = 96.dp),
            ) {
                items(results, key = { it.id + it.url }) { item ->
                    VideoRow(
                        item = item,
                        downloading = downloads.any { it.state == DownloadTask.State.RUNNING && it.title == item.title },
                        selecting = selecting,
                        selected = selectedIds.contains(item.id),
                        faved = favIds.contains(item.id),
                        onPlay = { doPlay(item) },
                        onDownload = { doDownload(item) },
                        onToggleFav = {
                            val added = Favorites.toggle(item, platform)
                            favIds = Favorites.items.value.map { it.id }.toSet()
                            status = if (added) "已收藏：${item.title.take(24)}" else "已取消收藏：${item.title.take(24)}"
                        },
                        onLongPress = { selecting = true; selectedIds = selectedIds + item.id },
                        onToggleSelect = {
                            selectedIds = if (selectedIds.contains(item.id)) selectedIds - item.id else selectedIds + item.id
                        },
                    )
                }
                if (loadingMore) {
                    item {
                        Box(Modifier.fillMaxWidth().padding(16.dp), contentAlignment = Alignment.Center) {
                            CircularProgressIndicator(Modifier.size(28.dp), strokeWidth = 3.dp)
                        }
                    }
                }
            }
        }
    }

    // ===== 底部面板 =====
    when (sheet) {
        "menu" -> MenuSheet(
            onDismiss = { sheet = null },
            onAccounts = { sheet = "accounts" },
            onLibrary = { sheet = "library" },
            onStorage = { sheet = "storage" },
            onFilters = { sheet = "filters" },
            onSettings = { sheet = "settings" },
            onFavorites = { sheet = "favorites" },
        )
        "filters" -> FilterSheet(
            current = filters,
            onDismiss = { sheet = null },
            onApply = { f ->
                filters = f
                sheet = null
                if (query.isNotBlank()) doSearch()
                else status = "过滤器已更新，输入关键词搜索"
            },
        )
        "accounts" -> AccountsSheet(
            cookieTick = cookieTick,
            onDismiss = { sheet = null },
            onChanged = { cookieTick++ },
            onLogin = { plat ->
                context.startActivity(Intent(context, LoginActivity::class.java).putExtra("platform", plat))
            },
        )
        "library" -> LibrarySheet(
            onDismiss = { sheet = null },
            onPlay = { f, list ->
                // 本地库整列表入队：支持"下一首/自动连播"
                val items = list.map {
                    VideoItem(id = it.path, title = it.name, url = it.path, durationSec = 0, uploader = "", cover = "")
                }
                val start = list.indexOfFirst { it.path == f.path }.coerceAtLeast(0)
                PlayQueue.setList(items, "local", start)
                val i = Intent(context, PlayerActivity::class.java).apply {
                    putExtra("url", f.path)
                    putExtra("audioUrl", "")
                    putExtra("title", f.name)
                    putExtra("platform", "local")
                    putExtra("key", f.path)                          // 本地文件同样记忆进度
                }
                context.startActivity(i)
            },
            onStatus = { status = it },
        )
        "favorites" -> FavoritesSheet(
            onDismiss = { sheet = null },
            onPlay = { fav, list ->
                sheet = null
                val items = list.map { it.toItem() }
                val idx = list.indexOfFirst { it.id == fav.id }.coerceAtLeast(0)
                PlayQueue.setList(items, fav.platform, idx)     // 收藏列表整体入队
                doPlay(fav.toItem(), fav.platform, setQueue = false)
            },
            onChanged = { favIds = Favorites.items.value.map { it.id }.toSet() },
        )
        "settings" -> SettingsSheet(
            onDismiss = { sheet = null },
            onOpenStorage = { sheet = "storage" },
            onStatus = { status = it },
        )
        "storage" -> StorageSheet(
            onDismiss = { sheet = null },
            onStatus = { status = it },
        )
    }
}

// ================= 组件 =================

@Composable
private fun VideoRow(
    item: VideoItem,
    downloading: Boolean,
    selecting: Boolean,
    selected: Boolean,
    faved: Boolean,
    onPlay: () -> Unit,
    onDownload: () -> Unit,
    onToggleFav: () -> Unit,
    onLongPress: () -> Unit,
    onToggleSelect: () -> Unit,
) {
    Card(
        colors = CardDefaults.cardColors(
            containerColor = if (selected) MaterialTheme.colorScheme.primaryContainer
            else MaterialTheme.colorScheme.surfaceVariant
        ),
        shape = MaterialTheme.shapes.medium,
        modifier = Modifier
            .fillMaxWidth()
            .combinedClickable(
                onClick = { if (selecting) onToggleSelect() else onPlay() },
                onLongClick = onLongPress,
            ),
    ) {
        Row(modifier = Modifier.padding(10.dp), verticalAlignment = Alignment.CenterVertically) {
            if (selecting) {
                Checkbox(checked = selected, onCheckedChange = { onToggleSelect() })
            }
            AsyncImage(
                model = item.cover,
                contentDescription = null,
                contentScale = ContentScale.Crop,
                modifier = Modifier
                    .size(
                        width = if (item.isMusic) 84.dp else 148.dp,
                        height = 84.dp,
                    )
                    .clip(RoundedCornerShape(12.dp))
                    .background(Color(0xFF222222)),
            )
            Spacer(Modifier.width(12.dp))
            // 标题占据整列宽度（按钮不再挤占），操作按钮与副信息同一行放在下方
            Column(Modifier.weight(1f)) {
                Text(
                    item.title,
                    style = MaterialTheme.typography.titleMedium,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
                Spacer(Modifier.height(4.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        buildString {
                            if (item.uploader.isNotEmpty()) append(item.uploader).append(" · ")
                            append(fmtDuration(item.durationSec))
                        },
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f),
                    )
                    if (!selecting) {
                        IconButton(onClick = onToggleFav, modifier = Modifier.size(36.dp)) {
                            Icon(
                                if (faved) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder,
                                contentDescription = if (faved) "取消收藏" else "收藏",
                                tint = if (faved) Color(0xFFE57373) else MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.size(20.dp),
                            )
                        }
                    }
                    IconButton(onClick = onPlay, modifier = Modifier.size(36.dp)) {
                        Icon(
                            Icons.Filled.PlayArrow,
                            contentDescription = "播放",
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(24.dp),
                        )
                    }
                    IconButton(onClick = onDownload, enabled = !downloading, modifier = Modifier.size(36.dp)) {
                        Icon(
                            if (downloading) Icons.Filled.CheckCircle else Icons.Filled.Download,
                            contentDescription = "下载",
                            tint = if (downloading) Color(0xFF81C995) else MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.size(22.dp),
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun DownloadCard(task: DownloadTask, onCancel: () -> Unit, onRetry: () -> Unit) {
    // 完成态 3.5 秒后自动淡出（不挡视野）
    if (task.state == DownloadTask.State.DONE) {
        LaunchedEffect(task.id) {
            delay(3500)
            onCancel()
        }
    }
    // 左右滑动均可划走
    val dismissState = rememberSwipeToDismissBoxState(
        confirmValueChange = { v ->
            if (v != SwipeToDismissBoxValue.Settled) {
                onCancel()
                true
            } else false
        }
    )
    SwipeToDismissBox(
        state = dismissState,
        backgroundContent = {
            Box(
                Modifier
                    .fillMaxSize()
                    .clip(MaterialTheme.shapes.medium)
                    .background(MaterialTheme.colorScheme.surfaceVariant),
                contentAlignment = Alignment.Center,
            ) {
                Icon(Icons.Filled.Close, contentDescription = "划走", tint = MaterialTheme.colorScheme.error)
            }
        },
    ) {
        Card(
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
            shape = MaterialTheme.shapes.medium,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Column(Modifier.padding(horizontal = 14.dp, vertical = 10.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        task.title,
                        style = MaterialTheme.typography.bodyLarge,
                        modifier = Modifier.weight(1f),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    when (task.state) {
                        DownloadTask.State.RUNNING -> {
                            Text(
                                "${(task.progress * 100).toInt()}%",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.primary,
                            )
                            IconButton(onClick = onCancel) {
                                Icon(Icons.Filled.Close, contentDescription = "取消", modifier = Modifier.size(18.dp))
                            }
                        }
                        DownloadTask.State.QUEUED -> {
                            Text("排队中", color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
                            IconButton(onClick = onCancel) {
                                Icon(Icons.Filled.Close, contentDescription = "取消排队", modifier = Modifier.size(18.dp))
                            }
                        }
                        DownloadTask.State.DONE -> {
                            Text("完成", color = Color(0xFF81C995), style = MaterialTheme.typography.bodyMedium)
                        }
                        DownloadTask.State.FAILED -> {
                            Text("失败", color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodyMedium)
                            IconButton(onClick = onRetry) {
                                Icon(Icons.Filled.Refresh, contentDescription = "重试", tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(18.dp))
                            }
                            IconButton(onClick = onCancel) {
                                Icon(Icons.Filled.Close, contentDescription = "关闭", tint = MaterialTheme.colorScheme.error, modifier = Modifier.size(18.dp))
                            }
                        }
                        DownloadTask.State.CANCELED -> {
                            Text("已取消", color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                }
                Spacer(Modifier.height(6.dp))
                LinearProgressIndicator(
                    progress = { task.progress },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(8.dp)
                        .clip(RoundedCornerShape(4.dp)),
                    color = if (task.state == DownloadTask.State.DONE) Color(0xFF81C995) else MaterialTheme.colorScheme.primary,
                    trackColor = MaterialTheme.colorScheme.surface,
                )
                if (task.state == DownloadTask.State.FAILED && task.error.isNotEmpty()) {
                    Spacer(Modifier.height(4.dp))
                    Text(
                        task.error,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.error,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
                if (task.state == DownloadTask.State.RUNNING && task.etaSec > 0) {
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "预计剩余 ${task.etaSec} 秒",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun MenuSheet(
    onDismiss: () -> Unit,
    onAccounts: () -> Unit,
    onLibrary: () -> Unit,
    onStorage: () -> Unit,
    onFilters: () -> Unit,
    onSettings: () -> Unit,
    onFavorites: () -> Unit,
) {
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(bottom = 24.dp)) {
            ListItem(
                headlineContent = { Text("账号登录") },
                leadingContent = { Icon(Icons.Filled.Person, contentDescription = null) },
                modifier = Modifier.clickable { onAccounts() },
            )
            ListItem(
                headlineContent = { Text("本地库") },
                leadingContent = { Icon(Icons.Filled.Folder, contentDescription = null) },
                modifier = Modifier.clickable { onLibrary() },
            )
            ListItem(
                headlineContent = { Text("搜索过滤器") },
                leadingContent = { Icon(Icons.Filled.Tune, contentDescription = null) },
                modifier = Modifier.clickable { onFilters() },
            )
            ListItem(
                headlineContent = { Text("收藏") },
                leadingContent = { Icon(Icons.Filled.Favorite, contentDescription = null) },
                modifier = Modifier.clickable { onFavorites() },
            )
            ListItem(
                headlineContent = { Text("设置") },
                leadingContent = { Icon(Icons.Filled.Settings, contentDescription = null) },
                modifier = Modifier.clickable { onSettings() },
            )
            ListItem(
                headlineContent = { Text("存储管理") },
                leadingContent = { Icon(Icons.Filled.Storage, contentDescription = null) },
                modifier = Modifier.clickable { onStorage() },
            )
        }
    }
}

