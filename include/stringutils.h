//
// Created by Jonathan on 17-Nov-25.
//

#ifndef MUSICPP_STRINGUTILS_H
#define MUSICPP_STRINGUTILS_H

#include <string>

std::string convertWString(const std::wstring &wstr);

bool is_extended_whitespace(wchar_t ch);

std::wstring trim_copy(const std::wstring &value);

bool split_artist_album(const std::wstring &input, std::wstring &outArtist, std::wstring &outAlbum);

int levenshteinDistance(const std::string &s1, const std::string &s2);

double calculateSimilarity(const std::string &str1, const std::string &str2);

std::string toLowerCase(const std::string &str);

std::string discord_bounds(const std::wstring &wstr, const std::string &fallback);

std::string sanitizeKeys(std::string input);

#endif //MUSICPP_STRINGUTILS_H