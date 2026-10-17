# 图标字体

桌面端的全部图标来自 Material Symbols Rounded，编进程序的是两份子集：

| 文件 | 来源轴值 | 用途 |
|---|---|---|
| `HovSymbolsOutlined.ttf`（族名 `HOV Symbols Outlined`） | FILL=0 GRAD=0 opsz=24 wght=400 | 默认 |
| `HovSymbolsFilled.ttf`（族名 `HOV Symbols Filled`） | FILL=1 GRAD=0 opsz=24 wght=400 | 选中、开启状态 |

- 上游：<https://github.com/google/material-design-icons>，提交 `49d4db35df873165d6bd6ba09b063c7dafbac2f4`
- 源文件：`variablefont/MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf`（sha256 `32e4011709e055596eb8d2dd0f7cb547b47c1d40ac9ecb8fad54f7f7eac202e7`）与同名 `.codepoints`（sha256 `0c9dae3ed678a81549c3be046bc8045335e5f57f2cbe2287360fbf0c2cc1355f`）
- 许可：Apache-2.0，全文见 `LICENSE-material-symbols.txt`。子集化与改族名属于修改，按许可第 4 条在此说明。

## 增删图标

1. 在 `icons.txt` 里加或删一行名称（名称查 <https://fonts.google.com/icons>，选 Rounded）。
2. 下载上面两个源文件到一个空目录，执行：

   ```bash
   python3 -m venv /tmp/fonttools && /tmp/fonttools/bin/pip install fonttools==4.60.1
   /tmp/fonttools/bin/python desktop/tools/gen-icons.py <源 .ttf> <源 .codepoints>
   ```

3. 脚本重写两份字体和 `desktop/src/IconIds.h`；名称不存在时以非零状态退出。
4. 编译后执行 `hov-qt --ui-gallery`，新图标出现在陈列页底部的登记表里。
