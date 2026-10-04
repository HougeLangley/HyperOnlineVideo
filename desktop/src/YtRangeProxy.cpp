#include "YtRangeProxy.h"
#include "HovLog.h"

#include <QEventLoop>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <algorithm>
#include <functional>

namespace {
constexpr qint64 kChunk = 8 * 1024 * 1024;   // 8 MiB：减少请求次数（android_vr 等客户端连发 1MiB 易触墙）
const char *kUa =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/120.0.0.0 Safari/537.36";

void writeSimple(QTcpSocket *sock, int code, const QByteArray &reason) {
    const QByteArray body = reason;
    QByteArray hdr = "HTTP/1.1 " + QByteArray::number(code) + ' ' + reason + "\r\n"
                     "Content-Type: text/plain; charset=utf-8\r\n"
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                     "Connection: close\r\n\r\n";
    sock->write(hdr);
    sock->write(body);
    sock->flush();
}

struct UpstreamResult {
    int code = 0;
    QByteArray data;
    qint64 totalHint = 0;
};

UpstreamResult fetchRange(QNetworkAccessManager &nam, const QString &url, qint64 start, qint64 end) {
    UpstreamResult r;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) QThread::msleep(400u * uint(attempt));
        QNetworkRequest req{QUrl(url)};
        req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromUtf8(kUa));
        req.setRawHeader("Referer", "https://www.youtube.com/");
        req.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(start) + '-'
                                      + QByteArray::number(end));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = nam.get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        r = UpstreamResult{};
        r.code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r.code == 200 || r.code == 206) r.data = reply->readAll();
        const QByteArray cr = reply->rawHeader("Content-Range");
        const int slash = cr.lastIndexOf('/');
        if (slash > 0) {
            bool ok = false;
            const qint64 t = cr.mid(slash + 1).trimmed().toLongLong(&ok);
            if (ok && t > 0) r.totalHint = t;
        }
        reply->deleteLater();
        if (r.code == 200 || r.code == 206) break;
        if (r.code != 403 && r.code != 429) break;
    }
    return r;
}

bool parseClientRange(const QByteArray &rangeHdr, qint64 total, qint64 *start, qint64 *end, bool *partial) {
    *start = 0;
    *end = total > 0 ? total - 1 : -1;
    *partial = false;
    if (!rangeHdr.startsWith("bytes=") || total <= 0) return true;
    const QByteArray spec = rangeHdr.mid(6);
    const int dash = spec.indexOf('-');
    if (dash < 0) return false;
    const QByteArray a = spec.left(dash).trimmed();
    const QByteArray b = spec.mid(dash + 1).trimmed();
    bool ok = true;
    *start = a.isEmpty() ? 0 : a.toLongLong(&ok);
    if (!ok) return false;
    if (b.isEmpty()) *end = total - 1;
    else {
        *end = b.toLongLong(&ok);
        if (!ok) return false;
    }
    if (*start < 0 || *end < *start || *start >= total) return false;
    *end = std::min(*end, total - 1);
    *partial = true;
    return true;
}

/** 接管 incomingConnection，把 socketDescriptor 交给工作线程（避免 QTcpSocket 跨线程） */
class ProxyServer : public QTcpServer {
public:
    using Handler = std::function<void(qintptr)>;
    explicit ProxyServer(Handler h, QObject *parent = nullptr) : QTcpServer(parent), handler_(std::move(h)) {}
protected:
    void incomingConnection(qintptr socketDescriptor) override {
        QThread *th = QThread::create([h = handler_, socketDescriptor] { h(socketDescriptor); });
        QObject::connect(th, &QThread::finished, th, &QObject::deleteLater);
        th->start();
    }
private:
    Handler handler_;
};
}  // namespace

YtRangeProxy::YtRangeProxy(QObject *parent) : QObject(parent) {}

YtRangeProxy::~YtRangeProxy() {
    if (server_) server_->close();
}

qint64 YtRangeProxy::contentLengthFromUrl(const QString &url) {
    const QUrl u(url);
    const QUrlQuery q(u);
    bool ok = false;
    const qint64 n = q.queryItemValue(QStringLiteral("clen")).toLongLong(&ok);
    return (ok && n > 0) ? n : 0;
}

bool YtRangeProxy::ensureListening() {
    if (server_ && server_->isListening()) return true;
    auto *ps = new ProxyServer([this](qintptr fd) { handleClient(fd); }, this);
    server_ = ps;
    if (!server_->listen(QHostAddress::LocalHost, 0)) {
        hovLog("[YT-PROXY] listen 失败: %s\n", qPrintable(server_->errorString()));
        return false;
    }
    port_ = int(server_->serverPort());
    hovLog("[YT-PROXY] 已监听 127.0.0.1:%d（googlevideo 分块 Range）\n", port_);
    return true;
}

QString YtRangeProxy::map(const QString &upstreamUrl) {
    if (!upstreamUrl.contains(QStringLiteral("googlevideo.com"))) return upstreamUrl;
    if (!ensureListening()) return upstreamUrl;
    const QString token = QUuid::createUuid().toString(QUuid::Id128);
    {
        QMutexLocker lock(&mu_);
        if (tokens_.size() > 32) tokens_.clear();
        tokens_.insert(token, upstreamUrl);
    }
    return QStringLiteral("http://127.0.0.1:%1/s/%2").arg(port_).arg(token);
}

void YtRangeProxy::onNewConnection() {
    // 由 ProxyServer::incomingConnection 处理；保留空实现以满足头文件声明可被链接
}

void YtRangeProxy::handleClient(qintptr socketDescriptor) {
    QTcpSocket sock;
    if (!sock.setSocketDescriptor(socketDescriptor)) return;
    sock.setReadBufferSize(256 * 1024);

    QByteArray reqBuf;
    while (!reqBuf.contains("\r\n\r\n")) {
        if (!sock.waitForReadyRead(15000)) return;
        reqBuf += sock.readAll();
        if (reqBuf.size() > 64 * 1024) return;
    }
    const int hdrEnd = reqBuf.indexOf("\r\n\r\n");
    const QByteArray headerBlock = reqBuf.left(hdrEnd);
    const QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty()) return;
    const QList<QByteArray> reqLine = lines.first().trimmed().split(' ');
    if (reqLine.size() < 2) return;
    const QByteArray method = reqLine.at(0);
    const QByteArray path = reqLine.at(1);
    QByteArray rangeHdr;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.toLower().startsWith("range:"))
            rangeHdr = line.mid(6).trimmed();
    }

    if (!path.startsWith("/s/")) {
        writeSimple(&sock, 404, "Not Found");
        sock.waitForBytesWritten(3000);
        sock.disconnectFromHost();
        return;
    }
    const QByteArray token = path.mid(3).split('?').value(0);
    QString upstream;
    {
        QMutexLocker lock(&mu_);
        upstream = tokens_.value(QString::fromLatin1(token));
    }
    if (upstream.isEmpty()) {
        writeSimple(&sock, 404, "Unknown token");
        sock.waitForBytesWritten(3000);
        sock.disconnectFromHost();
        return;
    }

    QNetworkAccessManager nam;
    qint64 total = contentLengthFromUrl(upstream);
    if (total <= 0) {
        const UpstreamResult probe = fetchRange(nam, upstream, 0, 0);
        if (probe.totalHint > 0) total = probe.totalHint;
        else if (probe.code == 200 || probe.code == 206)
            total = std::max<qint64>(probe.data.size(), 1);
        else {
            hovLog("[YT-PROXY] 上游探针失败 HTTP %d\n", probe.code);
            writeSimple(&sock, probe.code > 0 ? probe.code : 502, "Upstream probe failed");
            sock.waitForBytesWritten(3000);
            sock.disconnectFromHost();
            return;
        }
    }

    qint64 start = 0, end = total - 1;
    bool partial = false;
    if (!parseClientRange(rangeHdr, total, &start, &end, &partial)) {
        writeSimple(&sock, 416, "Range Not Satisfiable");
        sock.waitForBytesWritten(3000);
        sock.disconnectFromHost();
        return;
    }
    const qint64 length = end - start + 1;
    const bool headOnly = (method == "HEAD");

    QByteArray resp;
    resp += partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
    resp += "Content-Type: application/octet-stream\r\n";
    resp += "Accept-Ranges: bytes\r\n";
    resp += "Content-Length: " + QByteArray::number(length) + "\r\n";
    if (partial) {
        resp += "Content-Range: bytes " + QByteArray::number(start) + '-'
                + QByteArray::number(end) + '/' + QByteArray::number(total) + "\r\n";
    }
    resp += "Connection: close\r\n\r\n";
    sock.write(resp);
    sock.flush();
    if (headOnly) {
        sock.waitForBytesWritten(3000);
        sock.disconnectFromHost();
        return;
    }

    qint64 pos = start;
    while (pos <= end) {
        if (sock.state() != QAbstractSocket::ConnectedState) break;
        const qint64 chunkEnd = std::min(pos + kChunk - 1, end);
        const UpstreamResult chunk = fetchRange(nam, upstream, pos, chunkEnd);
        if ((chunk.code != 200 && chunk.code != 206) || chunk.data.isEmpty()) {
            hovLog("[YT-PROXY] 分块失败 pos=%lld HTTP %d bytes=%d\n",
                   static_cast<long long>(pos), chunk.code, int(chunk.data.size()));
            break;
        }
        qint64 off = 0;
        while (off < chunk.data.size()) {
            if (sock.state() != QAbstractSocket::ConnectedState) break;
            const qint64 n = sock.write(chunk.data.constData() + off, chunk.data.size() - off);
            if (n < 0) break;
            if (n == 0) {
                if (!sock.waitForBytesWritten(10000)) break;
                continue;
            }
            off += n;
            sock.waitForBytesWritten(10000);
        }
        if (off < chunk.data.size()) break;
        pos += chunk.data.size();
    }
    sock.disconnectFromHost();
    if (sock.state() != QAbstractSocket::UnconnectedState)
        sock.waitForDisconnected(3000);
}
