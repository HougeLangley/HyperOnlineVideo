#include "MasonryView.h"

#include <QScrollBar>
#include <cstdio>
#include "Theme.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QWheelEvent>
#include <cstdio>

namespace {
// 注意：**几何常量只有一份定义**（MasonryView.h 的类静态 ✓ 见踩坑 #135）——
// 这里原来重复定义了一份（编译器报 unused ✗ 实测发现）→ 已删 ✓
constexpr int kMaxLines = 2;   // 标题最多两行
constexpr int kMinColW = 140;  // 最小可读列宽（决定列数）

QString titleOf(const QString &t) {
    const int s = t.indexOf(" · ");
    return s > 0 ? t.left(s) : t;
}
QString metaOf(const QString &t, const QString &src) {
    QString sub;
    const int s = t.indexOf(" · ");
    if (s > 0) sub = t.mid(s + 3);
    if (src.isEmpty()) return sub;
    return sub.isEmpty() ? src : src + " · " + sub;
}
}  // namespace

class MasonryView::Canvas : public QWidget {
public:
    explicit Canvas(MasonryView *v) : QWidget(v), view_(v) {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override { view_->paintCanvas(this); }

    // ── 滚轮支持（v1.2.0 实测修复）────────────────────────────────────────
    // 自绘容器（普通 QWidget）**默认不处理滚轮**，而它铺满整个 viewport，
    // 导致滚轮完全无效：用户"拖到底"根本拖不动 → 无限滚动永远触发不了（实测 [MORE] 无任何滚动记录）。
    // 这里显式把滚轮转交给滚动条；像素增量优先（触控板/高精度滚轮），否则用角度增量折算。
    void wheelEvent(QWheelEvent *ev) override {
        QScrollBar *sb = view_->verticalScrollBar();
        if (!sb) { ev->ignore(); return; }
        const QPoint px = ev->pixelDelta();
        const QPoint ang = ev->angleDelta();
        const int dy = !px.isNull() ? px.y() : ang.y() / 2;
        if (dy == 0) { ev->ignore(); return; }
        sb->setValue(sb->value() - dy);
        ev->accept();
    }

private:
    MasonryView *view_;
};

MasonryView::MasonryView(QWidget *parent) : QScrollArea(parent) {
    setWidgetResizable(true);
    // W1 ✓ 允许**窗口**缩到比"纵向 4 张卡片"更小：
    //   画布的 setMinimumHeight 只应用于**滚动范围** ✓，但 QScrollArea 会把 widget 的最小高
    //   透传为自己的 minimumSizeHint ✗ → 进而顶住主窗口 ✗（用户 KDE 实测：窗口顶天立地、resize 无效 ✓）
    //   ✗ 实测：setSizePolicy(Ignored) **无效** —— QScrollArea 重写了 minimumSizeHint ✗
    //   → 已改为在头文件里**显式覆写** minimumSizeHint() ✓（见 MasonryView.h ✓）
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFocusPolicy(Qt::StrongFocus);         // 键盘 ↑↓ 可用
    canvas_ = new Canvas(this);
    canvas_->installEventFilter(this);
    setWidget(canvas_);
}

void MasonryView::setSource(QListWidget *list) {
    list_ = list;
    rebuild();
}

void MasonryView::setSelectedRow(int row) {
    selected_ = row;
    if (canvas_) canvas_->update();
}

void MasonryView::rebuild() {
    layout();
    if (canvas_) canvas_->update();
}

void MasonryView::refreshRows(int, int) {
    // 缩略图异步到达会**改变卡片高度**（高度跟随图片比例）→ 必须重排，不能只重绘
    rebuild();
}

/** 缩略图显示高度：按**源图真实宽高比**（16:9 ~ 1:1 之间）——
 *  这是瀑布流"错落感"的素材来源。全都压成 16:9 的话各卡片等高，列高同步增长 → 又变回整齐网格。
 *  setItemThumb 缓存的是 320×180 KeepAspectRatio 的结果，所以图标尺寸即反映源图比例。 */
static int thumbHeightFor(const QIcon &ic, int width) {
    const int iw = qMax(20, width - 2 * 5);
    qreal aspect = 16.0 / 9.0;
    if (!ic.isNull()) {
        const QSize s = ic.availableSizes().value(0);
        if (s.isValid() && s.height() > 0) aspect = qreal(s.width()) / s.height();
    }
    aspect = qBound(9.0 / 16.0, aspect, 1.0);        // 竖屏/超宽都夹到合理范围，避免极端高矮
    return qRound(iw / aspect);
}

QSize MasonryView::cardSizeFor(const QString &text, const QString &, int width, int thumbH) {
    const int iw = qMax(20, width - kPad * 2);
    QFont f = QApplication::font();
    f.setPixelSize(12);
    f.setWeight(QFont::DemiBold);
    const QRect br = QFontMetrics(f).boundingRect(QRect(0, 0, iw, 4000), Qt::TextWordWrap, titleOf(text));
    const int lines = qBound(1, (br.height() + kLineH - 1) / kLineH, kMaxLines);
    return QSize(width, kPad + thumbH + 4 + lines * kLineH + 1 + kMetaH + kPad);
}

void MasonryView::layout() {
    rects_.clear();
    rowOf_.clear();
    thumbOf_.clear();
    if (!list_ || !canvas_) return;
    const int vw = qMax(140, viewport()->width() - kGap);
    cols_ = qBound(1, vw / (kMinColW + kGap), 4);
    cardW_ = (vw - (cols_ - 1) * kGap) / cols_;

    QVector<int> colY(cols_, 0);
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem *it = list_->item(row);
        if (!it) continue;
        // 状态行（无 URL、非卡片）不进瀑布；它们已由主窗口分流到状态行
        if (it->data(Qt::UserRole).toString().isEmpty()) continue;
        if (it->text().trimmed().isEmpty() && it->icon().isNull()) continue;   // 空条目不成卡
        const QSize sz = cardSizeFor(it->text(), it->data(Qt::UserRole + 10).toString(), cardW_,
                                     thumbHeightFor(it->icon(), cardW_));
        int c = 0;
        for (int i = 1; i < cols_; ++i)
            if (colY[i] < colY[c]) c = i;          // ← 放进当前最矮的列：瀑布流的关键
        rects_.append(QRect(c * (cardW_ + kGap), colY[c], cardW_, sz.height()));
        rowOf_.append(row);
        thumbOf_.append(thumbHeightFor(it->icon(), cardW_));   // 记录该卡片的缩略图高（绘制必须用同一值）
        colY[c] += sz.height() + kGap;
    }
    int total = 0;
    for (int y : colY) total = qMax(total, y);
    // 只设**最小高度**（决定滚动范围）；**不要**设最小宽度 ——
    // 与 setWidgetResizable(true) 会互相反馈，导致各次 layout 用的宽度不一致（实测出现卡片宽窄不一）。
    canvas_->setMinimumHeight(qMax(total + kGap, viewport()->height()));
    // W3 探针 ✓：记录布局参数（PiP/全屏等形态切换后"卡片被横向裁切"类问题的客观判据 ✓）
    //   只在关键值变化时打印 ✓ → 不刷屏（滚动、悬停都不会触发 ✓）
    //   ⚠️ 只在**卡宽/列数/横向滚动**变化时打印 ✓：拖拽窗口会连续改 vw ✗ 若也打印会刷屏 ✗
    static int lastCardW = -1, lastCols = -1, lastSX = -1;
    const int sx = horizontalScrollBar() ? horizontalScrollBar()->value() : 0;
    if (cardW_ != lastCardW || cols_ != lastCols || sx != lastSX) {
        lastCardW = cardW_; lastCols = cols_; lastSX = sx;
        std::fprintf(stderr, "[MASONRY] 布局 vw=%d cardW=%d 列=%d 画布宽=%d 视口宽=%d 横向滚动=%d 可见=%d\n",
                     vw, cardW_, cols_, canvas_ ? canvas_->width() : 0,
                     viewport() ? viewport()->width() : 0, sx, int(isVisible()));
    }
}

int MasonryView::indexAt(const QPoint &p) const {
    for (int i = 0; i < rects_.size(); ++i)
        if (rects_[i].contains(p)) return i;
    return -1;
}

void MasonryView::paintCanvas(QWidget *w) {
    if (!list_) return;
    QPainter p(w);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QFont fTitle = QApplication::font();
    fTitle.setPixelSize(12);
    fTitle.setWeight(QFont::DemiBold);
    QFont fMeta = QApplication::font();
    fMeta.setPixelSize(11);
    const QFontMetrics fmTitle(fTitle), fmMeta(fMeta);

    const QRect clip = w->rect();
    for (int i = 0; i < rects_.size(); ++i) {
        const QRect r = rects_[i];
        if (!r.intersects(clip)) continue;                  // 只画可见卡片
        QListWidgetItem *it = list_->item(rowOf_[i]);
        if (!it) continue;
        const bool selected = (rowOf_[i] == selected_);
        const bool hovered = (i == hover_);
        const QString text = it->text();
        const QString src = it->data(Qt::UserRole + 10).toString();

        // 卡片底
        // 跟随主题：深色用半透明白提亮、浅色用半透明黑压暗（硬编码白色在浅色主题下会看不见 ✗）
        const bool dk = Theme::isDark();
        QColor bg = dk ? QColor(255, 255, 255, 12) : QColor(0, 0, 0, 10);
        if (selected)      bg = QColor(88, 136, 216, 135);
        else if (hovered)  bg = dk ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 22);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(r, 9, 9);

        // 缩略图（16:9 圆角 + cover 填充）
        const int iw = r.width() - kPad * 2;
        // 关键：用**布局阶段记录的**缩略图高（按图片真实比例）。之前这里写死 16:9，
        // 与 sizeHint 不一致 → 差额全变成卡片里的空白（用户截图实测）。
        const int th = (i < thumbOf_.size()) ? thumbOf_[i] : qRound(iw * 9.0 / 16.0);
        const QRect ir(r.left() + kPad, r.top() + kPad, iw, th);
        QPainterPath clipPath;
        clipPath.addRoundedRect(ir, 6, 6);
        p.save();
        p.setClipPath(clipPath);
        const QIcon ic = it->icon();
        if (ic.isNull()) {
            p.fillRect(ir, dk ? QColor(255, 255, 255, 16) : QColor(0, 0, 0, 14));
        } else {
            // **完整显示**（fit）而不是裁切：卡片高度已按图片真实比例算过（thumbHeightFor），
            // 所以 fit 之后几乎不会留边；用 cover 反而会把封面切掉（用户实测红框：标题字被切、图案被切）。
            const QSize nat = ic.availableSizes().value(0);
            const QPixmap src0 = nat.isValid() ? ic.pixmap(nat) : ic.pixmap(ir.size());
            const QPixmap sc = src0.scaled(ir.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QRect dst(ir.center().x() - sc.width() / 2, ir.center().y() - sc.height() / 2,
                            sc.width(), sc.height());
            p.drawPixmap(dst, sc);
        }
        p.restore();

        // 标题（最多两行）+ 元信息
        p.setFont(fTitle);
        p.setPen(selected ? QColor(255, 255, 255) : QColor(Theme::text()));
        const QRect tr(r.left() + kPad, ir.bottom() + 4, iw, kLineH * kMaxLines);
        p.drawText(tr, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, titleOf(text));

        p.setFont(fMeta);
        p.setPen(QColor(Theme::textDim()));
        // 紧跟标题（原来贴卡片底部 → 高度一旦有出入就在中间留白）
        const int usedLines = titleLinesFor(titleOf(text), iw);
        const QRect br(r.left() + kPad, tr.top() + usedLines * kLineH + 1, iw, kMetaH);
        p.drawText(br, Qt::AlignLeft | Qt::AlignVCenter,
                   fmMeta.elidedText(metaOf(text, src), Qt::ElideRight, br.width()));
    }
}

bool MasonryView::eventFilter(QObject *obj, QEvent *ev) {
    if (obj != canvas_) return QScrollArea::eventFilter(obj, ev);
    switch (ev->type()) {
    case QEvent::MouseMove: {
        const int i = indexAt(static_cast<QMouseEvent *>(ev)->position().toPoint());
        if (i != hover_) { hover_ = i; canvas_->update(); }
        break;
    }
    case QEvent::Leave:
        if (hover_ != -1) { hover_ = -1; canvas_->update(); }
        break;
    case QEvent::MouseButtonRelease: {
        auto *m = static_cast<QMouseEvent *>(ev);
        if (m->button() != Qt::LeftButton) break;
        const int i = indexAt(m->position().toPoint());
        if (i < 0) break;
        setSelectedRow(rowOf_[i]);
        canvas_->setFocus();
        if (onActivate) onActivate(rowOf_[i]);
        break;
    }
    default:
        break;
    }
    return false;   // 滚轮/其它事件继续走默认处理
}

void MasonryView::resizeEvent(QResizeEvent *ev) {
    QScrollArea::resizeEvent(ev);
    layout();
    if (canvas_) canvas_->update();
}

void MasonryView::keyPressEvent(QKeyEvent *ev) {
    const int n = rowOf_.size();
    if (n == 0) { QScrollArea::keyPressEvent(ev); return; }
    int idx = rowOf_.indexOf(selected_);
    if (idx < 0) idx = 0;
    switch (ev->key()) {
    case Qt::Key_Down:  case Qt::Key_Right: idx = qMin(idx + 1, n - 1); break;
    case Qt::Key_Up:    case Qt::Key_Left:  idx = qMax(idx - 1, 0);     break;
    case Qt::Key_Home:  idx = 0;       break;
    case Qt::Key_End:   idx = n - 1;   break;
    case Qt::Key_Return: case Qt::Key_Enter:
        if (selected_ >= 0 && onActivate) onActivate(selected_);
        return;
    default:
        QScrollArea::keyPressEvent(ev);
        return;
    }
    setSelectedRow(rowOf_[idx]);
    const QRect r = rects_[idx];
    ensureVisible(r.center().x(), r.top(), 0, 24);   // 跟随滚动
}

// 标题占几行：布局（sizeHint）与绘制（paint）**共用这一份**计算，
// 避免两边各算各的导致卡片内留白（用户截图实测的 bug）。
int MasonryView::titleLinesFor(const QString &t, int innerW) const {
    QFont f = QApplication::font();
    f.setPixelSize(12);
    f.setWeight(QFont::DemiBold);
    const QRect br = QFontMetrics(f).boundingRect(QRect(0, 0, qMax(20, innerW), 4000),
                                                  Qt::TextWordWrap, titleOf(t));
    return qBound(1, (br.height() + kLineH - 1) / kLineH, kMaxLines);
}
