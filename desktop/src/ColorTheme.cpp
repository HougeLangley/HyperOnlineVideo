#include "ColorTheme.h"
#include <QFile>
#include <QImageReader>
#include <QtMath>
#include <array>

QColor ColorTheme::dominantColor(const QImage &img, bool *ok) {
    if (ok) *ok = false;
    if (img.isNull()) return neutral();
    // 缩到 32×32（快）——不追求精度，只求"整体观感色"
    const QImage small = img.convertToFormat(QImage::Format_RGB32)
                            .scaled(32, 32, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    struct Acc { double w = 0, r = 0, g = 0, b = 0; };
    std::array<Acc, 12> buckets{};
    for (int y = 0; y < small.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        for (int x = 0; x < small.width(); ++x) {
            const QColor c(line[x]);
            const double h = c.hsvHueF() < 0 ? 0.0 : c.hsvHueF();
            const double s = c.hsvSaturationF();
            const double v = c.valueF();
            if (v < 0.12 || s < 0.15) continue;              // 过暗 / 过灰：不参与投票
            const int idx = qBound(0, int(h * 12.0), 11);
            const double w = s * s * v * v;                  // 权重 = 饱和² × 明度²
            auto &a = buckets[idx];
            a.w += w; a.r += c.redF() * w; a.g += c.greenF() * w; a.b += c.blueF() * w;
        }
    }
    const Acc *best = nullptr;
    for (const auto &a : buckets) if (a.w > 1e-6 && (!best || a.w > best->w)) best = &a;
    if (!best) return neutral();                             // 全灰/全黑 → 兜底
    QColor c = QColor::fromRgbF(best->r / best->w, best->g / best->w, best->b / best->w);
    // 观感强化：饱和度不低于 0.45、明度落在 0.34~0.78（背景"有色但不刺眼"）
    double h = c.hsvHueF() < 0 ? 0.0 : c.hsvHueF();
    double s = qMax(0.45, c.hsvSaturationF());
    double v = qBound(0.34, c.valueF(), 0.78);
    if (ok) *ok = true;
    return QColor::fromHsvF(h, qMin(1.0, s), v);
}

QImage ColorTheme::boxBlur(const QImage &src, int radius, int passes) {
    if (src.isNull() || radius < 1) return src;
    QImage img = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = img.width(), h = img.height();
    QImage tmp(w, h, QImage::Format_ARGB32_Premultiplied);
    auto blurLine = [radius](const QRgb *in, QRgb *out, int n, int stride) {
        // 前缀和实现的一维盒式模糊（水平/垂直同法，用 stride 复用）
        long long sa = 0, sr = 0, sg = 0, sb = 0;
        const int span = radius * 2 + 1;
        for (int i = -radius; i <= radius; ++i) {
            const int j = qBound(0, i, n - 1);
            const QRgb p = in[j * stride];
            sa += qAlpha(p); sr += qRed(p); sg += qGreen(p); sb += qBlue(p);
        }
        for (int i = 0; i < n; ++i) {
            out[i * stride] = qRgba(int(sr / span), int(sg / span), int(sb / span), int(sa / span));
            const int add = qBound(0, i + radius + 1, n - 1);
            const int sub = qBound(0, i - radius, n - 1);
            const QRgb pa = in[add * stride], ps = in[sub * stride];
            sa += qAlpha(pa) - qAlpha(ps); sr += qRed(pa) - qRed(ps);
            sg += qGreen(pa) - qGreen(ps); sb += qBlue(pa) - qBlue(ps);
        }
    };
    for (int p = 0; p < passes; ++p) {
        for (int y = 0; y < h; ++y) blurLine(reinterpret_cast<const QRgb *>(img.constScanLine(y)),
                                            reinterpret_cast<QRgb *>(tmp.scanLine(y)), w, 1);
        for (int x = 0; x < w; ++x) blurLine(reinterpret_cast<const QRgb *>(tmp.constScanLine(0)) + x,
                                             reinterpret_cast<QRgb *>(img.scanLine(0)) + x, h, w);
    }
    return img;
}

ColorTheme ColorTheme::make(const QImage &cover) {
    ColorTheme t;
    if (cover.isNull()) return t;
    bool ok = false;
    t.tint_ = dominantColor(cover, &ok);
    if (!ok) return t;                                       // 取色失败 → valid()==false，绘制端用兜底
    double h = t.tint_.hsvHueF() < 0 ? 0.0 : t.tint_.hsvHueF();
    const double s = t.tint_.hsvSaturationF(), v = t.tint_.valueF();
    t.tintDark_ = QColor::fromHsvF(h, qMin(1.0, s * 1.05), qMax(0.06, v * 0.42));
    // 磨砂底：缩到 ~220px → 大半径模糊 → 绘制时拉伸（≈ 高斯玻璃）
    const QImage small = cover.convertToFormat(QImage::Format_ARGB32_Premultiplied)
                             .scaled(220, qMax(1, 220 * cover.height() / qMax(1, cover.width())),
                                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    t.backdrop_ = boxBlur(small, qMax(2, qMin(small.width(), small.height()) / 8), 3);
    // 竖向渐变：1×256（底部暗 → 顶部亮）
    QImage g(1, 256, QImage::Format_ARGB32_Premultiplied);
    for (int i = 0; i < 256; ++i) {
        const double f = i / 255.0;                          // 0=底（暗）→1=顶（亮）
        const int r = int(t.tintDark_.red() + (t.tint_.red() - t.tintDark_.red()) * f);
        const int gg = int(t.tintDark_.green() + (t.tint_.green() - t.tintDark_.green()) * f);
        const int b = int(t.tintDark_.blue() + (t.tint_.blue() - t.tintDark_.blue()) * f);
        g.setPixel(0, 255 - i, qRgba(r, gg, b, 255));
    }
    t.gradient_ = g;
    t.valid_ = true;
    return t;
}

ColorTheme ColorTheme::makeFromPath(const QString &path) {
    QImageReader r(path);
    r.setAutoTransform(true);
    const QImage img = r.read();
    ColorTheme t = make(img);
    t.sourceName_ = path;
    return t;
}
