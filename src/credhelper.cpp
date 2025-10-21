//
// Created by Jonathan on 11-Oct-25.
//

#include "../include/credhelper.h"
#include <wincred.h>
#include <iostream>
#include <string>
#include <vector>

inline void CheckError(BOOL result, const std::wstring &task) {
    if (!result) {
        DWORD err = GetLastError();
        std::wcerr << L"Failed to " << task << L". Error: " << err << std::endl;
    }
}

void WriteGenericCredential(const std::wstring &targetName, const std::wstring &secret) {
    const auto *secretBuffer = reinterpret_cast<const BYTE *>(secret.c_str());
    const std::vector<BYTE> secretBlob(
        secretBuffer,
        secretBuffer + (secret.size() + 1) * sizeof(wchar_t)
    );

    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(targetName.c_str());
    cred.CredentialBlob = const_cast<LPBYTE>(secretBlob.data());
    cred.CredentialBlobSize = static_cast<DWORD>(secretBlob.size());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    BOOL success = CredWriteW(&cred, 0);

    CheckError(success, L"write credential");
}

std::wstring ReadGenericCredential(const std::wstring &targetName) {
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
            const wchar_t *secretData = reinterpret_cast<const wchar_t *>(pCred->CredentialBlob);
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
            CheckError(FALSE, L"read credential (Error code: " + std::to_wstring(err) + L")");
        }
    }
    return credentialBuffer;
}

void DeleteGenericCredential(const std::wstring &targetName) {
    BOOL success = CredDeleteW(
        targetName.c_str(), // Target name
        CRED_TYPE_GENERIC, // Type
        0 // Flags (must be 0)
    );

    CheckError(success, L"delete credential");
}

std::string wstr_to_str(const wchar_t *wstr) {
    if (wstr == nullptr) {
        return "";
    }

    int required_size = WideCharToMultiByte(
        CP_UTF8, // CodePage: Convert to UTF-8
        0, // dwFlags: Default flags
        wstr, // lpWideCharStr: The source wide string
        -1, // cchWideChar: Input string is null-terminated
        nullptr, // lpMultiByteStr: Output buffer (NULL to get size)
        0, // cbMultiByte: Output buffer size (0 to get size)
        nullptr, // lpDefaultChar: Not used with CP_UTF8
        nullptr // lpUsedDefaultChar: Not used with CP_UTF8
    );

    if (required_size <= 0) {
        return "";
    }

    std::string narrow_str(required_size, '\0');

    WideCharToMultiByte(
        CP_UTF8, // CodePage: Convert to UTF-8
        0, // dwFlags
        wstr, // lpWideCharStr
        -1, // cchWideChar
        &narrow_str[0], // lpMultiByteStr: Use the internal buffer of the std::string
        required_size, // cbMultiByte: Size of the buffer
        nullptr, // lpDefaultChar
        nullptr // lpUsedDefaultChar
    );

    narrow_str.resize(required_size - 1);
    return narrow_str;
}