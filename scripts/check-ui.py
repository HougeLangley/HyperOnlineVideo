#!/usr/bin/env python3
# -*- coding: utf-8 -*-
'''桌面端界面一致性检查（只读）：颜色、尺寸、间距、字号、按钮、图标只能从 token 与组件库来。

用法:
  python3 scripts/check-ui.py            # 有违规就列出并以 1 退出
  python3 scripts/check-ui.py --list     # 只列出各规则的豁免文件

规则与豁免的理由见 design/desktop-design-language.md「检查」一节。
另外按 WCAG 2.x 公式验算 Theme.cpp 两套色板的关键配对（文字 4.5:1，图形与边界 3:1）。
'''
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'desktop', 'src')

# 豁免：token 与组件库本身；画在视频画面上的内容（字幕、歌词、封面卡、压暗层）不跟随界面主题；自测断言里的数值
TOKEN_FILES = {'Tokens.h', 'Theme.cpp', 'Ui.cpp', 'UiGallery.cpp', 'Icons.cpp', 'IconIds.h'}
VIDEO_SURFACE = {'SubtitleOverlay.cpp', 'CoverArt.cpp', 'ColorTheme.cpp', 'ColorTheme.h', 'MpvWidget.cpp', 'Lyrics.cpp'}
TESTS = {'SelfTest.cpp'}

RULES = [
    # (编号, 说明, 正则, 豁免文件集合)
    ('color', '颜色字面量：用 Theme::color(Tok::…)',
     re.compile(r'#[0-9A-Fa-f]{6}\b|QColor\(\s*\d|QColor\(\s*"|rgba?\('), TOKEN_FILES | VIDEO_SURFACE | TESTS),
    ('stylesheet', '控件级 setStyleSheet：外观写进 Theme.cpp 的 QSS，按 hov 属性匹配',
     re.compile(r'\bsetStyleSheet\('), TOKEN_FILES),
    ('button', '直接 new QPushButton：用 Ui::filledButton / tonalButton / textButton / iconButton / chip',
     re.compile(r'\bnew\s+QPushButton\b'), TOKEN_FILES),
    ('size', '写死的控件尺寸：用 M::controlH 等 token',
     re.compile(r'\bsetFixed(?:Size|Height|Width)\(\s*\d'), TOKEN_FILES | VIDEO_SURFACE),
    ('spacing', '写死的间距：用 M::space1…space6（0 允许）',
     re.compile(r'\bset(?:Spacing|HorizontalSpacing|VerticalSpacing)\(\s*[1-9]|\bsetContentsMargins\([^)]*(?<![\w:.])[1-9]'),
     TOKEN_FILES),
    ('font', '写死的字号：用 M::fontTitle / fontBody / fontCaption',
     re.compile(r'\bset(?:PixelSize|PointSize|PointSizeF)\(\s*\d'), TOKEN_FILES | VIDEO_SURFACE),
    ('glyph', '字符当图标（媒体控制符、emoji）：用 Icons / IconId',
     re.compile(r'[⏩-⏺▶◀⬆⬇\U0001F300-\U0001FAFF]|\\u23[eEfF][0-9a-fA-F]|\\u25[bBcC][0-9a-fA-F]|\\U0001[fF]'),
     TOKEN_FILES | TESTS),
]


def code_only(text):
    '''去掉 // 与 /* */ 注释，保留字符串字面量与行号。'''
    out, i, n = [], 0, len(text)
    state = None  # None / 'str' / 'chr' / 'line' / 'block' / 'raw'
    raw_end = ''
    while i < n:
        c = text[i]
        if state is None:
            if text.startswith('R"', i):
                j = text.index('(', i)
                raw_end = ')' + text[i + 2:j] + '"'
                out.append(text[i:j + 1]); i = j + 1; state = 'raw'; continue
            if text.startswith('//', i):
                state = 'line'; i += 2; continue
            if text.startswith('/*', i):
                state = 'block'; i += 2; continue
            if c == '"':
                state = 'str'
            elif c == "'":
                state = 'chr'
            out.append(c)
        elif state == 'line':
            if c == '\n':
                state = None; out.append(c)
        elif state == 'block':
            if text.startswith('*/', i):
                state = None; i += 2; continue
            if c == '\n':
                out.append(c)
        elif state == 'raw':
            if text.startswith(raw_end, i):
                out.append(raw_end); i += len(raw_end); state = None; continue
            out.append(c)
        else:  # str / chr
            out.append(c)
            if c == '\\' and i + 1 < n:
                out.append(text[i + 1]); i += 2; continue
            if (state == 'str' and c == '"') or (state == 'chr' and c == "'") or c == '\n':
                state = None
        i += 1
    return ''.join(out)


# (前景, 背景, 最低对比度)：文字配对 4.5，图形/控件边界 3.0
CONTRAST_PAIRS = [
    ('OnSurface', 'Surface', 4.5), ('OnSurface', 'SurfaceHighest', 4.5),
    ('OnSurfaceVariant', 'SurfaceLow', 4.5), ('OnSurfaceVariant', 'SurfaceContainer', 4.5),
    ('OnSurfaceVariant', 'SurfaceHighest', 4.5),
    ('Primary', 'SurfaceLow', 4.5),           # 文字按钮、下载中状态文字
    ('OnPrimary', 'Primary', 4.5), ('OnPrimaryContainer', 'PrimaryContainer', 4.5),
    ('Error', 'SurfaceLow', 4.5), ('Success', 'SurfaceLow', 4.5),
    ('OnScrim', 'Scrim', 4.5),
    ('Outline', 'SurfaceContainer', 3.0), ('Primary', 'SurfaceHighest', 3.0),   # 悬停边框；滑块、进度条
]


def luminance(hexcolor):
    rgb = [int(hexcolor[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    rgb = [c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4 for c in rgb]
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]


def contrast_hits():
    with open(os.path.join(SRC, 'Tokens.h'), encoding='utf-8') as f:
        body = re.search(r'enum class Tok \{(.*?)\};', f.read(), re.S).group(1)
    names = [n.strip() for n in re.sub(r'//[^\n]*', '', body).split(',') if n.strip() and n.strip() != 'Count']
    with open(os.path.join(SRC, 'Theme.cpp'), encoding='utf-8') as f:
        theme = f.read()
    hits = []
    for mode in ('kDark', 'kLight'):
        block = re.search(r'constexpr const char \*%s\[\] = \{(.*?)\};' % mode, theme, re.S).group(1)
        values = re.findall(r'"(#[0-9A-Fa-f]{6})"', block)
        if len(values) != len(names):
            hits.append('%s 有 %d 个颜色，Tok 有 %d 个' % (mode, len(values), len(names)))
            continue
        pal = dict(zip(names, values))
        for fg, bg, need in CONTRAST_PAIRS:
            a, b = luminance(pal[fg]), luminance(pal[bg])
            ratio = (max(a, b) + 0.05) / (min(a, b) + 0.05)
            if ratio < need:
                hits.append('%s: %s %s / %s %s = %.2f < %.1f' % (mode, fg, pal[fg], bg, pal[bg], ratio, need))
    return hits


def main():
    if '--list' in sys.argv:
        for key, desc, _, exempt in RULES:
            print('%-10s %s\n           豁免：%s' % (key, desc, ', '.join(sorted(exempt))))
        return 0
    hits = []
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(('.cpp', '.h')):
            continue
        with open(os.path.join(SRC, name), encoding='utf-8') as f:
            lines = code_only(f.read()).split('\n')
        for key, desc, rx, exempt in RULES:
            if name in exempt:
                continue
            for no, line in enumerate(lines, 1):
                if rx.search(line):
                    hits.append((key, desc, name, no, line.strip()))
    contrast = contrast_hits() if os.path.exists(os.path.join(SRC, 'Tokens.h')) else ['缺少 desktop/src/Tokens.h']
    for key, desc, name, no, line in hits:
        print('%s:%d: [%s] %s\n    %s' % (os.path.join('desktop/src', name), no, key, desc, line[:120]))
    for c in contrast:
        print('desktop/src/Theme.cpp: [contrast] %s' % c)
    total = len(hits) + len(contrast)
    print('check-ui: %d 处违规' % total)
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main())
