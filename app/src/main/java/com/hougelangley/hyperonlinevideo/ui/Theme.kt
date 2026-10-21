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
    // 设计 token（与 desktop/src/Tokens.h / Theme.cpp 的深色板逐值一致）
    primary = Color(0xFF8AB4F8),
    onPrimary = Color(0xFF0B1B33),
    primaryContainer = Color(0xFF1E3A5F),
    onPrimaryContainer = Color(0xFFD3E3FD),
    secondary = Color(0xFFC3C7CF),
    onSecondary = Color(0xFF0F1115),
    secondaryContainer = Color(0xFF1E3A5F),
    onSecondaryContainer = Color(0xFFD3E3FD),
    background = Color(0xFF0F1115),
    onBackground = Color(0xFFE3E5EB),
    surface = Color(0xFF0F1115),
    onSurface = Color(0xFFE3E5EB),
    surfaceVariant = Color(0xFF282B31),
    onSurfaceVariant = Color(0xFFC3C7CF),
    surfaceContainerLowest = Color(0xFF0F1115),
    surfaceContainerLow = Color(0xFF171A1F),
    surfaceContainer = Color(0xFF1E2127),
    surfaceContainerHigh = Color(0xFF282B31),
    surfaceContainerHighest = Color(0xFF33363D),
    outline = Color(0xFF8F939B),
    outlineVariant = Color(0xFF4A4F57),
    error = Color(0xFFF28B82),
    onError = Color(0xFF0B1B33),
)

private val LightColors = lightColorScheme(
    // 深色板的浅色对应（同一套 token 角色）
    primary = Color(0xFF0B57D0),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFD3E3FD),
    onPrimaryContainer = Color(0xFF0B1B33),
    secondary = Color(0xFF44474E),
    onSecondary = Color(0xFFFFFFFF),
    secondaryContainer = Color(0xFFD3E3FD),
    onSecondaryContainer = Color(0xFF0B1B33),
    background = Color(0xFFF7F8FA),
    onBackground = Color(0xFF1A1C20),
    surface = Color(0xFFF7F8FA),
    onSurface = Color(0xFF1A1C20),
    surfaceVariant = Color(0xFFE4E7EC),
    onSurfaceVariant = Color(0xFF44474E),
    surfaceContainerLowest = Color(0xFFFFFFFF),
    surfaceContainerLow = Color(0xFFFFFFFF),
    surfaceContainer = Color(0xFFEEF0F3),
    surfaceContainerHigh = Color(0xFFE4E7EC),
    surfaceContainerHighest = Color(0xFFD9DDE3),
    outline = Color(0xFF74777F),
    outlineVariant = Color(0xFFC4C7CE),
    error = Color(0xFFB3261E),
    onError = Color(0xFFFFFFFF),
)

private val AppShapes = Shapes(
    // 圆角 token：4 / 8 / 12 / 16
    extraSmall = RoundedCornerShape(4.dp),
    small = RoundedCornerShape(8.dp),
    medium = RoundedCornerShape(12.dp),
    large = RoundedCornerShape(16.dp),
    extraLarge = RoundedCornerShape(16.dp),
)

private val AppTypography = Typography(
    headlineLarge = TextStyle(fontFamily = FontFamily.Default, fontWeight = FontWeight.SemiBold, fontSize = 34.sp, lineHeight = 40.sp),
    titleLarge = TextStyle(fontFamily = FontFamily.Default, fontWeight = FontWeight.Medium, fontSize = 22.sp, lineHeight = 28.sp),
    titleMedium = TextStyle(fontFamily = FontFamily.Default, fontWeight = FontWeight.Medium, fontSize = 16.sp, lineHeight = 22.sp),
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
