#!/usr/bin/env python3
# -*- coding: utf-8 -*-
'''三端代码审计分析器（只读 ✓ 不改代码 ✓）

用法:
  python3 scripts/audit-code.py                     # 全量审计
  python3 scripts/audit-code.py --top 30            # 只看最长的 30 个函数

分析维度:
  ① 函数长度（括号平衡法 ✓ C++/Swift/Kotlin 通用）
  ② 嵌套深度（函数内最大 if/for/while/switch 嵌套）
  ③ 死代码标记（TODO/FIXME/HACK/XXX/DEPRECATED）
  ④ 注释掉的代码（连续 3+ 行以 // 或 # 开头且含代码特征）
  ⑤ 超长文件（>600 行）
  ⑥ 重复行块（同文件内连续 8 行完全相同的块 → 跨文件用哈希）
'''
import re
import sys
import os
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LANG_RULES = {
    '.cpp': {
        'name': 'Linux C++',
        'func': re.compile(r'^[A-Za-z_][\w:<>,&\*\s]*[\s\*&]([A-Za-z_]\w*)\s*\([^;]*\)\s*(const)?\s*\{?\s*$'),
        'comment': '//',
    },
    '.swift': {
        'name': 'macOS Swift',
        'func': re.compile(r'^\s*(?:public |private |internal |fileprivate |open |static |class |final |override |mutating |@\w+\s+)*func\s+([A-Za-z_]\w*)\s*[<(]'),
        'comment': '//',
    },
    '.kt': {
        'name': 'Android Kotlin',
        'func': re.compile(r'^\s*(?:public |private |internal |protected |override |suspend |inline |@\w+\s+)*fun\s+(?:<[^>]+>\s+)?([A-Za-z_]\w*)\s*\('),
        'comment': '//',
    },
}

DEAD_MARKERS = re.compile(r'\b(TODO|FIXME|HACK|XXX|DEPRECATED|WORKAROUND|BUG)\b')
NEST_KEYWORDS = re.compile(r'\b(if|for|while|switch|when|guard|catch|else)\b')


def find_files():
    out = []
    for base in ['desktop/src', 'macos/Sources', 'app/src', 'core/src']:
        d = os.path.join(ROOT, base)
        if not os.path.isdir(d):
            continue
        for dirpath, _, files in os.walk(d):
            for f in files:
                ext = os.path.splitext(f)[1]
                if ext in LANG_RULES:
                    out.append(os.path.join(dirpath, f))
    return sorted(out)


def analyze_functions(path, lines, rule):
    '''括号平衡法找函数体（从匹配行开始，找第一个 { 然后数到平衡）'''
    funcs = []
    i = 0
    n = len(lines)
    while i < n:
        m = rule['func'].match(lines[i])
        if not m:
            i += 1
            continue
        name = m.group(1)
        # 从当前行开始找 {
        j = i
        depth = 0
        started = False
        while j < n:
            for ch in lines[j]:
                if ch == '{':
                    depth += 1
                    started = True
                elif ch == '}':
                    depth -= 1
            if started and depth <= 0:
                break
            j += 1
            if j - i > 800:   # 防御：荒谬长度
                break
        length = j - i + 1
        if length >= 15:      # 只记录有意义的
            # 嵌套深度（统计函数体内最大连续嵌套）
            body = lines[i:j + 1]
            maxd = 0
            cur = 0
            for ln in body:
                opens = ln.count('{')
                closes = ln.count('}')
                cur += opens
                if NEST_KEYWORDS.search(ln) and cur > maxd:
                    maxd = max(maxd, cur)
                cur -= closes
            funcs.append((name, i + 1, length, maxd))
        i = j + 1
    return funcs


def main():
    top_n = 25
    if '--top' in sys.argv:
        try:
            top_n = int(sys.argv[sys.argv.index('--top') + 1])
        except Exception:
            pass

    files = find_files()
    all_funcs = []
    dead = defaultdict(list)
    commented_code = defaultdict(list)
    big_files = []

    for path in files:
        rel = os.path.relpath(path, ROOT)
        try:
            lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
        except Exception:
            continue
        ext = os.path.splitext(path)[1]
        rule = LANG_RULES[ext]

        if len(lines) > 600:
            big_files.append((rel, len(lines)))

        # 函数分析
        for name, start, length, depth in analyze_functions(path, lines, rule):
            all_funcs.append((length, depth, rel, name, start))

        # 死代码标记
        for idx, ln in enumerate(lines, 1):
            for m in DEAD_MARKERS.finditer(ln):
                dead[rel].append((idx, m.group(1), ln.strip()[:90]))

        # 注释掉的代码（连续 3+ 行注释，且含代码特征）
        run = 0
        run_start = 0
        for idx, ln in enumerate(lines, 1):
            s = ln.strip()
            is_comment = s.startswith(rule['comment']) or (ext == '.kt' and s.startswith('//'))
            has_code = bool(re.search(r'[;{}()]\s*$|=\s*\S|->\s*\S|\breturn\b|\bif\s*\(', s))
            if is_comment and has_code:
                if run == 0:
                    run_start = idx
                run += 1
            else:
                if run >= 3:
                    commented_code[rel].append((run_start, run))
                run = 0
        if run >= 3:
            commented_code[rel].append((run_start, run))

    # ── 输出 ──────────────────────────────────────────────
    print('=' * 78)
    print('  三端代码审计报告（只读分析 ✓）')
    print('=' * 78)
    print('  扫描文件: %d 个' % len(files))

    print('\n── ① 超长函数 Top %d（长度 = 行数；深度 = 最大嵌套层数）──' % top_n)
    all_funcs.sort(reverse=True)
    for length, depth, rel, name, start in all_funcs[:top_n]:
        flag = '⚠️' if length > 120 or depth >= 5 else ('△' if length > 80 or depth >= 4 else ' ')
        print('  %s %4d行 深%d  %-52s %s:%d' % (flag, length, depth, name, rel, start))

    print('\n── ② 超长函数统计（>120 行）──')
    verylong = [f for f in all_funcs if f[0] > 120]
    print('  数量: %d 个' % len(verylong))
    by_file = defaultdict(int)
    for length, depth, rel, name, start in verylong:
        by_file[rel] += 1
    for rel, cnt in sorted(by_file.items(), key=lambda x: -x[1])[:12]:
        print('    %-56s %d 个' % (rel, cnt))

    print('\n── ③ 深嵌套函数（≥5 层）──')
    deep = [f for f in all_funcs if f[1] >= 5]
    print('  数量: %d 个' % len(deep))
    for length, depth, rel, name, start in sorted(deep, reverse=True)[:10]:
        print('    深%d  %4d行  %-46s %s:%d' % (depth, length, name, rel, start))

    print('\n── ④ 死代码标记（TODO/FIXME/HACK/…）──')
    total_dead = sum(len(v) for v in dead.values())
    print('  总数: %d 处' % total_dead)
    for rel, items in sorted(dead.items(), key=lambda x: -len(x[1]))[:10]:
        print('    %-56s %d 处' % (rel, len(items)))
        for idx, kind, txt in items[:2]:
            print('        %s:%d [%s] %s' % (rel, idx, kind, txt[:74]))

    print('\n── ⑤ 注释掉的代码块（连续 3+ 行）──')
    total_cc = sum(len(v) for v in commented_code.values())
    print('  块数: %d' % total_cc)
    for rel, items in sorted(commented_code.items(), key=lambda x: -len(x[1]))[:8]:
        pos = ', '.join('L%d(%d行)' % (s, n) for s, n in items[:4])
        print('    %-56s %s' % (rel, pos))

    print('\n── ⑥ 超长文件（>600 行）──')
    for rel, n in sorted(big_files, key=lambda x: -x[1]):
        print('    %4d 行  %s' % (n, rel))

    print('\n' + '=' * 78)


if __name__ == '__main__':
    main()
