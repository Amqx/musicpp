//
// Created by Jonathan on 17-Nov-25.
//

#ifndef MUSICPP_STRINGUTILS_H
#define MUSICPP_STRINGUTILS_H

#include <string>
#include <libxml/tree.h>

bool FuzzyMatch(const std::string &a, const std::string &b);

std::wstring ConvertToWString(const std::string &str);

std::string ConvertWString(const std::wstring &wstr);

int LevenshteinDistance(const std::string &s1, const std::string &s2);

double CalculateSimilarity(const std::string &str1, const std::string &str2);

std::string ToLowerCase(const std::string &str);

std::string DiscordBounds(const std::wstring &wstr, const std::string &fallback);

std::string SanitizeKeys(std::string input);

std::string Md5(const std::string &input);

std::string CleanAlbumName(const std::string &input);

std::string GetAttribute(const xmlNodePtr &node, const char *attribute);

std::string GetText(const xmlNodePtr &node);

std::string Normalize(const std::string &text);

xmlNodePtr FindDescendantWithAttr(const xmlNodePtr &node, const char *tag_name, const char *attr_name,
                                  const char *attr_value);

xmlNodePtr FindDivWithClass(const xmlNodePtr &node, const std::string &class_value);

bool TrackMatches(const xmlNodePtr &li_node, const std::string &target_title, const std::string &target_artist);

xmlNodePtr FindMatchingListItem(const xmlNodePtr &node, const std::string &title, const std::string &artist);

std::wstring Truncate(const std::wstring &input);

std::wstring EscapeAmpersands(const std::wstring &s);

#endif //MUSICPP_STRINGUTILS_H