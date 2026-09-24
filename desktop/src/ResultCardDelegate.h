// ── ResultCardDelegate：结果卡片自绘委托（S0.5a ✓ 从 main.cpp 原样搬出 ✓ 零行为改动 ✓）──
#pragma once
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QModelIndex>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QString>

class ResultCardDelegate : public QStyledItemDelegate {
public:
    // 字体与度量在**构造时建一次**：避免每帧新建 QFont/QFontMetrics（既省 CPU，
    // 也避免字体子系统被反复触碰 —— 与句柄泄漏排查方向一致）
    explicit ResultCardDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {
        fTitle_ = QApplication::font(); fTitle_.setPixelSize(12); fTitle_.setWeight(QFont::DemiBold);
        fMeta_  = QApplication::font(); fMeta_.setPixelSize(11);
        fmTitle_ = new QFontMetrics(fTitle_);
        fmMeta_  = new QFontMetrics(fMeta_);
    }
    ~ResultCardDelegate() override { delete fmTitle_; delete fmMeta_; }

    static constexpr int kCardW = 186;      // 单列最小宽（决定两列阈值）
    static constexpr int kCardH = 138;      // 兼容常量（不再用于固定高度）
    static constexpr int kRowH  = 24;       // 状态行（"共 N 条结果"等）
    static constexpr int kPad   = 5;
    static constexpr int kLineH = 16;       // 标题行高
    static constexpr int kMetaH = 15;       // 元信息行高
    static constexpr int kMaxTitleLines = 2;

    /** 卡片判定：显式标记（见 setItemThumb）或已拿到缩略图 */
    static bool isCard(const QModelIndex &idx) {
        return idx.data(Qt::UserRole + 12).toBool()
            || !idx.data(Qt::DecorationRole).value<QIcon>().isNull();
    }
    QFont fTitle_, fMeta_;
    QFontMetrics *fmTitle_ = nullptr;
    QFontMetrics *fmMeta_ = nullptr;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &idx) const override {
        if (!isCard(idx)) return QSize(qMax(kCardW, viewWidth() - 24), kRowH);
        const int w = cardWidth();
        // 高度按内容算 → 标题长的卡片更高 → 列内错落 = 瀑布流
        return QSize(w, kPad + thumbHeight(w) + 4
                        + titleLines(titleOf(idx), w) * kLineH + 1 + kMetaH + kPad);
    }
    // 列数由"**最小可读列宽**"决定，而不是由固定阈值决定。
    // 之前用 kCardW(186)*2+18=390 当阈值 → 面板 330px 时只有 1 列 = 单列满宽列表（用户实测"没有瀑布流"）。
    // 现在：列宽不低于 140px 就尽量多排列 → 窄面板也能出 2~3 列瀑布。
    static constexpr int kMinColW = 140;
    int colsFor(int vw) const { return qBound(1, vw / (kMinColW + 8), 4); }
    int cardWidth() const {
        const int vw = viewWidth() - 24;
        const int cols = colsFor(vw);
        return (vw - (cols - 1) * 8) / cols;
    }
    int thumbHeight(int w) const { const int iw = w - kPad * 2; return qRound(iw * 9.0 / 16.0); }
    static QString titleOf(const QModelIndex &idx) {
        const QString t = idx.data(Qt::DisplayRole).toString();
        const int sep = t.indexOf(" · ");
        return sep > 0 ? t.left(sep) : t;
    }
    /** 标题占几行（1~2 行，按词换行测量） */
    int titleLines(const QString &t, int w) const {
        const QRect r = fmTitle_->boundingRect(QRect(0, 0, qMax(20, w - kPad * 2), 4000),
                                              Qt::TextWordWrap, t);
        return qBound(1, (r.height() + kLineH - 1) / kLineH, kMaxTitleLines);
    }
    /** 视图可用宽度（delegate 的 parent 就是结果列表） */
    int viewWidth() const {
        if (auto *v = qobject_cast<const QListView *>(parent())) return qMax(200, v->viewport()->width());
        return 392;
    }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setRenderHint(QPainter::SmoothPixmapTransform, true);
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered  = opt.state & QStyle::State_MouseOver;
        const QString text = idx.data(Qt::DisplayRole).toString();

        if (!isCard(idx)) {                              // ── 状态行：单行暗色
            QFont f = opt.font; f.setPixelSize(12);
            p->setFont(f);
            p->setPen(QColor(Theme::textDim()));
            p->drawText(opt.rect.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter,
                        QFontMetrics(f).elidedText(text, Qt::ElideRight, opt.rect.width() - 16));
            p->restore();
            return;
        }

        // ── 卡片底（悬停/选中）
        const QRect r = opt.rect.adjusted(2, 2, -2, -2);
        QColor bg(255, 255, 255, 12);
        const bool dkCard = Theme::isDark();
        if (selected)      bg = QColor(88, 136, 216, 135);
        else if (hovered)  bg = dkCard ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 22);
        p->setPen(Qt::NoPen);
        p->setBrush(bg);
        p->drawRoundedRect(r, 9, 9);

        // ── 缩略图（16:9，圆角裁剪，cover 填充不裁半截）
        const int pad = 5;
        const int iw = r.width() - pad * 2;
        const QRect ir(r.left() + pad, r.top() + pad, iw, qRound(iw * 9.0 / 16.0));
        QPainterPath clip;
        clip.addRoundedRect(ir, 6, 6);
        p->setClipPath(clip);
        const QIcon ic = idx.data(Qt::DecorationRole).value<QIcon>();
        if (ic.isNull()) {
            p->fillRect(ir, dkCard ? QColor(255, 255, 255, 16) : QColor(0, 0, 0, 14));   // 占位底色（跟随主题）
        } else {
            // 完整显示（fit），不裁切 —— 与瀑布流视图保持一致（用户要求缩略图必须完整）
            const QSize nat = ic.availableSizes().value(0);
            const QPixmap src = nat.isValid() ? ic.pixmap(nat) : ic.pixmap(ir.size());
            const QPixmap sc = src.scaled(ir.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p->drawPixmap(QRect(ir.center().x() - sc.width() / 2, ir.center().y() - sc.height() / 2,
                                sc.width(), sc.height()), sc);
        }
        p->setClipping(false);

        // ── 标题（最多两行，自动换行 → 卡片高度不同 → 瀑布流错落感）
        QString title = text, sub;
        const int sep = text.indexOf(" · ");
        if (sep > 0) { title = text.left(sep); sub = text.mid(sep + 3); }
        const QString src = idx.data(Qt::UserRole + 10).toString();
        const QString bottom = src.isEmpty() ? sub
                             : (sub.isEmpty() ? src : src + " · " + sub);

        p->setFont(fTitle_);
        p->setPen(selected ? QColor(255, 255, 255) : QColor(Theme::text()));
        const QRect tr(r.left() + pad, ir.bottom() + 4, iw, kLineH * kMaxTitleLines);
        p->drawText(tr, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, title);

        // ── 元信息一行：来源 · 作者/时长（暗色小字，单行省略）
        p->setFont(fMeta_);
        p->setPen(QColor(Theme::textDim()));
        const QRect br(r.left() + pad, tr.top() + kLineH * titleLines(title, r.width()) + 1, iw, kMetaH);
        p->drawText(br, Qt::AlignLeft | Qt::AlignVCenter,
                    fmMeta_->elidedText(bottom, Qt::ElideRight, br.width()));
        p->restore();
    }
};
