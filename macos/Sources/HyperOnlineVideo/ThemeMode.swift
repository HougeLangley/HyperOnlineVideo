import AppKit

/// 主题模式：`ui.theme` = auto | dark | light —— 与 **Linux / Android 同键同语义** ✓
/// 用户要求（2026-09-18）：默认 auto 跟随系统；设置里可强制浅色/深色。
enum ThemeMode: String {
    case auto, dark, light

    static func parse(_ s: String) -> ThemeMode { ThemeMode(rawValue: s.trimmingCharacters(in: .whitespaces).lowercased()) ?? .auto }

    var label: String {
        switch self {
        case .auto: return "跟随系统（浅色 / 深色）"
        case .light: return "浅色"
        case .dark: return "深色"
        }
    }

    /// auto 时看系统当前明暗（NSApp.effectiveAppearance ✓）
    var isLight: Bool {
        switch self {
        case .light: return true
        case .dark: return false
        case .auto:
            let m = NSApp.effectiveAppearance.bestMatch(from: [.aqua, .darkAqua])
            return m == .aqua
        }
    }
}

/// 集中配色（阶段二 ✓）：**自绘/图层**用的颜色统一从这里取 ✓
/// 说明：文本类颜色优先用 AppKit **语义色**（`.labelColor` / `.secondaryLabelColor` ✓ 它们自动跟随外观 ✓）；
/// 这里只补充"语义色覆盖不到"的**卡片底/边框/选中态**（原来硬编码 NSColor.white ✗ 浅色主题下几乎看不见 ✗）。
enum ThemeColors {
    private static var light: Bool { ThemeController.shared.mode.isLight }

    /// 卡片底：深色=白色 7% ✓ / 浅色=黑色 6% ✓
    static var cardBG: NSColor {
        light ? NSColor.black.withAlphaComponent(0.055) : NSColor.white.withAlphaComponent(0.07)
    }
    /// 卡片边框：深色=白色 10% ✓ / 浅色=黑色 12% ✓
    static var cardBorder: NSColor {
        light ? NSColor.black.withAlphaComponent(0.12) : NSColor.white.withAlphaComponent(0.10)
    }
    /// 选中态卡片底
    static var cardSelected: NSColor {
        light ? NSColor.controlAccentColor.withAlphaComponent(0.18) : NSColor.white.withAlphaComponent(0.18)
    }
    /// 吸顶/浮层面板底（覆盖在画面上时用 ✓）
    static var overlayPanel: NSColor {
        light ? NSColor.white.withAlphaComponent(0.86) : NSColor.black.withAlphaComponent(0.62)
    }
}

/// 主题控制器：读 `ui.theme` → 设置 AppKit 外观；auto 时**监听系统明暗变化**并即时跟随 ✓
///
/// 说明（阶段一 ✓）：本类负责**外观与标准控件**跟随（窗口标题栏 / 面板 / 原生控件 ✓）；
/// 应用内**自绘画布**（播放器覆盖层、卡片、歌词等）目前仍是深色 ✗ —— 那层的浅色化是**阶段二** ✓
/// （与 Linux 端同一套做法：那边先做了 `Theme::isDark()` 让绘制层跟随 ✓ 这里照同一路线推进 ✓）
final class ThemeController {
    static let shared = ThemeController()

    private(set) var mode: ThemeMode = .auto
    private var observingSystem = false

    /// 从设置读取并应用（启动时 + 设置保存后各调用一次 ✓ 幂等 ✓）
    /// ⚠️ 必须传**已加载的 Settings 实例** ✗ —— 新建 `Settings.shared` 内部是空表 ✓ 会永远读到默认 "auto" ✗✓（本次实测踩到）
    func applyFromSettings(_ settings: Settings) {
        // 优先用传入实例；但启动早期 settings 可能**还没 load** ✗（实测：日志顺序证明读到的是默认值 ✗）
        // → 只要实例里没有该键，就**直接读设置文件** ✓（不依赖调用顺序，稳 ✓ 与 Linux 端 has() 同思路 ✓）
        // ⚠️ **文件优先**：Settings 实例的 values 里混了 defaults ✗ → 默认值会伪装成"用户设置" ✗
        //（与 Linux 端 Settings::has 踩的是**同一个坑** ✓ 教训：判断用户选择要查原始来源 ✓）
        var raw = ThemeController.readThemeFromFile()
        if raw.isEmpty { raw = settings.string("ui.theme", "") }   // 文件里没有才退回实例
        mode = ThemeMode.parse(raw.isEmpty ? "auto" : raw)
        applyEffective()
        if mode == .auto { startObservingSystem() } else { stopObservingSystem() }
    }

    /// 应用当前模式（auto 会实时读系统明暗 ✓）
    func applyEffective() {
        let light = mode.isLight
        let name: NSAppearance.Name = light ? .aqua : .darkAqua
        let app = NSAppearance(named: name)
        NSApp.appearance = app
        for w in NSApp.windows { w.appearance = app }      // 已存在的窗口也一起切 ✓
        Config.log("[THEME] ui.theme=\(mode.rawValue) → AppKit 外观=\(light ? "aqua(浅色)" : "darkAqua(深色)")"
                   + "（自绘画布浅色化为阶段二）")
    }

    /// 直接读设置文件里的 ui.theme（settings 实例尚未 load 时的兜底 ✓）
    static func readThemeFromFile() -> String {
        let obj = Config.readJSON(Config.settingsPath)      // 返回非可选字典 ✓
        return (obj["ui.theme"] as? String) ?? ""
    }

    private func startObservingSystem() {
        guard !observingSystem else { return }
        observingSystem = true
        // 系统明暗变化通知（auto 模式才有意义 ✓）
        DistributedNotificationCenter.default().addObserver(
            forName: NSNotification.Name("AppleInterfaceThemeChangedNotification"),
            object: nil, queue: .main) { [weak self] _ in
                guard let self, self.mode == .auto else { return }
                Config.log("[THEME] 系统明暗变化 → 重新应用（auto）")
                self.applyEffective()
            }
    }

    private func stopObservingSystem() {
        guard observingSystem else { return }
        observingSystem = false
        DistributedNotificationCenter.default().removeObserver(self)
    }
}
