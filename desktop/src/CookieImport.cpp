#include "CookieImport.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

namespace {
const char *kNetscapeHeader = "# Netscape HTTP Cookie File";
const char *kComment = "# 由 Hyper Online Video 从系统浏览器导入";

}   // namespace

QString CookieImport::siteOfDomain(const QString &domainIn) {
    const QString d = domainIn.startsWith('.') ? domainIn.mid(1) : domainIn;
    const QString low = d.toLower();
    if (low.endsWith("youtube.com") || low.endsWith("google.com") || low.endsWith("googlevideo.com")
        || low.endsWith("youtu.be") || low.endsWith("googleapis.com") || low.endsWith("gstatic.com")) {
        return "youtube";
    }
    if (low.endsWith("bilibili.com") || low.endsWith("hdslb.com") || low.endsWith("b23.tv")
        || low.endsWith("bilivideo.com")) {
        return "bilibili";
    }
    if (low.endsWith("163.com") || low.endsWith("126.com") || low.endsWith("126.net")
        || low.endsWith("music.126.net")) {
        return "netease";
    }
    if (low.endsWith("qq.com") || low.endsWith("gtimg.cn") || low.endsWith("qqmusic.qq.com")
        || low.endsWith("y.qq.com")) {
        return "qqmusic";
    }
    return QString();
}

QHash<QString, QString> CookieImport::splitBySite(const QString &combined) {
    QHash<QString, QList<QString>> buckets;
    const QStringList lines = combined.split(QRegularExpression("\r?\n"));
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty() || line.startsWith('#')) continue;
        const QStringList f = line.split('\t');
        if (f.size() < 7) continue;                       // 不是合法的 Netscape 行
        const QString site = siteOfDomain(f.at(0));
        if (site.isEmpty()) continue;                     // 与本应用无关的域名直接丢弃（隐私）
        const QString domain = f.at(0);
        // Netscape 第 2 列必须与域名是否带前导点一致（yt-dlp 否则报 invalid format）
        const QString includeSub = domain.startsWith('.') ? "TRUE" : "FALSE";
        const QString rebuilt = QStringList{ domain, includeSub, f.at(2), f.at(3), f.at(4), f.at(5), f.at(6) }
                                    .join('\t');
        buckets[site].append(rebuilt);
    }
    QHash<QString, QString> out;
    for (auto it = buckets.begin(); it != buckets.end(); ++it) {
        QString text = QString("%1\n%2\n\n").arg(kNetscapeHeader, kComment);
        text += it.value().join('\n');
        text += '\n';
        out.insert(it.key(), text);
    }
    return out;
}

QHash<QString, int> CookieImport::countsOf(const QHash<QString, QString> &bySite) {
    QHash<QString, int> out;
    for (auto it = bySite.begin(); it != bySite.end(); ++it) {
        int n = 0;
        for (const QString &l : it.value().split('\n'))
            if (!l.trimmed().isEmpty() && !l.startsWith('#')) n++;
        out.insert(it.key(), n);
    }
    return out;
}

bool CookieImport::importFromBrowser(const QString &browser, const QString &probeUrl, QString *summary) {
    if (browser.isEmpty()) {
        if (summary) *summary = "未指定浏览器";
        return false;
    }
    const QString dir = QDir::homePath() + "/.config/hov/cookies";
    QDir().mkpath(dir);
    const QString tmp = dir + "/.import-combined.txt";
    QFile::remove(tmp);

    // yt-dlp：从浏览器读 cookie，并把合并后的 jar 写回 tmp（这就是"导出"）
    QProcess p;
    p.start("yt-dlp", QStringList{ "--cookies-from-browser", browser, "--cookies", tmp,
                                   "--simulate", "--skip-download", "--no-warnings", probeUrl });
    if (!p.waitForStarted(5000) || !p.waitForFinished(90000)) {
        p.kill();
        if (summary) *summary = "yt-dlp 未能在 90 秒内完成（浏览器可能弹出了钥匙串授权窗口）";
        return false;
    }
    QFile f(tmp);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        if (summary)
            *summary = QString("未能导出 cookie（yt-dlp 退出码 %1；浏览器名是否正确？）").arg(p.exitCode());
        return false;
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();

    const QHash<QString, QString> bySite = splitBySite(text);
    if (bySite.isEmpty()) {
        QFile::remove(tmp);
        if (summary) *summary = "浏览器里没有找到与本站点相关的 cookie（请先在浏览器登录对应站点）";
        return false;
    }
    const QHash<QString, int> counts = countsOf(bySite);
    QStringList parts;
    for (auto it = bySite.begin(); it != bySite.end(); ++it) {
        const QString path = QString("%1/%2.txt").arg(dir, it.key());
        QFile out(path);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out.write(it.value().toUtf8());
            out.close();
            parts << QString("%1=%2 条").arg(it.key()).arg(counts.value(it.key()));
        }
    }
    QFile::remove(tmp);                                   // 合并文件含全部浏览器 cookie → 立刻删掉
    if (summary) *summary = parts.isEmpty() ? "写入失败（目录不可写？）" : parts.join("，");
    return !parts.isEmpty();
}
