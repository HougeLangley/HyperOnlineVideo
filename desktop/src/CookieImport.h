#pragma once
#include <QHash>
#include <QString>

/**
 * 从系统浏览器导入 cookie（A0：跨端统一登录方案，不依赖内置 WebView）
 *
 * 原理：yt-dlp 的 `--cookies FILE` 会**读入并把 cookie jar 写回**该文件，
 * 与 `--cookies-from-browser <浏览器>` 组合时，就把浏览器里的 cookie 落成了 Netscape 文件
 * （我们的音乐 API 与 yt-dlp 都吃这个格式）。
 *
 * 本类只做纯逻辑（解析/按站点切分/映射），真正跑 yt-dlp 的放在 importFromBrowser()。
 */
class CookieImport {
public:
    /** 域名 → 站点（youtube / bilibili / netease / qqmusic；不认识的返回空） */
    static QString siteOfDomain(const QString &domain);

    /** 把合并的 Netscape 文件内容按站点切分；返回 站点 → 该站点的 Netscape 文本（含标准文件头） */
    static QHash<QString, QString> splitBySite(const QString &combined);

    /** 跑 yt-dlp 抓一次（用浏览器 cookie），把合并 jar 落到临时文件后按站点切分写入 cookies/ 目录。
     *  summary 回填人类可读的结果说明（含各站点条数）。 */
    static bool importFromBrowser(const QString &browser, const QString &probeUrl, QString *summary);

    /** 解析结果：站点 → 条数（便于自检与日志） */
    static QHash<QString, int> countsOf(const QHash<QString, QString> &bySsite);
};
