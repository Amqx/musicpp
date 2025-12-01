//
// Created by Jonathan on 11-Oct-25.
//

#ifndef MUSICPP_CREDHELPER_H
#define MUSICPP_CREDHELPER_H

#include <Windows.h>
#include <string>

namespace spdlog {
    class logger;
}

inline void CheckError(BOOL result, const std::wstring &task, spdlog::logger *logger = nullptr);

void WriteGenericCredential(const std::wstring &target_name, const std::wstring &secret,
                            spdlog::logger *logger = nullptr);

std::wstring ReadGenericCredential(const std::wstring &target_name, spdlog::logger *logger = nullptr);

void DeleteGenericCredential(const std::wstring &target_name, spdlog::logger *logger = nullptr);

std::string wstr_to_str(const wchar_t *wstr);

std::wstring EnsureCredential(const std::wstring &key_path, const std::wstring &friendly_name,
                              const std::wstring &help_url, bool force_reset, spdlog::logger *logger = nullptr);

#endif //MUSICPP_CREDHELPER_H