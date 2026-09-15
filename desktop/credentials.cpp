#include "credentials.h"
#ifdef Q_OS_MACOS
#include <Security/Security.h>
namespace {
CFMutableDictionaryRef query(const QString &key) {
    auto q = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    auto account = key.toCFString();
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, CFSTR("com.speedsync.sql"));
    CFDictionarySetValue(q, kSecAttrAccount, account);
    CFRelease(account);
    return q;
}
bool check(OSStatus status, QString &error) {
    if (status == errSecSuccess) return true;
    error = QStringLiteral("系统钥匙串操作失败（%1）").arg(status);
    return false;
}
}
QString Credentials::read(const QString &key, QString &error, bool *missing) {
    if (missing) *missing = false;
    auto q = query(key);
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFTypeRef result = nullptr;
    auto status = SecItemCopyMatching(q, &result);
    CFRelease(q);
    if (status == errSecItemNotFound) { if (missing) *missing = true; error = QStringLiteral("已保存的密码不存在，请重新输入"); return {}; }
    if (!check(status, error)) return {};
    auto bytes = static_cast<CFDataRef>(result);
    auto value = QString::fromUtf8(reinterpret_cast<const char *>(CFDataGetBytePtr(bytes)), CFDataGetLength(bytes));
    CFRelease(result);
    return value;
}
bool Credentials::write(const QString &key, const QString &value, QString &error) {
    auto q = query(key);
    auto bytes = value.toUtf8();
    auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(bytes.constData()), bytes.size());
    auto attrs = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kSecValueData, data);
    auto status = SecItemUpdate(q, attrs);
    if (status == errSecItemNotFound) {
        CFDictionarySetValue(q, kSecValueData, data);
        status = SecItemAdd(q, nullptr);
    }
    CFRelease(attrs); CFRelease(data); CFRelease(q);
    return check(status, error);
}
bool Credentials::remove(const QString &key, QString &error) {
    auto q = query(key); auto status = SecItemDelete(q); CFRelease(q);
    return status == errSecItemNotFound || check(status, error);
}
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincred.h>
namespace {
QString target(const QString &key) { return "SpeedSyncSQL/" + key; }
bool check(bool ok, QString &error) {
    if (!ok) error = QStringLiteral("系统凭据操作失败（%1）").arg(GetLastError());
    return ok;
}
}
QString Credentials::read(const QString &key, QString &error, bool *missing) {
    if (missing) *missing = false;
    auto name = target(key); PCREDENTIALW cred = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(name.utf16()), CRED_TYPE_GENERIC, 0, &cred)) {
        const auto code = GetLastError();
        if (missing) *missing = code == ERROR_NOT_FOUND;
        error = QStringLiteral("系统凭据读取失败（%1）").arg(code); return {};
    }
    auto value = QString::fromUtf8(reinterpret_cast<const char *>(cred->CredentialBlob), cred->CredentialBlobSize);
    CredFree(cred); return value;
}
bool Credentials::write(const QString &key, const QString &value, QString &error) {
    auto name = target(key); auto bytes = value.toUtf8();
    CREDENTIALW cred{}; cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = reinterpret_cast<LPWSTR>(name.data());
    cred.CredentialBlobSize = static_cast<DWORD>(bytes.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return check(CredWriteW(&cred, 0), error);
}
bool Credentials::remove(const QString &key, QString &error) {
    auto name = target(key);
    if (CredDeleteW(reinterpret_cast<LPCWSTR>(name.utf16()), CRED_TYPE_GENERIC, 0)) return true;
    return GetLastError() == ERROR_NOT_FOUND || check(false, error);
}
#else
QString Credentials::read(const QString &, QString &e, bool *missing) { if (missing) *missing = false; e = "当前平台没有凭据实现"; return {}; }
bool Credentials::write(const QString &, const QString &, QString &e) { e = "当前平台没有凭据实现"; return false; }
bool Credentials::remove(const QString &, QString &e) { e = "当前平台没有凭据实现"; return false; }
#endif
