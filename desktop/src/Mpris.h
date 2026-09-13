#pragma once
#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class MpvWidget;

/**
 * MPRIS：桌面端"后台播放 + 媒体键"的标准通道（org.mpris.MediaPlayer2[.Player]）。
 *
 * 结构说明（踩坑 #64）：QtDBus **一个对象路径只能注册一个 QObject**，
 * 想在同一路径上暴露多个接口，必须用 QDBusAbstractAdaptor 子对象（每个 adaptor 一个接口）。
 * 所以这里：本类持有全部状态与逻辑，两个 adaptor 只做"协议翻译"。
 *
 * 设计：切歌/停止通过信号回抛给 MainWindow，仍走播放队列那一套
 * （保证媒体键与 ⏭ 键/自动续播/CLI 的行为完全一致，逻辑只有一份）。
 *
 * 无会话总线时静默降级：只打一条日志，其余功能不受任何影响。
 */
class Mpris : public QObject {
    Q_OBJECT
public:
    explicit Mpris(MpvWidget *player, QObject *parent = nullptr);

    /** 注册服务与对象（adaptor 在此创建）；返回是否真的挂上了总线 */
    bool start();
    bool registered() const { return registered_; }

    /** 换曲时更新元数据（标题/艺术家/时长秒） */
    void updateMeta(const QString &title, const QString &artist, double durSec);
    /** 播放/暂停状态变化时调（内部去重，不会重复发信号） */
    void updateStatus();
    void setNavAvailable(bool next, bool prev);

    // —— 供 adaptor 转调的实际动作 ——
    void doPlayPause();
    void doPlay();
    void doPause();
    void doStop();
    void doNext();
    void doPrevious();
    void doSeek(qlonglong offsetUs);
    void doSetPosition(const QString &trackId, qlonglong posUs);
    QString playbackStatus() const;
    QVariantMap metadata() const;
    qlonglong position() const;
    double rate() const;
    double volume() const;
    void setVolume(double v);

signals:
    /** 交给 MainWindow：保持"切歌逻辑单一来源"（与 ⏭ 键/自动续播同一套） */
    void requestNext();
    void requestPrevious();
    void requestStop();

private:
    void emitProps(const QVariantMap &changed);
    QString trackId() const;                                // 形如 /org/hov/track/1
    MpvWidget *player_ = nullptr;
    bool registered_ = false;
    bool navNext_ = false;
    bool navPrev_ = false;
    QString title_;
    QString artist_;
    QString status_ = QStringLiteral("Stopped");
    double dur_ = 0.0;
    int trackSeq_ = 1;

    friend class MprisPlayerAdaptor;
    friend class MprisRootAdaptor;
};

/** 播放器接口（媒体键/锁屏控件实际调的那一层） */
class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ PlaybackStatus)
    Q_PROPERTY(QVariantMap Metadata READ Metadata)
    Q_PROPERTY(qlonglong Position READ Position)          // 只读，且协议要求不进 PropertiesChanged
    Q_PROPERTY(double Rate READ Rate)
    Q_PROPERTY(double Volume READ Volume WRITE setVolume)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanControl READ canControl)
public:
    explicit MprisPlayerAdaptor(Mpris *m);
public slots:
    void PlayPause() { m_->doPlayPause(); }
    void Play() { m_->doPlay(); }
    void Pause() { m_->doPause(); }
    void Stop() { m_->doStop(); }
    void Next() { m_->doNext(); }
    void Previous() { m_->doPrevious(); }
    void Seek(qlonglong offsetUs) { m_->doSeek(offsetUs); }
    void SetPosition(const QString &trackId, qlonglong posUs) { m_->doSetPosition(trackId, posUs); }
public:
    QString PlaybackStatus() const { return m_->playbackStatus(); }
    QVariantMap Metadata() const { return m_->metadata(); }
    qlonglong Position() const { return m_->position(); }
    double Rate() const { return m_->rate(); }
    double Volume() const { return m_->volume(); }
    void setVolume(double v) { m_->setVolume(v); }
    bool canSeek() const { return true; }
    bool canPause() const { return true; }
    bool canGoNext() const { return m_->navNext_; }
    bool canGoPrevious() const { return m_->navPrev_; }
    bool canControl() const { return true; }

private:
    Mpris *m_ = nullptr;
};

/** 根接口（没有它 playerctl 不认这个播放器） */
class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)
public:
    explicit MprisRootAdaptor(Mpris *m) : QDBusAbstractAdaptor(m) { setObjectName("MprisRoot"); }
public slots:
    void Raise() {}
    void Quit() {}
public:
    bool canQuit() const { return false; }        // 由用户关窗口，不接受远程退出
    bool canRaise() const { return false; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Hyper Online Video"); }
    QStringList supportedUriSchemes() const
    {
        return { QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("file") };
    }
    QStringList supportedMimeTypes() const { return {}; }
};
