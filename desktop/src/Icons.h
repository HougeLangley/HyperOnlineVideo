#pragma once
#include "IconIds.h"
#include "Tokens.h"

#include <QIcon>

class QPainter;
class QRectF;
class QColor;

// 图标唯一来源：Material Symbols Rounded 子集（resources/icons，生成方法见 tools/gen-icons.py）。
namespace Icons {
// 注册两份字体，main() 里 QApplication 构造之后调用一次；失败返回 false。
bool loadFonts();
// 颜色在绘制时按当前主题取，切换明暗后自动跟随。On 状态（checked）用实心字形和 checkedRole。
QIcon icon(IconId id, Tok role = Tok::OnSurfaceVariant, Tok checkedRole = Tok::OnPrimaryContainer);
void paint(QPainter &p, const QRectF &r, IconId id, const QColor &c, bool filled = false);
}  // namespace Icons
