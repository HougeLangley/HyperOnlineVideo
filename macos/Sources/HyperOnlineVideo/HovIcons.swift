import AppKit
import CoreText

/// Material Symbols glyphs from HovSymbols*.ttf (bundled into the app).
/// Codepoints match desktop/src/IconIds.h so both platforms draw the same icons.
enum HovIcons {
    /// Register the bundled Material Symbols fonts explicitly (ATSApplicationFontsPath is not
    /// always enough, and the PostScript names are case sensitive).
    static func registerFonts() {
        for name in ["HovSymbolsOutlined", "HovSymbolsFilled"] {
            if let url = Bundle.main.url(forResource: name, withExtension: "ttf") {
                CTFontManagerRegisterFontsForURL(url as CFURL, .process, nil)
            }
        }
    }

    static func font(size: CGFloat) -> NSFont {
        NSFont(name: "HOVSymbolsOutlined", size: size) ?? .systemFont(ofSize: size)
    }

    static func glyph(_ cp: UInt16) -> String {
        guard let scalar = UnicodeScalar(UInt32(cp)) else { return "" }
        return String(Character(scalar))
    }

    /// Template image so AppKit tints it with the control's content tint.
    static func image(_ cp: UInt16, size: CGFloat = 18) -> NSImage {
        let side = size + 6
        let img = NSImage(size: NSSize(width: side, height: side))
        img.lockFocus()
        let attrs: [NSAttributedString.Key: Any] = [
            .font: font(size: size),
            .foregroundColor: NSColor.black,
        ]
        let text = NSAttributedString(string: glyph(cp), attributes: attrs)
        let bounds = text.size()
        text.draw(at: NSPoint(x: (side - bounds.width) / 2, y: (side - bounds.height) / 2))
        img.unlockFocus()
        img.isTemplate = true
        return img
    }
}

/// Opaque surface background for the main window (replaces the full-window glass).
final class SurfaceBackgroundView: NSView {
    override var isOpaque: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        ThemeColors.color(.surface).setFill()
        dirtyRect.fill()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        needsDisplay = true
    }
}
