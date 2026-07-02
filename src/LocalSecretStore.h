#ifndef LOCALSECRETSTORE_H
#define LOCALSECRETSTORE_H

#include <QObject>
#include <QVariantMap>

class LocalSecretStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString storageStatus READ storageStatus NOTIFY storageStatusChanged)

public:
    explicit LocalSecretStore(QObject *parent = nullptr);

    QString storageStatus() const;

    Q_INVOKABLE bool hasSession() const;
    Q_INVOKABLE QVariantMap loadSession();
    Q_INVOKABLE bool storeSession(const QString &apiBaseUrl,
                                  const QString &systemName,
                                  const QString &sessionToken,
                                  const QString &sessionSource,
                                  const QString &userId,
                                  const QString &userRole,
                                  const QString &userLogin,
                                  const QString &sessionCookie,
                                  const QString &spaceId);
    Q_INVOKABLE bool updateSpace(const QString &spaceId, const QString &systemName);
    Q_INVOKABLE void clearSession();

signals:
    void storageStatusChanged();

private:
    QString basePath() const;
    QString storePath() const;
    QString seedPath() const;
    QByteArray appKeySeed() const;
    QByteArray legacyAppDeviceId() const;
    QByteArray machineId() const;
    QByteArray keyMaterial(const QByteArray &salt, const QByteArray &appSeed) const;
    QByteArray deriveKey(const QByteArray &label, const QByteArray &material) const;
    QByteArray crypt(const QByteArray &input, const QByteArray &key, const QByteArray &nonce) const;
    QByteArray macFor(const QByteArray &macKey,
                      const QByteArray &salt,
                      const QByteArray &nonce,
                      const QByteArray &cipherText) const;
    QByteArray randomBytes(int length) const;
    QList<QByteArray> keySeeds() const;
    QVariantMap decodePayload(const QJsonObject &envelope, const QByteArray &appSeed, bool *ok = nullptr) const;
    QVariantMap loadPayload(bool *ok = nullptr);
    bool writePayload(const QVariantMap &payload);
    void setStorageStatus(const QString &status);

    QString m_storageStatus;
};

#endif // LOCALSECRETSTORE_H
