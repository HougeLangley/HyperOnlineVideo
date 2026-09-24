#pragma once
// ── SettingsDialog：设置对话框（从 main.cpp 的 MainWindow 成员搬出 ✓ S2 拆分第二步 ✓ 见文档 45/51）──
// 设计与 SelfTest 一致（S1 验证过的手法是有效的 ✓）：
//   * 上下文**注入** ✓ 不把 MainWindow 的私有成员改成 public ✗
//   * **零行为改动** ✓：函数体逐字搬迁 ✓ 仅做 成员名 → ctx.成员名 的机械替换 ✓（6 项替换均带计数断言 ✓）
//   * 调用方仍是 MainWindow 的薄封装 ✓（`openSettingsDialog()` ✓ 保持原签名 ✓ 两处调用点无需改动 ✓）
#include <functional>
#include <QString>

class QWidget;
class QListWidget;
class Settings;
class MpvWidget;

namespace SettingsDialog {

struct Ctx {
    Settings    *settings = nullptr;  // 读取当前值以初始化各控件 ✓
    QListWidget *results  = nullptr;  // 保存后追加一行"设置已保存" ✓
    MpvWidget   *player   = nullptr;  // 玻璃质感等需立即下发的项 ✓

    std::function<bool(const QString &, const QString &)> applySetting;  // 写一项设置（含校验与落盘 ✓）
    std::function<void()>                  applyThemeSettingNow;        // 主题立即生效 ✓
    std::function<void(const QString &)>   setStatusLine;               // 状态栏提示 ✓
};

/// 打开模态设置对话框 ✓（parent 一般是 MainWindow ✓ 用于对话框归属与居中 ✓）
void open(QWidget *parent, const Ctx &ctx);

} // namespace SettingsDialog
