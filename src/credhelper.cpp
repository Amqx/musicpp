//
// Created by Jonathan on 11-Oct-25.
//

#include <credhelper.h>
#include <iostream>
#include <wincred.h>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

#include "consoleutils.h"
#include "constants.h"
#include "stringutils.h"
#include "utils.h"

std::wstring EnsureCredential(const std::wstring &key_path, const std::wstring &friendly_name,
                              const std::wstring &help_url, bool &console_created, spdlog::logger *logger) {
    std::wstring value = ReadGenericCredential(key_path);

    if (value.empty()) {
        if (!console_created) {
            SetupConsole();
            console_created = true;
        }
        std::wcout << L"\n" << friendly_name << L" not set! Please get one here: " << help_url << std::endl;
        std::wcout << L"Enter your " << friendly_name << L":" << std::endl;

        std::wstring new_value;
        if (key_path == kSpotifyDbClientIdKey) {
            while (std::wcin >> new_value) {
                if (ValidateInput(new_value, SAPI_CLIENT_ID)) break;

                std::wcout << L"Invalid key format! Try again: " << std::endl;
            }
        }
        if (key_path == kSpotifyDbClientSecretKey) {
            while (std::wcin >> new_value) {
                if (ValidateInput(new_value, SAPI_SECRET)) break;

                std::wcout << L"Invalid key format! Try again: " << std::endl;
            }
        }
        if (key_path == kLastFmDbApikey) {
            while (std::wcin >> new_value) {
                if (ValidateInput(new_value, LFM_KEY)) break;

                std::wcout << L"Invalid key format! Try again: " << std::endl;
            }
        }
        if (key_path == kLastFmDbSecret) {
            while (std::wcin >> new_value) {
                if (ValidateInput(new_value, LFM_SECRET)) break;

                std::wcout << L"Invalid key format! Try again: " << std::endl;
            }
        }
        if (key_path == kImgurDbClientIdKey) {
            while (std::wcin >> new_value) {
                if (ValidateInput(new_value, IMGUR)) break;

                std::wcout << L"Invalid key format! Try again: " << std::endl;
            }
        }

        WriteGenericCredential(key_path, new_value, logger);
        if (logger) {
            logger->info("Stored new credential for {}", ConvertWString(friendly_name.c_str()));
        }
        return new_value;
    }
    return value;
}

inline void CheckError(const BOOL result, const std::wstring &task, spdlog::logger *logger) {
    if (!result) {
        DWORD err = GetLastError();
        if (logger) {
            logger->error("Failed to {}. Error code: {}", ConvertWString(task.c_str()), err);
        }
    }
}

void WriteGenericCredential(const std::wstring &target_name, const std::wstring &secret, spdlog::logger *logger) {
    const auto *secret_buffer = reinterpret_cast<const BYTE *>(secret.c_str());
    const std::vector secret_blob(
        secret_buffer,
        secret_buffer + (secret.size() + 1) * sizeof(wchar_t)
    );

    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target_name.c_str());
    cred.CredentialBlob = const_cast<LPBYTE>(secret_blob.data());
    cred.CredentialBlobSize = static_cast<DWORD>(secret_blob.size());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    const BOOL success = CredWriteW(&cred, 0);

    CheckError(success, L"write credential", logger);
}

std::wstring ReadGenericCredential(const std::wstring &target_name, spdlog::logger *logger) {
    std::wstring credential_buffer;
    PCREDENTIALW p_cred = nullptr; // Pointer to receive the credential structure

    const BOOL success = CredReadW(
        target_name.c_str(),
        CRED_TYPE_GENERIC,
        0,
        &p_cred
    );

    if (success && p_cred != nullptr) {
        if (p_cred->CredentialBlob && p_cred->CredentialBlobSize > 0) {
            const auto *secret_data = reinterpret_cast<const wchar_t *>(p_cred->CredentialBlob);
            const size_t char_count = p_cred->CredentialBlobSize / sizeof(wchar_t);
            credential_buffer.assign(secret_data, secret_data + char_count);
            if (!credential_buffer.empty() && credential_buffer.back() == L'\0') {
                credential_buffer.pop_back();
            }
        }
        CredFree(p_cred);
    } else {
        if (const DWORD err = GetLastError(); err != ERROR_NOT_FOUND) {
            CheckError(FALSE, L"read credential (Error code: " + std::to_wstring(err) + L")", logger);
        }
    }
    return credential_buffer;
}

void DeleteGenericCredential(const std::wstring &target_name, spdlog::logger *logger) {
    const BOOL success = CredDeleteW(
        target_name.c_str(), // Target name
        CRED_TYPE_GENERIC, // Type
        0 // Flags (must be 0)
    );

    CheckError(success, L"delete credential", logger);
}
