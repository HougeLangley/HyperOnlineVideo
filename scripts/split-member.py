#!/usr/bin/env python3
"""split-member.py —— 把类内联成员函数体搬到独立 .cpp（文档 52/53/54 手法 ✓）

用法：python3 scripts/split-member.py <类头.h> <输出.cpp> <类名> <函数名...> [--dry-run] [--append]

要点（每条都有断言 ✓ 不静默 ✗）：
  ① 花括号**配对**定位函数体 ✓ 不靠正则猜 ✗
  ② 头里**整段替换** `h[s:e+1] = [声明行]` ✓ 且**从下往上** ✓（保持上方行号有效 ✓）
  ③ .cpp 里写 `ret Class::name(args) { 体 }` ✓（逐字搬运 ✓ 只加类名前缀 ✓）
  ④ `= 默认值` **只在定义处剥离** ✓（声明处保留 ✓）—— 单测见 tool 自测 ✓
  ⑤ `--dry-run` 只汇报不写 ✓；`--append` 追加到已有 .cpp（不覆盖 ✓）
"""
import re, sys

BS = chr(92)                      # 反斜杠（拼出来 ✓ 免转义地狱 ✗）
QUOTES = chr(34) + chr(39)        # " 与 '


def strip_defaults(paren):
    """剥离参数表里的 `= 表达式`（只在**定义处**用 ✓）
    '(bool a = false, const QString &s = QString())' → '(bool a, const QString &s)' ✓"""
    assert paren.startswith('(') and paren.endswith(')'), paren
    inner = paren[1:-1]
    parts, cur, depth, q, esc = [], [], 0, None, False
    for c in inner:
        if q:
            cur.append(c)
            if esc: esc = False
            elif c == BS: esc = True
            elif c == q: q = None
        elif c in QUOTES:
            q = c; cur.append(c)
        elif c in '([{':
            depth += 1; cur.append(c)
        elif c in ')]}':
            depth -= 1; cur.append(c)
        elif c == ',' and depth == 0:
            parts.append(''.join(cur)); cur = []
        else:
            cur.append(c)
    if cur:
        parts.append(''.join(cur))
    cleaned = []
    for par in parts:
        d, q2, esc2, cut = 0, None, False, None
        for k, ch in enumerate(par):
            if q2:
                if esc2: esc2 = False
                elif ch == BS: esc2 = True
                elif ch == q2: q2 = None
            elif ch in QUOTES:
                q2 = ch
            elif ch in '([{': d += 1
            elif ch in ')]}': d -= 1
            elif ch == '=' and d == 0:
                cut = k; break
        cleaned.append((par[:cut] if cut is not None else par).strip())
    return '(' + ', '.join(cleaned) + ')'


def selftest():
    cases = [('(bool a = false)', '(bool a)'),
             ('(const QString &k, int n = 1)', '(const QString &k, int n)'),
             ('(int a, int b)', '(int a, int b)'),
             ('(const char *s = "a,b=c")', '(const char *s)'),
             ('(const QString &f = QString())', '(const QString &f)')]
    bad = [(i, strip_defaults(i), w) for i, w in cases if strip_defaults(i) != w]
    for i, g, w in bad:
        print(f"  ✗ 单测失败: {i} → {g}（期望 {w}）")
    return not bad


def main():
    argv = sys.argv[1:]
    dry = '--dry-run' in argv
    append = '--append' in argv
    a = [x for x in argv if not x.startswith('--')]
    if len(a) < 4:
        print(__doc__); return 2
    header, out, cls, names = a[0], a[1], a[2], a[3:]
    if not dry and not selftest():
        print("  ✗ 工具自测未过 → 拒绝运行 ✗"); return 1
    lines = open(header, encoding='utf-8').read().split('\n')
    found, one, ctor = {}, {}, {}
    for i, l in enumerate(lines):
        m1 = re.match(r'^    ([\w:<>,\s\*&]+?)\s+(\w+)\s*(\(.*\))\s*\{(.*)\}\s*$', l)
        if m1 and m1.group(2) in names and l.count('{') == l.count('}') == 1:
            one[m1.group(2)] = (i, m1.group(1).strip(), m1.group(3), m1.group(4)); continue
        # 构造/析构：`Name(args) [ : init ] {` 或 `~Name(args) {`（无返回类型 ✓）
        mc = re.match(r'^    (~?\w+)\s*(\(.*?\))\s*(?::\s*(.*?))?\s*\{\s*$', l)
        if mc and mc.group(1) in names:
            d, e = 0, None
            for j in range(i, len(lines)):
                d += lines[j].count('{') - lines[j].count('}')
                if d == 0 and j > i:
                    e = j; break
            ctor[mc.group(1)] = (i, e, mc.group(1), mc.group(2))
            continue
        m = re.match(r'^    ([\w:<>,\s\*&]+?)\s+(\w+)\s*\(', l)
        if not m or m.group(2) not in names or not l.rstrip().endswith('{'):
            continue
        d, e = 0, None
        for j in range(i, len(lines)):
            d += lines[j].count('{') - lines[j].count('}')
            if d == 0 and j > i:
                e = j; break
        if e is None:
            print(f"  ✗ [{m.group(2)}] 花括号未配对 ✗"); return 1
        found[m.group(2)] = (i, e, m.group(1).strip())
    # ⚠️ 模板护栏（文档 56 ✓）：模板成员的定义**必须留在头文件** ✗ 否则实例化点找不到 ✓
    tmpl = []
    for i, l in enumerate(lines):
        if re.search(r'^\s*template\s*<', l):
            nm = i + 1
            while nm < len(lines) and not re.search(r'\b(\w+)\s*\(', lines[nm]):
                nm += 1
            if nm < len(lines):
                mm2 = re.search(r'\b(\w+)\s*\(', lines[nm])
                if mm2 and mm2.group(1) in names:
                    tmpl.append(mm2.group(1))
    if tmpl:
        print(f"  ✗ 拒绝搬运模板成员（必须留在头文件 ✗）: {tmpl}")
        return 1
    miss = [n for n in names if n not in found and n not in one and n not in ctor]
    if miss:
        print(f"  ⚠ 未找到（跳过 ✓）: {miss}")
    if not found and not one and not ctor:
        print("  ✗ 一个都没找到 ✗"); return 1
    parts, decls = [], []
    for name, (i, e, nm, paren) in sorted(ctor.items(), key=lambda kv: kv[1][0]):
        init = lines[i].rstrip()[:-1]                     # 去掉行尾 { ✓ 保留 `: 初始化列表` 原文 ✓
        init = init[init.index(nm) + len(nm) + len(paren):].strip()   # 截出 `: ...` 部分 ✓
        head = f"MainWindow::{name}{paren}" + (f" {init}" if init else "") + " {"
        parts.append(head + "\n" + '\n'.join(lines[i+1:e]) + "\n}")
        decls.append((i, e, "    " + lines[i].strip()[:-1].strip() + ";"))
        print(f"    {name:30s} 行 {i+1:5d}-{e+1:<5d} ({e-i+1:4d} 行) [构造/析构 ✓ 初始化列表: {init or '无'}]")
    for name, (i, e, ret) in sorted(found.items(), key=lambda kv: kv[1][0]):
        sig = lines[i].strip()[:-1].strip()
        paren = sig[sig.index('('):]
        paren_def = strip_defaults(paren) if '=' in paren else paren
        assert '=' not in paren_def, f"默认值未剥离: {paren_def}"
        ret_def = re.sub(r"^static\s+", "", ret)          # static 成员：定义处不能重复 static ✗
        paren_def = re.sub(r"\s+\b(override|final)\b", "", paren_def)   # override/final 只允许类内 ✗（const 必须保留 ✓）
        parts.append(f"{ret_def} {cls}::{name}{paren_def} {{\n" + '\n'.join(lines[i+1:e]) + "\n}")
        decls.append((i, e, f"    {ret} {name}{paren};"))
        print(f"    {name:30s} 行 {i+1:5d}-{e+1:<5d} ({e-i+1:4d} 行)")
    for name, (i, ret, paren, body) in sorted(one.items(), key=lambda kv: -kv[1][0]):
        paren_def = strip_defaults(paren) if '=' in paren else paren
        assert '=' not in paren_def, f"默认值未剥离: {paren_def}"
        ret_def = re.sub(r"^static\s+", "", ret)
        paren_def = re.sub(r"\s+\b(override|final)\b", "", paren_def)
        parts.append(f"{ret_def} {cls}::{name}{paren_def} {{{body}}}")
        decls.append((i, i, f"    {ret} {name}{paren};"))
        print(f"    {name:30s} 行 {i+1:5d} (单行 ✓)")
    print(f"  ✓ 定位 {len(parts)}/{len(names)}")
    if dry:
        print("  （--dry-run ✓ 未写文件 ✓）"); return 0
    for _, _, d in decls:
        assert d.rstrip().endswith(';'), f'声明行缺分号: {d}'
    text = '\n\n'.join(parts) + '\n'
    # ① 先写 .cpp ✓（万一半损，至少定义存在 ✓ 不会出现"声明无定义"✗ 的顺序）
    if append and __import__('os').path.exists(out):
        open(out, 'a', encoding='utf-8').write('\n' + text)
    else:
        open(out, 'w', encoding='utf-8').write(
            f"// ── {out.split('/')[-1]}：从 {header.split('/')[-1]} 搬出的成员实现（文档 52/53/54 ✓ 零行为改动 ✓）──\n"
            f"// 搬运清单: {' '.join(sorted(list(found) + list(one)))}\n"
            f'#include "{header.split("/")[-1]}"\n\n' + text)
    # ② 再改头文件 ✓（本轮教训 ✓ 见文档 55 §三）
    for i, e, decl in sorted(decls, key=lambda x: -x[0]):
        lines[i:e+1] = [decl]
    open(header, 'w', encoding='utf-8').write('\n'.join(lines))
    print(f"  ✓ {out}（{len(open(out, encoding='utf-8').read().splitlines())} 行）· {header} → {len(lines)} 行")
    return 0


if __name__ == '__main__':
    sys.exit(main())
