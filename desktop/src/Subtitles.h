#pragma once
#include <QString>
#include <QVector>

/**
 * 字幕 / 歌词解析（桌面端）
 *
 * 与 Android 版 core/data/Subtitles.kt 同等职责，但用 C++ 实现 —— 桌面端同样采用
 * 「App 层渲染字幕」方案（不依赖 libmpv 的 libass：该构建缺 font provider，见项目文档）。
 *
 * 支持格式：
 *   - SRT（.srt）  序号 + 00:00:01,000 --> 00:00:04,000 + 正文
 *   - WebVTT（.vtt）时间戳用点号，忽略 WEBVTT/NOTE/STYLE 块
 *   - LRC（.lrc）  [mm:ss.xx]歌词 —— 音乐场景；结束时间用下一条补齐
 */
/** 逐字时间（YRC / QRC 才有的字级时间戳） */
struct SubtitleWord {
    double start = 0.0;
    double end = 0.0;
    QString text;
};

struct SubtitleCue {
    double start = 0.0;
    double end = 0.0;
    QString text;
    QVector<SubtitleWord> words;   // 空 = 仅行级时间（LRC/SRT）；非空 = 可真·逐字高亮
};

/** 一条可选字幕轨：label 给用户看；source 为本地路径（本地外挂 / yt-dlp 落地文件），
 *  cues 非空则为「内存字幕轨」（B站 CC API 直接取回的 JSON，不必落盘） */
struct SubtitleTrack {
    QString label;
    QString source;
    QVector<SubtitleCue> cues;   // 在线轨：内容直接给；本地轨：空，读 source
    bool karaoke = false;        // 歌词类轨（在线歌词）——本地轨按扩展名判定
};

class Subtitles {
public:
    /** 按扩展名选择解析器；extHint 形如 "srt"/"vtt"/"lrc"（不区分大小写） */
    static QVector<SubtitleCue> parse(const QString &text, const QString &extHint);

    /** 读文件并解析（自动处理 UTF-8 BOM；UTF-8 解码失败时回退 GB18030/GBK/Big5） */
    static QVector<SubtitleCue> loadFile(const QString &path);

    /** 解析 JSON 字幕：B站（body/from/to/content）与 YouTube json3（events/segs）自动判别 */
    static QVector<SubtitleCue> parseJson(const QString &text);

    /** 逐字歌词：网易云 YRC（[行起,行时长](字起,字时长,0)字…） */
    static QVector<SubtitleCue> parseYrc(const QString &text);
    /** 逐字歌词：QQ QRC 解密后的内容（XML 里 LyricContent="[行起,行时长]字(字起,字时长)…"） */
    static QVector<SubtitleCue> parseQrc(const QString &text);

    /**
     * 语言优先级（用于字幕轨排序；越小越优先）：
     *   0 中文简体（原生） 1 简体（由其他语言再翻） 2 中文繁体 3 繁体（再翻） 4 英文 5 英文（再翻） 6 其他
     * 背景：“按文件名字母序”会把 en 排在 zh 前面，自动启用的将是英文字幕（实测踩到）。
     */
    static int languageRank(const QString &lang);

    /** 为媒体文件查找同名字幕（a.mp4 → a.srt / a.vtt / a.lrc），没有则返回空。
     *  暂不含 .ass/.ssa（未实现解析，避免加载后显示乱码） */
    static QString findSidecar(const QString &mediaPath);

    /** 列出**全部**同名字幕轨（支持语言后缀：a.zh-Hans.srt / a.en.srt / a.srt …），
     *  按「中文优先 → 英文 → 其他语言 → 无后缀默认」排序”；标签形如 “zh-Hans · SRT” */
    static QVector<SubtitleTrack> findSidecarTracks(const QString &mediaPath);

    /** 把翻译轨按时间对齐合并进主轨（同名/同开始时间的行合并为两行显示，与 Android 版双语一致） */
    static QVector<SubtitleCue> mergeBilingual(const QVector<SubtitleCue> &main,
                                               const QVector<SubtitleCue> &trans);

    /** 行内进度 0~1（用于逐字高亮：按行时长线性插值；越界自动夹紧） */
    static double lineProgress(const SubtitleCue &c, double seconds);

    /** 取秒数处的字幕；cues 需按 start 升序（解析器已保证） */
    static const SubtitleCue *cueAt(const QVector<SubtitleCue> &cues, double seconds);
};
