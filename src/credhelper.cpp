//
// Created by Jonathan on 11-Oct-25.
//

#include <credhelper.h>
#include <iostream>
#include <wincred.h>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

#include "stringutils.h"

std::wstring EnsureCredential(const std::wstring &keyPath, const std::wstring &friendlyName,
                              const std::wstring &helpUrl, bool forceReset, spdlog::logger *logger) {
    std::wstring value = ReadGenericCredential(keyPath);

    if (value.empty() || forceReset) {
        std::wcout << L"\n" << friendlyName << L" not set! Please get one here: " << helpUrl << std::endl;
        std::wcout << L"Enter your " << friendlyName << L":" << std::endl;

        std::wstring newValue;
        std::wcin >> newValue;

        WriteGenericCredential(keyPath, newValue, logger);
        if (logger) {
            logger->info("Stored new credential for {}", convertWString(friendlyName.c_str()));
        }
        return newValue;
    }
    return value;
}

inline void CheckError(BOOL result, const std::wstring &task, spdlog::logger *logger) {
    if (!result) {
        DWORD err = GetLastError();
        if (logger) {
            logger -> error("Failed to {}. Error code: {}", convertWString(task.c_str()), err);
        }
    }
}

void WriteGenericCredential(const std::wstring &targetName, const std::wstring &secret, spdlog::logger *logger) {
    const auto *secretBuffer = reinterpret_cast<const BYTE *>(secret.c_str());
    const std::vector secretBlob(
        secretBuffer,
        secretBuffer + (secret.size() + 1) * sizeof(wchar_t)
    );

    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(targetName.c_str());
    cred.CredentialBlob = const_cast<LPBYTE>(secretBlob.data());
    cred.CredentialBlobSize = static_cast<DWORD>(secretBlob.size());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    const BOOL success = CredWriteW(&cred, 0);

    CheckError(success, L"write credential", logger);
}

std::wstring ReadGenericCredential(const std::wstring &targetName, spdlog::logger *logger) {
    std::wstring credentialBuffer;
    PCREDENTIALW pCred = nullptr; // Pointer to receive the credential structure

    // 1. Call CredReadW
    BOOL success = CredReadW(
        targetName.c_str(), // Target name
        CRED_TYPE_GENERIC, // Type
        0, // Flags (must be 0)
        &pCred // Pointer to the result
    );

    if (success && pCred != nullptr) {
        if (pCred->CredentialBlob && pCred->CredentialBlobSize > 0) {
            const auto *secretData = reinterpret_cast<const wchar_t *>(pCred->CredentialBlob);
            const size_t charCount = pCred->CredentialBlobSize / sizeof(wchar_t);
            credentialBuffer.assign(secretData, secretData + charCount);
            if (!credentialBuffer.empty() && credentialBuffer.back() == L'\0') {
                credentialBuffer.pop_back();
            }
        }
        CredFree(pCred);
    } else {
        DWORD err = GetLastError();
        if (err != ERROR_NOT_FOUND) {
            CheckError(FALSE, L"read credential (Error code: " + std::to_wstring(err) + L")", logger);
        }
    }
    return credentialBuffer;
}

void DeleteGenericCredential(const std::wstring &targetName, spdlog::logger *logger) {
    BOOL success = CredDeleteW(
        targetName.c_str(), // Target name
        CRED_TYPE_GENERIC, // Type
        0 // Flags (must be 0)
    );

    CheckError(success, L"delete credential", logger);
}