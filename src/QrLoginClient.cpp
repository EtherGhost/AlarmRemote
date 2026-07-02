#include "QrLoginClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSslError>
#include <QUrl>
#include <QUuid>

namespace {
const char *kQrSessionUrl =
    "https://mobile-gw.prod.ajax.systems/"
    "systems.ajax.api.ecosystem.v3.mobilegwsvc.service.stream_qr_session."
    "StreamQrSessionService/execute";

bool readVarint(const QByteArray &data, int &pos, quint64 &value)
{
    value = 0;
    int shift = 0;
    while (pos < data.size() && shift <= 63) {
        const quint8 byte = static_cast<quint8>(data.at(pos++));
        value |= static_cast<quint64>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            return true;
        }
        shift += 7;
    }
    return false;
}

bool readLengthDelimited(const QByteArray &data, int &pos, QByteArray &value)
{
    quint64 length = 0;
    if (!readVarint(data, pos, length)) {
        return false;
    }
    if (length > static_cast<quint64>(data.size() - pos)) {
        return false;
    }
    value = data.mid(pos, static_cast<int>(length));
    pos += static_cast<int>(length);
    return true;
}

bool skipField(const QByteArray &data, int &pos, quint64 wireType)
{
    QByteArray ignored;
    quint64 varint = 0;
    switch (wireType) {
    case 0:
        return readVarint(data, pos, varint);
    case 1:
        pos += 8;
        return pos <= data.size();
    case 2:
        return readLengthDelimited(data, pos, ignored);
    case 5:
        pos += 4;
        return pos <= data.size();
    default:
        return false;
    }
}
}

QrLoginClient::QrLoginClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_running(false)
    , m_completed(false)
    , m_state(QStringLiteral("Not started"))
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(120000);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        fail(QStringLiteral("QR login timed out."));
    });
}

bool QrLoginClient::running() const
{
    return m_running;
}

QString QrLoginClient::state() const
{
    return m_state;
}

void QrLoginClient::start()
{
    cancel();

    m_buffer.clear();
    m_completed = false;
    setRunning(true);
    setState(QStringLiteral("Creating QR session"));

    QNetworkRequest request(QUrl(QString::fromLatin1(kQrSessionUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/grpc"));
    request.setRawHeader("te", "trailers");
    request.setRawHeader("client-device-id", deviceId().toUtf8());
    request.setRawHeader("client-device-model", "Ubuntu Touch");
    request.setRawHeader("client-os", "Android");
    request.setRawHeader("client-version-major", "3.50");
    request.setRawHeader("application-label", "Ajax");
    request.setRawHeader("client-device-type", "MOBILE");
    request.setAttribute(QNetworkRequest::HTTP2AllowedAttribute, true);

    const QByteArray emptyGrpcFrame(5, '\0');
    m_reply = m_network->post(request, emptyGrpcFrame);
    connect(m_reply, &QNetworkReply::readyRead, this, &QrLoginClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &QrLoginClient::onFinished);
    connect(m_reply, QOverload<const QList<QSslError> &>::of(&QNetworkReply::sslErrors),
            this, [this]() {
                fail(QStringLiteral("TLS validation failed."));
            });

    m_timeout.start();
}

void QrLoginClient::cancel()
{
    m_timeout.stop();
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    m_buffer.clear();
    setRunning(false);
}

void QrLoginClient::setRunning(bool running)
{
    if (m_running == running) {
        return;
    }
    m_running = running;
    emit runningChanged();
}

void QrLoginClient::setState(const QString &state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

void QrLoginClient::onReadyRead()
{
    if (!m_reply) {
        return;
    }
    m_buffer.append(m_reply->readAll());
    parseBufferedFrames();
}

void QrLoginClient::onFinished()
{
    if (!m_reply) {
        return;
    }

    if (!m_completed && m_reply->error() != QNetworkReply::OperationCanceledError) {
        const QString message = m_reply->error() == QNetworkReply::NoError
            ? QStringLiteral("QR session ended before login was approved.")
            : QStringLiteral("QR session failed: %1").arg(m_reply->errorString());
        fail(message);
        return;
    }

    m_timeout.stop();
    m_reply->deleteLater();
    m_reply.clear();
    setRunning(false);
}

void QrLoginClient::fail(const QString &message)
{
    m_completed = true;
    setState(QStringLiteral("Failed"));
    emit failed(message);
    cancel();
}

void QrLoginClient::parseBufferedFrames()
{
    while (m_buffer.size() >= 5) {
        const quint8 compressed = static_cast<quint8>(m_buffer.at(0));
        const quint32 length =
            (static_cast<quint8>(m_buffer.at(1)) << 24)
            | (static_cast<quint8>(m_buffer.at(2)) << 16)
            | (static_cast<quint8>(m_buffer.at(3)) << 8)
            | static_cast<quint8>(m_buffer.at(4));

        if (m_buffer.size() < 5 + static_cast<int>(length)) {
            return;
        }

        const QByteArray message = m_buffer.mid(5, static_cast<int>(length));
        m_buffer.remove(0, 5 + static_cast<int>(length));

        if (compressed != 0) {
            fail(QStringLiteral("Compressed QR response is not supported."));
            return;
        }
        parseStreamResponse(message);
    }
}

void QrLoginClient::parseStreamResponse(const QByteArray &message)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            fail(QStringLiteral("Invalid QR response."));
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                fail(QStringLiteral("Invalid QR response payload."));
                return;
            }
            if (field == 1) {
                parseSuccess(payload);
            } else if (field == 2) {
                fail(QStringLiteral("Ajax rejected the QR session."));
                return;
            }
        } else if (!skipField(message, pos, wireType)) {
            fail(QStringLiteral("Unsupported QR response field."));
            return;
        }
    }
}

void QrLoginClient::parseSuccess(const QByteArray &message)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            fail(QStringLiteral("Invalid QR success response."));
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                fail(QStringLiteral("Invalid QR success payload."));
                return;
            }
            if (field == 1) {
                parseInitial(payload);
            } else if (field == 2) {
                parseLoginResponse(payload);
            }
        } else if (!skipField(message, pos, wireType)) {
            fail(QStringLiteral("Unsupported QR success field."));
            return;
        }
    }
}

void QrLoginClient::parseInitial(const QByteArray &message)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            fail(QStringLiteral("Invalid QR initial response."));
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                fail(QStringLiteral("Invalid QR initial payload."));
                return;
            }
            if (field == 1 && !payload.isEmpty()) {
                setState(QStringLiteral("Waiting for approval"));
                emit qrPayloadReady(QString::fromLatin1(payload.toHex()));
            }
        } else if (!skipField(message, pos, wireType)) {
            fail(QStringLiteral("Unsupported QR initial field."));
            return;
        }
    }
}

void QrLoginClient::parseLoginResponse(const QByteArray &message)
{
    int pos = 0;
    QString sessionTokenHex;
    QString userId;
    QString userRole;
    QString userLogin;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            fail(QStringLiteral("Invalid QR login response."));
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                fail(QStringLiteral("Invalid QR login payload."));
                return;
            }
            if (field == 1 && !payload.isEmpty()) {
                sessionTokenHex = QString::fromLatin1(payload.toHex());
            } else if (field == 2) {
                parseLiteAccount(payload, userId, userRole, userLogin);
            }
        } else if (!skipField(message, pos, wireType)) {
            fail(QStringLiteral("Unsupported QR login field."));
            return;
        }
    }
    if (!sessionTokenHex.isEmpty()) {
        m_completed = true;
        m_timeout.stop();
        setState(QStringLiteral("Approved"));
        emit loginSucceeded(sessionTokenHex, userId, userRole, userLogin, sessionCookie());
        if (m_reply) {
            m_reply->abort();
        }
    }
}

void QrLoginClient::parseLiteAccount(const QByteArray &message, QString &userId, QString &userRole, QString &userLogin)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        quint64 value = 0;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return;
            }
            if (field == 1) {
                userId = QString::fromUtf8(payload);
            } else if (field == 8) {
                userLogin = QString::fromUtf8(payload);
            }
        } else if (wireType == 0) {
            if (!readVarint(message, pos, value)) {
                return;
            }
            if (field == 2) {
                userRole = value == 2 ? QStringLiteral("PRO")
                                      : value == 1 ? QStringLiteral("USER")
                                                   : QString();
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

QString QrLoginClient::sessionCookie() const
{
    if (!m_reply) {
        return QString();
    }
    const QList<QNetworkReply::RawHeaderPair> headers = m_reply->rawHeaderPairs();
    for (const QNetworkReply::RawHeaderPair &header : headers) {
        if (QString::fromLatin1(header.first).compare(QStringLiteral("set-cookie"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QString cookie = QString::fromLatin1(header.second);
        const QString firstPart = cookie.section(';', 0, 0).trimmed();
        if (firstPart.startsWith(QStringLiteral("AWSALB="), Qt::CaseInsensitive)
            || firstPart.startsWith(QStringLiteral("AWSALBCORS="), Qt::CaseInsensitive)) {
            return firstPart;
        }
    }
    return QString();
}

QString QrLoginClient::deviceId() const
{
    QSettings settings;
    const QString key = QStringLiteral("ajax/deviceId");
    QString id = settings.value(key).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue(key, id);
    }
    return id;
}
