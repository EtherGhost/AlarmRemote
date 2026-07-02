#pragma once

#include <QObject>
#include <QByteArray>
#include <QPointer>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

class QrLoginClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)

public:
    explicit QrLoginClient(QObject *parent = nullptr);

    bool running() const;
    QString state() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();

signals:
    void runningChanged();
    void stateChanged();
    void qrPayloadReady(const QString &payload);
    void loginSucceeded(const QString &sessionTokenHex, const QString &userId, const QString &userRole,
                        const QString &userLogin, const QString &sessionCookie);
    void failed(const QString &message);

private:
    void setRunning(bool running);
    void setState(const QString &state);
    void onReadyRead();
    void onFinished();
    void fail(const QString &message);
    void parseBufferedFrames();
    void parseStreamResponse(const QByteArray &message);
    void parseSuccess(const QByteArray &message);
    void parseInitial(const QByteArray &message);
    void parseLoginResponse(const QByteArray &message);
    void parseLiteAccount(const QByteArray &message, QString &userId, QString &userRole, QString &userLogin);
    QString sessionCookie() const;
    QString deviceId() const;

    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QByteArray m_buffer;
    bool m_running;
    bool m_completed;
    QString m_state;
};
