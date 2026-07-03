#pragma once

#include <QObject>
#include <QByteArray>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

class AjaxApiClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit AjaxApiClient(QObject *parent = nullptr);

    bool busy() const;

    Q_INVOKABLE void setSession(const QString &sessionTokenHex, const QString &userId,
                                const QString &userRole, const QString &userLogin,
                                const QString &sessionCookie);
    Q_INVOKABLE void discoverSpaces();
    Q_INVOKABLE void arm(const QString &spaceId);
    Q_INVOKABLE void disarm(const QString &spaceId);
    Q_INVOKABLE void enableNightMode(const QString &spaceId);
    Q_INVOKABLE void triggerPanic(const QString &spaceId);
    Q_INVOKABLE void debugFetchLightDevices(const QString &spaceId);
    Q_INVOKABLE void fetchHubDevice(const QString &hubId, const QString &deviceId);
    Q_INVOKABLE void fetchSpaceRooms(const QString &spaceId);
    Q_INVOKABLE void fetchMessages(const QString &spaceId);
    Q_INVOKABLE void startSpaceStream(const QString &spaceId);
    Q_INVOKABLE void stopSpaceStream();
    Q_INVOKABLE void startAlarmStream(const QString &spaceId);
    Q_INVOKABLE void stopAlarmStream();
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void spacesDiscovered(const QString &spaceId, const QString &spaceName, int count);
    void spacesListed(const QVariantList &spaces);
    void commandSucceeded(const QString &command, const QString &mode);
    void inventoryLoaded(const QVariantList &devices, const QVariantList &rooms);
    void roomsLoaded(const QVariantList &rooms);
    void devicesLoaded(const QVariantList &devices);
    void messagesLoaded(const QVariantList &messages);
    void alarmEventsLoaded(const QVariantList &messages);
    void failed(const QString &message);

private:
    enum class Operation {
        None,
        DiscoverSpaces,
        Arm,
        Disarm,
        Night,
        Panic,
        DebugLightDevices,
        DeviceDetails,
        SpaceRooms,
        Messages
    };

    void setBusy(bool busy);
    void callUnary(Operation operation, const QString &path, const QByteArray &protobuf);
    void onReadyRead();
    void onSpaceStreamReadyRead();
    void onSpaceStreamFinished();
    void onAlarmStreamReadyRead();
    void onAlarmStreamFinished();
    void onFinished();
    void fail(const QString &message);
    void parseResponse(Operation operation, const QByteArray &body);
    QString responseDetail(QNetworkReply *reply, int bodySize) const;
    void parseDiscoverSpacesResponse(const QByteArray &message);
    void parseLightDevicesResponse(const QByteArray &message);
    void parseHubDeviceResponse(const QByteArray &message);
    void parseSpaceRoomsResponse(const QByteArray &message);
    void parseMessagesResponse(const QByteArray &message);
    QVariantList parseUnconfirmedAlarmStreamMessages(const QByteArray &message) const;
    QVariantList parseNotificationLogStreamMessages(const QByteArray &message) const;
    QString parseSpaceStreamMode(const QByteArray &message) const;
    QString parseSpaceSuccessMode(const QByteArray &message) const;
    QString parseSpaceSnapshotMode(const QByteArray &message) const;
    QString parseSpaceUpdateMode(const QByteArray &message) const;
    QString parseSpaceSecurityMode(const QByteArray &message) const;
    QString parseDisplayedSecurityStateV2(const QByteArray &message) const;
    QString parseUserSpacesStreamMode(const QByteArray &message) const;
    QString parseUserSpacesSuccessMode(const QByteArray &message) const;
    QString parseUserSpacesInitialMode(const QByteArray &message) const;
    QString parseLiteSpaceMode(const QByteArray &message) const;
    QString parseLiteSpaceEventMode(const QByteArray &message) const;
    QString parseLiteSpaceUpdatedMode(const QByteArray &message) const;
    bool responseHasFailure(const QByteArray &message) const;
    void finishCommand(Operation operation);
    QByteArray makeFindSpacesRequest() const;
    QByteArray makeSpaceCommandRequest(const QString &spaceId, bool includeIgnoreAlarms) const;
    QByteArray makePanicRequest(const QString &spaceId) const;
    QByteArray makeFindNotificationsRequest(const QString &spaceId) const;
    QByteArray makeSpaceLocator(const QString &spaceId) const;
    QByteArray makeGrpcFrame(const QByteArray &protobuf) const;
    QString deviceId() const;
    void addAuthHeaders(QNetworkRequest &request) const;

    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_spaceStreamReply;
    QPointer<QNetworkReply> m_alarmStreamReply;
    QByteArray m_streamBuffer;
    QByteArray m_spaceStreamBuffer;
    QByteArray m_alarmStreamBuffer;
    QTimer m_timeout;
    bool m_busy;
    Operation m_operation;
    QString m_sessionTokenHex;
    QString m_userId;
    QString m_userRole;
    QString m_userLogin;
    QString m_sessionCookie;
    QString m_lastResponseDetail;
    QString m_pendingHubDeviceId;
    QString m_spaceStreamSpaceId;
    QString m_alarmStreamSpaceId;
};
