#pragma once

#include <QString>

/**
 * GPU 兼容层（虚拟化环境）
 *
 * 背景（Arch aarch64 VM 实测 ✗ 2026-09-24）：
 *   虚拟机里（virtio-gpu/virgl）Chromium 的 GL 上下文创建失败 ——
 *     [ERROR] EGL Driver message (Error) eglCreateContext: Requested version is not supported
 *     [ERROR] ContextResult::kTransientFailure: Failed to send GpuControl.CreateCommandBuffer
 *   后果：登录窗等 QtWebEngine 内容**首帧渲染不稳**：窗口区域在页面绘出前会显示
 *   **未初始化像素**（用户看到的是窗口后面的 Plasma 桌面壁纸与小组件 ✗ 形似"页面变成了别的"）。
 *   修复：虚拟化环境自动为 QtWebEngine 关闭 GPU（Chromium 转 CPU 光栅 ✓ 实测 GL 报错清零 ✓
 *   页面渲染完全正常 ✓）。真机（有稳定 GPU）不受影响。
 *
 * 仅 Linux 桌面端使用；macOS（WKWebView）/Android（WebView）由系统托管，无此路径。
 */
namespace GpuCompat {

/** 是否处于虚拟机/虚拟化环境（QEMU/KVM/VMware/VirtualBox/Hyper-V 等）。 */
bool isVirtualMachine();

/** 纯函数（供自检断言 ✓）：把 add 里的每个 token 追加进 flags（已含则不重复）。 */
QString mergeFlags(const QString &flags, const QString &add);

/**
 * 启动早期调用（**必须在 QApplication 构造之前** ✓ Chromium 只读一次环境）。
 * 仅在：①虚拟化环境 ②用户未自行设置 QTWEBENGINE_CHROMIUM_FLAGS 时生效 ✓。
 */
void applyChromiumCompat();

} // namespace GpuCompat
