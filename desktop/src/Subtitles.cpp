#include "Subtitles.h"
#include <QLocale>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>
#include <QtMath>

#include <algorithm>

namespace {

/** SRT / VTT 时间戳：00:00:01,000 或 00:00:01.000（也容忍缺毫秒） */
double stampTime(const QString &raw) {
    const QString s = raw.trimmed();
    const int c1 = s.indexOf(':');
    if (c1 < 0) return -1.0;
    const int c2 = s.indexOf(':', c1 + 1);
    if (c2 < 0) return -1.0;
    bool okH = false, okM = false, okS = false;
    const double h = s.left(c1).toDouble(&okH);
    const double m = s.mid(c1 + 1, c2 - c1 - 1).toDouble(&okM);
    QString secStr = s.mid(c2 + 1);
    secStr.replace(',', '.');
    const double sec = secStr.toDouble(&okS);
    if (!okH || !okM || !okS) return -1.0;
    return h * 3600.0 + m * 60.0 + sec;
}

/** LRC 时间标签：[mm:ss.xx] 或 [mm:ss] */
double lrcTime(const QString &mStr, const QString &sStr) {
    bool okM = false, okS = false;
    const double m = mStr.toDouble(&okM);
    QString s = sStr;
    s.replace(',', '.');
    const double sec = s.toDouble(&okS);
    if (!okM || !okS) return -1.0;
    return m * 60.0 + sec;
}

/** 去掉标记与转义、还原实体；ASS 的 \N 换成换行 */
QString cleanText(QString t) {
    t.replace("\\N", "\n");
    t.replace("\\n", "\n");
    static const QRegularExpression htmlTag("<[^>]*>");
    t.remove(htmlTag);
    static const QRegularExpression assTag("\\{[^}]*\\}");
    t.remove(assTag);
    t.replace("&nbsp;", " ");
    t.replace("&amp;", "&");
    t.replace("&lt;", "<");
    t.replace("&gt;", ">");
    return t.trimmed();
}

QVector<SubtitleCue> parseSrtLike(const QString &text, bool vtt) {
    QVector<SubtitleCue> out;
    const QStringList lines = text.split(QRegularExpression("\r?\n"));
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i];
        if (!line.contains("-->")) continue;
        if (vtt) line = line.section(QRegularExpression("\\s+"), 0, 2); // 去掉 cue 设置
        const QStringList parts = line.split("-->");
        if (parts.size() < 2) continue;
        SubtitleCue c;
        c.start = stampTime(parts[0]);
        // 注意：parts[1] 形如 " 00:00:05,000"（前导空格），必须先 trim，
        // 否则 section(' ',0,0) 会取到空串 → 结束时间解析失败 → 回落成开始时间，
        // 结果是任何时刻都命中不到字幕（实测踩过：end == start）。
        c.end = stampTime(parts[1].trimmed().section(' ', 0, 0));
        if (c.start < 0.0) continue;
        if (c.end < c.start) c.end = c.start;

        QStringList body;
        for (int j = i + 1; j < lines.size(); ++j) {
            const QString &b = lines[j];
            if (b.trimmed().isEmpty()) break;
            if (b.contains("-->")) break;          // 容错：缺空行分隔
            body << b;
            i = j;
        }
        c.text = cleanText(body.join("\n"));
        if (c.text.isEmpty()) continue;            // 跳过纯音乐符号等空字幕
        out.append(c);
    }
    std::sort(out.begin(), out.end(),
              [](const SubtitleCue &a, const SubtitleCue &b) { return a.start < b.start; });
    return out;
}

QVector<SubtitleCue> parseLrc(const QString &text) {
    QVector<SubtitleCue> out;
    static const QRegularExpression ts("\\[(\\d{1,3}):(\\d{1,2}(?:[.,]\\d{1,3})?)\\]");
    const QStringList lines = text.split(QRegularExpression("\r?\n"));
    for (const QString &line : lines) {
        QVector<double> times;
        auto it = ts.globalMatch(line);
        while (it.hasNext()) {
            const auto m = it.next();
            const double t = lrcTime(m.captured(1), m.captured(2));
            if (t >= 0.0) times.append(t);
        }
        if (times.isEmpty()) continue;
        QString body = line;
        body.remove(ts);                            // 标签行（作词/作曲）去掉标签后为空 → 跳过
        body = cleanText(body);
        if (body.isEmpty()) continue;
        for (double t : times) {
            SubtitleCue c;
            c.start = t;
            c.end = t;
            c.text = body;
            out.append(c);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const SubtitleCue &a, const SubtitleCue &b) { return a.start < b.start; });
    for (int i = 0; i + 1 < out.size(); ++i) out[i].end = out[i + 1].start;  // 逐行滚动
    if (!out.isEmpty()) out.last().end = out.last().start + 5.0;
    return out;
}

QString decodeBytes(const QByteArray &data) {
    QByteArray d = data;
    if (d.startsWith("\xEF\xBB\xBF")) d.remove(0, 3);          // UTF-8 BOM
    const QString utf8 = QString::fromUtf8(d);
    if (!utf8.contains(QChar(0xFFFD))) return utf8;
    // 中文外挂字幕常见 GB18030/GBK/Big5：Qt6 走 ICU 转换器，可用则尝试
    for (const char *enc : {"GB18030", "GBK", "Big5"}) {
        QStringDecoder dec(enc);
        if (!dec.isValid()) continue;
        const QString t = dec(d);
        if (!t.contains(QChar(0xFFFD))) return t;
    }
    return utf8;
}

}  // namespace

QVector<SubtitleCue> Subtitles::parse(const QString &text, const QString &extHint) {
    const QString e = extHint.toLower();
    if (e == "lrc") return parseLrc(text);
    if (e == "vtt") return parseSrtLike(text, true);
    if (e == "json" || e == "json3") return parseJson(text);
    if (e == "yrc") return parseYrc(text);
    if (e == "qrc") return parseQrc(text);
    return parseSrtLike(text, false);   // srt 及未知格式都按 SRT 试
}

// JSON 字幕：B站（{"body":[{"from":..,"to":..,"content":..}]}）
// 与 YouTube json3（{"events":[{"tStartMs":..,"dDurationMs":..,"segs":[{"utf8":..}]}]}）两种结构自动判别
QVector<SubtitleCue> Subtitles::parseJson(const QString &text) {
    QVector<SubtitleCue> out;
    const QJsonObject root = QJsonDocument::fromJson(text.toUtf8()).object();

    const QJsonArray body = root.value("body").toArray();          // B站
    for (const auto &v : body) {
        const QJsonObject o = v.toObject();
        SubtitleCue c;
        c.start = o.value("from").toDouble(-1.0);
        c.end = o.value("to").toDouble(c.start);
        c.text = cleanText(o.value("content").toString());
        if (c.start < 0.0 || c.text.isEmpty()) continue;
        out.append(c);
    }

    const QJsonArray events = root.value("events").toArray();      // YouTube json3
    for (const auto &v : events) {
        const QJsonObject o = v.toObject();
        const QJsonArray segs = o.value("segs").toArray();
        QString txt;
        for (const auto &s : segs) txt += s.toObject().value("utf8").toString();
        txt = cleanText(txt);
        if (txt.isEmpty()) continue;
        SubtitleCue c;
        c.start = o.value("tStartMs").toDouble(0.0) / 1000.0;
        c.end = c.start + o.value("dDurationMs").toDouble(2000.0) / 1000.0;
        c.text = txt;
        out.append(c);
    }

    std::sort(out.begin(), out.end(),
              [](const SubtitleCue &a, const SubtitleCue &b) { return a.start < b.start; });
    // json3 会输出大量重复滚动行：相邻相同文本合并（保留更早的开始）
    QVector<SubtitleCue> merged;
    for (const SubtitleCue &c : out) {
        if (!merged.isEmpty() && merged.last().text == c.text) {
            merged.last().end = qMax(merged.last().end, c.end);
            continue;
        }
        merged.append(c);
    }
    return merged;
}

QVector<SubtitleCue> Subtitles::loadFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "字幕打开失败:" << path;
        return {};
    }
    const QByteArray data = f.readAll();
    f.close();
    const QString text = decodeBytes(data);
    const QVector<SubtitleCue> cues = parse(text, QFileInfo(path).suffix());
    qInfo() << "字幕已解析:" << QFileInfo(path).fileName() << cues.size() << "条";
    return cues;
}

QString Subtitles::findSidecar(const QString &mediaPath) {
    const QFileInfo fi(mediaPath);
    if (!fi.exists()) return QString();
    const QString want = fi.completeBaseName().toLower();
    // 优先级：srt > vtt > lrc（仅包含**已实现解析**的格式；ass/ssa 尚未支持，不在此列）
    static const QStringList order{"srt", "vtt", "yrc", "qrc", "lrc", "json3", "json"};
    QDir dir(fi.absolutePath());
    const QStringList files = dir.entryList(QDir::Files);
    for (const QString &ext : order) {
        for (const QString &name : files) {
            const QFileInfo sf(dir.filePath(name));
            if (sf.completeBaseName().toLower() == want && sf.suffix().toLower() == ext)
                return sf.absoluteFilePath();
        }
    }
    return QString();
}

QVector<SubtitleTrack> Subtitles::findSidecarTracks(const QString &mediaPath) {
    const QFileInfo fi(mediaPath);
    if (!fi.exists()) return {};
    const QString base = fi.completeBaseName();
    static const QStringList exts{"srt", "vtt", "yrc", "qrc", "lrc"};
    // 排序权重：中文 > 台港中 > 英文 > 其他语言 > 无后缀默认
    static const QStringList langOrder{"zh-hans", "zh-cn", "zh", "chs", "sc", "zh-hant", "zh-tw",
                                       "cht", "tc", "en", "eng", "ja", "jp"};
    QDir dir(fi.absolutePath());
    const QStringList names = dir.entryList(QDir::Files, QDir::Name);

    QVector<QPair<int, SubtitleTrack>> tmp;
    for (const QString &name : names) {
        const QFileInfo sf(dir.filePath(name));
        const QString ext = sf.suffix().toLower();
        if (!exts.contains(ext)) continue;
        const QString cbase = sf.completeBaseName();               // 去掉最后一个扩展名
        QString lang;
        if (cbase.compare(base, Qt::CaseInsensitive) == 0) {
            lang = QStringLiteral("默认");
        } else if (cbase.startsWith(base + ".", Qt::CaseInsensitive)) {
            lang = cbase.mid(base.length() + 1);                   // 语言后缀，如 zh-Hans / en
        } else {
            continue;
        }
        int rank = 100;                                            // 无后缀默认排最后
        if (lang != QStringLiteral("默认")) {
            const int idx = langOrder.indexOf(lang.toLower());
            rank = (idx >= 0) ? idx : 50;
        }
        SubtitleTrack t;
        t.label = QString("%1 · %2").arg(lang, ext.toUpper());
        t.source = sf.absoluteFilePath();
        tmp.append({rank, t});
    }
    // 同语言后缀时按**格式信息量**排序：yrc/qrc（字级时间）> srt > vtt > lrc
    // （否则"lyrictest.lrc"会因字母序排在"lyrictest.yrc"前面，白白丢掉逐字能力）
    auto fmtRank = [](const QString &path) {
        const QString e = QFileInfo(path).suffix().toLower();
        if (e == "yrc" || e == "qrc") return 0;
        if (e == "srt") return 1;
        if (e == "vtt") return 2;
        if (e == "lrc") return 3;
        return 4;
    };
    std::sort(tmp.begin(), tmp.end(), [&fmtRank](const QPair<int, SubtitleTrack> &a,
                                                 const QPair<int, SubtitleTrack> &b) {
        if (a.first != b.first) return a.first < b.first;
        const int fa = fmtRank(a.second.source), fb = fmtRank(b.second.source);
        if (fa != fb) return fa < fb;
        return a.second.source < b.second.source;
    });
    QVector<SubtitleTrack> out;
    for (const auto &p : tmp) out.append(p.second);
    return out;
}

QVector<SubtitleCue> Subtitles::mergeBilingual(const QVector<SubtitleCue> &main,
                                               const QVector<SubtitleCue> &trans) {
    if (main.isEmpty() || trans.isEmpty()) return main;
    QVector<SubtitleCue> out = main;
    for (auto &c : out) {
        // 翻译轨按开始时间匹配（容差 0.3s，避免浮点/取整差异导致配不上）
        for (const auto &t : trans) {
            if (qAbs(t.start - c.start) <= 0.3 && !t.text.isEmpty() && t.text != c.text) {
                c.text = c.text + "\n" + t.text;
                break;
            }
        }
    }
    return out;
}


// 语言优先级：中文简体 > 中文繁体 > 英文 > 其他；"再翻"（zh-Hans-en 这种带两段）排在原生之后
QStringList Subtitles::systemLanguageHints() {
    // QLocale::uiLanguages() 形如 ["zh-Hans-CN","zh-Hans","zh"] ✓（与 macOS 的 Locale.preferredLanguages 同构 ✓）
    QStringList hints;
    for (const QString &tag : QLocale::system().uiLanguages()) {
        const QString low = tag.toLower();
        hints << low;                                       // zh-hans-cn
        const QStringList parts = low.split('-');
        if (parts.size() >= 2) hints << (parts[0] + "-" + parts[1]);   // zh-hans
        if (!parts.isEmpty()) hints << parts[0];            // zh
    }
    hints << "en";                                          // 兜底：英文
    hints.removeAll(QString());
    hints.removeDuplicates();
    return hints;
}

QString Subtitles::subLangsForSystem() {
    QStringList langs = systemLanguageHints();
    langs << "zh-Hans" << "zh-CN" << "zh" << "zh-Hant" << "zh-TW" << "en";   // 兼容补足 ✓ yt-dlp 会自己取交集 ✓
    langs.removeDuplicates();
    while (langs.size() > 8) langs.removeLast();
    return langs.join(',');
}

int Subtitles::languageRank(const QString &lang) {
    const QString l = lang.trimmed().toLower();
    // 字幕标签形如 "zh-Hans · SRT" / "en · SRT" / "中文（中国）· CC" → 取分隔符之前的部分做语言判定 ✓
    const QString label = l.contains(QString::fromUtf8("·")) ? l.section(QString::fromUtf8("·"), 0, 0).trimmed() : l;
    const QStringList hints = systemLanguageHints();
    const bool sysZh = std::any_of(hints.cbegin(), hints.cend(),
                                   [](const QString &h) { return h.startsWith("zh"); });

    // ① 系统语言驱动：**按 hint 下标打分**（下标越小越优先 ✓ 不能压成 0/1 ✗ 否则英文与中文同级 ✗）
    for (int idx = 0; idx < hints.size(); ++idx) {
        const QString h = hints.at(idx);
        if (h.size() < 2) continue;
        if (label == h || label.startsWith(h + "-") || label.startsWith(h)) return idx;
    }
    // ② 中文轨可能写成自然语言（简体/繁體/中文（中国））→ 视为命中系统语言 ✓
    if (sysZh) {
        const bool hans = label.contains(QString::fromUtf8("简体"))
                          || label.contains(QString::fromUtf8("中文（中国）")) || label.contains(QString::fromUtf8("中文(中国)"));
        const bool hant = label.contains(QString::fromUtf8("繁體"))
                          || label.contains(QString::fromUtf8("中文（台")) || label.contains(QString::fromUtf8("中文(台"));
        if (hans || hant) {
            const bool sysHant = hints.first().startsWith("zh-hant");
            return (sysHant == hant) ? 0 : 1;
        }
    }
    // ③ 都未命中：排在所有 hints 之后（英文优先 ✓；中文系统再保留"简体优先"的原有观感 ✓）
    const int base = hints.size();
    if (label == "en" || label.startsWith("en-") || label.startsWith("english")) return base;
    if (sysZh) {
        const bool hans = label.startsWith("zh-hans") || label == "zh" || label == "zh-cn" || label == "zh-sg";
        const bool hant = label.startsWith("zh-hant") || label == "zh-tw" || label == "zh-hk" || label == "zh-mo";
        if (hans) return base + 1;
        if (hant) return base + 2;
    }
    return base + 3;
}

// ---------------- 逐字歌词（YRC / QRC）----------------
// 网易云 YRC 行： [行起毫秒,行时长毫秒](字起,字时长,0)你(240,260,0)好
// QQ QRC（解密后）：XML 的 LyricContent 里为 [行起,行时长]你(0,240)好(240,260)——同构，少一个尾字段
QVector<SubtitleCue> Subtitles::parseYrc(const QString &text) {
    // 注意顺序：YRC 是「(字起,字时长,标记)字」，QRC 是「字(字起,字时长)」——两者相反，
    // 一开始按同一种写法解析导致文本/时间全部错位（自检直接抓到，见 #60）。
    QVector<SubtitleCue> out;
    static const QRegularExpression lineRe("\\[(\\d+),(\\d+)\\]");
    static const QRegularExpression wordRe("\\((\\d+),(\\d+)(?:,\\d+)?\\)([^()\\[\\]]*)");
    const QStringList lines = text.split(QRegularExpression("\r?\n"));
    for (const QString &raw : lines) {
        const auto lm = lineRe.match(raw);
        if (!lm.hasMatch()) continue;
        const double lineStart = lm.captured(1).toDouble() / 1000.0;
        const double lineDur = lm.captured(2).toDouble() / 1000.0;

        SubtitleCue cue;
        cue.start = lineStart;
        cue.end = lineStart + lineDur;
        QString body = raw;
        body.remove(0, static_cast<int>(lm.capturedEnd(0)));   // 去掉行首 [起,时长]
        auto it = wordRe.globalMatch(body);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString wordText = m.captured(3);
            if (wordText.isEmpty()) continue;
            SubtitleWord w;
            w.start = lineStart + m.captured(1).toDouble() / 1000.0;
            w.end = w.start + m.captured(2).toDouble() / 1000.0;
            w.text = wordText;
            cue.words.append(w);
            cue.text += w.text;
        }
        cue.text = cue.text.trimmed();
        if (cue.text.isEmpty()) continue;
        out.append(cue);
    }
    std::sort(out.begin(), out.end(),
              [](const SubtitleCue &a, const SubtitleCue &b) { return a.start < b.start; });
    return out;
}

QVector<SubtitleCue> Subtitles::parseQrc(const QString &text) {
    // QRC（解密后）形如：<LyricInfo LyricContent="[行起,行时长]字(字起,字时长)字(…)" />
    QString body = text;
    static const QRegularExpression contentRe("LyricContent=\"([^\"]*)\"");
    const auto cm = contentRe.match(body);
    if (cm.hasMatch()) body = cm.captured(1);
    body.replace("&apos;", "'");
    body.replace("&quot;", "\"");
    body.replace("&lt;", "<");
    body.replace("&gt;", ">");
    body.replace("&amp;", "&");
    body.replace("\\n", "\n");

    QVector<SubtitleCue> out;
    static const QRegularExpression lineRe("\\[(\\d+),(\\d+)\\]");
    static const QRegularExpression wordRe("([^()\\[\\]]+)\\((\\d+),(\\d+)\\)");
    const QStringList lines = body.split(QRegularExpression("\r?\n"));
    for (const QString &raw : lines) {
        const auto lm = lineRe.match(raw);
        if (!lm.hasMatch()) continue;
        const double lineStart = lm.captured(1).toDouble() / 1000.0;
        const double lineDur = lm.captured(2).toDouble() / 1000.0;
        SubtitleCue cue;
        cue.start = lineStart;
        cue.end = lineStart + lineDur;
        QString rest = raw;
        rest.remove(0, static_cast<int>(lm.capturedEnd(0)));
        auto it = wordRe.globalMatch(rest);
        while (it.hasNext()) {
            const auto m = it.next();
            SubtitleWord w;
            w.start = lineStart + m.captured(2).toDouble() / 1000.0;
            w.end = w.start + m.captured(3).toDouble() / 1000.0;
            w.text = m.captured(1);
            cue.words.append(w);
            cue.text += w.text;
        }
        cue.text = cue.text.trimmed();
        if (cue.text.isEmpty()) continue;
        out.append(cue);
    }
    std::sort(out.begin(), out.end(),
              [](const SubtitleCue &a, const SubtitleCue &b) { return a.start < b.start; });
    return out;
}

double Subtitles::lineProgress(const SubtitleCue &c, double seconds) {
    const double span = c.end - c.start;
    if (span <= 0.01) return seconds >= c.start ? 1.0 : 0.0;   // 无时长（纯 LRC 补过）直接算唱完
    const double p = (seconds - c.start) / span;
    return p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
}

const SubtitleCue *Subtitles::cueAt(const QVector<SubtitleCue> &cues, double seconds) {
    if (cues.isEmpty()) return nullptr;
    // 二分：最后一条 start <= seconds 的候选
    int lo = 0, hi = cues.size() - 1, idx = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (cues[mid].start <= seconds) { idx = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    if (idx < 0) return nullptr;
    if (seconds > cues[idx].end) return nullptr;
    return &cues[idx];
}
