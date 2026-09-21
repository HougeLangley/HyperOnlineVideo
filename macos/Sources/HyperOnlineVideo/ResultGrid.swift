import AppKit

/// 搜索结果卡片：缩略图在上、标题与来源在下（多列自适应 → 窄了自动变单列，就是"瀑布流"）。
/// 说明：卡片里的图片/文字用**鼠标穿透**子类，避免像表格那样把点击吃掉（踩过一次）。
final class ResultCardItem: NSCollectionViewItem {
    static let identifier = NSUserInterfaceItemIdentifier("ResultCard")

    private var thumbView: NSImageView!
    private var titleLabel: NSTextField!
    private var metaLabel: NSTextField!

    override func loadView() {
        let root = NSView()
        root.wantsLayer = true
        root.layer?.cornerRadius = 10
        // 半透明卡片（配合全屏时的毛玻璃面板；普通窗口下也更有层次）
        root.layer?.backgroundColor = ThemeColors.cardBG.cgColor
        root.layer?.borderWidth = 1
        root.layer?.borderColor = ThemeColors.cardBorder.cgColor

        thumbView = PassThroughImageView(frame: .zero)
        thumbView.imageScaling = .scaleProportionallyUpOrDown
        thumbView.wantsLayer = true
        thumbView.layer?.cornerRadius = 8
        thumbView.layer?.masksToBounds = true
        thumbView.layer?.backgroundColor = NSColor.quaternaryLabelColor.cgColor
        root.addSubview(thumbView)

        titleLabel = PassThroughTextField(frame: .zero)
        titleLabel.isEditable = false
        titleLabel.isSelectable = false
        titleLabel.isBordered = false
        titleLabel.isBezeled = false
        titleLabel.drawsBackground = false
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.usesSingleLineMode = false
        titleLabel.maximumNumberOfLines = 2
        root.addSubview(titleLabel)

        metaLabel = PassThroughTextField(frame: .zero)
        metaLabel.isEditable = false
        metaLabel.isSelectable = false
        metaLabel.isBordered = false
        metaLabel.isBezeled = false
        metaLabel.drawsBackground = false
        metaLabel.lineBreakMode = .byTruncatingTail
        metaLabel.textColor = .secondaryLabelColor
        metaLabel.font = .systemFont(ofSize: 11)
        root.addSubview(metaLabel)

        view = root
    }

    /// 卡片底部标签：显示来源（比"视频/音乐"更有信息量）
    static func sourceLabel(_ key: String) -> String {
        if key.contains("youtube.com") { return "YouTube" }
        if key.contains("bilibili.com") { return "B站" }
        if key.hasPrefix("netease:") { return "网易云音乐" }
        if key.hasPrefix("qq:") { return "QQ音乐" }
        if key.hasPrefix("/") { return "本地文件" }
        return "在线"
    }

    /// 内容布局完全用 frame 计算（卡片尺寸由外部布局决定，简单可控）
    func configure(row: Row, thumbSize: CGFloat, image: NSImage?, selected: Bool) {
        view.layer?.backgroundColor = (selected ? ThemeColors.cardSelected : ThemeColors.cardBG).cgColor
        let pad: CGFloat = 8
        let w = view.bounds.width - pad * 2
        let thumbH = thumbSize
        thumbView.frame = NSRect(x: pad, y: view.bounds.height - pad - thumbH, width: w, height: thumbH)
        thumbView.isHidden = row.thumb.isEmpty
        thumbView.image = image

        let metaH: CGFloat = 15
        let titleH: CGFloat = min(36, max(16, thumbSize * 0.30))
        metaLabel.frame = NSRect(x: pad, y: pad, width: w, height: metaH)
        titleLabel.frame = NSRect(x: pad, y: pad + metaH + 4, width: w, height: titleH)
        titleLabel.stringValue = row.text
        titleLabel.font = .systemFont(ofSize: min(14, max(11.5, thumbSize * 0.15)))
        titleLabel.textColor = (row.isLog || row.key.isEmpty) ? .secondaryLabelColor : .labelColor
        if row.thumb.isEmpty {
            // 日志/状态行：没有缩略图，就把文字铺满卡片
            titleLabel.frame = NSRect(x: pad, y: pad + metaH, width: w, height: max(20, view.bounds.height - pad * 2 - metaH))
            metaLabel.stringValue = ""
        } else {
            metaLabel.stringValue = ResultCardItem.sourceLabel(row.key)
        }
    }
}

extension AppDelegate: NSCollectionViewDataSource, NSCollectionViewDelegate {
    func collectionView(_ collectionView: NSCollectionView, numberOfItemsInSection section: Int) -> Int {
        rows.count
    }

    func collectionView(_ collectionView: NSCollectionView, itemForRepresentedObjectAt indexPath: IndexPath) -> NSCollectionViewItem {
        let item = collectionView.makeItem(withIdentifier: ResultCardItem.identifier, for: indexPath)
        guard let card = item as? ResultCardItem, indexPath.item < rows.count else { return item }
        let r = rows[indexPath.item]
        card.configure(row: r, thumbSize: thumbSize(), image: thumbCache[r.thumb],
                       selected: collectionView.selectionIndexPaths.contains(indexPath))
        item.view.toolTip = r.key.isEmpty ? nil : r.text
        return card
    }

    /// 单击即播放（与 Android 的"点按播放"一致）
    func collectionView(_ collectionView: NSCollectionView, didSelectItemsAt indexPaths: Set<IndexPath>) {
        guard let idx = indexPaths.first?.item, idx < rows.count else { return }
        let r = rows[idx]
        guard !r.key.isEmpty else {
            Config.log("[CLICK] 单击卡片 \(idx + 1)（日志行，不播放）")
            setStatus("（日志行）\(r.text)")
            return
        }
        guard playKey != r.key else {           // 双击会触发两次选择：同一行正在播就不重启
            Config.log("[CLICK] 卡片 \(idx + 1) 已在播放，忽略重复点击")
            return
        }
        Config.log("[CLICK] 单击卡片 \(idx + 1) → 播放：\(r.text.prefix(48))")
        playRow(idx)
    }
}

/// 列表/网格容器：让点击能落到集合视图上（子视图已做穿透，这里再兜一层）
final class ResultsGridView: NSCollectionView {
    override func mouseDown(with event: NSEvent) {
        super.mouseDown(with: event)
    }
}
