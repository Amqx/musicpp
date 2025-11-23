//
// Created by Jonathan on 17-Nov-25.
//

#ifndef MUSICPP_STRINGUTILS_H
#define MUSICPP_STRINGUTILS_H

#include <string>

std::wstring convertToWString(const std::string &str);

std::string convertWString(const std::wstring &wstr);

int levenshteinDistance(const std::string &s1, const std::string &s2);

double calculateSimilarity(const std::string &str1, const std::string &str2);

std::string toLowerCase(const std::string &str);

std::string discord_bounds(const std::wstring &wstr, const std::string &fallback);

std::string sanitizeKeys(std::string input);

std::string md5(const std::string &input);

std::string cleanAlbumName(const std::string &input);

const char *b(bool v);

#endif //MUSICPP_STRINGUTILS_H