#pragma once
// ── SelfTest：队列 / 收藏逻辑自检（从 main.cpp 的 MainWindow 成员搬出 ✓ S1 拆分第一步 ✓ 见文档 45/49）──
// 设计（遵守文档 45 的风险控制 ✓）：
//   * 自检**不接触** MainWindow 的私有成员 ✗，改由调用方注入一个**极小上下文** ✓（只含它真正需要的 6 样 ✓）
//   * **零行为改动** ✓：函数体逐字搬迁 ✓ 仅把 成员名 → ctx.成员名 ✓（替换数已逐一断言 ✓）
//   * 输出格式**一字不改** ✓（`[SELFTEST] 队列自检: N/M 通过` ✓ 基线对比靠它 ✓）
#include <functional>

class QListWidget;
class MpvWidget;

namespace SelfTest {

struct Ctx {
    QListWidget *results     = nullptr;  // 结果输出列表（仅用于显示 PASS/FAIL 与汇总 ✓）
    MpvWidget   *player      = nullptr;  // 真 mpv 属性往返检查（renderReady() 为假时自动跳过 ✓）
    bool        *fillApplied = nullptr;  // MainWindow::lastFillApplied_（全屏铺满状态机 ✓）

    std::function<void(bool)> applyFillMode;   // 铺满下发（必须走 MainWindow 的门控 ✓）
    std::function<bool()>     isFullScreen;    // 是否处于视频全屏
    std::function<void()>     enterFullscreen; // 视频全屏：进入
    std::function<void()>     exitFullscreen;  // 视频全屏：退出
};

/// 跑完整套自检 ✓ 结果同时打到 qInfo / stderr（无人值守可读 ✓）并追加到 ctx.results ✓
void runQueueSelfTest(const Ctx &ctx);

} // namespace SelfTest
