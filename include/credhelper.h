//
// Created by Jonathan on 11-Oct-25.
//

#ifndef MUSICPP_CREDHELPER_H
#define MUSICPP_CREDHELPER_H

#include <Windows.h>
#include <string>

inline void CheckError(BOOL result, const std::wstring &task);

void WriteGenericCredential(const std::wstring &targetName, const std::wstring &secret);

std::wstring ReadGenericCredential(const std::wstring &targetName);

void DeleteGenericCredential(const std::wstring &targetName);

std::string wstr_to_str(const wchar_t *wstr);

std::wstring EnsureCredential(const std::wstring &keyPath, const std::wstring &friendlyName,
                              const std::wstring &helpUrl, bool forceReset);

#endif //MUSICPP_CREDHELPER_H
