// ── Lyrics.cpp：从 MainWindow.h 搬出的成员实现（文档 52/53/54 ✓ 零行为改动 ✓）──
// 搬运清单: loadLyricsFor loadQQLyricsFor
#include "MainWindow.h"

void MainWindow::loadLyricsFor(const QString &id, const QString &label, NetEaseApi *api) {
        api->lyricFull(id, [this, api, label](const QString &lrc, const QString &trans, const QString &yrc) {
            lastLRC_ = lrc;                  // ③ 供下载写 .lrc 侧车
            // 有逐字歌词就用它（真·逐字高亮）；否则退回行级 + 行内插值近似
            QVector<SubtitleCue> cues = yrc.isEmpty() ? Subtitles::parse(lrc, "lrc")
                                                      : Subtitles::parseYrc(yrc);
            if (!yrc.isEmpty()) results_->addItem("歌词带逐字时间（逐字高亮）");
            const QVector<SubtitleCue> tc = Subtitles::parse(trans, "lrc");
            cues = Subtitles::mergeBilingual(cues, tc);
            if (cues.isEmpty()) {
                results_->addItem("该曲目没有歌词");
            } else {
                player_->setSubtitleCues(cues, QString("歌词：%1").arg(label),
                                              settings_.boolean("subtitle.karaoke", true));   // true=歌词面板模式
                results_->addItem(QString("已加载歌词 %1 行%2")
                                      .arg(cues.size())
                                      .arg(tc.isEmpty() ? "" : "（含翻译）"));
            }
            api->deleteLater();
        });
}

void MainWindow::loadQQLyricsFor(const QString &mid, const QString &label, QQMusicApi *api) {
        api->lyric(mid, [this, api, label](const QString &lrc, const QString &trans) {
            lastLRC_ = lrc;                  // ③ 同上（QQ 音乐）
            QVector<SubtitleCue> cues = Subtitles::parse(lrc, "lrc");
            const QVector<SubtitleCue> tc = Subtitles::parse(trans, "lrc");
            cues = Subtitles::mergeBilingual(cues, tc);
            if (cues.isEmpty()) {
                results_->addItem("该曲目没有歌词");
            } else {
                player_->setSubtitleCues(cues, QString("歌词：%1").arg(label),
                                              settings_.boolean("subtitle.karaoke", true));   // true=歌词面板模式
                results_->addItem(QString("已加载歌词 %1 行%2")
                                      .arg(cues.size())
                                      .arg(tc.isEmpty() ? "" : "（含翻译）"));
            }
            api->deleteLater();
        });
}
