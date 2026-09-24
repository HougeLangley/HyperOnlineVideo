# AUR 发布说明（Arch User Repository）

> 2026-09-24 调研 + **全流程预演通过** ✓（真实构建 ✓ 发布待 tag 就绪 ✓）

## 一、需要的信息（全部已具备 ✓）

| 项 | 值 | 状态 |
|---|---|---|
| AUR 账号 | `hougelangley` | ✓ 已注册（SSH 验证：`Welcome to AUR, hougelangley!`）|
| SSH 密钥 | 本机 `~/.ssh/id_rsa` | ✓ 已被 AUR 接受（`Server accepts key`）|
| 包名 | `hov-qt` | ✓ 未被占用（RPC `resultcount=0` + 网页 404 ✓）|
| 维护者邮箱 | `hougelangley1987@gmail.com` | ✓（用于 git commit 身份 ✓）|
| AUR 网页密码 | （已提供 ✓ 仅网页登录用 ✗ git 推送不需要）| ○ 本次未使用 |
| 40-hex `B8DD64D5…` | 用途待确认 | ⚠️ 与本机两把密钥、提供的公钥 SHA1 **均不符** ✗ — 不阻塞发布 ✓ |

## 二、发布机制（AUR 官方规范 ✓）

AUR 是 **Git 仓库托管** ✗ 不是文件上传 ✗：

```
ssh://aur@aur.archlinux.org/hov-qt.git    ← 推送地址（首次推送即创建包 ✓）
```

**必需文件**（放仓库根，只有两个 ✓）：
1. `PKGBUILD` —— 构建配方（`source=` 必须是**公开可下载**的 URL ✗ 不能是本地文件 ✗ 不能用 SKIP 校验 ✗）
2. `.SRCINFO` —— 元数据（必须由 `makepkg --printsrcinfo` 生成 ✗ 手写易错 ✗）

**校验规则**（AUR 会拒绝 ✗）：
- `sha256sums` 必须真实（stable 源禁用 `SKIP` ✗）
- `pkgname` 全小写 ✓ 不能与已有包重名 ✓
- `pkgdesc` 建议 ≤ 80 字符英文 ✓（本包 66 ✓）
- 许可证必须与仓库一致（本包 GPL-3.0-only ✓）

## 三、本仓库的发布流程（已脚本化 ✓）

```bash
# 演练（默认 ✓ 不推送 ✗ 下载+算sha256+生成.SRCINFO+本地commit）
bash scripts/publish-aur.sh

# 正式发布（推送 ✓ 这是发布动作 ✗ 由用户决定后执行 ✓）
bash scripts/publish-aur.sh --push
```

脚本做的事（全自动 ✓）：
1. 检查 GitHub `v<pkgver>` tag 存在（不存在 → 友好退出 ✓ 提示等 tag ✓）
2. 下载 tag archive → 计算 sha256 → 写入 PKGBUILD
3. `makepkg --printsrcinfo` 生成 .SRCINFO（本机无 makepkg → 自动走 Arch VM `HOV_ARCH_VM` ✓）
4. 克隆 AUR 仓库 → 复制两文件 → commit（显式 git 身份 ✓）
5. `--push` 时才 `git push` ✓

## 四、唯一阻塞：GitHub tag

**AUR 的 source 必须公开可下载** ✗ 当前 GitHub 远端只有 `v1.0.0 / v1.1.0 / v1.2.0` ✗
（本地工作树是 1.2.1 ✓ 但未推送 ✗ —— 推送时机由项目维护者决定 ✓）

**tag 就绪后一条命令即可发布** ✓：
```bash
bash scripts/publish-aur.sh --push      # 约 2 分钟（下载+sha256+.SRCINFO+推送 ✓）
```

## 五、预演证据（2026-09-24 ✓ 用 v1.2.0 演练全链路 ✓）

| 步骤 | 结果 |
|---|---|
| GitHub tag archive 下载 | ✓ 9.2 MB / sha256 `7a39f45e…`（与后续正式版同法 ✓）|
| `makepkg` 完整构建 | ✓ `Finished making: hov-qt 1.2.0-1` |
| 包内容 | ✓ bin×2 / desktop / man×2 / 图标×2 / LICENSE |
| 启动器修复 | ✓ `LC_NUMERIC`×5 |
| `.SRCINFO` 生成 | ✓ 格式正确（pkgbase/pkgdesc/arch×2/depends… ✓）|

> 预演在 Arch VM（`192.168.64.9`）上进行 ✓ 未对 AUR 做任何写入 ✗（无发布动作 ✓）

## 六、发布后的维护

| 场景 | 操作 |
|---|---|
| 新版本 | 更新 `PKGBUILD` 的 `pkgver` → 跑 `scripts/publish-aur.sh --push`（自动重算全部 ✓）|
| 依赖/构建修正（不换版本） | `pkgrel` +1 → 重跑脚本 ✓ |
| 撤包 | AUR 网页 → 删除；或 `ssh aur@aur.archlinux.org disown hov-qt`（放弃维护权 ✓）|
| 用户报告 | AUR 包页面评论区 ✓ |

**AUR 包页面**（发布前 404 ✓）：`https://aur.archlinux.org/packages/hov-qt`
**用户安装**（发布后 ✓）：`yay -S hov-qt` 或 `paru -S hov-qt`
