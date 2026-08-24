#include "device_identity.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

namespace {

void setError(QString* error, const QString& value) {
    if (error) {
        *error = value;
    }
}

QString deviceDirectory() {
    const QString overrideDirectory = qEnvironmentVariable("LAN_CHAT_TEST_DEVICE_DATA_ROOT").trimmed();
    if (!overrideDirectory.isEmpty()) {
        return QDir::cleanPath(overrideDirectory);
    }
    const QString localData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(QDir(localData).filePath(QStringLiteral("DUSK1NG/LAN Chat/devices")));
}

bool certificateScope(const QString& certificateFile, QByteArray* scope, QString* error) {
    if (!scope) {
        setError(error, QStringLiteral("Device scope output is unavailable"));
        return false;
    }
    QFile certificate(certificateFile);
    if (!certificate.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Unable to read the trusted certificate"));
        return false;
    }
    const QByteArray contents = certificate.readAll();
    if (contents.isEmpty()) {
        setError(error, QStringLiteral("Trusted certificate is empty"));
        return false;
    }
    *scope = QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
    return true;
}

bool protect(const QByteArray& plaintext, QByteArray* encrypted, QString* error) {
    if (!encrypted || plaintext.isEmpty()) {
        setError(error, QStringLiteral("Device credential is empty"));
        return false;
    }
    DATA_BLOB input{static_cast<DWORD>(plaintext.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.constData()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"LAN Chat device credential", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        setError(error, QStringLiteral("Windows could not protect the device credential"));
        return false;
    }
    *encrypted = QByteArray(reinterpret_cast<const char*>(output.pbData), static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return true;
}

bool unprotect(const QByteArray& encrypted, QByteArray* plaintext, QString* error) {
    if (!plaintext || encrypted.isEmpty()) {
        setError(error, QStringLiteral("Stored device credential is empty"));
        return false;
    }
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.constData()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        setError(error, QStringLiteral("Windows could not unlock the device credential"));
        return false;
    }
    *plaintext = QByteArray(reinterpret_cast<const char*>(output.pbData), static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return true;
}

bool makeToken(QByteArray* token, QString* error) {
    if (!token) {
        setError(error, QStringLiteral("Device credential output is unavailable"));
        return false;
    }
    QByteArray random(32, Qt::Uninitialized);
    const NTSTATUS status = BCryptGenRandom(nullptr,
                                            reinterpret_cast<PUCHAR>(random.data()),
                                            static_cast<ULONG>(random.size()),
                                            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) {
        setError(error, QStringLiteral("Windows random generator is unavailable"));
        return false;
    }
    *token = random.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return true;
}

} // namespace

namespace DeviceIdentity {

bool loadOrCreate(const QString& certificateFile, QString* token, QString* error) {
    if (!token) {
        setError(error, QStringLiteral("Device credential output is unavailable"));
        return false;
    }
    token->clear();

    QByteArray scope;
    if (!certificateScope(certificateFile, &scope, error)) {
        return false;
    }
    const QDir directory(deviceDirectory());
    if (!directory.mkpath(QStringLiteral("."))) {
        setError(error, QStringLiteral("Unable to create the local device credential directory"));
        return false;
    }
    const QString credentialPath = directory.filePath(QString::fromLatin1(scope) + QStringLiteral(".bin"));
    QFile credentialFile(credentialPath);
    QByteArray protectedToken;
    if (credentialFile.exists()) {
        if (!credentialFile.open(QIODevice::ReadOnly)) {
            setError(error, QStringLiteral("Unable to read the local device credential"));
            return false;
        }
        protectedToken = credentialFile.readAll();
        QByteArray plaintext;
        if (!unprotect(protectedToken, &plaintext, error) || plaintext.isEmpty()) {
            return false;
        }
        *token = QString::fromLatin1(plaintext);
        return true;
    }

    QByteArray plaintext;
    if (!makeToken(&plaintext, error) || !protect(plaintext, &protectedToken, error)) {
        return false;
    }
    if (!credentialFile.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        credentialFile.write(protectedToken) != protectedToken.size()) {
        setError(error, QStringLiteral("Unable to save the local device credential"));
        return false;
    }
    *token = QString::fromLatin1(plaintext);
    return true;
}

} // namespace DeviceIdentity
