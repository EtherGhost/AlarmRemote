#include "AjaxApiClient.h"

#include <QNetworkAccessManager>
#include <QDebug>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSet>
#include <QDateTime>
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

struct RoomInfo {
    QString id;
    QString name;
};

struct SourceInfo {
    QString id;
    QString name;
    QString roomId;
    QString roomName;
    QString imageUrl;
    int type = 0;
};

struct NotificationContentInfo {
    QString kind;
    SourceInfo source;
    int tagField = 0;
    int transitionField = 0;
    int caseValue = 0;
};

QString imageUrlFromInfo(const QByteArray &message)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QString();
            }
            if (field == 1) {
                return QString::fromUtf8(payload);
            }
        } else if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    return QString();
}

QString normalizedGatewayUrl(const QString &value)
{
    if (value.isEmpty()
            || value.startsWith(QStringLiteral("http://"))
            || value.startsWith(QStringLiteral("https://"))) {
        return value;
    }

    const QString gateway = QString::fromLatin1(kGateway);
    if (value.startsWith(QLatin1Char('/'))) {
        return gateway.left(gateway.length() - 1) + value;
    }
    return gateway + value;
}

SourceInfo parseNotificationSource(const QByteArray &message, bool hubSource)
{
    SourceInfo source;
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return source;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        quint64 value = 0;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return source;
            }
            if (field == 2) {
                source.id = QString::fromUtf8(payload);
            } else if (field == 3) {
                source.name = QString::fromUtf8(payload);
            } else if ((!hubSource && field == 4) || (hubSource && field == 21)) {
                source.roomId = QString::fromUtf8(payload);
            } else if ((!hubSource && field == 5) || (hubSource && field == 22)) {
                source.roomName = QString::fromUtf8(payload);
            } else if (field == 10) {
                source.imageUrl = normalizedGatewayUrl(imageUrlFromInfo(payload));
            }
        } else if (wireType == 0) {
            if (!readVarint(message, pos, value)) {
                return source;
            }
            if (field == 1) {
                source.type = static_cast<int>(value);
            }
        } else if (!skipField(message, pos, wireType)) {
            return source;
        }
    }
    return source;
}

int parseHubEventTagField(const QByteArray &message)
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
        if (field > 0) {
            return static_cast<int>(field);
        }
    }
    return 0;
}

int parseEventTransitionField(const QByteArray &message)
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
        if (field > 0) {
            return static_cast<int>(field);
        }
    }
    return 0;
}

void parseEventQualifier(const QByteArray &message, NotificationContentInfo &info)
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
        if (wireType == 2 && readLengthDelimited(message, pos, payload)) {
            if (field == 1) {
                info.tagField = parseHubEventTagField(payload);
            } else if (field == 2) {
                info.transitionField = parseEventTransitionField(payload);
            }
        } else if (wireType == 0 && readVarint(message, pos, value)) {
            if (field == 9) {
                info.caseValue = static_cast<int>(value);
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

NotificationContentInfo parseHubNotificationContent(const QByteArray &message)
{
    NotificationContentInfo info;
    info.kind = QStringLiteral("Hub");
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return info;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return info;
            }
            if (field == 2) {
                parseEventQualifier(payload, info);
            } else if (field == 3) {
                info.source = parseNotificationSource(payload, true);
            }
        } else if (!skipField(message, pos, wireType)) {
            return info;
        }
    }
    return info;
}

NotificationContentInfo parseSpaceNotificationContent(const QByteArray &message)
{
    NotificationContentInfo info;
    info.kind = QStringLiteral("Space");
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return info;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return info;
            }
            if (field == 1) {
                parseEventQualifier(payload, info);
            } else if (field == 2) {
                info.source = parseNotificationSource(payload, false);
            } else if (field == 3) {
                info.source = parseNotificationSource(payload, true);
            }
        } else if (!skipField(message, pos, wireType)) {
            return info;
        }
    }
    return info;
}

QString folderCategory(int folder)
{
    switch (folder) {
    case 1:
        return QStringLiteral("alarm");
    case 2:
        return QStringLiteral("malfunction");
    case 3:
    case 4:
    case 5:
        return QStringLiteral("arming");
    default:
        return QStringLiteral("all");
    }
}

QString folderLabel(int folder)
{
    switch (folder) {
    case 1:
        return QStringLiteral("Alarm");
    case 2:
        return QStringLiteral("Malfunction");
    case 3:
        return QStringLiteral("User automation");
    case 4:
        return QStringLiteral("Home automation");
    case 5:
        return QStringLiteral("System");
    case 6:
        return QStringLiteral("Video");
    default:
        return QStringLiteral("Ajax");
    }
}

QString messageTitleForFolder(int folder)
{
    switch (folder) {
    case 1:
        return QStringLiteral("Alarm event");
    case 2:
        return QStringLiteral("Malfunction");
    case 3:
    case 4:
    case 5:
        return QStringLiteral("Arming mode");
    case 6:
        return QStringLiteral("Video event");
    default:
        return QStringLiteral("Ajax message");
    }
}

QString notificationSentence(int folder, int tagField, const QString &sourceName)
{
    const QString actor = sourceName.isEmpty() ? QStringLiteral("Ajax") : sourceName;
    switch (tagField) {
    case 1:
        if (folder == 3) {
            return QStringLiteral("Tillkopplad av %1").arg(actor);
        }
        return QStringLiteral("Tillkopplingsläge ändrat av %1").arg(actor);
    case 2:
        if (folder == 3) {
            return QStringLiteral("Frånkopplad av %1").arg(actor);
        }
        return QStringLiteral("Tillkopplingsläge ändrat av %1").arg(actor);
    case 3:
        if (folder == 3) {
            return QStringLiteral("Nattläge tillkopplat av %1").arg(actor);
        }
        return QStringLiteral("Tillkopplingsläge ändrat av %1").arg(actor);
    case 4:
        if (folder == 3) {
            return QStringLiteral("Nattläge frånkopplat av %1").arg(actor);
        }
        return QStringLiteral("Tillkopplingsläge ändrat av %1").arg(actor);
    case 83:
    case 90:
        return QStringLiteral("Tillkopplad av %1").arg(actor);
    case 87:
    case 93:
        return QStringLiteral("Frånkopplad av %1").arg(actor);
    case 95:
        return QStringLiteral("Nattläge frånkopplat av %1").arg(actor);
    case 96:
        return QStringLiteral("Nattläge tillkopplat av %1").arg(actor);
    case 99:
        return QStringLiteral("%1 tryckte på panikknappen").arg(actor);
    default:
        if (folder == 1) {
            return QStringLiteral("Larm från %1").arg(actor);
        }
        if (folder == 2) {
            return QStringLiteral("Funktionsstörning: %1").arg(actor);
        }
        if (folder == 3 || folder == 4 || folder == 5) {
            return QStringLiteral("Tillkopplingsläge ändrat av %1").arg(actor);
        }
        return actor;
    }
}

QString contentKind(int field)
{
    switch (field) {
    case 1:
        return QStringLiteral("Hub");
    case 2:
        return QStringLiteral("Space");
    case 3:
        return QStringLiteral("Video");
    case 4:
        return QStringLiteral("Smart lock");
    case 5:
        return QStringLiteral("Company");
    case 6:
        return QStringLiteral("Accounting");
    case 7:
        return QStringLiteral("Ajax");
    default:
        return QStringLiteral("Ajax");
    }
}

QString yesNo(bool value)
{
    return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

QString signalLevelLabel(int value)
{
    switch (value) {
    case 1:
        return QStringLiteral("No signal");
    case 2:
        return QStringLiteral("Weak");
    case 3:
        return QStringLiteral("Normal");
    case 4:
        return QStringLiteral("Strong");
    case 5:
        return QStringLiteral("Disconnected");
    default:
        return QStringLiteral("Unknown");
    }
}

int signalBarsForLevel(int value)
{
    switch (value) {
    case 1:
    case 5:
        return 0;
    case 2:
        return 2;
    case 3:
        return 3;
    case 4:
        return 4;
    default:
        return -1;
    }
}

QString batteryStateLabel(int value)
{
    switch (value) {
    case 1:
        return QStringLiteral("OK");
    case 2:
        return QStringLiteral("Error");
    case 3:
        return QStringLiteral("Warning");
    case 4:
        return QStringLiteral("Alert");
    case 5:
        return QStringLiteral("EN54 warning");
    default:
        return QStringLiteral("Unknown");
    }
}

QString lightDeviceStateLabel(int value)
{
    switch (value) {
    case 1:
        return QStringLiteral("Locked");
    case 2:
        return QStringLiteral("Suspended");
    case 3:
        return QStringLiteral("Adding failed");
    case 4:
        return QStringLiteral("Transferring failed");
    case 5:
        return QStringLiteral("Adding");
    case 6:
        return QStringLiteral("Transferring");
    case 7:
        return QStringLiteral("Battery saving");
    case 8:
        return QStringLiteral("Not migrated");
    case 9:
        return QStringLiteral("Offline");
    case 10:
        return QStringLiteral("Updating");
    case 11:
        return QStringLiteral("Walk test");
    default:
        return QStringLiteral("Normal");
    }
}

qint64 parseTimestampSeconds(const QByteArray &message)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return 0;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        quint64 value = 0;
        if (wireType == 0) {
            if (!readVarint(message, pos, value)) {
                return 0;
            }
            if (field == 1) {
                return static_cast<qint64>(value);
            }
        } else if (!skipField(message, pos, wireType)) {
            return 0;
        }
    }
    return 0;
}

struct NotificationInfo {
    QString id;
    qint64 timestamp = 0;
    int folder = 0;
    int importance = 0;
    NotificationContentInfo content;
};

NotificationInfo parseNotification(const QByteArray &message)
{
    NotificationInfo info;
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return info;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        quint64 value = 0;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return info;
            }
            if (field == 1) {
                info.id = QString::fromUtf8(payload);
            } else if (field == 4) {
                info.timestamp = parseTimestampSeconds(payload);
            } else if (field == 7) {
                int contentPos = 0;
                while (contentPos < payload.size()) {
                    quint64 contentKey = 0;
                    if (!readVarint(payload, contentPos, contentKey)) {
                        break;
                    }
                    const quint64 contentField = contentKey >> 3;
                    const quint64 contentWireType = contentKey & 0x07;
                    QByteArray contentPayload;
                    if (contentWireType == 2 && readLengthDelimited(payload, contentPos, contentPayload)) {
                        if (contentField == 1) {
                            info.content = parseHubNotificationContent(contentPayload);
                        } else if (contentField == 2) {
                            info.content = parseSpaceNotificationContent(contentPayload);
                        } else {
                            info.content.kind = contentKind(static_cast<int>(contentField));
                        }
                    } else if (!skipField(payload, contentPos, contentWireType)) {
                        break;
                    }
                }
            }
        } else if (wireType == 0) {
            if (!readVarint(message, pos, value)) {
                return info;
            }
            if (field == 5) {
                info.folder = static_cast<int>(value);
            } else if (field == 6) {
                info.importance = static_cast<int>(value);
            }
        } else if (!skipField(message, pos, wireType)) {
            return info;
        }
    }
    return info;
}

RoomInfo parseRoom(const QByteArray &message)
{
    RoomInfo room;
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return room;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return room;
            }
            if (field == 1) {
                room.id = QString::fromUtf8(payload);
            } else if (field == 2) {
                room.name = QString::fromUtf8(payload);
            }
        } else if (!skipField(message, pos, wireType)) {
            return room;
        }
    }
    return room;
}

void addRoomToMap(const RoomInfo &room, QMap<QString, QString> &rooms)
{
    if (room.id.isEmpty()) {
        return;
    }
    rooms.insert(room.id, room.name.isEmpty() ? QStringLiteral("Room %1").arg(room.id.left(6)) : room.name);
}

void collectRoomsFromSpaceSnapshot(const QByteArray &message, QMap<QString, QString> &rooms)
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
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return;
            }
            if (field == 6) {
                addRoomToMap(parseRoom(payload), rooms);
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

QVariantMap parseStandaloneDeviceDetails(const QByteArray &message)
{
    QVariantMap device;
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return device;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return device;
            }
            if (field == 21) {
                device.insert(QStringLiteral("sortingKey"), QString::fromUtf8(payload));
                continue;
            }

            QString kind;
            if (field == 1) {
                kind = QStringLiteral("Hub");
            } else if (field == 2) {
                kind = QStringLiteral("Video Edge");
            } else if (field == 3) {
                kind = QStringLiteral("Passive Hub");
            } else if (field == 4) {
                kind = QStringLiteral("Smart Lock");
            } else if (field == 5) {
                kind = QStringLiteral("Vaelsys Camera");
            }
            if (kind.isEmpty()) {
                continue;
            }

            device.insert(QStringLiteral("kind"), kind);
            int payloadPos = 0;
            int channels = 0;
            while (payloadPos < payload.size()) {
                quint64 payloadKey = 0;
                if (!readVarint(payload, payloadPos, payloadKey)) {
                    break;
                }
                const quint64 payloadField = payloadKey >> 3;
                const quint64 payloadWireType = payloadKey & 0x07;
                QByteArray valuePayload;
                quint64 value = 0;
                if (payloadWireType == 2) {
                    if (!readLengthDelimited(payload, payloadPos, valuePayload)) {
                        break;
                    }
                    if (payloadField == 1) {
                        device.insert(QStringLiteral("id"), QString::fromUtf8(valuePayload));
                    } else if (payloadField == 2) {
                        device.insert(QStringLiteral("roomId"), QString::fromUtf8(valuePayload));
                    } else if (field == 5 && payloadField == 3) {
                        device.insert(QStringLiteral("name"), QString::fromUtf8(valuePayload));
                    } else if (field == 2 && payloadField == 5) {
                        channels++;
                    }
                } else if (payloadWireType == 0) {
                    if (!readVarint(payload, payloadPos, value)) {
                        break;
                    }
                    if (field == 1) {
                        if (payloadField == 3) {
                            device.insert(QStringLiteral("serviceState"), static_cast<int>(value));
                        } else if (payloadField == 4) {
                            device.insert(QStringLiteral("deviceColor"), static_cast<int>(value));
                        } else if (payloadField == 5) {
                            device.insert(QStringLiteral("boxType"), static_cast<int>(value));
                        } else if (payloadField == 8) {
                            device.insert(QStringLiteral("chimeStatus"), static_cast<int>(value));
                        } else if (payloadField == 9) {
                            device.insert(QStringLiteral("restoreRequired"), static_cast<int>(value));
                        }
                    } else if (field == 2) {
                        if (payloadField == 3) {
                            device.insert(QStringLiteral("nonZombieChannelsCount"), static_cast<int>(value));
                        } else if (payloadField == 4) {
                            device.insert(QStringLiteral("videoEdgeType"), static_cast<int>(value));
                        } else if (payloadField == 6) {
                            device.insert(QStringLiteral("serviceState"), static_cast<int>(value));
                        }
                    }
                } else if (!skipField(payload, payloadPos, payloadWireType)) {
                    break;
                }
            }
            if (channels > 0) {
                device.insert(QStringLiteral("channels"), channels);
            }
        } else if (!skipField(message, pos, wireType)) {
            return device;
        }
    }
    return device;
}

void collectDevicesFromSpaceSnapshot(const QByteArray &message, QVariantList &devices)
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
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return;
            }
            if (field == 4) {
                const QVariantMap device = parseStandaloneDeviceDetails(payload);
                if (!device.value(QStringLiteral("id")).toString().isEmpty()) {
                    devices.append(device);
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void collectRoomsFromSpaceUpdate(const QByteArray &message, QMap<QString, QString> &rooms)
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
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return;
            }
            if (field == 15) {
                addRoomToMap(parseRoom(payload), rooms);
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void collectRoomsFromSpaceUpdatesList(const QByteArray &message, QMap<QString, QString> &rooms)
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
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return;
            }
            if (field == 1) {
                collectRoomsFromSpaceUpdate(payload, rooms);
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void collectRoomsFromSpaceSuccess(const QByteArray &message, QMap<QString, QString> &rooms)
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
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return;
            }
            if (field == 1) {
                collectRoomsFromSpaceSnapshot(payload, rooms);
            } else if (field == 2) {
                collectRoomsFromSpaceUpdate(payload, rooms);
            } else if (field == 3) {
                collectRoomsFromSpaceUpdatesList(payload, rooms);
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void parseLightDeviceStatus(const QByteArray &message, QVariantMap &device)
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
        if (field == 2 && wireType == 0) {
            quint64 value = 0;
            if (!readVarint(message, pos, value)) {
                return;
            }
            device.insert(QStringLiteral("signalStrength"), signalLevelLabel(static_cast<int>(value)));
            device.insert(QStringLiteral("signalValue"), static_cast<int>(value));
            device.insert(QStringLiteral("signalBars"), signalBarsForLevel(static_cast<int>(value)));
        } else if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return;
            }
            int payloadPos = 0;
            if (field == 2) {
                while (payloadPos < payload.size()) {
                    quint64 payloadKey = 0;
                    if (!readVarint(payload, payloadPos, payloadKey)) {
                        break;
                    }
                    const quint64 payloadField = payloadKey >> 3;
                    const quint64 payloadWireType = payloadKey & 0x07;
                    quint64 value = 0;
                    if (payloadWireType == 0 && readVarint(payload, payloadPos, value)) {
                        if (payloadField == 1) {
                            device.insert(QStringLiteral("signalStrength"), signalLevelLabel(static_cast<int>(value)));
                            device.insert(QStringLiteral("signalValue"), static_cast<int>(value));
                            device.insert(QStringLiteral("signalBars"), signalBarsForLevel(static_cast<int>(value)));
                        }
                    } else if (!skipField(payload, payloadPos, payloadWireType)) {
                        break;
                    }
                }
            } else if (field == 9) {
                int batteryState = 0;
                int charge = -1;
                bool withoutCharge = false;
                while (payloadPos < payload.size()) {
                    quint64 payloadKey = 0;
                    if (!readVarint(payload, payloadPos, payloadKey)) {
                        break;
                    }
                    const quint64 payloadField = payloadKey >> 3;
                    const quint64 payloadWireType = payloadKey & 0x07;
                    quint64 value = 0;
                    if (payloadWireType == 0 && readVarint(payload, payloadPos, value)) {
                        if (payloadField == 1) {
                            batteryState = static_cast<int>(value);
                        } else if (payloadField == 2) {
                            charge = static_cast<int>(value);
                        } else if (payloadField == 3) {
                            withoutCharge = value != 0;
                        }
                    } else if (!skipField(payload, payloadPos, payloadWireType)) {
                        break;
                    }
                }
                QStringList parts;
                if (charge >= 0) {
                    parts << QStringLiteral("%1%").arg(charge);
                }
                if (batteryState > 0) {
                    parts << batteryStateLabel(batteryState);
                }
                if (withoutCharge) {
                    parts << QStringLiteral("Without charge");
                }
                if (!parts.isEmpty()) {
                    device.insert(QStringLiteral("batteryLevel"), parts.join(QStringLiteral(" · ")));
                }
            } else if (field == 73) {
                while (payloadPos < payload.size()) {
                    quint64 payloadKey = 0;
                    if (!readVarint(payload, payloadPos, payloadKey)) {
                        break;
                    }
                    const quint64 payloadField = payloadKey >> 3;
                    const quint64 payloadWireType = payloadKey & 0x07;
                    quint64 value = 0;
                    if (payloadWireType == 0 && readVarint(payload, payloadPos, value)) {
                        if (payloadField == 1) {
                            device.insert(QStringLiteral("temperature"), QStringLiteral("%1 C").arg(static_cast<qint64>(value)));
                        }
                    } else if (!skipField(payload, payloadPos, payloadWireType)) {
                        break;
                    }
                }
            } else {
                if (field == 10) {
                    device.insert(QStringLiteral("alwaysActive"), yesNo(true));
                } else if (field == 13) {
                    device.insert(QStringLiteral("armedInNightMode"), yesNo(true));
                } else if (field == 58 || field == 60 || field == 64) {
                    device.insert(QStringLiteral("disable"), yesNo(true));
                } else if (field == 62 || field == 65) {
                    device.insert(QStringLiteral("oneTimeDisable"), yesNo(true));
                } else if (field == 63) {
                    device.insert(QStringLiteral("oneTimeDisable"), QStringLiteral("Tamper"));
                } else if (field == 59) {
                    device.insert(QStringLiteral("disable"), QStringLiteral("Tamper"));
                } else if (field == 69) {
                    device.insert(QStringLiteral("lock"), QStringLiteral("SmartBracket unlocked"));
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

QVariantMap parseLightDeviceProfile(const QByteArray &message)
{
    QVariantMap device;
    QStringList states;
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return device;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        quint64 value = 0;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return device;
            }
            if (field == 1) {
                device.insert(QStringLiteral("id"), QString::fromUtf8(payload));
            } else if (field == 2) {
                device.insert(QStringLiteral("name"), QString::fromUtf8(payload));
            } else if (field == 3) {
                device.insert(QStringLiteral("roomId"), QString::fromUtf8(payload));
            } else if (field == 9) {
                parseLightDeviceStatus(payload, device);
            } else if (field == 10) {
                device.insert(QStringLiteral("sortingKey"), QString::fromUtf8(payload));
            } else if (field == 15) {
                device.insert(QStringLiteral("deviceMarketingId"), QString::fromUtf8(payload));
            }
        } else if (wireType == 0) {
            if (!readVarint(message, pos, value)) {
                return device;
            }
            if (field == 8) {
                states << lightDeviceStateLabel(static_cast<int>(value));
            } else if (field == 11) {
                device.insert(QStringLiteral("deviceIndex"), static_cast<int>(value));
            } else if (field == 12) {
                device.insert(QStringLiteral("disable"), yesNo(value != 0));
            } else if (field == 13) {
                device.insert(QStringLiteral("interactionDisabled"), yesNo(value != 0));
            } else if (field == 16) {
                device.insert(QStringLiteral("marketingDeviceIndex"), static_cast<int>(value));
            }
        } else if (!skipField(message, pos, wireType)) {
            return device;
        }
    }
    if (!states.isEmpty()) {
        device.insert(QStringLiteral("lock"), states.join(QStringLiteral(" · ")));
        device.insert(QStringLiteral("connection"), states.contains(QStringLiteral("Offline")) ? QStringLiteral("Offline") : QStringLiteral("Online"));
    } else if (!device.contains(QStringLiteral("connection"))) {
        device.insert(QStringLiteral("connection"), QStringLiteral("Online"));
    }
    return device;
}

QVariantMap parseLightCommonHubDevice(const QByteArray &message)
{
    QVariantMap device;
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QVariantMap();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QVariantMap();
            }
            if (field == 1) {
                device = parseLightDeviceProfile(payload);
            } else if (field == 2) {
                device.insert(QStringLiteral("hubId"), QString::fromUtf8(payload));
            } else if (field == 3) {
                int objectTypePos = 0;
                quint64 value = 0;
                if (readVarint(payload, objectTypePos, value)) {
                    device.insert(QStringLiteral("objectType"), static_cast<int>(value));
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return QVariantMap();
        }
    }
    return device;
}

QVariantMap parseLightHubDevice(const QByteArray &message)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QVariantMap();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QVariantMap();
            }
            if (field == 1) {
                return parseLightCommonHubDevice(payload);
            }
        } else if (!skipField(message, pos, wireType)) {
            return QVariantMap();
        }
    }
    return QVariantMap();
}

QVariantMap parseLightDeviceItem(const QByteArray &message)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QVariantMap();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QVariantMap();
            }
            if (field == 1) {
                return parseLightHubDevice(payload);
            }
        } else if (!skipField(message, pos, wireType)) {
            return QVariantMap();
        }
    }
    return QVariantMap();
}

QString sensitivityLabel(int value)
{
    switch (value) {
    case 1:
        return QStringLiteral("Low");
    case 2:
        return QStringLiteral("Normal");
    case 3:
        return QStringLiteral("High");
    case 4:
        return QStringLiteral("High");
    case 7:
        return QStringLiteral("Very high");
    default:
        return QString();
    }
}

QString armingModeLabel(int value)
{
    switch (value) {
    case 1:
        return QStringLiteral("Immediately");
    case 2:
        return QStringLiteral("Entry/exit");
    case 3:
        return QStringLiteral("Follower");
    case 4:
        return QStringLiteral("Disabled");
    default:
        return QString();
    }
}

QString enabledLabel(int value)
{
    if (value == 1) {
        return yesNo(false);
    }
    if (value == 2) {
        return yesNo(true);
    }
    return QString();
}

void parseDeviceTelemetry(const QByteArray &message, QVariantMap &device)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        quint64 value = 0;
        if (wireType == 0 && readVarint(message, pos, value)) {
            if (field == 3) {
                device.insert(QStringLiteral("signalStrength"), signalLevelLabel(static_cast<int>(value)));
                device.insert(QStringLiteral("signalValue"), static_cast<int>(value));
                device.insert(QStringLiteral("signalBars"), signalBarsForLevel(static_cast<int>(value)));
            } else if (field == 11) {
                device.insert(QStringLiteral("connection"), value == 2 ? QStringLiteral("Online") : QStringLiteral("Offline"));
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void parseDeviceTemperature(const QByteArray &message, QVariantMap &device)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        quint64 value = 0;
        if (wireType == 0 && readVarint(message, pos, value)) {
            if (field == 1) {
                device.insert(QStringLiteral("temperature"), QStringLiteral("%1 C").arg(static_cast<qint64>(value)));
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void parseDeviceBattery(const QByteArray &message, QVariantMap &device)
{
    int pos = 0;
    int charge = -1;
    int status = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            break;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        quint64 value = 0;
        if (wireType == 0 && readVarint(message, pos, value)) {
            if (field == 5) {
                charge = static_cast<int>(value);
            } else if (field == 44) {
                status = static_cast<int>(value);
            }
        } else if (!skipField(message, pos, wireType)) {
            break;
        }
    }
    QStringList parts;
    if (charge >= 0) {
        parts << QStringLiteral("%1%").arg(charge);
    }
    if (status > 0) {
        parts << batteryStateLabel(status);
    }
    if (!parts.isEmpty()) {
        device.insert(QStringLiteral("batteryLevel"), parts.join(QStringLiteral(" · ")));
    }
}

void parseBypassPart(const QByteArray &message, QVariantMap &device)
{
    int pos = 0;
    bool disable = false;
    bool oneTime = false;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        quint64 value = 0;
        if (wireType == 0 && readVarint(message, pos, value)) {
            if (field == 1) {
                if (value == 2 || value == 3 || value == 4 || value == 5) {
                    disable = true;
                } else if (value == 6 || value == 7) {
                    oneTime = true;
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
    device.insert(QStringLiteral("disable"), yesNo(disable));
    device.insert(QStringLiteral("oneTimeDisable"), yesNo(oneTime));
}

void parseCommonJewellerPart(const QByteArray &message, QVariantMap &device)
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
        if (wireType == 2 && readLengthDelimited(message, pos, payload)) {
            if (field == 2) {
                parseDeviceTelemetry(payload, device);
            } else if (field == 183) {
                parseBypassPart(payload, device);
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void parseCommonArmingPart(const QByteArray &message, QVariantMap &device)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        quint64 value = 0;
        if (wireType == 0 && readVarint(message, pos, value)) {
            if (field == 22) {
                const QString label = enabledLabel(static_cast<int>(value));
                if (!label.isEmpty()) {
                    device.insert(QStringLiteral("armedInNightMode"), label);
                }
            } else if (field == 197) {
                const QString label = armingModeLabel(static_cast<int>(value));
                if (!label.isEmpty()) {
                    device.insert(QStringLiteral("operatingMode"), label);
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void parseSensitivityPart(const QByteArray &message, QVariantMap &device)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        quint64 value = 0;
        if (wireType == 0 && readVarint(message, pos, value)) {
            if (field == 1) {
                const QString label = sensitivityLabel(static_cast<int>(value));
                if (!label.isEmpty()) {
                    device.insert(QStringLiteral("sensitivity"), label);
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void parseMotionDetectionPart(const QByteArray &message, QVariantMap &device)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        quint64 value = 0;
        if (wireType == 0 && readVarint(message, pos, value)) {
            if (field == 1 || field == 6) {
                const QString label = sensitivityLabel(static_cast<int>(value));
                if (!label.isEmpty()) {
                    device.insert(QStringLiteral("sensitivity"), label);
                }
            } else if (field == 3) {
                const QString label = enabledLabel(static_cast<int>(value));
                if (!label.isEmpty()) {
                    device.insert(QStringLiteral("alwaysActive"), label);
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void parseSirenTriggers(const QByteArray &message, QVariantMap &device)
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
        if (wireType == 2 && readLengthDelimited(message, pos, payload)) {
            if (field == 1) {
                int settingsPos = 0;
                bool motion = false;
                while (settingsPos < payload.size()) {
                    quint64 settingsKey = 0;
                    if (!readVarint(payload, settingsPos, settingsKey)) {
                        break;
                    }
                    const quint64 settingsField = settingsKey >> 3;
                    const quint64 settingsWireType = settingsKey & 0x07;
                    quint64 value = 0;
                    if (settingsWireType == 0 && readVarint(payload, settingsPos, value)) {
                        if (settingsField == 1 && value == 6) {
                            motion = true;
                        }
                    } else if (!skipField(payload, settingsPos, settingsWireType)) {
                        break;
                    }
                }
                device.insert(QStringLiteral("sirenOnMovement"), yesNo(motion));
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void parseLedIndicationPart(const QByteArray &message, QVariantMap &device)
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        quint64 value = 0;
        if (wireType == 0 && readVarint(message, pos, value)) {
            if (field == 1) {
                const QString label = enabledLabel(static_cast<int>(value));
                if (!label.isEmpty()) {
                    device.insert(QStringLiteral("alarmLedIndication"), label);
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void parseCommonMotionProtectPart(const QByteArray &message, QVariantMap &device)
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
        if (wireType == 2 && readLengthDelimited(message, pos, payload)) {
            if (field == 1) {
                parseCommonArmingPart(payload, device);
            } else if (field == 2) {
                parseDeviceTemperature(payload, device);
            } else if (field == 3) {
                parseSensitivityPart(payload, device);
            } else if (field == 5) {
                parseSirenTriggers(payload, device);
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void parseCommonDoorProtectPart(const QByteArray &message, QVariantMap &device)
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
        if (wireType == 2 && readLengthDelimited(message, pos, payload)) {
            if (field == 1) {
                parseCommonArmingPart(payload, device);
            } else if (field == 2) {
                parseDeviceTemperature(payload, device);
            } else if (field == 3) {
                parseSirenTriggers(payload, device);
            }
        } else if (wireType == 0 && readVarint(message, pos, value)) {
            if (field == 49) {
                if (value == 1) {
                    device.insert(QStringLiteral("lock"), QStringLiteral("Opened"));
                } else if (value == 2) {
                    device.insert(QStringLiteral("lock"), QStringLiteral("Closed"));
                } else if (value == 3) {
                    device.insert(QStringLiteral("lock"), QStringLiteral("Disabled"));
                }
            } else if (field == 51) {
                const QString label = enabledLabel(static_cast<int>(value));
                if (!label.isEmpty()) {
                    device.insert(QStringLiteral("alwaysActive"), label);
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

void parseHubDevicePayload(const QByteArray &message, QVariantMap &device)
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
        if (wireType == 2 && readLengthDelimited(message, pos, payload)) {
            if (field == 1) {
                parseCommonJewellerPart(payload, device);
            } else if (field == 2) {
                parseCommonMotionProtectPart(payload, device);
                parseCommonDoorProtectPart(payload, device);
            } else if (field == 3 || field == 8) {
                parseDeviceBattery(payload, device);
            } else if (field == 6) {
                parseSirenTriggers(payload, device);
            } else if (field == 7) {
                parseMotionDetectionPart(payload, device);
            } else if (field == 12) {
                parseLedIndicationPart(payload, device);
            }
        } else if (!skipField(message, pos, wireType)) {
            return;
        }
    }
}

QVariantMap parseHubDeviceDetails(const QByteArray &message)
{
    QVariantMap device;
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return device;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2 && readLengthDelimited(message, pos, payload)) {
            device.insert(QStringLiteral("hubDeviceType"), static_cast<int>(field));
            parseHubDevicePayload(payload, device);
        } else if (!skipField(message, pos, wireType)) {
            return device;
        }
    }
    return device;
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

void AjaxApiClient::debugFetchLightDevices(const QString &spaceId)
{
    callUnary(Operation::DebugLightDevices,
              QStringLiteral("systems.ajax.api.ecosystem.v3.mobilegwsvc.service.stream_light_devices.StreamLightDevicesService/execute"),
              fieldString(1, spaceId));
}

void AjaxApiClient::fetchHubDevice(const QString &hubId, const QString &deviceId)
{
    if (hubId.isEmpty() || deviceId.isEmpty()) {
        emit failed(QStringLiteral("Device details blocked: missing Ajax hub or device id."));
        return;
    }
    m_pendingHubDeviceId = deviceId;
    callUnary(Operation::DeviceDetails,
              QStringLiteral("systems.ajax.api.ecosystem.v3.mobilegwsvc.service.stream_hub_device.StreamHubDeviceService/execute"),
              fieldString(1, hubId) + fieldString(2, deviceId));
}

void AjaxApiClient::fetchSpaceRooms(const QString &spaceId)
{
    callUnary(Operation::SpaceRooms,
              QStringLiteral("systems.ajax.api.mobile.v2.space.SpaceService/stream"),
              fieldMessage(1, makeSpaceLocator(spaceId)));
}

void AjaxApiClient::fetchMessages(const QString &spaceId)
{
    callUnary(Operation::Messages,
              QStringLiteral("systems.ajax.api.mobile.v2.notification.NotificationLogService/findNotifications"),
              makeFindNotificationsRequest(spaceId));
}

void AjaxApiClient::startSpaceStream(const QString &spaceId)
{
    if (m_sessionTokenHex.isEmpty() || spaceId.isEmpty()) {
        return;
    }
    if (m_spaceStreamReply && m_spaceStreamSpaceId == spaceId) {
        return;
    }

    stopSpaceStream();
    m_spaceStreamSpaceId = spaceId;

    QNetworkRequest request(QUrl(QString::fromLatin1(kGateway)
                                 + QStringLiteral("systems.ajax.api.mobile.v2.space.SpaceService/streamUserSpaces")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/grpc"));
    request.setRawHeader("accept", "application/grpc");
    request.setRawHeader("te", "trailers");
    request.setAttribute(QNetworkRequest::HTTP2AllowedAttribute, true);
    addAuthHeaders(request);

    m_spaceStreamReply = m_network->post(request, makeGrpcFrame(QByteArray()));
    connect(m_spaceStreamReply, &QNetworkReply::readyRead, this, &AjaxApiClient::onSpaceStreamReadyRead);
    connect(m_spaceStreamReply, &QNetworkReply::finished, this, &AjaxApiClient::onSpaceStreamFinished);
}

void AjaxApiClient::stopSpaceStream()
{
    if (m_spaceStreamReply) {
        disconnect(m_spaceStreamReply, nullptr, this, nullptr);
        m_spaceStreamReply->abort();
        m_spaceStreamReply->deleteLater();
        m_spaceStreamReply.clear();
    }
    m_spaceStreamBuffer.clear();
    m_spaceStreamSpaceId.clear();
}

void AjaxApiClient::startAlarmStream(const QString &spaceId)
{
    if (m_sessionTokenHex.isEmpty() || spaceId.isEmpty()) {
        return;
    }
    if (m_alarmStreamReply && m_alarmStreamSpaceId == spaceId) {
        return;
    }

    stopAlarmStream();
    m_alarmStreamSpaceId = spaceId;

    QNetworkRequest request(QUrl(QString::fromLatin1(kGateway)
                                 + QStringLiteral("systems.ajax.api.mobile.v2.notification.NotificationLogService/streamUpdates")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/grpc"));
    request.setRawHeader("accept", "application/grpc");
    request.setRawHeader("te", "trailers");
    request.setAttribute(QNetworkRequest::HTTP2AllowedAttribute, true);
    addAuthHeaders(request);

    m_alarmStreamReply = m_network->post(request, makeGrpcFrame(fieldMessage(1, fieldString(2, spaceId))));
    connect(m_alarmStreamReply, &QNetworkReply::readyRead, this, &AjaxApiClient::onAlarmStreamReadyRead);
    connect(m_alarmStreamReply, &QNetworkReply::finished, this, &AjaxApiClient::onAlarmStreamFinished);
}

void AjaxApiClient::stopAlarmStream()
{
    if (m_alarmStreamReply) {
        disconnect(m_alarmStreamReply, nullptr, this, nullptr);
        m_alarmStreamReply->abort();
        m_alarmStreamReply->deleteLater();
        m_alarmStreamReply.clear();
    }
    m_alarmStreamBuffer.clear();
    m_alarmStreamSpaceId.clear();
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
    m_streamBuffer.clear();
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
    if (operation == Operation::DebugLightDevices || operation == Operation::DeviceDetails || operation == Operation::SpaceRooms) {
        connect(m_reply, &QNetworkReply::readyRead, this, &AjaxApiClient::onReadyRead);
    }
    connect(m_reply, &QNetworkReply::finished, this, &AjaxApiClient::onFinished);
    m_timeout.start();
}

void AjaxApiClient::onReadyRead()
{
    if (!m_reply
            || (m_operation != Operation::DebugLightDevices
                && m_operation != Operation::DeviceDetails
                && m_operation != Operation::SpaceRooms)) {
        return;
    }

    m_streamBuffer += m_reply->readAll();
    if (m_streamBuffer.size() < 5) {
        return;
    }

    const quint32 length =
        (static_cast<quint8>(m_streamBuffer.at(1)) << 24)
        | (static_cast<quint8>(m_streamBuffer.at(2)) << 16)
        | (static_cast<quint8>(m_streamBuffer.at(3)) << 8)
        | static_cast<quint8>(m_streamBuffer.at(4));
    if (m_streamBuffer.size() < static_cast<int>(length) + 5) {
        return;
    }

    const quint8 compressed = static_cast<quint8>(m_streamBuffer.at(0));
    const Operation operation = m_operation;
    const QByteArray message = m_streamBuffer.mid(5, static_cast<int>(length));
    disconnect(m_reply, nullptr, this, nullptr);
    m_timeout.stop();
    m_reply->abort();
    m_reply->deleteLater();
    m_reply.clear();
    m_streamBuffer.clear();
    m_operation = Operation::None;
    setBusy(false);

    if (compressed != 0) {
        emit failed(QStringLiteral("Compressed Ajax device stream response is not supported."));
        return;
    }
    if (operation == Operation::DebugLightDevices) {
        parseLightDevicesResponse(message);
    } else if (operation == Operation::DeviceDetails) {
        parseHubDeviceResponse(message);
    } else {
        parseSpaceRoomsResponse(message);
    }
}

void AjaxApiClient::onSpaceStreamReadyRead()
{
    if (!m_spaceStreamReply) {
        return;
    }

    m_spaceStreamBuffer += m_spaceStreamReply->readAll();
    while (m_spaceStreamBuffer.size() >= 5) {
        const quint32 length =
            (static_cast<quint8>(m_spaceStreamBuffer.at(1)) << 24)
            | (static_cast<quint8>(m_spaceStreamBuffer.at(2)) << 16)
            | (static_cast<quint8>(m_spaceStreamBuffer.at(3)) << 8)
            | static_cast<quint8>(m_spaceStreamBuffer.at(4));
        if (m_spaceStreamBuffer.size() < static_cast<int>(length) + 5) {
            return;
        }

        const quint8 compressed = static_cast<quint8>(m_spaceStreamBuffer.at(0));
        const QByteArray message = m_spaceStreamBuffer.mid(5, static_cast<int>(length));
        m_spaceStreamBuffer.remove(0, static_cast<int>(length) + 5);

        if (compressed != 0) {
            emit failed(QStringLiteral("Compressed Ajax space stream response is not supported."));
            continue;
        }

        const QString mode = parseUserSpacesStreamMode(message);
        if (!mode.isEmpty() && mode != QStringLiteral("Unknown")) {
            qInfo() << "AjaxRemote user space stream mode:" << mode;
            emit commandSucceeded(QStringLiteral("State"), mode);
        }
    }
}

void AjaxApiClient::onSpaceStreamFinished()
{
    if (!m_spaceStreamReply) {
        return;
    }
    const QNetworkReply::NetworkError error = m_spaceStreamReply->error();
    const QString errorString = m_spaceStreamReply->errorString();
    m_spaceStreamReply->deleteLater();
    m_spaceStreamReply.clear();
    m_spaceStreamBuffer.clear();
    const QString spaceId = m_spaceStreamSpaceId;
    m_spaceStreamSpaceId.clear();

    if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
        emit failed(QStringLiteral("Ajax space stream ended: %1").arg(errorString));
    }
    Q_UNUSED(spaceId)
}

void AjaxApiClient::onAlarmStreamReadyRead()
{
    if (!m_alarmStreamReply) {
        return;
    }

    m_alarmStreamBuffer += m_alarmStreamReply->readAll();
    while (m_alarmStreamBuffer.size() >= 5) {
        const quint32 length =
            (static_cast<quint8>(m_alarmStreamBuffer.at(1)) << 24)
            | (static_cast<quint8>(m_alarmStreamBuffer.at(2)) << 16)
            | (static_cast<quint8>(m_alarmStreamBuffer.at(3)) << 8)
            | static_cast<quint8>(m_alarmStreamBuffer.at(4));
        if (m_alarmStreamBuffer.size() < static_cast<int>(length) + 5) {
            return;
        }

        const quint8 compressed = static_cast<quint8>(m_alarmStreamBuffer.at(0));
        const QByteArray message = m_alarmStreamBuffer.mid(5, static_cast<int>(length));
        m_alarmStreamBuffer.remove(0, static_cast<int>(length) + 5);

        if (compressed != 0) {
            emit failed(QStringLiteral("Compressed Ajax alarm stream response is not supported."));
            continue;
        }

        const QVariantList messages = parseNotificationLogStreamMessages(message);
        if (!messages.isEmpty()) {
            qInfo() << "AjaxRemote notification stream events:" << messages.size();
            emit alarmEventsLoaded(messages);
        }
    }
}

void AjaxApiClient::onAlarmStreamFinished()
{
    if (!m_alarmStreamReply) {
        return;
    }
    const QNetworkReply::NetworkError error = m_alarmStreamReply->error();
    const QString errorString = m_alarmStreamReply->errorString();
    m_alarmStreamReply->deleteLater();
    m_alarmStreamReply.clear();
    m_alarmStreamBuffer.clear();
    m_alarmStreamSpaceId.clear();

    if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
        emit failed(QStringLiteral("Ajax alarm stream ended: %1").arg(errorString));
    }
}

void AjaxApiClient::onFinished()
{
    if (!m_reply) {
        return;
    }
    const Operation operation = m_operation;
    const QByteArray body = m_streamBuffer + m_reply->readAll();
    const QNetworkReply::NetworkError error = m_reply->error();
    const QString errorString = m_reply->errorString();
    m_lastResponseDetail = responseDetail(m_reply, body.size());
    m_reply->deleteLater();
    m_reply.clear();
    m_timeout.stop();
    m_operation = Operation::None;
    m_streamBuffer.clear();
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
        } else if (operation == Operation::DebugLightDevices) {
            parseLightDevicesResponse(message);
        } else if (operation == Operation::DeviceDetails) {
            parseHubDeviceResponse(message);
        } else if (operation == Operation::SpaceRooms) {
            parseSpaceRoomsResponse(message);
        } else if (operation == Operation::Messages) {
            parseMessagesResponse(message);
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

void AjaxApiClient::parseLightDevicesResponse(const QByteArray &message)
{
    QVariantList devices;
    QMap<QString, QString> rooms;
    int signalCount = 0;
    int pos = 0;

    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            emit failed(QStringLiteral("Invalid Ajax device stream response."));
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray successPayload;
        if (wireType == 2 && readLengthDelimited(message, pos, successPayload)) {
            if (field == 1) {
                int successPos = 0;
                while (successPos < successPayload.size()) {
                    quint64 successKey = 0;
                    if (!readVarint(successPayload, successPos, successKey)) {
                        break;
                    }
                    const quint64 successField = successKey >> 3;
                    const quint64 successWireType = successKey & 0x07;
                    QByteArray snapshotPayload;
                    if (successWireType == 2 && readLengthDelimited(successPayload, successPos, snapshotPayload)) {
                        if (successField == 1) {
                            int snapshotPos = 0;
                            while (snapshotPos < snapshotPayload.size()) {
                                quint64 snapshotKey = 0;
                                if (!readVarint(snapshotPayload, snapshotPos, snapshotKey)) {
                                    break;
                                }
                                const quint64 snapshotField = snapshotKey >> 3;
                                const quint64 snapshotWireType = snapshotKey & 0x07;
                                QByteArray itemPayload;
                                if (snapshotWireType == 2 && readLengthDelimited(snapshotPayload, snapshotPos, itemPayload)) {
                                    if (snapshotField == 1) {
                                        QVariantMap item = parseLightDeviceItem(itemPayload);
                                        const QString id = item.value(QStringLiteral("id")).toString();
                                        const QString name = item.value(QStringLiteral("name")).toString();
                                        const QString roomId = item.value(QStringLiteral("roomId")).toString();
                                        if (!id.isEmpty() || !name.isEmpty()) {
                                            if (name.isEmpty()) {
                                                item.insert(QStringLiteral("name"), QStringLiteral("Ajax device"));
                                            }
                                            item.insert(QStringLiteral("roomName"), roomId.isEmpty() ? QString() : QStringLiteral("Room %1").arg(roomId.left(6)));
                                            if (item.contains(QStringLiteral("signalStrength"))) {
                                                signalCount++;
                                            }
                                            devices.append(item);
                                        }
                                        if (!roomId.isEmpty()) {
                                            rooms.insert(roomId, QStringLiteral("Room %1").arg(roomId.left(6)));
                                        }
                                    }
                                } else if (!skipField(snapshotPayload, snapshotPos, snapshotWireType)) {
                                    break;
                                }
                            }
                        }
                    } else if (!skipField(successPayload, successPos, successWireType)) {
                        break;
                    }
                }
            } else if (field == 2) {
                emit failed(QStringLiteral("Ajax rejected device stream request."));
                return;
            }
        } else if (!skipField(message, pos, wireType)) {
            emit failed(QStringLiteral("Unsupported Ajax device stream response field."));
            return;
        }
    }

    qInfo() << "AjaxRemote debug inventory:"
            << "devices" << devices.size()
            << "roomIds" << rooms.size()
            << "signals" << signalCount;
    for (const QVariant &entry : devices) {
        const QVariantMap item = entry.toMap();
        qInfo() << "AjaxRemote device signal:"
                << item.value(QStringLiteral("name")).toString()
                << "strength" << item.value(QStringLiteral("signalStrength")).toString()
                << "value" << item.value(QStringLiteral("signalValue")).toString()
                << "bars" << item.value(QStringLiteral("signalBars")).toString();
    }

    QVariantList roomList;
    for (auto it = rooms.constBegin(); it != rooms.constEnd(); ++it) {
        QVariantMap room;
        room.insert(QStringLiteral("id"), it.key());
        room.insert(QStringLiteral("name"), it.value());
        roomList.append(room);
    }
    emit inventoryLoaded(devices, roomList);
}

void AjaxApiClient::parseHubDeviceResponse(const QByteArray &message)
{
    QVariantMap device;
    int pos = 0;

    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            emit failed(QStringLiteral("Invalid Ajax hub device response."));
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2 && readLengthDelimited(message, pos, payload)) {
            if (field == 1) {
                int successPos = 0;
                while (successPos < payload.size()) {
                    quint64 successKey = 0;
                    if (!readVarint(payload, successPos, successKey)) {
                        break;
                    }
                    const quint64 successField = successKey >> 3;
                    const quint64 successWireType = successKey & 0x07;
                    QByteArray snapshotPayload;
                    if (successWireType == 2 && readLengthDelimited(payload, successPos, snapshotPayload)) {
                        if (successField == 1) {
                            int snapshotPos = 0;
                            while (snapshotPos < snapshotPayload.size()) {
                                quint64 snapshotKey = 0;
                                if (!readVarint(snapshotPayload, snapshotPos, snapshotKey)) {
                                    break;
                                }
                                const quint64 snapshotField = snapshotKey >> 3;
                                const quint64 snapshotWireType = snapshotKey & 0x07;
                                QByteArray hubDevicePayload;
                                if (snapshotWireType == 2 && readLengthDelimited(snapshotPayload, snapshotPos, hubDevicePayload)) {
                                    if (snapshotField == 1) {
                                        device = parseHubDeviceDetails(hubDevicePayload);
                                    }
                                } else if (!skipField(snapshotPayload, snapshotPos, snapshotWireType)) {
                                    break;
                                }
                            }
                        }
                    } else if (!skipField(payload, successPos, successWireType)) {
                        break;
                    }
                }
            } else if (field == 2) {
                emit failed(QStringLiteral("Ajax rejected hub device request."));
                return;
            }
        } else if (!skipField(message, pos, wireType)) {
            emit failed(QStringLiteral("Unsupported Ajax hub device response field."));
            return;
        }
    }

    if (device.isEmpty()) {
        emit failed(QStringLiteral("Ajax returned no hub device details."));
        return;
    }
    if (!m_pendingHubDeviceId.isEmpty()) {
        device.insert(QStringLiteral("id"), m_pendingHubDeviceId);
        m_pendingHubDeviceId.clear();
    }
    qInfo() << "AjaxRemote hub device details:" << device.keys();
    emit devicesLoaded(QVariantList { device });
}

QString AjaxApiClient::parseDisplayedSecurityStateV2(const QByteArray &message) const
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        if (field >= 1 && field <= 8) {
            return modeForState(static_cast<int>(field));
        }
        if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    return QString();
}

QString AjaxApiClient::parseSpaceSecurityMode(const QByteArray &message) const
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        quint64 value = 0;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QString();
            }
            if (field == 14) {
                const QString mode = parseDisplayedSecurityStateV2(payload);
                if (!mode.isEmpty()) {
                    return mode;
                }
            }
        } else if (wireType == 0) {
            if (!readVarint(message, pos, value)) {
                return QString();
            }
            if (field == 13) {
                return modeForState(static_cast<int>(value));
            }
        } else if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    return QString();
}

QString AjaxApiClient::parseSpaceSnapshotMode(const QByteArray &message) const
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QString();
            }
            if (field == 3) {
                int securityPos = 0;
                while (securityPos < payload.size()) {
                    quint64 securityKey = 0;
                    if (!readVarint(payload, securityPos, securityKey)) {
                        return QString();
                    }
                    const quint64 securityField = securityKey >> 3;
                    const quint64 securityWireType = securityKey & 0x07;
                    QByteArray modePayload;
                    if (securityWireType == 2) {
                        if (!readLengthDelimited(payload, securityPos, modePayload)) {
                            return QString();
                        }
                        if (securityField == 2) {
                            return parseSpaceSecurityMode(modePayload);
                        }
                    } else if (!skipField(payload, securityPos, securityWireType)) {
                        return QString();
                    }
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    return QString();
}

QString AjaxApiClient::parseSpaceUpdateMode(const QByteArray &message) const
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QString();
            }
            if (field == 11) {
                return parseSpaceSecurityMode(payload);
            }
        } else if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    return QString();
}

QString AjaxApiClient::parseSpaceSuccessMode(const QByteArray &message) const
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QString();
            }
            if (field == 1) {
                const QString mode = parseSpaceSnapshotMode(payload);
                if (!mode.isEmpty()) {
                    return mode;
                }
            } else if (field == 2) {
                const QString mode = parseSpaceUpdateMode(payload);
                if (!mode.isEmpty()) {
                    return mode;
                }
            } else if (field == 3) {
                int updatesPos = 0;
                while (updatesPos < payload.size()) {
                    quint64 updatesKey = 0;
                    if (!readVarint(payload, updatesPos, updatesKey)) {
                        return QString();
                    }
                    const quint64 updatesField = updatesKey >> 3;
                    const quint64 updatesWireType = updatesKey & 0x07;
                    QByteArray updatePayload;
                    if (updatesWireType == 2) {
                        if (!readLengthDelimited(payload, updatesPos, updatePayload)) {
                            return QString();
                        }
                        if (updatesField == 1) {
                            const QString mode = parseSpaceUpdateMode(updatePayload);
                            if (!mode.isEmpty()) {
                                return mode;
                            }
                        }
                    } else if (!skipField(payload, updatesPos, updatesWireType)) {
                        return QString();
                    }
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    return QString();
}

QString AjaxApiClient::parseSpaceStreamMode(const QByteArray &message) const
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QString();
            }
            if (field == 1) {
                return parseSpaceSuccessMode(payload);
            }
        } else if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    return QString();
}

QString AjaxApiClient::parseLiteSpaceMode(const QByteArray &message) const
{
    QString spaceId;
    QString mode;
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        quint64 value = 0;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QString();
            }
            if (field == 1) {
                spaceId = QString::fromUtf8(payload);
            } else if (field == 4) {
                mode = parseDisplayedSecurityStateV2(payload);
            }
        } else if (wireType == 0) {
            if (!readVarint(message, pos, value)) {
                return QString();
            }
            if (field == 3) {
                mode = modeForState(static_cast<int>(value));
            }
        } else if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    if (!m_spaceStreamSpaceId.isEmpty() && !spaceId.isEmpty() && spaceId != m_spaceStreamSpaceId) {
        return QString();
    }
    return mode;
}

QString AjaxApiClient::parseUserSpacesInitialMode(const QByteArray &message) const
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QString();
            }
            if (field == 1) {
                const QString mode = parseLiteSpaceMode(payload);
                if (!mode.isEmpty()) {
                    return mode;
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    return QString();
}

QString AjaxApiClient::parseLiteSpaceUpdatedMode(const QByteArray &message) const
{
    QString eventSpaceId;
    QString mode;
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        quint64 value = 0;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QString();
            }
            if (field == 1) {
                eventSpaceId = QString::fromUtf8(payload);
            } else if (field == 9) {
                mode = parseDisplayedSecurityStateV2(payload);
            }
        } else if (wireType == 0) {
            if (!readVarint(message, pos, value)) {
                return QString();
            }
            if (field == 3) {
                mode = modeForState(static_cast<int>(value));
            }
        } else if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    if (!m_spaceStreamSpaceId.isEmpty() && !eventSpaceId.isEmpty() && eventSpaceId != m_spaceStreamSpaceId) {
        return QString();
    }
    return mode;
}

QString AjaxApiClient::parseLiteSpaceEventMode(const QByteArray &message) const
{
    QString eventSpaceId;
    QString mode;
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QString();
            }
            if (field == 99) {
                eventSpaceId = QString::fromUtf8(payload);
            } else if (field == 1) {
                mode = parseLiteSpaceMode(payload);
            } else if (field == 2) {
                mode = parseLiteSpaceUpdatedMode(payload);
            }
        } else if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    if (!m_spaceStreamSpaceId.isEmpty() && !eventSpaceId.isEmpty() && eventSpaceId != m_spaceStreamSpaceId) {
        return QString();
    }
    return mode;
}

QString AjaxApiClient::parseUserSpacesSuccessMode(const QByteArray &message) const
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QString();
            }
            if (field == 1) {
                const QString mode = parseUserSpacesInitialMode(payload);
                if (!mode.isEmpty()) {
                    return mode;
                }
            } else if (field == 2) {
                const QString mode = parseLiteSpaceEventMode(payload);
                if (!mode.isEmpty()) {
                    return mode;
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    return QString();
}

QString AjaxApiClient::parseUserSpacesStreamMode(const QByteArray &message) const
{
    int pos = 0;
    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return QString();
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                return QString();
            }
            if (field == 1) {
                return parseUserSpacesSuccessMode(payload);
            }
        } else if (!skipField(message, pos, wireType)) {
            return QString();
        }
    }
    return QString();
}

void AjaxApiClient::parseSpaceRoomsResponse(const QByteArray &message)
{
    QMap<QString, QString> rooms;
    QVariantList devices;
    int pos = 0;

    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            emit failed(QStringLiteral("Invalid Ajax space stream response."));
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                emit failed(QStringLiteral("Invalid Ajax space stream payload."));
                return;
            }
            if (field == 1) {
                collectRoomsFromSpaceSuccess(payload, rooms);
                int successPos = 0;
                while (successPos < payload.size()) {
                    quint64 successKey = 0;
                    if (!readVarint(payload, successPos, successKey)) {
                        break;
                    }
                    const quint64 successField = successKey >> 3;
                    const quint64 successWireType = successKey & 0x07;
                    QByteArray successPayload;
                    if (successWireType == 2 && readLengthDelimited(payload, successPos, successPayload)) {
                        if (successField == 1) {
                            collectDevicesFromSpaceSnapshot(successPayload, devices);
                        }
                    } else if (!skipField(payload, successPos, successWireType)) {
                        break;
                    }
                }
            } else if (field == 2) {
                emit failed(QStringLiteral("Ajax rejected space stream request."));
                return;
            }
        } else if (!skipField(message, pos, wireType)) {
            emit failed(QStringLiteral("Unsupported Ajax space stream response field."));
            return;
        }
    }

    QVariantList roomList;
    for (auto it = rooms.constBegin(); it != rooms.constEnd(); ++it) {
        QVariantMap room;
        room.insert(QStringLiteral("id"), it.key());
        room.insert(QStringLiteral("name"), it.value());
        roomList.append(room);
    }

    if (!roomList.isEmpty()) {
        qInfo() << "AjaxRemote rooms:" << roomList.size();
        emit roomsLoaded(roomList);
    }
    if (!devices.isEmpty()) {
        qInfo() << "AjaxRemote space devices:" << devices.size();
        emit devicesLoaded(devices);
    }
}

QVariantList AjaxApiClient::parseUnconfirmedAlarmStreamMessages(const QByteArray &message) const
{
    QVariantList messages;
    int pos = 0;

    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return messages;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray successPayload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, successPayload)) {
                return messages;
            }
            if (field != 1) {
                continue;
            }

            int successPos = 0;
            while (successPos < successPayload.size()) {
                quint64 successKey = 0;
                if (!readVarint(successPayload, successPos, successKey)) {
                    break;
                }
                const quint64 successField = successKey >> 3;
                const quint64 successWireType = successKey & 0x07;
                QByteArray notificationsPayload;
                if (successWireType == 2 && readLengthDelimited(successPayload, successPos, notificationsPayload)) {
                    if (successField < 1 || successField > 3) {
                        continue;
                    }
                    int notificationsPos = 0;
                    while (notificationsPos < notificationsPayload.size()) {
                        quint64 notificationKey = 0;
                        if (!readVarint(notificationsPayload, notificationsPos, notificationKey)) {
                            break;
                        }
                        const quint64 notificationField = notificationKey >> 3;
                        const quint64 notificationWireType = notificationKey & 0x07;
                        QByteArray notificationPayload;
                        if (notificationWireType == 2 && readLengthDelimited(notificationsPayload, notificationsPos, notificationPayload)) {
                            if (notificationField != 1) {
                                continue;
                            }
                            const NotificationInfo info = parseNotification(notificationPayload);
                            QVariantMap item;
                            const bool hasTimestamp = info.timestamp > 0;
                            const QDateTime timestamp = hasTimestamp
                                                        ? QDateTime::fromSecsSinceEpoch(info.timestamp).toLocalTime()
                                                        : QDateTime::currentDateTime();
                            const QString sourceName = info.content.source.name;
                            const QString title = notificationSentence(info.folder, info.content.tagField, sourceName);
                            item.insert(QStringLiteral("id"), info.id);
                            item.insert(QStringLiteral("time"), timestamp.toString(QStringLiteral("HH:mm:ss")));
                            item.insert(QStringLiteral("timestampMs"), hasTimestamp ? timestamp.toMSecsSinceEpoch() : 0);
                            item.insert(QStringLiteral("dateKey"), timestamp.toString(QStringLiteral("yyyy-MM-dd")));
                            item.insert(QStringLiteral("dateLabel"), timestamp.date() == QDate::currentDate()
                                        ? QStringLiteral("Today")
                                        : timestamp.date().toString(Qt::DefaultLocaleShortDate));
                            item.insert(QStringLiteral("title"), title);
                            item.insert(QStringLiteral("text"), info.content.source.roomName);
                            item.insert(QStringLiteral("category"), QStringLiteral("alarm"));
                            item.insert(QStringLiteral("avatar"), sourceName.isEmpty()
                                        ? title.left(1).toUpper()
                                        : sourceName.left(1).toUpper());
                            item.insert(QStringLiteral("sourceName"), sourceName);
                            item.insert(QStringLiteral("avatarImage"), info.content.source.imageUrl);
                            item.insert(QStringLiteral("sourceId"), info.content.source.id);
                            item.insert(QStringLiteral("sourceType"), info.content.source.type);
                            item.insert(QStringLiteral("roomName"), info.content.source.roomName);
                            item.insert(QStringLiteral("eventTag"), info.content.tagField);
                            item.insert(QStringLiteral("folder"), info.folder);
                            item.insert(QStringLiteral("importance"), info.importance);
                            messages.append(item);
                        } else if (!skipField(notificationsPayload, notificationsPos, notificationWireType)) {
                            break;
                        }
                    }
                } else if (!skipField(successPayload, successPos, successWireType)) {
                    break;
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return messages;
        }
    }

    return messages;
}

QVariantList AjaxApiClient::parseNotificationLogStreamMessages(const QByteArray &message) const
{
    QVariantList messages;
    int pos = 0;

    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            return messages;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray successPayload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, successPayload)) {
                return messages;
            }
            if (field != 1) {
                continue;
            }

            int successPos = 0;
            while (successPos < successPayload.size()) {
                quint64 successKey = 0;
                if (!readVarint(successPayload, successPos, successKey)) {
                    break;
                }
                const quint64 successField = successKey >> 3;
                const quint64 successWireType = successKey & 0x07;
                QByteArray eventPayload;
                if (successWireType == 2 && readLengthDelimited(successPayload, successPos, eventPayload)) {
                    if (successField != 1) {
                        continue;
                    }

                    int eventPos = 0;
                    while (eventPos < eventPayload.size()) {
                        quint64 eventKey = 0;
                        if (!readVarint(eventPayload, eventPos, eventKey)) {
                            break;
                        }
                        const quint64 eventField = eventKey >> 3;
                        const quint64 eventWireType = eventKey & 0x07;
                        QByteArray notificationPayload;
                        if (eventWireType == 2 && readLengthDelimited(eventPayload, eventPos, notificationPayload)) {
                            if (eventField != 1 && eventField != 2) {
                                continue;
                            }
                            const NotificationInfo info = parseNotification(notificationPayload);
                            QVariantMap item;
                            const bool hasTimestamp = info.timestamp > 0;
                            const QDateTime timestamp = hasTimestamp
                                                        ? QDateTime::fromSecsSinceEpoch(info.timestamp).toLocalTime()
                                                        : QDateTime::currentDateTime();
                            const QString sourceName = info.content.source.name;
                            const QString title = notificationSentence(info.folder, info.content.tagField, sourceName);
                            const QString category = folderCategory(info.folder);
                            item.insert(QStringLiteral("id"), info.id);
                            item.insert(QStringLiteral("time"), timestamp.toString(QStringLiteral("HH:mm:ss")));
                            item.insert(QStringLiteral("timestampMs"), hasTimestamp ? timestamp.toMSecsSinceEpoch() : 0);
                            item.insert(QStringLiteral("dateKey"), timestamp.toString(QStringLiteral("yyyy-MM-dd")));
                            item.insert(QStringLiteral("dateLabel"), timestamp.date() == QDate::currentDate()
                                        ? QStringLiteral("Today")
                                        : timestamp.date().toString(Qt::DefaultLocaleShortDate));
                            item.insert(QStringLiteral("title"), title);
                            item.insert(QStringLiteral("text"), info.content.source.roomName);
                            item.insert(QStringLiteral("category"), category);
                            item.insert(QStringLiteral("avatar"), sourceName.isEmpty()
                                        ? title.left(1).toUpper()
                                        : sourceName.left(1).toUpper());
                            item.insert(QStringLiteral("sourceName"), sourceName);
                            item.insert(QStringLiteral("avatarImage"), info.content.source.imageUrl);
                            item.insert(QStringLiteral("sourceId"), info.content.source.id);
                            item.insert(QStringLiteral("sourceType"), info.content.source.type);
                            item.insert(QStringLiteral("roomName"), info.content.source.roomName);
                            item.insert(QStringLiteral("eventTag"), info.content.tagField);
                            item.insert(QStringLiteral("folder"), info.folder);
                            item.insert(QStringLiteral("importance"), info.importance);
                            messages.append(item);
                        } else if (!skipField(eventPayload, eventPos, eventWireType)) {
                            break;
                        }
                    }
                } else if (!skipField(successPayload, successPos, successWireType)) {
                    break;
                }
            }
        } else if (!skipField(message, pos, wireType)) {
            return messages;
        }
    }

    return messages;
}

void AjaxApiClient::parseMessagesResponse(const QByteArray &message)
{
    QVariantList messages;
    int avatarImageCount = 0;
    QMap<QString, int> categoryCounts;
    QMap<QString, int> eventCounts;
    int pos = 0;

    while (pos < message.size()) {
        quint64 key = 0;
        if (!readVarint(message, pos, key)) {
            emit failed(QStringLiteral("Invalid Ajax messages response."));
            return;
        }
        const quint64 field = key >> 3;
        const quint64 wireType = key & 0x07;
        QByteArray payload;
        if (wireType == 2) {
            if (!readLengthDelimited(message, pos, payload)) {
                emit failed(QStringLiteral("Invalid Ajax messages payload."));
                return;
            }
            if (field == 1) {
                int successPos = 0;
                while (successPos < payload.size()) {
                    quint64 successKey = 0;
                    if (!readVarint(payload, successPos, successKey)) {
                        break;
                    }
                    const quint64 successField = successKey >> 3;
                    const quint64 successWireType = successKey & 0x07;
                    QByteArray notificationPayload;
                    if (successWireType == 2 && readLengthDelimited(payload, successPos, notificationPayload)) {
                        if (successField == 1) {
                            const NotificationInfo info = parseNotification(notificationPayload);
                            QVariantMap item;
                            const bool hasTimestamp = info.timestamp > 0;
                            const QDateTime timestamp = hasTimestamp
                                                        ? QDateTime::fromSecsSinceEpoch(info.timestamp).toLocalTime()
                                                        : QDateTime::currentDateTime();
                            const QString sourceName = info.content.source.name;
                            const QString title = notificationSentence(info.folder, info.content.tagField, sourceName);
                            const QString detail = info.content.source.roomName;
                            item.insert(QStringLiteral("id"), info.id);
                            item.insert(QStringLiteral("time"), timestamp.toString(QStringLiteral("HH:mm:ss")));
                            item.insert(QStringLiteral("timestampMs"), hasTimestamp ? timestamp.toMSecsSinceEpoch() : 0);
                            item.insert(QStringLiteral("dateKey"), timestamp.toString(QStringLiteral("yyyy-MM-dd")));
                            item.insert(QStringLiteral("dateLabel"), timestamp.date() == QDate::currentDate()
                                        ? QStringLiteral("Today")
                                        : timestamp.date().toString(Qt::DefaultLocaleShortDate));
                            item.insert(QStringLiteral("title"), title);
                            item.insert(QStringLiteral("text"), detail);
                            const QString category = folderCategory(info.folder);
                            item.insert(QStringLiteral("category"), category);
                            item.insert(QStringLiteral("avatar"), sourceName.isEmpty()
                                        ? title.left(1).toUpper()
                                        : sourceName.left(1).toUpper());
                            item.insert(QStringLiteral("sourceName"), sourceName);
                            item.insert(QStringLiteral("avatarImage"), info.content.source.imageUrl);
                            item.insert(QStringLiteral("sourceId"), info.content.source.id);
                            item.insert(QStringLiteral("sourceType"), info.content.source.type);
                            item.insert(QStringLiteral("roomName"), info.content.source.roomName);
                            item.insert(QStringLiteral("eventTag"), info.content.tagField);
                            item.insert(QStringLiteral("folder"), info.folder);
                            item.insert(QStringLiteral("importance"), info.importance);
                            messages.append(item);
                            categoryCounts[category] = categoryCounts.value(category) + 1;
                            eventCounts[QStringLiteral("%1:%2:%3:%4")
                                        .arg(info.folder)
                                        .arg(info.content.tagField)
                                        .arg(info.content.caseValue)
                                        .arg(info.content.transitionField)] =
                                    eventCounts.value(QStringLiteral("%1:%2:%3:%4")
                                                      .arg(info.folder)
                                                      .arg(info.content.tagField)
                                                      .arg(info.content.caseValue)
                                                      .arg(info.content.transitionField)) + 1;
                            if (!info.content.source.imageUrl.isEmpty()) {
                                avatarImageCount++;
                            }
                        }
                    } else if (!skipField(payload, successPos, successWireType)) {
                        break;
                    }
                }
            } else if (field == 2) {
                emit failed(QStringLiteral("Ajax rejected messages request."));
                return;
            }
        } else if (!skipField(message, pos, wireType)) {
            emit failed(QStringLiteral("Unsupported Ajax messages response field."));
            return;
        }
    }

    qInfo() << "AjaxRemote messages:" << messages.size()
            << "alarm" << categoryCounts.value(QStringLiteral("alarm"))
            << "malfunction" << categoryCounts.value(QStringLiteral("malfunction"))
            << "arming" << categoryCounts.value(QStringLiteral("arming"))
            << "all" << categoryCounts.value(QStringLiteral("all"))
            << "imageUrls" << avatarImageCount;
    qInfo() << "AjaxRemote message event counts:" << eventCounts;
    emit messagesLoaded(messages);
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

QByteArray AjaxApiClient::makeFindNotificationsRequest(const QString &spaceId) const
{
    const QByteArray origin = fieldString(2, spaceId);
    const QByteArray filter = fieldMessage(1, origin);
    return fieldMessage(1, filter) + fieldVarint(2, 50);
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
