#include "AjaxApiClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

namespace {
const char *kGateway = "https://mobile-gw.prod.ajax.systems/";

QByteArray varint(quint64 value)
{
    QByteArray out;
    while (value >= 0x80) {
        out.append(static_cast<char>((value & 0x7f) | 0x80));
        value >>= 7;
    }
    out.append(static_cast<char>(value));
    return out;
}

QByteArray fieldVarint(int field, quint64 value)
{
    return varint((static_cast<quint64>(field) << 3) | 0) + varint(value);
}

QByteArray fieldString(int field, const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return varint((static_cast<quint64>(field) << 3) | 2) + varint(bytes.size()) + bytes;
}

QByteArray fieldMessage(int field, const QByteArray &value)
{
    return varint((static_cast<quint64>(field) << 3) | 2) + varint(value.size()) + value;
}

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
    if (!readVarint(data, pos, length) || length > static_cast<quint64>(data.size() - pos)) {
        return false;
    }
    value = data.mid(pos, static_cast<int>(length));
    pos += static_cast<int>(length);
    return true;
}

bool skipField(const QByteArray &data, int &pos, quint64 wireType)
{
    QByteArray ignored;
    quint64 ignoredVarint = 0;
    switch (wireType) {
    case 0:
        return readVarint(data, pos, ignoredVarint);
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

QString parseLiteSpaceName(const QByteArray &profile)
{
    int pos = 0;
    while (pos < profile.size()) {
        quint64 key = 0;
        if (!readVarint(profile, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(profile, pos, payload)) {
                return QString();
            }
            if (field == 1) {
                return QString::fromUtf8(payload);
            }
        } else if (!skipField(profile, pos, wireType)) {
            return QString();
        }
    }
    return QString();
}

struct LiteSpace {
    QString id;
    QString name;
    int state = 0;
};

int parseSecurityStateV2(const QByteArray &message)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return 0;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        if (!skipField(message, pos, wireType)) {
            return 0;
        }
        if (field >= 1 && field <= 8) {
            return static_cast<int>(field);
        }
    }
    return 0;
}

LiteSpace parseLiteSpace(const QByteArray &message)
{
    LiteSpace space;
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return space;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        quint64 value = 0;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return space;
            }
            if (field == 1) {
                space.id = QString::fromUtf8(payload);
            } else if (field == 2) {
                space.name = parseLiteSpaceName(payload);
            } else if (field == 4) {
                const int stateV2 = parseSecurityStateV2(payload);
                if (stateV2 != 0) {
                    space.state = stateV2;
                }
            }
        } else if (wireType == 0) {
            if (!readVarint(message, pos, value)) {
                return space;
            }
            if (field == 3 && space.state == 0) {
                space.state = static_cast<int>(value);
            }
        } else if (!skipField(message, pos, wireType)) {
            return space;
        }
    }
    return space;
}

QString modeForState(int state)
{
    switch (state) {
    case 1:
        return QStringLiteral("Armed");
    case 2:
        return QStringLiteral("Disarmed");
    case 3:
        return QStringLiteral("Night mode");
    case 4:
        return QStringLiteral("Partially armed");
    case 5:
        return QStringLiteral("Arming");
    case 6:
        return QStringLiteral("Awaiting confirmation");
    case 7:
        return QStringLiteral("Arming incomplete");
    case 8:
        return QStringLiteral("Arming");
    default:
        return QStringLiteral("Unknown");
    }
}

QString headerUserRole(const QString &role)
{
    if (role == QStringLiteral("USER_ROLE_USER")) {
        return QStringLiteral("USER");
    }
    if (role == QStringLiteral("USER_ROLE_PRO")) {
        return QStringLiteral("PRO");
    }
    return role;
}
}

AjaxApiClient::AjaxApiClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_busy(false)
    , m_operation(Operation::None)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        fail(QStringLiteral("Ajax API request timed out."));
    });
}

bool AjaxApiClient::busy() const
{
    return m_busy;
}

void AjaxApiClient::setSession(const QString &sessionTokenHex, const QString &userId,
                               const QString &userRole, const QString &userLogin,
                               const QString &sessionCookie)
{
    m_sessionTokenHex = sessionTokenHex;
    m_userId = userId;
    m_userRole = userRole;
    m_userLogin = userLogin;
    m_sessionCookie = sessionCookie;
}

void AjaxApiClient::discoverSpaces()
{
    callUnary(Operation::DiscoverSpaces,
              QStringLiteral("systems.ajax.api.ecosystem.v3.mobilegwsvc.service.find_user_spaces_with_pagination.FindUserSpacesWithPaginationService/execute"),
              makeFindSpacesRequest());
}

void AjaxApiClient::arm(const QString &spaceId)
{
    callUnary(Operation::Arm,
              QStringLiteral("systems.ajax.api.mobile.v2.space.security.SpaceSecurityService/arm"),
              makeSpaceCommandRequest(spaceId, true));
}

void AjaxApiClient::disarm(const QString &spaceId)
{
    callUnary(Operation::Disarm,
              QStringLiteral("systems.ajax.api.mobile.v2.space.security.SpaceSecurityService/disarm"),
              makeSpaceCommandRequest(spaceId, false));
}

void AjaxApiClient::enableNightMode(const QString &spaceId)
{
    callUnary(Operation::Night,
              QStringLiteral("systems.ajax.api.mobile.v2.space.security.SpaceSecurityService/armToNightMode"),
              makeSpaceCommandRequest(spaceId, true));
}

void AjaxApiClient::triggerPanic(const QString &spaceId)
{
    callUnary(Operation::Panic,
              QStringLiteral("systems.ajax.api.mobile.v2.space.SpaceService/pressPanicButton"),
              makePanicRequest(spaceId));
}

void AjaxApiClient::cancel()
{
    m_timeout.stop();
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    m_operation = Operation::None;
    setBusy(false);
}

void AjaxApiClient::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void AjaxApiClient::callUnary(Operation operation, const QString &path, const QByteArray &protobuf)
{
    if (m_sessionTokenHex.isEmpty()) {
        fail(QStringLiteral("No Ajax session token is available."));
        return;
    }

    cancel();
    m_operation = operation;
    setBusy(true);

    QNetworkRequest request(QUrl(QString::fromLatin1(kGateway) + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/grpc"));
    request.setRawHeader("accept", "application/grpc");
    request.setRawHeader("te", "trailers");
    request.setAttribute(QNetworkRequest::HTTP2AllowedAttribute, true);
    addAuthHeaders(request);

    m_reply = m_network->post(request, makeGrpcFrame(protobuf));
    connect(m_reply, &QNetworkReply::finished, this, &AjaxApiClient::onFinished);
    m_timeout.start();
}

void AjaxApiClient::onFinished()
{
    if (!m_reply) {
        return;
    }
    const Operation operation = m_operation;
    const QByteArray body = m_reply->readAll();
    const QNetworkReply::NetworkError error = m_reply->error();
    const QString errorString = m_reply->errorString();
    m_lastResponseDetail = responseDetail(m_reply, body.size());
    m_reply->deleteLater();
    m_reply.clear();
    m_timeout.stop();
    m_operation = Operation::None;
    setBusy(false);

    if (error != QNetworkReply::NoError) {
        emit failed(QStringLiteral("Ajax API request failed: %1").arg(errorString));
        return;
    }
    parseResponse(operation, body);
}

void AjaxApiClient::fail(const QString &message)
{
    cancel();
    emit failed(message);
}

void AjaxApiClient::parseResponse(Operation operation, const QByteArray &body)
{
    int pos = 0;
    while (pos + 5 <= body.size()) {
        const quint8 compressed = static_cast<quint8>(body.at(pos));
        const quint32 length =
            (static_cast<quint8>(body.at(pos + 1)) << 24)
            | (static_cast<quint8>(body.at(pos + 2)) << 16)
            | (static_cast<quint8>(body.at(pos + 3)) << 8)
            | static_cast<quint8>(body.at(pos + 4));
        pos += 5;
        if (body.size() - pos < static_cast<int>(length)) {
            emit failed(QStringLiteral("Ajax API returned an incomplete gRPC frame."));
            return;
        }
        const QByteArray message = body.mid(pos, static_cast<int>(length));
        pos += static_cast<int>(length);
        if (compressed != 0) {
            emit failed(QStringLiteral("Compressed Ajax API response is not supported."));
            return;
        }
        if (operation == Operation::DiscoverSpaces) {
            parseDiscoverSpacesResponse(message);
        } else if (responseHasFailure(message)) {
            emit failed(QStringLiteral("Ajax rejected the command."));
        } else {
            finishCommand(operation);
        }
        return;
    }
    if (operation == Operation::DiscoverSpaces) {
        emit failed(QStringLiteral("Ajax returned no spaces response. %1").arg(m_lastResponseDetail));
    } else {
        finishCommand(operation);
    }
}

QString AjaxApiClient::responseDetail(QNetworkReply *reply, int bodySize) const
{
    const QVariant statusAttribute = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int httpStatus = statusAttribute.isValid() ? statusAttribute.toInt() : 0;
    QStringList parts;
    if (httpStatus > 0) {
        parts << QStringLiteral("HTTP %1").arg(httpStatus);
    }
    const QByteArray grpcStatus = reply->rawHeader("grpc-status");
    if (!grpcStatus.isEmpty()) {
        parts << QStringLiteral("grpc-status %1").arg(QString::fromLatin1(grpcStatus));
    }
    const QByteArray grpcMessage = reply->rawHeader("grpc-message");
    if (!grpcMessage.isEmpty()) {
        parts << QStringLiteral("grpc-message %1").arg(QUrl::fromPercentEncoding(grpcMessage));
    }
    parts << QStringLiteral("body %1 bytes").arg(bodySize);
    return QStringLiteral("(%1)").arg(parts.join(QStringLiteral(", ")));
}

void AjaxApiClient::parseDiscoverSpacesResponse(const QByteArray &message)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            emit failed(QStringLiteral("Invalid spaces response."));
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                emit failed(QStringLiteral("Invalid spaces payload."));
                return;
            }
            if (field == 1) {
                int successPos = 0;
                int count = 0;
                LiteSpace selected;
                QVariantList spaces;
                while (successPos < payload.size()) {
                    quint64 successKey = 0;
                    if (!readVarint(payload, successPos, successKey)) {
                        break;
                    }
                    const quint64 successField = successKey >> 3;
                    const quint64 successWireType = successKey & 0x07;
                    QByteArray spacePayload;
                    if (successWireType == 2 && readLengthDelimited(payload, successPos, spacePayload)) {
                        if (successField == 1) {
                            const LiteSpace candidate = parseLiteSpace(spacePayload);
                            if (!candidate.id.isEmpty()) {
                                count++;
                                QVariantMap item;
                                item.insert(QStringLiteral("id"), candidate.id);
                                item.insert(QStringLiteral("name"), candidate.name.isEmpty() ? QStringLiteral("Ajax space") : candidate.name);
                                item.insert(QStringLiteral("mode"), modeForState(candidate.state));
                                item.insert(QStringLiteral("state"), candidate.state);
                                spaces.append(item);
                                if (selected.id.isEmpty()) {
                                    selected = candidate;
                                }
                            }
                        }
                    } else if (!skipField(payload, successPos, successWireType)) {
                        break;
                    }
                }
                if (selected.id.isEmpty()) {
                    emit failed(QStringLiteral("No Ajax space was found for this account."));
                    return;
                }
                emit spacesListed(spaces);
                emit spacesDiscovered(selected.id,
                                      selected.name.isEmpty() ? QStringLiteral("Ajax space") : selected.name,
                                      count);
                return;
            }
            if (field == 2) {
                emit failed(QStringLiteral("Ajax rejected space discovery."));
                return;
            }
        } else if (!skipField(message, pos, wireType)) {
            emit failed(QStringLiteral("Unsupported spaces response field."));
            return;
        }
    }
    emit failed(QStringLiteral("No Ajax space was found for this account."));
}

bool AjaxApiClient::responseHasFailure(const QByteArray &message) const
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return true;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        if (field == 2) {
            return true;
        }
        if (!skipField(message, pos, wireType)) {
            return true;
        }
    }
    return false;
}

void AjaxApiClient::finishCommand(Operation operation)
{
    switch (operation) {
    case Operation::Arm:
        emit commandSucceeded(QStringLiteral("Arm"), QStringLiteral("Armed"));
        break;
    case Operation::Disarm:
        emit commandSucceeded(QStringLiteral("Disarm"), QStringLiteral("Disarmed"));
        break;
    case Operation::Night:
        emit commandSucceeded(QStringLiteral("Night mode"), QStringLiteral("Night mode"));
        break;
    case Operation::Panic:
        emit commandSucceeded(QStringLiteral("Panic"), QString());
        break;
    default:
        break;
    }
}

QByteArray AjaxApiClient::makeFindSpacesRequest() const
{
    return fieldVarint(1, 10);
}

QByteArray AjaxApiClient::makeSpaceCommandRequest(const QString &spaceId, bool includeIgnoreAlarms) const
{
    QByteArray request = fieldMessage(1, makeSpaceLocator(spaceId));
    if (includeIgnoreAlarms) {
        request += fieldVarint(2, 0);
    }
    return request;
}

QByteArray AjaxApiClient::makePanicRequest(const QString &spaceId) const
{
    return fieldMessage(1, makeSpaceLocator(spaceId));
}

QByteArray AjaxApiClient::makeSpaceLocator(const QString &spaceId) const
{
    return fieldString(1, spaceId);
}

QByteArray AjaxApiClient::makeGrpcFrame(const QByteArray &protobuf) const
{
    QByteArray frame;
    frame.append('\0');
    frame.append(static_cast<char>((protobuf.size() >> 24) & 0xff));
    frame.append(static_cast<char>((protobuf.size() >> 16) & 0xff));
    frame.append(static_cast<char>((protobuf.size() >> 8) & 0xff));
    frame.append(static_cast<char>(protobuf.size() & 0xff));
    frame.append(protobuf);
    return frame;
}

QString AjaxApiClient::deviceId() const
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

void AjaxApiClient::addAuthHeaders(QNetworkRequest &request) const
{
    request.setRawHeader("client-device-id", deviceId().toUtf8());
    request.setRawHeader("client-device-model", "Ubuntu Touch");
    request.setRawHeader("client-os", "Android");
    request.setRawHeader("client-version-major", "3.50");
    request.setRawHeader("application-label", "Ajax");
    request.setRawHeader("client-device-type", "MOBILE");
    request.setRawHeader("client-session-token", m_sessionTokenHex.toUtf8());
    if (!m_sessionCookie.isEmpty()) {
        request.setRawHeader("cookie", m_sessionCookie.toUtf8());
    }
    if (!m_userId.isEmpty()) {
        request.setRawHeader("a911-user-id", m_userId.toUtf8());
    }
    if (!m_userRole.isEmpty()) {
        request.setRawHeader("client-user-role", headerUserRole(m_userRole).toUtf8());
    }
    if (!m_userLogin.isEmpty()) {
        request.setRawHeader("client-user-login", m_userLogin.toUtf8());
    }
}
