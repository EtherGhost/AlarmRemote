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
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void spacesDiscovered(const QString &spaceId, const QString &spaceName, int count);
    void spacesListed(const QVariantList &spaces);
    void commandSucceeded(const QString &command, const QString &mode);
    void failed(const QString &message);

private:
    enum class Operation {
        None,
        DiscoverSpaces,
        Arm,
        Disarm,
        Night,
        Panic
    };

    void setBusy(bool busy);
    void callUnary(Operation operation, const QString &path, const QByteArray &protobuf);
    void onFinished();
    void fail(const QString &message);
    void parseResponse(Operation operation, const QByteArray &body);
    QString responseDetail(QNetworkReply *reply, int bodySize) const;
    void parseDiscoverSpacesResponse(const QByteArray &message);
    bool responseHasFailure(const QByteArray &message) const;
    void finishCommand(Operation operation);
    QByteArray makeFindSpacesRequest() const;
    QByteArray makeSpaceCommandRequest(const QString &spaceId, bool includeIgnoreAlarms) const;
    QByteArray makePanicRequest(const QString &spaceId) const;
    QByteArray makeSpaceLocator(const QString &spaceId) const;
    QByteArray makeGrpcFrame(const QByteArray &protobuf) const;
    QString deviceId() const;
    void addAuthHeaders(QNetworkRequest &request) const;

    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    bool m_busy;
    Operation m_operation;
    QString m_sessionTokenHex;
    QString m_userId;
    QString m_userRole;
    QString m_userLogin;
    QString m_sessionCookie;
    QString m_lastResponseDetail;
};
