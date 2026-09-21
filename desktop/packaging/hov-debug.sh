#!/bin/sh
# ── hov-debug：一键诊断启动器（KDE 会话内使用）──────────────────────────────
# 用法：
#   hov-debug                 # 正常启动应用；退出/崩溃后自动收集诊断到一个日志文件
#   hov-debug --last          # 不启动应用，只看最近一次崩溃的关键信息
#   HOV_QPA=xcb hov-debug     # 强制走 X11 后端（用于与 Wayland 对比排查）
#
# 它会自动收集：环境与版本、应用 stderr（含 [PLAY]/[FD]/[MUX]/[SNAPSHOT] 诊断行）、
# mpv 自身日志末尾、journald 里的 glib/fatal 记录、以及 coredump 的 Message 与调用栈。
set -u
REAL=/usr/bin/hov-qt            # 已带 locale/ulimit/QT_NO_GLIB 修正的启动器
LOG="${HOV_DEBUG_LOG:-$HOME/hov-debug-$(date +%Y%m%d-%H%M%S).log}"
MPVLOG=/tmp/hov-mpv.log

collect() {
    pid="${1:-}"
    {
        echo
        echo "================ 诊断汇总 ================"
        echo "---- 环境 ----"
        date
        uname -a
        echo "session=${XDG_SESSION_TYPE:-未知} wayland=${WAYLAND_DISPLAY:-无} desktop=${XDG_CURRENT_DESKTOP:-未知}"
        echo "QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-（默认）} QT_NO_GLIB=${QT_NO_GLIB:-（未设）} LC_NUMERIC=${LC_NUMERIC:-（未设）} LC_ALL=${LC_ALL:-（未设）}"
        echo "fd 上限=$(ulimit -n)  hov-qt 版本=$(pacman -Q hov-qt 2>/dev/null | awk '{print $2}')"
        pacman -Q qt6-base mpv ffmpeg yt-dlp qt6-webengine 2>/dev/null | sed 's/^/  /'
        command -v deno >/dev/null 2>&1 && { printf '  deno '; deno --version 2>/dev/null | head -1; }
        command -v /usr/local/bin/yt-dlp >/dev/null 2>&1 && printf '  yt-dlp(/usr/local) %s\n' "$(/usr/local/bin/yt-dlp --version 2>/dev/null)"
        echo "---- coredump（本次 PID=${pid:-未知}）----"
        if [ -n "$pid" ]; then
            coredumpctl info "$pid" 2>/dev/null | head -70
        fi
        coredumpctl info hov-qt-bin 2>/dev/null | head -25
        echo "---- journald（近 3 小时内的 hov/glib/fatal/assert）----"
        journalctl --user -b --since "-3 hours" 2>/dev/null \
            | grep -iE "hov-qt|glib|fatal|assert|QEventDispatcher|abort" | tail -40
        echo "---- 应用 stderr 末尾 250 行 ----"
        tail -250 "$LOG"
        echo "---- mpv 日志末尾 250 行 ----"
        [ -f "$MPVLOG" ] && tail -250 "$MPVLOG"
        echo "================ 汇总结束（把整个文件发我即可）================"
    } >> "$LOG" 2>&1
}

if [ "${1:-}" = "--last" ]; then
    echo "==== 最近一次 hov-qt-bin coredump ===="
    coredumpctl list hov-qt-bin 2>/dev/null | tail -4
    echo
    coredumpctl info hov-qt-bin 2>/dev/null | head -45
    exit 0
fi

[ -x "$REAL" ] || { echo "找不到 $REAL（请确认 hov-qt 已安装）"; exit 1; }

{
    echo "==== hov-debug 启动 $(date) ===="
    echo "参数: $*"
} > "$LOG"
: > "$MPVLOG" 2>/dev/null || true

echo "════════════════════════════════════════════"
echo " 诊断日志：$LOG"
echo " 现在正常使用应用即可（搜索 / 播放 / 下载 / 登录…）"
echo " 应用退出或崩溃后，本窗口会自动打印诊断汇总。"
echo "════════════════════════════════════════════"

[ -n "${HOV_QPA:-}" ] && { QT_QPA_PLATFORM="$HOV_QPA"; export QT_QPA_PLATFORM; }

"$REAL" "$@" >> "$LOG" 2>&1 &
pid=$!
# 后台采样：每 5 分钟记录 fd 数量 + fd 目标类型直方图（用于定位句柄泄漏源）
(
    while kill -0 "$pid" 2>/dev/null; do
        sleep 300
        kill -0 "$pid" 2>/dev/null || break
        {
            echo "---- fd 采样 $(date +%H:%M:%S) ----"
            echo "  fd=$(ls /proc/$pid/fd 2>/dev/null | wc -l)  线程=$(ls /proc/$pid/task 2>/dev/null | wc -l)  RSS=$(awk '/VmRSS/{print int($2/1024)}' /proc/$pid/status 2>/dev/null)MB"
            ls -l /proc/$pid/fd 2>/dev/null | awk '{print $NF}' \
                | sed 's/[0-9]\+/N/g' | sed 's/\[.*\]//' \
                | sort | uniq -c | sort -rn | head -8 | sed 's/^/    /'
        } >> "$LOG" 2>&1
    done
) &
SAMPLER=$!

wait "$pid"
rc=$?
kill "$SAMPLER" 2>/dev/null
echo "==== 应用退出 rc=$rc（134/137 等表示被信号中止）$(date) ====" >> "$LOG"
sleep 2
collect "$pid"
echo
echo "════════════════════════════════════════════"
echo " 应用已退出 rc=$rc"
echo " 诊断已写入：$LOG"
echo " 把**整个文件**发我即可（或至少发送上面“诊断汇总”那一段）"
echo "════════════════════════════════════════════"
