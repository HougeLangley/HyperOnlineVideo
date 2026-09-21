#pragma once
// ── 真·瀑布流结果视图（每列独立流动）────────────────────────────────────────
// 为什么自建：QListWidget/QListView 的 IconMode 只能"按行"排（行高 = 该行最高卡片 →
// 短卡片下方留白，用户实测"不是瀑布流"）；Flow::TopToBottom 又是"按视口高度分列"，会横向溢出。
// Qt 的 item view 没有 masonry 布局，故自建。
//
// 设计：**保留 QListWidget 作为数据源与逻辑层**（所有 addItem/点击/缩略图逻辑保持不变，仅隐藏它），
//       本视图镜像其内容，并把每张卡片放进"当前累计高度最小的那一列" → 真瀑布流。
#include <QListWidget>
#include <QRect>
#include <QScrollArea>
#include <QVector>
#include <functional>

class MasonryView : public QScrollArea {
    Q_OBJECT
public:
    // ── 卡片度量：**唯一真相**（布局、绘制、默认尺寸全部用这几个常量；
    //    .cpp 里供自由函数使用的同名常量必须与这里保持一致）──
    static constexpr int kCardW = 186;   // 单列最小宽（决定列数）
    static constexpr int kPad   = 5;     // 卡片内边距
    static constexpr int kLineH = 16;    // 标题行高
    static constexpr int kMetaH = 15;    // 来源行高
    static constexpr int kGap   = 8;     // 卡片间距
    explicit MasonryView(QWidget *parent = nullptr);
    /** 绑定数据源（不接管所有权）；绑定后会重建 */
    void setSource(QListWidget *list);
    void rebuild();                        // 全量重建（插入/删除/模型重置后调用）
    void refreshRows(int first, int last); // 局部刷新（缩略图异步到达）
    void setSelectedRow(int row);   // 定义在 .cpp：Canvas 在此处还是不完整类型
    int selectedRow() const { return selected_; }
    int columnCount() const { return cols_; }
    // ── 结果区"合适尺寸"的唯一真相（宽度/高度都从卡片度量算出，供主窗口设分栏与窗口尺寸）──
    // 横向刚好 2 张、纵向刚好 4 张；与绘制共用同一组常量，避免两处各算各的。
    static int cardHeightForDefault() {          // 以"标题两行"的卡片为基准
        const int iw = kCardW - 2 * kPad;
        return kPad + qRound(iw * 9.0 / 16.0) + 4 + 2 * kLineH + 1 + kMetaH + kPad;
    }
    static int defaultPanelWidth()  { return 2 * kCardW + 3 * kGap + 8; }
    static int defaultPanelHeight() { return 4 * cardHeightForDefault() + 5 * kGap; }
    std::function<void(int)> onActivate;   // 单击卡片/回车（主窗口接 playItem）

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev) override;

private:
    class Canvas;
    QListWidget *list_ = nullptr;
    Canvas *canvas_ = nullptr;
    QVector<QRect> rects_;      // 卡片几何
    QVector<int> rowOf_;        // rects_ 下标 → QListWidget 行号
    QVector<int> thumbOf_;      // rects_ 下标 → 该卡片的缩略图显示高（按图片真实比例）
    int cols_ = 2;
    int cardW_ = 160;
    int hover_ = -1;            // rects_ 下标（-1 = 无）
    int selected_ = -1;         // QListWidget 行号

    void layout();
    int indexAt(const QPoint &p) const;
    void paintCanvas(QWidget *w);
    static QSize cardSizeFor(const QString &text, const QString &source, int width, int thumbH);
    /** 标题占几行（布局与绘制共用同一份计算，避免两边各算各的导致留白） */
    int titleLinesFor(const QString &t, int innerW) const;
};
