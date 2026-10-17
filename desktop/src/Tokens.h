#pragma once
// 设计 token：颜色角色与尺寸。取值与用途见 design/desktop-design-language.md；改值只改这里和 Theme.cpp 的色板。

enum class Tok {
    Surface, SurfaceLow, SurfaceContainer, SurfaceHigh, SurfaceHighest,
    OnSurface, OnSurfaceVariant, Outline, OutlineVariant,
    Primary, OnPrimary, PrimaryContainer, OnPrimaryContainer,
    Error, Success, Scrim, OnScrim,
    Count
};

namespace M {
// 间距：4 的倍数，只用这五档
constexpr int space1 = 4;
constexpr int space2 = 8;
constexpr int space3 = 12;
constexpr int space4 = 16;
constexpr int space6 = 24;
// 控件高度
constexpr int controlH      = 40;   // 按钮、输入框、下拉框、标准图标按钮
constexpr int controlSmallH = 32;   // 表格行内的小图标按钮、筛选芯片
constexpr int controlLargeH = 48;   // 只给播放/暂停
// 图标字号（像素）
constexpr int iconSmall    = 20;    // 带文字的按钮、菜单、输入框、小图标按钮
constexpr int iconStandard = 24;    // 标准图标按钮
constexpr int iconLarge    = 28;    // 播放/暂停
constexpr int iconHero     = 48;    // 空状态插图
// 圆角
constexpr int radiusXs = 4;         // 复选框
constexpr int radiusSm = 8;         // 缩略图、菜单项、提示框
constexpr int radiusMd = 12;        // 结果卡片、列表、表格
constexpr int radiusLg = 16;        // 面板、对话框
// 字号（像素）
constexpr int fontTitle   = 16;
constexpr int fontBody    = 14;
constexpr int fontCaption = 12;
}  // namespace M
