/**
 * @file credentials.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 26-Jun-26
 */

#include "security/credentials.hpp"
#include <windows.h>
#include <wincred.h>

std::string readCredential(const std::string &target) {
    PCREDENTIALA cred = nullptr;
    if (!CredReadA(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) || cred == nullptr) {
        return {};
    }

    std::string result;
    if (cred->CredentialBlob && cred->CredentialBlobSize > 0) {
        const auto *data = reinterpret_cast<const char *>(cred->CredentialBlob);
        result.assign(data, data + cred->CredentialBlobSize);

        // Strip all trailing null terminators
        const auto end = result.find_last_not_of('\0');
        result = (end != std::string::npos) ? result.substr(0, end + 1) : "";
    }

    CredFree(cred);
    return result;
}

void writeCredential(const std::string &target, const std::string &secret) {
    CREDENTIALA cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPSTR>(target.c_str());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(secret.c_str()));
    cred.CredentialBlobSize = static_cast<DWORD>(secret.size());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

    (void) CredWriteA(&cred, 0);
}

void deleteCredential(const std::string &target) {
    (void) CredDeleteA(target.c_str(), CRED_TYPE_GENERIC, 0);
}
