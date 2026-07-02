#include "LocalSecretStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {
const char *StoreFileName = "ajax-session.store";
const char *SeedFileName = "local-secret.seed";
const char *StoreVersion = "1";

QByteArray sha256(const QByteArray &input)
{
    return QCryptographicHash::hash(input, QCryptographicHash::Sha256);
}
}

LocalSecretStore::LocalSecretStore(QObject *parent)
    : QObject(parent)
    , m_storageStatus(QStringLiteral("Protected local session store ready."))
{
}

QString LocalSecretStore::storageStatus() const
{
    return m_storageStatus;
}

bool LocalSecretStore::hasSession() const
{
    return QFileInfo::exists(storePath());
}

QVariantMap LocalSecretStore::loadSession()
{
    bool ok = false;
    QVariantMap payload = loadPayload(&ok);
    if (!ok) {
        return QVariantMap();
    }

    writePayload(payload);
    setStorageStatus(QStringLiteral("Protected local session loaded."));
    return payload;
}

bool LocalSecretStore::storeSession(const QString &apiBaseUrl,
                                    const QString &systemName,
                                    const QString &sessionToken,
                                    const QString &sessionSource,
                                    const QString &userId,
                                    const QString &userRole,
                                    const QString &userLogin,
                                    const QString &sessionCookie,
                                    const QString &spaceId)
{
    QVariantMap payload;
    payload.insert(QStringLiteral("apiBaseUrl"), apiBaseUrl);
    payload.insert(QStringLiteral("systemName"), systemName);
    payload.insert(QStringLiteral("sessionToken"), sessionToken);
    payload.insert(QStringLiteral("sessionSource"), sessionSource);
    payload.insert(QStringLiteral("userId"), userId);
    payload.insert(QStringLiteral("userRole"), userRole);
    payload.insert(QStringLiteral("userLogin"), userLogin);
    payload.insert(QStringLiteral("sessionCookie"), sessionCookie);
    payload.insert(QStringLiteral("spaceId"), spaceId);

    return writePayload(payload);
}

bool LocalSecretStore::updateSpace(const QString &spaceId, const QString &systemName)
{
    bool ok = false;
    QVariantMap payload = loadPayload(&ok);
    if (!ok || payload.value(QStringLiteral("sessionToken")).toString().isEmpty()) {
        return false;
    }

    payload.insert(QStringLiteral("spaceId"), spaceId);
    payload.insert(QStringLiteral("systemName"), systemName);
    return writePayload(payload);
}

void LocalSecretStore::clearSession()
{
    QFile::remove(storePath());
    setStorageStatus(QStringLiteral("Protected local session cleared."));
}

QString LocalSecretStore::storePath() const
{
    return QDir(basePath()).filePath(QString::fromLatin1(StoreFileName));
}

QString LocalSecretStore::basePath() const
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QDir::homePath() + QStringLiteral("/.local/share/ajaxremote.cloudsite");
    }
    return basePath;
}

QString LocalSecretStore::seedPath() const
{
    return QDir(basePath()).filePath(QString::fromLatin1(SeedFileName));
}

QByteArray LocalSecretStore::appKeySeed() const
{
    const QString path = seedPath();
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray seed = QByteArray::fromHex(file.readAll().trimmed());
        if (seed.size() >= 32) {
            return seed;
        }
    }

    QDir dir(QFileInfo(path).absolutePath());
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    const QByteArray seed = randomBytes(32);
    QSaveFile saveFile(path);
    if (saveFile.open(QIODevice::WriteOnly)) {
        saveFile.write(seed.toHex());
        if (saveFile.commit()) {
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
            return seed;
        }
    }

    return legacyAppDeviceId();
}

QByteArray LocalSecretStore::legacyAppDeviceId() const
{
    QSettings settings(QStringLiteral("cloudsite"), QStringLiteral("ajaxremote"));
    QString deviceId = settings.value(QStringLiteral("localSecretStore/deviceId")).toString();
    if (deviceId.isEmpty()) {
        const QByteArray random = randomBytes(32).toHex();
        deviceId = QString::fromLatin1(random);
        settings.setValue(QStringLiteral("localSecretStore/deviceId"), deviceId);
        settings.sync();
    }
    return deviceId.toUtf8();
}

QByteArray LocalSecretStore::machineId() const
{
    const QStringList candidates = {
        QStringLiteral("/etc/machine-id"),
        QStringLiteral("/var/lib/dbus/machine-id")
    };

    for (const QString &path : candidates) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray value = file.readAll().trimmed();
            if (!value.isEmpty()) {
                return value;
            }
        }
    }

    return QByteArray("unknown-machine");
}

QByteArray LocalSecretStore::keyMaterial(const QByteArray &salt, const QByteArray &appSeed) const
{
    QByteArray input;
    input += "AjaxRemote local protected session v1\n";
    input += appSeed;
    input += '\n';
    input += machineId();
    input += '\n';
    input += salt;
    return sha256(input);
}

QByteArray LocalSecretStore::deriveKey(const QByteArray &label, const QByteArray &material) const
{
    QByteArray input;
    input += "AjaxRemote key ";
    input += label;
    input += '\n';
    input += material;
    return sha256(input);
}

QByteArray LocalSecretStore::crypt(const QByteArray &input, const QByteArray &key, const QByteArray &nonce) const
{
    QByteArray output;
    output.resize(input.size());

    int offset = 0;
    quint32 counter = 0;
    while (offset < input.size()) {
        QByteArray blockInput;
        blockInput += key;
        blockInput += nonce;
        blockInput += QByteArray::number(counter++);
        const QByteArray stream = sha256(blockInput);
        for (int i = 0; i < stream.size() && offset < input.size(); ++i, ++offset) {
            output[offset] = input[offset] ^ stream[i];
        }
    }

    return output;
}

QByteArray LocalSecretStore::macFor(const QByteArray &macKey,
                                    const QByteArray &salt,
                                    const QByteArray &nonce,
                                    const QByteArray &cipherText) const
{
    QByteArray input;
    input += macKey;
    input += salt;
    input += nonce;
    input += cipherText;
    input += macKey;
    return sha256(input);
}

QByteArray LocalSecretStore::randomBytes(int length) const
{
    QByteArray bytes;
    bytes.resize(length);
    QRandomGenerator *generator = QRandomGenerator::system();
    for (int i = 0; i < length; i += 4) {
        const quint32 value = generator->generate();
        for (int j = 0; j < 4 && i + j < length; ++j) {
            bytes[i + j] = static_cast<char>((value >> (j * 8)) & 0xff);
        }
    }
    return bytes;
}

QList<QByteArray> LocalSecretStore::keySeeds() const
{
    QList<QByteArray> seeds;
    const QByteArray currentSeed = appKeySeed();
    if (!currentSeed.isEmpty()) {
        seeds.append(currentSeed);
    }

    const QByteArray legacySeed = legacyAppDeviceId();
    if (!legacySeed.isEmpty() && !seeds.contains(legacySeed)) {
        seeds.append(legacySeed);
    }
    return seeds;
}

QVariantMap LocalSecretStore::decodePayload(const QJsonObject &envelope, const QByteArray &appSeed, bool *ok) const
{
    if (ok) {
        *ok = false;
    }

    const QByteArray salt = QByteArray::fromBase64(envelope.value(QStringLiteral("salt")).toString().toLatin1());
    const QByteArray nonce = QByteArray::fromBase64(envelope.value(QStringLiteral("nonce")).toString().toLatin1());
    const QByteArray cipherText = QByteArray::fromBase64(envelope.value(QStringLiteral("ciphertext")).toString().toLatin1());
    const QByteArray storedMac = QByteArray::fromBase64(envelope.value(QStringLiteral("mac")).toString().toLatin1());
    if (salt.isEmpty() || nonce.isEmpty() || cipherText.isEmpty() || storedMac.isEmpty()) {
        return QVariantMap();
    }

    const QByteArray material = keyMaterial(salt, appSeed);
    const QByteArray encKey = deriveKey("enc", material);
    const QByteArray macKey = deriveKey("mac", material);
    if (macFor(macKey, salt, nonce, cipherText) != storedMac) {
        return QVariantMap();
    }

    const QByteArray clearText = crypt(cipherText, encKey, nonce);
    const QJsonDocument payloadDoc = QJsonDocument::fromJson(clearText);
    if (!payloadDoc.isObject()) {
        return QVariantMap();
    }

    if (ok) {
        *ok = true;
    }
    return payloadDoc.object().toVariantMap();
}

QVariantMap LocalSecretStore::loadPayload(bool *ok)
{
    if (ok) {
        *ok = false;
    }

    QFile file(storePath());
    if (!file.open(QIODevice::ReadOnly)) {
        setStorageStatus(QStringLiteral("No protected local session found."));
        return QVariantMap();
    }

    const QJsonDocument envelopeDoc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject envelope = envelopeDoc.object();
    if (envelope.value(QStringLiteral("version")).toString() != QString::fromLatin1(StoreVersion)) {
        setStorageStatus(QStringLiteral("Protected local session has an unsupported version."));
        return QVariantMap();
    }

    if (envelope.value(QStringLiteral("salt")).toString().isEmpty()
            || envelope.value(QStringLiteral("nonce")).toString().isEmpty()
            || envelope.value(QStringLiteral("ciphertext")).toString().isEmpty()
            || envelope.value(QStringLiteral("mac")).toString().isEmpty()) {
        setStorageStatus(QStringLiteral("Protected local session is incomplete."));
        return QVariantMap();
    }

    const QList<QByteArray> seeds = keySeeds();
    for (const QByteArray &seed : seeds) {
        bool decodeOk = false;
        const QVariantMap payload = decodePayload(envelope, seed, &decodeOk);
        if (decodeOk) {
            if (ok) {
                *ok = true;
            }
            return payload;
        }
    }

    setStorageStatus(QStringLiteral("Protected local session could not be verified."));
    return QVariantMap();
}

bool LocalSecretStore::writePayload(const QVariantMap &payload)
{
    const QString path = storePath();
    QDir dir(QFileInfo(path).absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        setStorageStatus(QStringLiteral("Could not create protected local session directory."));
        return false;
    }

    const QByteArray salt = randomBytes(16);
    const QByteArray nonce = randomBytes(16);
    const QByteArray material = keyMaterial(salt, appKeySeed());
    const QByteArray encKey = deriveKey("enc", material);
    const QByteArray macKey = deriveKey("mac", material);
    const QByteArray clearText = QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact);
    const QByteArray cipherText = crypt(clearText, encKey, nonce);

    QJsonObject envelope;
    envelope.insert(QStringLiteral("version"), QString::fromLatin1(StoreVersion));
    envelope.insert(QStringLiteral("salt"), QString::fromLatin1(salt.toBase64()));
    envelope.insert(QStringLiteral("nonce"), QString::fromLatin1(nonce.toBase64()));
    envelope.insert(QStringLiteral("ciphertext"), QString::fromLatin1(cipherText.toBase64()));
    envelope.insert(QStringLiteral("mac"), QString::fromLatin1(macFor(macKey, salt, nonce, cipherText).toBase64()));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setStorageStatus(QStringLiteral("Could not open protected local session file."));
        return false;
    }

    file.write(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        setStorageStatus(QStringLiteral("Could not save protected local session file."));
        return false;
    }

    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    setStorageStatus(QStringLiteral("Protected local session saved."));
    return true;
}

void LocalSecretStore::setStorageStatus(const QString &status)
{
    if (m_storageStatus == status) {
        return;
    }
    m_storageStatus = status;
    emit storageStatusChanged();
}
