// ── SelfTest.cpp：由 main.cpp 搬出的自检实现（S1 ✓ 零行为改动 ✓）──
// 搬迁记录：main.cpp 第 722-1072 行（351 行）✓ 2026-09-22
#include "SelfTest.h"
#include "MpvWidget.h"
#include "Settings.h"
#include "PlayQueue.h"
#include "SubtitleOverlay.h"
#include "UrlResolver.h"
#include "ColorTheme.h"
#include "CookieImport.h"
#include "Subtitles.h"
#include "Favorites.h"
#include "LocalLibrary.h"
#include "ProgressStore.h"
#include <QListWidget>
#include <QThread>
#include <QDebug>
#include <cmath>
#include <QByteArray>
#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QVector>
#include <iostream>

#include "GpuCompat.h"

void SelfTest::runQueueSelfTest(const Ctx &ctx) {
        int pass = 0, total = 0;
        auto check = [&](bool ok, const QString &name) {
            total++; if (ok) pass++;
            ctx.results->addItem(QString("%1  %2").arg(ok ? "PASS" : "FAIL", name));
            if (!ok) std::cerr << "[FAIL] " << name.toStdString() << std::endl;   // 无人值守时也能定位
        };
        PlayQueue q;
        q.setList({ {"a", "A"}, {"b", "B"}, {"c", "C"} }, 0);
        check(q.size() == 3 && q.index() == 0, "入队 3 项且从第 0 项开始");
        check(q.current() && q.current()->key == "a", "当前项为 a");
        q.next();  check(q.index() == 1, "顺序模式下一首 → 索引 1");
        q.next(); q.next(); check(q.index() == 0, "末尾环绕回第 0 项");
        q.prev();  check(q.index() == 2, "上一首从第 0 项环绕到末项");
        q.cycleMode(); check(q.mode() == PlayQueue::Mode::RepeatOne, "模式切换 → 单曲");
        const int before = q.index();
        check(q.next() == true && q.index() != before, "单曲模式：手动切歌仍前进（与 macOS/Android 一致）");
            const int rep = q.index();
            check(q.autoNext() == true && q.index() == rep, "单曲模式：自动续播不移动（调用方重播本曲）");
            q.cycleMode(); q.cycleMode();                       // → 列表循环
            check(q.mode() == PlayQueue::Mode::RepeatAll, "模式：四档循环到列表循环");
            q.jumpTo(q.size() - 1);
            check(q.autoNext() == true && q.index() == 0, "列表循环：到底自动回到第一项");
            check(q.next() == true, "手动切歌：列表循环下可前进");
            // 持久化往返：四种模式都必须原样保存/加载（本轮真实 bug：RepeatAll 存了但加载丢）
            {
                const QString tp = QDir::tempPath() + "/hov-mode-roundtrip.json";
                for (auto m2 : { PlayQueue::Mode::Sequential, PlayQueue::Mode::RepeatOne,
                                 PlayQueue::Mode::Shuffle, PlayQueue::Mode::RepeatAll }) {
                    PlayQueue a; a.setList({ {"x","X"}, {"y","Y"} }, 0);
                    while (a.mode() != m2) a.cycleMode();
                    a.saveToPath(tp);
                    PlayQueue b; b.loadFromPath(tp);
                    check(b.mode() == m2, QString("持久化往返：模式 %1 保持").arg(static_cast<int>(m2)));
                }
                QFile::remove(tp);
            }
        // 接在上面的"列表循环"块之后（此时模式 = RepeatAll），把四档整圈走完
        q.cycleMode(); check(q.mode() == PlayQueue::Mode::Sequential, "模式切换：列表循环 → 顺序");
        q.cycleMode(); check(q.mode() == PlayQueue::Mode::RepeatOne, "模式切换：顺序 → 单曲");
        q.cycleMode(); check(q.mode() == PlayQueue::Mode::Shuffle, "模式切换：单曲 → 随机");
        const int beforeRand = q.index();
        q.next(); check(q.index() != beforeRand, "随机模式移动到别的项");
        q.cycleMode(); check(q.mode() == PlayQueue::Mode::RepeatAll, "模式切换：随机 → 列表循环（整圈闭合）");
        q.jumpTo(99); check(q.index() >= 0 && q.index() < q.size(), "越界 jumpTo 被忽略且索引合法");
        // 逐字高亮的纯函数：行内进度
        // 本地库：排序 / 过滤 / 重命名守卫（临时目录，测完自清）
        {
            const QString td = QDir::tempPath() + "/hov-lib-selftest";
            QDir(td).removeRecursively();
            QDir().mkpath(td);
            auto writeFile = [](const QString &p, int kb) {
                QFile f(p);
                if (f.open(QIODevice::WriteOnly)) { f.write(QByteArray(kb * 1024, 'x')); f.close(); }
            };
            auto setMtime = [](const QString &p, QDate d) {
                QFile f(p);
                if (f.open(QIODevice::ReadOnly)) {
                    f.setFileTime(QDateTime(d, QTime(0, 0)), QFileDevice::FileModificationTime);
                    f.close();
                }
            };
            writeFile(td + "/a.flac", 3);                       // 最大
            writeFile(td + "/b.mp3", 2);
            writeFile(td + "/x.mp3", 1);                        // 重命名测试用
            writeFile(td + "/y.mp3", 1);                        // 重命名测试用（冲突目标）
            writeFile(td + "/c.txt", 1);                        // 非媒体：应被忽略
            setMtime(td + "/x.mp3", QDate(2025, 1, 1));
            setMtime(td + "/y.mp3", QDate(2025, 1, 2));
            setMtime(td + "/a.flac", QDate(2026, 1, 1));
            setMtime(td + "/b.mp3", QDate(2026, 6, 1));         // 最新

            LocalLibrary lib(td);
            lib.setSort(LocalLibrary::Sort::Name);
            const auto byName = lib.scan();
            check(byName.size() == 4, "本地库：只列媒体文件（忽略 .txt）");
            check(byName.size() == 4 && byName[0].name == "a.flac", "本地库：按名称排序");
            lib.setSort(LocalLibrary::Sort::Date);
            const auto byDate = lib.scan();
            check(byDate.size() == 4 && byDate[0].name == "b.mp3", "本地库：按时间排序（新→旧）");
            lib.setSort(LocalLibrary::Sort::Size);
            const auto bySize = lib.scan();
            check(bySize.size() == 4 && bySize[0].name == "a.flac" && bySize[3].size == 1024,
                  "本地库：按大小排序（大→小）");
            check(lib.scan("b").size() == 1, "本地库：文件名过滤生效");
            check(lib.rename(td + "/x.mp3", "bad/name").contains("路径"), "本地库：重命名拒绝路径分隔符");
            check(!lib.rename(td + "/x.mp3", "y").isEmpty(), "本地库：重命名拒绝已存在的目标");
            check(lib.rename(td + "/y.mp3", "新的名字").isEmpty() && QFile::exists(td + "/新的名字.mp3"),
                  "本地库：重命名成功且保留扩展名");
            check(!lib.rename(td + "/y.mp3", "x").isEmpty(), "本地库：原文件不存在时明确报错");
            QDir(td).removeRecursively();
            check(!QDir(td).exists(), "本地库：测试目录已清理");
        }

        // 在线字幕：语言优先级（修复"英文轨排在中文前"）+ B站 CC JSON 格式
        {
            {
                // 断言必须**环境自适应** ✗：系统语言是中文时中文优先 ✓；POSIX/英文系统时英文优先 ✓
                //（VM 的 LANG 为空 → QLocale 判为英文 ✓ → 写死"中文优先"的断言会假红 ✗ 实测踩到）
                const QStringList hints = Subtitles::systemLanguageHints();
                const bool sysZh = !hints.isEmpty() && hints.first().startsWith("zh");
                if (sysZh) {
                    check(Subtitles::languageRank("zh-Hans · SRT") < Subtitles::languageRank("en · SRT"),
                          "字幕排序：中文系统 → 简体排在英文之前");
                    check(Subtitles::languageRank("中文（中国）· CC") <= Subtitles::languageRank("zh-Hans · SRT"),
                          "字幕排序：B站中文（中国）不劣于 zh-Hans");
                    // 2026-09-25 ✓ B站裸"中文"/YouTube "Chinese (Simplified)（自动）"也要命中（此前漏判 ✗）
                    check(Subtitles::languageRank("中文 · CC") < Subtitles::languageRank("en · SRT"),
                          "字幕排序：B站裸「中文」轨命中中文系统");
                    check(Subtitles::languageRank("Chinese (Simplified)（自动）") < Subtitles::languageRank("en · SRT"),
                          "字幕排序：YouTube 中文标签命中中文系统");
                    check(Subtitles::languageRank("zh-Hans · SRT") < Subtitles::languageRank("zh-Hant · SRT"),
                          "字幕排序：简体排在繁体之前");
                    check(Subtitles::languageRank("zh-Hant · SRT") < Subtitles::languageRank("ja · SRT"),
                          "字幕排序：中文（含繁体）排在其它语种之前");
                } else {
                    check(Subtitles::languageRank("en · SRT") < Subtitles::languageRank("zh-Hans · SRT"),
                          "字幕排序：非中文系统 → 英文排在中文之前");
                }
                check(Subtitles::languageRank("zh-Hans-en") >= 0, "字幕排序：复合语言标签不崩溃");
                check(!hints.isEmpty() && !Subtitles::subLangsForSystem().isEmpty(),
                      "字幕语言：能读到系统语言偏好并生成 --sub-langs");
            }
            check(Subtitles::languageRank("en") < Subtitles::languageRank("ja"),
                  "字幕排序：英文优先于其他语种");
            const QString biliJson =
                "{\"body\":[{\"from\":1.5,\"to\":4.0,\"content\":\"第一句\"},"
                "{\"from\":4.2,\"to\":7.0,\"content\":\"第二句\"}]}";
            const QVector<SubtitleCue> bc = Subtitles::parseJson(biliJson);
            check(bc.size() == 2 && qAbs(bc.value(1).start - 4.2) < 0.001
                      && bc.value(0).text == "第一句",
                  "B站 CC JSON：from/to/content 解析正确");
        }

        // A0 跨端登录：cookie 导入的纯逻辑（域名映射 / 按站点切分 / 隐私过滤 / 格式修正）
        {
            check(CookieImport::siteOfDomain("music.163.com") == "netease", "cookie：网易云域名映射");
            check(CookieImport::siteOfDomain(".bilibili.com") == "bilibili", "cookie：带点域名也认得 B站");
            check(CookieImport::siteOfDomain("www.youtube.com") == "youtube", "cookie：YouTube 域名映射");
            check(CookieImport::siteOfDomain("y.qq.com") == "qqmusic", "cookie：QQ音乐域名映射");
            check(CookieImport::siteOfDomain("example.com").isEmpty(), "cookie：无关域名不映射");
            const QString combined =
                "# Netscape HTTP Cookie File\n"
                ".bilibili.com\tTRUE\t/\tFALSE\t0\tSESSDATA\tabc\n"
                "music.163.com\tFALSE\t/\tFALSE\t0\tMUSIC_U\tdef\n"
                ".y.qq.com\tTRUE\t/\tFALSE\t0\tqqmusic_key\tghi\n"
                ".youtube.com\tTRUE\t/\tTRUE\t0\tSID\tjkl\n"
                ".example.com\tTRUE\t/\tFALSE\t0\tTRACK\tzzz\n";
            const QHash<QString, QString> by = CookieImport::splitBySite(combined);
            const QHash<QString, int> cnt = CookieImport::countsOf(by);
            check(by.size() == 4, "cookie：按 4 个站点切分（无关域名被丢弃）");
            check(cnt.value("bilibili") == 1 && cnt.value("netease") == 1 && cnt.value("qqmusic") == 1
                      && cnt.value("youtube") == 1, "cookie：各站点条数正确");
            check(!by.value("bilibili").contains("example.com"), "cookie：无关域名不进文件（隐私）");
            check(by.value("netease").contains("music.163.com\tFALSE\t"), "cookie：includeSubdomains 与域名一致");
            check(by.value("bilibili").contains(".bilibili.com\tTRUE\t"), "cookie：带点域名写 TRUE");
            check(by.value("bilibili").startsWith("# Netscape HTTP Cookie File"), "cookie：带标准文件头");
            check(Settings::validate("network.cookiesFromBrowser", "chrome").isEmpty(), "设置：浏览器名合法");
            check(Settings::validate("network.cookiesFromBrowser", "notabrowser").isEmpty() == false,
                  "设置：非法浏览器名被拒");
            check(Settings::validate("network.cookiesFromBrowser", "").isEmpty(), "设置：留空=关闭合法");
            // 参数组装：浏览器模式要把 --cookies-from-browser 与站点文件都带上（文件不存在也要带，yt-dlp 才会导出）
            {
                UrlResolver r0;
                const QStringList off = r0.cookieArgsFor("https://music.163.com/song?id=1");
                check(!off.contains("--cookies-from-browser"), "cookie 参数：关闭时不带浏览器参数");
                UrlResolver r1;
                r1.setCookiesFromBrowser("chrome");
                const QStringList on = r1.cookieArgsFor("https://music.163.com/song?id=1");
                check(on.contains("--cookies-from-browser") && on.contains("chrome"), "cookie 参数：开启时带浏览器名");
                check(on.contains("--cookies") && on.value(on.indexOf("--cookies") + 1).endsWith("netease.txt"),
                      "cookie 参数：带上站点文件（用于导出）");
                check(r1.cookieArgsFor("https://example.com/x").size() == 2,
                      "cookie 参数：未知站点只带浏览器参数（不写文件）");
            }
        }

        // B1 清晰度（纯函数 + 设置校验）
        {
            check(UrlResolver::formatArgsFor(0) == QStringList{"-f", "bv*+ba/b"}, "清晰度：自动档保持原行为");
            check(UrlResolver::formatArgsFor(-1) == QStringList{"-f", "ba/b"}, "清晰度：仅音频档");
            const QStringList a720 = UrlResolver::formatArgsFor(720);
            check(a720.size() == 2 && a720.at(1).contains("height<=720"), "清晰度：720 档带上限");
            check(a720.at(1).contains("+ba/b[height<="), "清晰度：音频部分不设 height 过滤（否则整条失配）");
            check(a720.at(1).endsWith("/bv*+ba/b"), "清晰度：拿不到该高度时有兜底");
            check(UrlResolver::qualityLabel(0) == "自动" && UrlResolver::qualityLabel(-1) == "仅音频"
                      && UrlResolver::qualityLabel(1080) == "1080p", "清晰度：档位标签");
            check(Settings::validate("video.maxHeight", "0").isEmpty()
                      && Settings::validate("video.maxHeight", "-1").isEmpty()
                      && Settings::validate("video.maxHeight", "1080").isEmpty(), "设置：清晰度合法值通过");
            check(!Settings::validate("video.maxHeight", "99").isEmpty(), "设置：清晰度 99 被拒");
        }

        // 进度记忆（仅内存，不碰真文件：不调用 load/save）
        {
            ProgressStore ps;
            ps.remember("k", 3.0, 100.0, "t");
            check(!ps.has("k"), "进度：不足 5 秒不记录");
            ps.remember("k", 30.0, 100.0, "t");
            check(qAbs(ps.resumePos("k") - 30.0) < 0.001, "进度：30/100 秒可续播");
            ps.remember("k", 10.0, 100.0, "t");
            check(ps.has("k") && ps.resumePos("k") == 0.0, "进度：10 秒保留记录但不续播");
            ps.remember("k", 92.0, 100.0, "t");
            check(!ps.has("k"), "进度：距结尾 15 秒内视为已看完并清除");
            ps.remember("k2", 20.0, 100.0, "t");
            check(qAbs(ps.resumePos("k2") - 20.0) < 0.001 && ps.size() == 1, "进度：条目互相独立");
        }

        // 逐字歌词解析（YRC / QRC）—— 在线接口对已登录用户返回 yrc；这里用固定样本验证解析与字级时间
        {
            const QString yrcText = "[12340,3200](0,240,0)你(240,260,0)好(500,300,0)啊";
            const QVector<SubtitleCue> y = Subtitles::parseYrc(yrcText);
            check(y.size() == 1, "YRC：解析出 1 行");
            check(!y.isEmpty() && qAbs(y[0].start - 12.34) < 0.001 && qAbs(y[0].end - 15.54) < 0.001,
                  "YRC：行起止时间正确（12.34~15.54s）");
            check(!y.isEmpty() && y[0].words.size() == 3, "YRC：得到 3 个字级时间戳");
            check(!y.isEmpty() && y[0].words.size() == 3 && y[0].words[1].text == "好"
                      && qAbs(y[0].words[1].start - 12.58) < 0.001,
                  "YRC：第 2 字的时间与文本正确");
            check(!y.isEmpty() && y[0].text == "你好啊", "YRC：正文拼接正确");
            const QString qrcText =
                "<QrcInfos><LyricInfo LyricContent=\"[1000,2000]逐(0,500)字(500,500)高(1000,500)亮(1500,500)\"/></QrcInfos>";
            const QVector<SubtitleCue> qq = Subtitles::parseQrc(qrcText);
            check(qq.size() == 1 && qq[0].words.size() == 4 && qq[0].text == "逐字高亮",
                  "QRC：XML/LyricContent 解析出 4 个字级时间戳");
        }

        SubtitleCue cue{ 10.0, 20.0, "t", {} };   // 第 4 个成员 words 显式空初始化
        check(qAbs(Subtitles::lineProgress(cue, 10.0)) < 0.01, "行内进度：起点为 0");
        check(qAbs(Subtitles::lineProgress(cue, 15.0) - 0.5) < 0.01, "行内进度：中点为 0.5");
        check(qAbs(Subtitles::lineProgress(cue, 20.0) - 1.0) < 0.01, "行内进度：终点为 1");
        check(qAbs(Subtitles::lineProgress(cue, 5.0)) < 0.01
                  && qAbs(Subtitles::lineProgress(cue, 30.0) - 1.0) < 0.01, "行内进度：越界自动夹紧");
        SubtitleCue zero{ 5.0, 5.0, "z", {} };
        check(qAbs(Subtitles::lineProgress(zero, 6.0) - 1.0) < 0.01, "行内进度：零时长视为已唱完");
        check(!q.label().isEmpty(), "队列标签非空（形如 第 2/3 首 · 顺序）");
        // 持久化往返：独立对象 + 临时文件（不碰真实队列文件）
        const QString tmp = QDir::tempPath() + "/hov-queue-selftest.json";
        PlayQueue q2;
        q2.setList({ {"x", "X"}, {"y", "Y"} }, 1);
        q2.cycleMode();
        const bool saved = q2.saveToPath(tmp);
        PlayQueue q3;
        const bool loaded = q3.loadFromPath(tmp);
        check(saved && loaded, "队列持久化：保存 + 读取成功");
        check(q3.size() == 2 && q3.index() == 1, "队列持久化：条目数与下标一致");
        check(q3.mode() == PlayQueue::Mode::RepeatOne, "队列持久化：模式一致");
        check(q3.at(1) && q3.at(1)->key == "y" && q3.at(1)->label == "Y", "队列持久化：条目内容一致");
        QFile::remove(tmp);
        Favorites fav;
        Favorites::Fav f; f.id = "netease:1"; f.key = f.id; f.title = "T";
        check(fav.add(f) && fav.contains("netease:1"), "收藏：加入后命中");
        check(!fav.add(f), "收藏：重复加入返回 false");
        check(fav.remove("netease:1") && !fav.contains("netease:1"), "收藏：移除生效");
        // ---- B7 全屏铺满：真 mpv 属性往返 + 门控逻辑 ----
        if (ctx.player && ctx.player->renderReady()) {
            ctx.applyFillMode(true);                       // 自检时窗口非全屏 → 门控应为"不铺满"
            check(std::abs(ctx.player->panscan()) < 0.01, "全屏铺满：非全屏不下发 panscan（保持比例）");
            check(!*ctx.fillApplied, "全屏铺满：状态机记为未铺满");
            // applyFillMode 走异步下发（mpv_set_property_async），与下面的同步 set 可能乱序落盘 →
            // 先等异步 panscan=0 落地再下发 1.0（此前偶发 FAIL 的真因；快照 5Hz 刷新还需轮询回读）。
            QThread::msleep(300);
            ctx.player->setPanscan(1.0);
            auto waitPanscan = [&](double want) {
                for (int i = 0; i < 40; ++i) {
                    if (std::abs(ctx.player->panscan() - want) < 0.01) return true;
                    QThread::msleep(50);
                }
                return false;
            };
            check(waitPanscan(1.0), "全屏铺满：panscan=1 下发并回读成功（真 mpv）");
            ctx.player->setPanscan(0.0);
            check(waitPanscan(0.0), "全屏铺满：panscan=0 恢复保持比例");
            // v1.2.0 视频全屏：进入收起面板/退出还原（对齐 macOS/Android；Linux 原先完全没有）
            ctx.enterFullscreen();
            check(ctx.isFullScreen(), "视频全屏：进入后窗口为全屏");
            check(ctx.results && !ctx.results->isVisible(), "视频全屏：进入后结果面板已收起");
            ctx.exitFullscreen();
            check(!ctx.isFullScreen(), "视频全屏：退出后窗口还原");
            check(ctx.results && ctx.results->isVisible(), "视频全屏：退出后结果面板已恢复");
        }
        // ── 第四阶段扩测：纯函数与配置契约（不触网、不写用户设置、不碰 mpv）──
        {
            // ④ 唯一档位表：控制条/快捷键/设置面板共用的一张表
            const QVector<int> hs = Settings::qualityHeights();
            check(hs.size() == 8, "档位表：共 8 档（含 2160/1440）");
            check(hs.contains(2160) && hs.contains(1440), "档位表：包含 2160 与 1440");
            check(hs.first() == 0 && hs.last() == -1, "档位表：首档=自动、末档=仅音频");
            check(hs.indexOf(2160) < hs.indexOf(1080), "档位表：2160 在 1080 之前（从高到低）");
            check(Settings::qualityLabelFor(2160) == "2160p" && Settings::qualityLabelFor(0) == "自动"
                      && Settings::qualityLabelFor(-1) == "仅音频", "档位标签：与表格一致");
            // 清晰度格式串：只限制视频部分；高档位也要带上限
            const QStringList a2160 = UrlResolver::formatArgsFor(2160);
            check(a2160.size() == 2 && a2160.at(1).contains("height<=2160"), "清晰度：2160 档带上限");
            check(a2160.at(1).contains("+ba/b[height<=2160]") && a2160.at(1).endsWith("/bv*+ba/b"),
                  "清晰度：2160 档音频不设限且有兜底");
            // 来源识别与直链判定（下载/播放都依赖）
            check(UrlResolver::serviceOf("https://www.bilibili.com/video/BV1xx") == "bilibili",
                  "来源识别：B 站");
            check(UrlResolver::serviceOf("https://y.qq.com/n/ryqq/songDetail/xxx") == "qqmusic",
                  "来源识别：QQ 音乐");
            check(UrlResolver::isDirectMedia("https://x.com/a.mp4") && !UrlResolver::isDirectMedia("https://www.bilibili.com/video/BV1"),
                  "直链判定：媒体直链 vs 网页地址");
            // ── UrlResolver 静态纯函数整组（审计 P2-7 ✓ 跨端一致性守卫 —— macOS 端有同款断言 ✓）──
            check(UrlResolver::serviceOf("https://www.youtube.com/watch?v=x") == "youtube", "来源识别：YouTube");
            check(UrlResolver::serviceOf("https://youtu.be/abc") == "youtube", "来源识别：youtu.be 短链");
            check(UrlResolver::serviceOf("https://b23.tv/abc") == "bilibili", "来源识别：b23.tv 短链");
            check(UrlResolver::serviceOf("https://music.163.com/song?id=1") == "netease", "来源识别：网易云");
            check(UrlResolver::serviceOf("https://example.com/x").isEmpty(), "来源识别：未知站点返回空");
            check(UrlResolver::isDirectMedia("/home/u/v.mp4") && UrlResolver::isDirectMedia("file:///tmp/a.mkv"),
                  "直链判定：本地路径与 file://");
            check(UrlResolver::isDirectMedia("https://r1.googlevideo.com/videoplayback?x=1"),
                  "直链判定：googlevideo 无扩展名也认");
            check(UrlResolver::isDirectMedia("https://upos.bilivideo.com/v/x"),
                  "直链判定：bilivideo 无扩展名也认");
            check(UrlResolver::isDirectMedia("https://x/a.ts") && UrlResolver::isDirectMedia("https://x/a.mov")
                      && UrlResolver::isDirectMedia("https://x/a.mpd"),
                  "直链判定：.ts/.mov/.mpd 并集");
            check(!UrlResolver::isDirectMedia("https://space.bilibili.com/1"), "直链判定：普通页面 → 否");
            check(UrlResolver::qualityLabel(-2) == "自动", "清晰度标签：负数也归自动（与 macOS 对齐）");
            check(UrlResolver::configDir().endsWith("/.config/hov"), "配置目录：~/.config/hov");
            // 字幕/歌词解析健壮性
            check(Subtitles::parse(QString(), "lrc").isEmpty(), "字幕解析：空输入返回空");
            check(Subtitles::parseYrc("{}").isEmpty(), "逐字歌词：垃圾输入不崩溃、返回空");
            // 设置校验契约
            check(Settings::validate("video.fillScreen", "true").isEmpty()
                      && Settings::validate("video.fillScreen", "maybe").isEmpty() == false,
                  "设置校验：布尔值严格判定");
            check(Settings::validate("music.qualityCeiling", "lossless").isEmpty()
                      && !Settings::validate("music.qualityCeiling", "hd").isEmpty(),
                  "设置校验：音质三档");
            check(!Settings::validate("video.noSuchKey", "1").isEmpty(), "设置校验：未知键被拒");
            check(ProgressStore::kRecordMin < ProgressStore::kResumeMin && ProgressStore::kEndGuard > 0,
                  "进度阈值：记录 < 续播，且看完守卫为正");
        }
        // ── 玻璃质感（ColorTheme）纯逻辑断言：主色提取 / 磨砂底 / 渐变 ──
        {
            QImage solid(64, 64, QImage::Format_RGB32);
            solid.fill(QColor(30, 90, 200));
            bool ok = false;
            const QColor d = ColorTheme::dominantColor(solid, &ok);
            check(ok, "玻璃：纯色封面取色成功");
            check(qAbs(d.hsvHueF() - QColor(30, 90, 200).hsvHueF()) < 0.06, "玻璃：主色色相与封面一致");
            check(d.hsvSaturationF() >= 0.449 && d.valueF() >= 0.339 && d.valueF() <= 0.781,
                  "玻璃：饱和/明度被夹到观感范围（≥0.45 / 0.34~0.78）");
            QImage gray(64, 64, QImage::Format_RGB32);
            gray.fill(QColor(128, 128, 128));
            bool ok2 = true;
            ColorTheme::dominantColor(gray, &ok2);
            check(!ok2, "玻璃：纯灰封面取色失败 → 回退兜底色");
            check(ColorTheme::neutral() == QColor(0x24, 0x2B, 0x38), "玻璃：兜底色为中性深蓝灰");
            const ColorTheme t = ColorTheme::make(solid);
            check(t.valid() && !t.backdrop().isNull() && !t.gradient().isNull(), "玻璃：主题构造出磨砂底与渐变");
            check(t.gradient().width() == 1 && t.gradient().height() == 256, "玻璃：渐变为 1×256（拉伸铺满用）");
            check(t.tintDark().valueF() < t.tint().valueF(), "玻璃：渐变底部比顶部暗（有纵深）");
            QImage uni(32, 32, QImage::Format_ARGB32_Premultiplied);
            uni.fill(QColor(10, 20, 30));
            const QImage bl = ColorTheme::boxBlur(uni, 4);
            check(bl.pixelColor(16, 16) == QColor(10, 20, 30), "玻璃：模糊均匀色不变（不产生色偏）");
            const ColorTheme same = ColorTheme::make(solid);
            check(same.tint() == t.tint(), "玻璃：同一封面结果稳定（可安全缓存）");
        }
        // 字幕字号基准（与 macOS 同：画面高 3.5%，下限 14px）—— 曾经是 height/16，比 macOS 大近 1.8 倍
        check(SubtitleOverlay::fontPxFor(700, 1.0) == 19, "字幕字号：700px 画面 → 19px（2.8%，向 macOS 观感靠拢）");
        check(SubtitleOverlay::fontPxFor(200, 1.0) == 13, "字幕字号：小画面命中 13px 下限");
        check(SubtitleOverlay::fontPxFor(700, 2.0) == 39, "字幕字号：用户倍数 2.0 生效");
        check(SubtitleOverlay::fontPxFor(700, 0.5) == 13, "字幕字号：缩到 0.5 时仍不低于下限 13px");

        // GPU 兼容层（虚拟化环境自动 --disable-gpu ✗ 防登录窗首帧渲染异常）—— 纯函数部分
        check(GpuCompat::mergeFlags(QString(), "--disable-gpu") == "--disable-gpu",
              "GPU 兼容：空 flags 直接填入");
        check(GpuCompat::mergeFlags("--no-sandbox", "--disable-gpu") == "--no-sandbox --disable-gpu",
              "GPU 兼容：已有 flags 末尾追加");
        check(GpuCompat::mergeFlags("--disable-gpu", "--disable-gpu") == "--disable-gpu",
              "GPU 兼容：重复 token 不叠加");
        check(GpuCompat::mergeFlags("--disable-gpu-compositing", "--disable-gpu") == "--disable-gpu-compositing --disable-gpu",
              "GPU 兼容：前缀相近不误判为已含（逐 token 比较）");

        qInfo() << "队列自检:" << pass << "/" << total << "通过";
        std::cerr << "[SELFTEST] 队列自检: " << pass << "/" << total << " 通过" << std::endl;   // 无人值守（CI/SSH）时也能读到汇总
        ctx.results->addItem(QString("自检结果：%1/%2 通过").arg(pass).arg(total));}
