package com.hougelangley.hyperonlinevideo.ui

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.foundation.isSystemInDarkTheme
import com.hougelangley.hyperonlinevideo.data.Settings
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.material3.Typography

private val DarkColors = darkColorScheme(
    primary = Color(0xFF8AB4F8),
    onPrimary = Color(0xFF0B1B33),
    primaryContainer = Color(0xFF1E3A5F),
    onPrimaryContainer = Color(0xFFD3E3FD),
    secondary = Color(0xFF9AA0A6),
    background = Color(0xFF0F0F0F),
    onBackground = Color(0xFFE8EAED),
    surface = Color(0xFF141414),
    onSurface = Color(0xFFE8EAED),
    surfaceVariant = Color(0xFF1E1E1E),
    onSurfaceVariant = Color(0xFFBDBDBD),
    outline = Color(0xFF444746),
    error = Color(0xFFF28B82),
)

/** 浅色方案：与深色同一套强调色（蓝），背景/文字按浅色可读性取 ✓ */
private val LightColors = lightColorScheme(
    primary = Color(0xFF1A73E8),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFD3E3FD),
    onPrimaryContainer = Color(0xFF0B1B33),
    secondary = Color(0xFF5F6368),
    background = Color(0xFFF6F7F9),
    onBackground = Color(0xFF1B1D21),
    surface = Color(0xFFFFFFFF),
    onSurface = Color(0xFF1B1D21),
    surfaceVariant = Color(0xFFEDEFF2),
    onSurfaceVariant = Color(0xFF44474C),
    outline = Color(0xFFC6C9CE),
    error = Color(0xFFB3261E),
)

private val AppShapes = Shapes(
    extraSmall = RoundedCornerShape(10.dp),
    small = RoundedCornerShape(14.dp),
    medium = RoundedCornerShape(20.dp),
    large = RoundedCornerShape(28.dp),
    extraLarge = RoundedCornerShape(32.dp),
)

private val AppTypography = Typography(
    headlineLarge = TextStyle(fontFamily = FontFamily.Default, fontWeight = FontWeight.SemiBold, fontSize = 34.sp, lineHeight = 40.sp),
    titleLarge = TextStyle(fontFamily = FontFamily.Default, fontWeight = FontWeight.Medium, fontSize = 22.sp, lineHeight = 28.sp),
    titleMedium = TextStyle(fontFamily = FontFamily.Default, fontWeight = FontWeight.Medium, fontSize = 17.sp, lineHeight = 24.sp),
    bodyLarge = TextStyle(fontFamily = FontFamily.Default, fontWeight = FontWeight.Normal, fontSize = 16.sp, lineHeight = 24.sp),
    bodyMedium = TextStyle(fontFamily = FontFamily.Default, fontWeight = FontWeight.Normal, fontSize = 14.sp, lineHeight = 20.sp),
    labelLarge = TextStyle(fontFamily = FontFamily.Default, fontWeight = FontWeight.Medium, fontSize = 14.sp, lineHeight = 20.sp),
    labelSmall = TextStyle(fontFamily = FontFamily.Default, fontWeight = FontWeight.Normal, fontSize = 12.sp, lineHeight = 16.sp),
)

/**
 * 主题模式（Compose 状态）—— 设置里一改就**立即重组** ✓，同时写回 SharedPreferences ✓。
 * 为什么不直接读 Settings.theme ✗：SharedPreferences 不是 Compose 状态，改了界面不会重组 ✓
 */
object ThemeState {
    var mode by mutableStateOf("auto")
        private set

    /** 启动时用持久化值初始化（Settings.init 之后调用 ✓） */
    fun load() { mode = Settings.theme }

    fun set(m: String) {
        mode = m
        Settings.theme = m
    }
}

@Composable
fun HyperTheme(themeMode: String = ThemeState.mode, content: @Composable () -> Unit) {
    val dark = when (themeMode) {
        "dark" -> true
        "light" -> false
        else -> isSystemInDarkTheme()      // auto：跟随系统 ✓
    }
    MaterialTheme(
        colorScheme = if (dark) DarkColors else LightColors,
        shapes = AppShapes,
        typography = AppTypography,
        content = content,
    )
}
