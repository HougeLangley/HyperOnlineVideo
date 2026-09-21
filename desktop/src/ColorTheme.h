#pragma once
// ColorTheme —— 玻璃质感（Glass）的主题色 + 磨砂底 + 渐变
//
// 蓝本：macOS 端 macos/Sources/HyperOnlineVideo/MusicTheme.swift（自绘玻璃）。
// 为什么不用系统模糊（KWin _KDE_NET_WM_BLUR_BEHIND_REGION / WA_TranslucentBackground）：
//   播放画面是 mpv 的 GL 上下文，上面叠半透明子控件不会正确合成 —— 该结论 macOS 与
//   Linux（见 SubtitleOverlay.h 注释）两端各自独立验证过，故统一走"同层自绘"。
#include <QColor>
#include <QImage>

class ColorTheme {
public:
    /// 从封面图构造主题（失败时 valid()==false，绘制端应回退到 neutral()）
    static ColorTheme make(const QImage &cover);
    static ColorTheme makeFromPath(const QString &path);

    bool valid() const { return valid_; }
    QColor tint() const { return tint_; }          // 主题色（亮，渐变顶部）
    QColor tintDark() const { return tintDark_; }  // 主题色（暗，渐变底部）
    const QImage &backdrop() const { return backdrop_; }   // 缩小+模糊后的封面（磨砂玻璃底，绘制时拉伸）
    const QImage &gradient() const { return gradient_; }   // 1×256 竖向渐变（拉伸铺满）
    QString sourceName() const { return sourceName_; }

    /// 兜底色：中性深蓝灰（保证白字清晰）
    static QColor neutral() { return QColor(0x24, 0x2B, 0x38); }

    /// 主色提取：缩到 32×32 → 按色相 12 桶加权投票（权重 = 饱和² × 明度²，跳过过暗/过灰）
    static QColor dominantColor(const QImage &img, bool *ok = nullptr);
    /// 快速盒式模糊（跑 3 遍 ≈ 高斯）；只用缩小后的图，毫秒级
    static QImage boxBlur(const QImage &src, int radius, int passes = 3);

private:
    bool valid_ = false;
    QColor tint_ = ColorTheme::neutral();
    QColor tintDark_ = QColor(0x12, 0x15, 0x1C);
    QImage backdrop_, gradient_;
    QString sourceName_;
};
