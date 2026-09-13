#include "Mpris.h"

#include "MpvWidget.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDebug>

namespace {
const char *kObjectPath = "/org/mpris/MediaPlayer2";
const char *kServiceName = "org.mpris.MediaPlayer2.hov";
const char *kPlayerIface = "org.mpris.MediaPlayer2.Player";
const char *kPropsIface = "org.freedesktop.DBus.Properties";
}   // namespace

MprisPlayerAdaptor::MprisPlayerAdaptor(Mpris *m) : QDBusAbstractAdaptor(m), m_(m) {
    setAutoRelaySignals(false);      // 属性变更由 Mpris::emitProps 手工发（协议要求 PropertiesChanged）
    setObjectName("MprisPlayer");
}

Mpris::Mpris(MpvWidget *player, QObject *parent) : QObject(parent), player_(player) {}

bool Mpris::start() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qInfo() << "MPRIS：无会话总线，媒体键/桌面控件不可用（其余功能不受影响）";
        return false;
    }
    if (!bus.registerService(kServiceName)) {
        // 常见原因：已经跑着一个实例（服务名被占）——不影响播放，只是第二实例不接管媒体键
        qInfo() << "MPRIS：服务名注册失败（可能已有实例在跑）:" << bus.lastError().message();
        return false;
    }
    // 两个接口 = 本对象 + 两个 adaptor 子对象，**必须一次性在同一路径注册**
    new MprisPlayerAdaptor(this);
    new MprisRootAdaptor(this);
    if (!bus.registerObject(kObjectPath, this,
                            QDBusConnection::ExportAdaptors | QDBusConnection::ExportAllSlots
                                | QDBusConnection::ExportAllProperties)) {
        qWarning() << "MPRIS：对象注册失败:" << bus.lastError().message();
        return false;
    }
    registered_ = true;
    qInfo() << "MPRIS 已就绪:" << kServiceName << "（媒体键 / playerctl / 桌面锁屏控件可用）";
    return true;
}

void Mpris::emitProps(const QVariantMap &changed) {
    if (!registered_ || changed.isEmpty()) return;
    QDBusMessage sig = QDBusMessage::createSignal(kObjectPath, kPropsIface, "PropertiesChanged");
    sig << QString::fromLatin1(kPlayerIface) << changed << QStringList();
    QDBusConnection::sessionBus().send(sig);
}

QString Mpris::trackId() const {
    // MPRIS 要求 mpris:trackid 是合法的 D-Bus object path
    return QString("/org/hov/track/%1").arg(trackSeq_);
}

void Mpris::updateMeta(const QString &title, const QString &artist, double durSec) {
    title_ = title;
    artist_ = artist;
    dur_ = durSec;
    ++trackSeq_;                       // 每换一首换一个 trackid（桌面控件据此刷新）
    emitProps({ { "Metadata", metadata() } });
    updateStatus();
}

void Mpris::setNavAvailable(bool next, bool prev) {
    if (navNext_ == next && navPrev_ == prev) return;
    navNext_ = next;
    navPrev_ = prev;
    emitProps({ { "CanGoNext", navNext_ }, { "CanGoPrevious", navPrev_ } });
}

void Mpris::updateStatus() {
    const QString s = playbackStatus();
    if (s == status_) return;
    status_ = s;
    emitProps({ { "PlaybackStatus", status_ } });
}

QString Mpris::playbackStatus() const {
    if (!player_ || player_->durationSec() < 1.0) return QStringLiteral("Stopped");
    return player_->paused() ? QStringLiteral("Paused") : QStringLiteral("Playing");
}

QVariantMap Mpris::metadata() const {
    QVariantMap m;
    m.insert("mpris:trackid", QVariant::fromValue(QDBusObjectPath(trackId())));
    if (dur_ > 0.0) m.insert("mpris:length", static_cast<qlonglong>(dur_ * 1e6));
    m.insert("xesam:title", title_);
    if (!artist_.isEmpty()) m.insert("xesam:artist", QStringList{ artist_ });
    return m;
}

qlonglong Mpris::position() const {
    if (!player_) return 0;
    return static_cast<qlonglong>(player_->positionSec() * 1e6);
}

double Mpris::rate() const { return player_ ? player_->speed() : 1.0; }

double Mpris::volume() const { return player_ ? player_->volume() / 100.0 : 1.0; }

void Mpris::setVolume(double v) {
    if (!player_) return;
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    player_->setVolume(static_cast<int>(v * 100.0 + 0.5));
    emitProps({ { "Volume", volume() } });
}

void Mpris::doPlayPause() {
    if (!player_ || player_->durationSec() < 1.0) return;      // 没有内容：什么也不做
    player_->togglePause();
    updateStatus();
}

void Mpris::doPlay() {
    if (!player_) return;
    if (player_->paused() && player_->durationSec() >= 1.0) player_->togglePause();
    updateStatus();
}

void Mpris::doPause() {
    if (!player_) return;
    if (!player_->paused() && player_->durationSec() >= 1.0) player_->togglePause();
    updateStatus();
}

void Mpris::doStop() {
    emit requestStop();     // 真正的"停"交给 MainWindow（顺便落盘进度）
    updateStatus();
}

void Mpris::doNext() { emit requestNext(); }

void Mpris::doPrevious() { emit requestPrevious(); }

void Mpris::doSeek(qlonglong offsetUs) {
    if (!player_ || player_->durationSec() < 1.0) return;
    const double target = player_->positionSec() + static_cast<double>(offsetUs) / 1e6;
    player_->seekTo(target);
    QDBusMessage sig = QDBusMessage::createSignal(kObjectPath, kPlayerIface, "Seeked");
    sig << static_cast<qlonglong>(player_->positionSec() * 1e6);
    QDBusConnection::sessionBus().send(sig);
}

void Mpris::doSetPosition(const QString &trackIdIn, qlonglong posUs) {
    if (!player_ || player_->durationSec() < 1.0) return;
    if (!trackIdIn.isEmpty() && trackIdIn != trackId()) return;   // 不是当前曲目：忽略（协议要求）
    player_->seekTo(static_cast<double>(posUs) / 1e6);
}
