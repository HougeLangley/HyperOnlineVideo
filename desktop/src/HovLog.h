#pragma once
// 统一日志时间前缀（Issue #1 评论请求 ✓ 2026-10-01）：
// 所有 stderr 日志经此输出 "[HH:MM:SS] ..."；qInfo 侧由 main.cpp 的 messageHandler 统一加 ✓
#include <cstdio>
#include <cstdarg>
#include <ctime>

inline void hovLog(const char *fmt, ...) {
    char ts[16];
    const std::time_t t = std::time(nullptr);
    std::strftime(ts, sizeof ts, "%H:%M:%S", std::localtime(&t));
    std::fprintf(stderr, "[%s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
}
