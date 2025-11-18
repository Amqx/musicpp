//
// Created by Jonathan on 11-Oct-25.
//

#ifndef MUSICPP_CREDHELPER_H
#define MUSICPP_CREDHELPER_H

#include <Windows.h>
#include <string>
#include <spdlog/spdlog.h>

inline void CheckError(BOOL result, const std::wstring &task, spdlog::logger *logger = nullptr);

void WriteGenericCredential(const std::wstring &targetName, const std::wstring &secret, spdlog::logger *logger = nullptr);

std::wstring ReadGenericCredential(const std::wstring &targetName, spdlog::logger *logger = nullptr);

void DeleteGenericCredential(const std::wstring &targetName, spdlog::logger *logger = nullptr);

std::string wstr_to_str(const wchar_t *wstr);

std::wstring EnsureCredential(const std::wstring &keyPath, const std::wstring &friendlyName,
                              const std::wstring &helpUrl, bool forceReset, spdlog::logger *logger = nullptr);

#endif //MUSICPP_CREDHELPER_H
