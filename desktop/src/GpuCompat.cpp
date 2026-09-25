#include "GpuCompat.h"

#include <QDebug>
#include <QFile>
#include <QStringList>

namespace {

QString firstLine(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readLine(256)).trimmed();
}

bool hasAny(const QString &hay, std::initializer_list<const char *> keys) {
    for (const char *k : keys)
        if (hay.contains(QLatin1String(k), Qt::CaseInsensitive))
            return true;
    return false;
}

} // namespace

namespace GpuCompat {

bool isVirtualMachine() {
    // ① aarch64：设备树 model（QEMU 常见 "linux,dummy-virt" / 含 qemu、virt）
    if (hasAny(firstLine(QStringLiteral("/proc/device-tree/model")),
               {"qemu", "dummy-virt", "kvm", "virtual"}))
        return true;

    // ② x86/其它：DMI 厂商与产品名
    if (hasAny(firstLine(QStringLiteral("/sys/class/dmi/id/sys_vendor")),
               {"qemu", "kvm", "vmware", "virtualbox", "innotek", "xen", "microsoft"}) ||
        hasAny(firstLine(QStringLiteral("/sys/class/dmi/id/product_name")),
               {"kvm", "qemu", "virtual", "vmware", "virtualbox", "hyper-v"}))
        return true;

    // ③ 兜底：CPU 的 hypervisor 标志（流式读，不整读大文件）
    QFile cpu(QStringLiteral("/proc/cpuinfo"));
    if (cpu.open(QIODevice::ReadOnly)) {
        while (!cpu.atEnd()) {
            const QByteArray line = cpu.readLine(512);
            if (line.contains(" hypervisor"))
                return true;
            if (line.startsWith("bogomips") || line.startsWith("BogoMIPS"))
                break;   // 每个 CPU 块末尾，读一个块足够
        }
    }
    return false;
}

QString mergeFlags(const QString &flags, const QString &add) {
    QStringList have = flags.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QStringList want = add.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &w : want)
        if (!have.contains(w))
            have << w;
    return have.join(QLatin1Char(' '));
}

void applyChromiumCompat() {
    if (!isVirtualMachine())
        return;

    const QString cur = qEnvironmentVariable("QTWEBENGINE_CHROMIUM_FLAGS");
    if (!cur.isEmpty()) {
        if (!cur.contains(QStringLiteral("--disable-gpu")))
            qInfo() << "[GPU] 虚拟化环境，但已存在自定义 QTWEBENGINE_CHROMIUM_FLAGS → 不覆盖"
                       "（若登录窗渲染异常，可加 --disable-gpu）";
        return;
    }

    // 实测：GL 上下文创建失败（eglCreateContext ✗）在虚拟机上必然发生 → CPU 光栅最稳 ✓
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");
    qInfo() << "[GPU] 检测到虚拟化环境 → QtWebEngine 使用 CPU 渲染（--disable-gpu ✓ 避免首帧渲染异常）";
}

} // namespace GpuCompat
