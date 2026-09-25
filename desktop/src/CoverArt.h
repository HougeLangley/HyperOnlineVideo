#pragma once
#include <QImage>
#include <QPixmap>
#include <QString>
#include <memory>

class QPainter;
class QNetworkAccessManager;

/**
 * 专辑封面（桌面端"音乐封面"）
 *
 * 设计：
 *   - 只负责"下载 + 缓存 + 画到左上/右上角"，不掺和播放逻辑
 *   - 内存 + 磁盘双缓存（~/.cache/hov/covers/<md5>.jpg），重绘不重复下载
 *   - 无封面时 paint() 什么都不画（视频播放时 main 会主动 clear）
 *   - 网络失败静默降级（封面没有也能听歌）
 */
class CoverArt {
public:
    CoverArt();
    ~CoverArt();      // 在 .cpp 定义：unique_ptr<QNetworkAccessManager> 需要完整类型

    /** 载入封面（url 为空 = 清除）；标题/艺术家用于封面下方文字 */
    void load(const QString &url, const QString &title, const QString &artist);
    /// 已下载完成的封面图（null = 还没有）；玻璃主题（ColorTheme）据此取色/做磨砂底
    const QImage &image() const { return img_; }
    void clear();
    bool hasImage() const { return !img_.isNull(); }

    /** 在给定区域画封面（圆角 + 边框 + 标题/艺术家）；无图直接返回 */
    void paint(QPainter &p, const QRect &area) const;

private:
    static QString cachePathFor(const QString &url);
    std::unique_ptr<QNetworkAccessManager> net_;   // 自己持有（封面下载与播放器无关）
    QImage img_;
    QString url_;
    QString title_;
    QString artist_;
    // 预缩放 + 预圆角的小图缓存（paint() 为 const ✓ 故 mutable；side 或 url 变化时重算 ✓）
    mutable QImage rounded_;
    mutable QPixmap roundedPix_;   // drawPixmap 走 GL 原生纹理路径（drawImage 实测不显示 ✗）
    mutable int roundedSide_ = 0;
    mutable QString roundedUrl_;
};
