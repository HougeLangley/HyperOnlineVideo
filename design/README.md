# 设计资产

## App 图标（2026-09-12 终版 v3，MiniMax 生成）
- 源图：`app_icon_source_minimax.png`（1024x1024，MiniMax image-01 生成）
- 设计目标：**正面对称 + 强立体感 + 满幅可直接用**（v1 斜视扁平被否；v2 因裁剪对位偏移被否）
- 提示词（选定版 V50）：
  > App icon artwork: a glossy dark glass rounded-square tile with a thick beveled white 3D play triangle in the center, electric blue neon rim light on the top edge and hot pink neon rim light on the bottom edge, soft reflection below, the tile is perfectly centered in the square frame, deep black background, front view, mirror symmetrical, luxurious 3D render, no text
- **制作原则（v3 修正）**：不做内容裁剪。整图**等比缩放**（灯牌宽 → 画布 52%）+ 按**灯牌中心**对位贴到画布中心（灯牌中心由高阈值 bbox 测得），逐层数值校验：**灯牌居中偏移 (0,-3)px**、占比 52%
- 分层：background=渐变底+图形（就是最终图标）/ foreground=透明（零错位来源）/ monochrome=亮结构剪影
- v2 失败原因存档：用"含辉光的包围盒"求中心 → 辉光不对称把灯牌推偏；改为按亮结构（灯牌边框）中心对位后归正
- 加工：Pillow 亮度键控去底 → 自适应图标三层（前景 / 深色径向渐变背景 / 单色剪影）+ 启动页 logo
- 桌面遮罩预览：`app_icon_masks_preview.png`
- 生成产物：`app/src/main/res/mipmap-xxxhdpi/ic_launcher_{foreground,background,monochrome}.png`、`mipmap-anydpi-v26/ic_launcher*.xml`、`drawable-nodpi/splash_logo.png`
