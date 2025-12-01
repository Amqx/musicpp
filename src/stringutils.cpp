//
// Created by Jonathan on 17-Nov-25.
//

#include <windows.h>
#include <vector>
#include <algorithm>
#include <wincrypt.h>
#include "constants.h"
#include "stringutils.h"

using namespace std;

bool FuzzyMatch(const std::string &a, const std::string &b) {
    return CalculateSimilarity(a, b) >= kMatchGenerosity;
}

string ConvertWString(const wstring &wstr) {
    if (wstr.empty()) {
        return "";
    }

    const int required_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        wstr.data(),
        static_cast<int>(wstr.length()),
        nullptr,
        0,
        nullptr, nullptr
    );

    if (required_size <= 0) {
        return ""; // Conversion error
    }
    std::string narrow_str(required_size, '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        wstr.data(),
        static_cast<int>(wstr.length()),
        &narrow_str[0],
        required_size,
        nullptr, nullptr
    );

    return narrow_str;
}

std::string CleanAlbumName(const std::string &input) {
    auto pos = input.rfind(" — Single");

    if (pos == string::npos) {
        pos = input.rfind(" — EP");
        if (pos == string::npos) {
            return input;
        }
    }
    return input.substr(0, pos);
}

wstring ConvertToWString(const string &str) {
    if (str.empty()) {
        return L"";
    }

    const int required_size = MultiByteToWideChar(CP_UTF8, 0, str.data(),
                                                  static_cast<int>(str.length()), nullptr, 0);

    if (required_size <= 0) {
        return L"";
    }

    wstring wstr(required_size, L'\0');

    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.length()),
                        &wstr[0], required_size);

    return wstr;
}

int LevenshteinDistance(const string &s1, const string &s2) {
    const size_t len1 = s1.size();
    const size_t len2 = s2.size();
    vector dp(len1 + 1, vector<int>(len2 + 1));

    for (size_t i = 0; i <= len1; i++) {
        dp[i][0] = i;
    }
    for (size_t j = 0; j <= len2; j++) {
        dp[0][j] = j;
    }

    for (size_t i = 1; i <= len1; i++) {
        for (size_t j = 1; j <= len2; j++) {
            const int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            dp[i][j] = min({
                dp[i - 1][j] + 1, // deletion
                dp[i][j - 1] + 1, // insertion
                dp[i - 1][j - 1] + cost // substitution
            });
        }
    }

    return dp[len1][len2];
}

double CalculateSimilarity(const string &str1, const string &str2) {
    if (str1.empty() && str2.empty()) return 100.0;
    if (str1.empty() || str2.empty()) return 0.0;

    const string s1 = ToLowerCase(str1);
    const string s2 = ToLowerCase(str2);

    const double distance = LevenshteinDistance(s1, s2);
    const double max_len = max(s1.length(), s2.length());

    return (max_len - distance) / max_len * 100.0;
}

string ToLowerCase(const string &str) {
    string result = str;
    ranges::transform(result, result.begin(),
                      [](const unsigned char c) { return tolower(c); });
    return result;
}

std::string DiscordBounds(const wstring &wstr, const string &fallback) {
    const size_t len = wstr.length();
    if (len > kDiscordMaxStrLen) {
        string input = ConvertWString(wstr);
        auto l = input.begin();
        while (l != input.end() && isspace(*l)) ++l;
        auto r = input.end();
        do {
            if (r == l) {
                break;
            }
            --r;
        } while (isspace(*r));

        string out;
        if (r >= l) {
            out.assign(l, r + 1);
        }
        if (out.size() > kDiscordMaxStrLen) {
            out.resize(kDiscordMaxStrLen);
        }

        return out;
    }
    if (len <= 1) {
        return fallback;
    }
    return ConvertWString(wstr);
}

std::string SanitizeKeys(std::string input) {
    ranges::replace(input, '|', '-');
    return input;
}

std::string Md5(const std::string &input) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE digest[16];
    DWORD digest_len = 16;

    if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return "";
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }

    CryptHashData(hHash, reinterpret_cast<const BYTE *>(input.data()), input.size(), 0);
    CryptGetHashParam(hHash, HP_HASHVAL, digest, &digest_len, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);

    for (const unsigned char b: digest) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0x0F]);
    }

    return out;
}

std::string GetAttribute(const xmlNodePtr &node, const char *attribute) {
    if (!node) return "";
    xmlChar *value = xmlGetProp(node, reinterpret_cast<const xmlChar *>(attribute));
    if (!value) return "";
    std::string out = reinterpret_cast<char *>(value);
    xmlFree(value);
    return out;
}

std::string GetText(const xmlNodePtr &node) {
    if (!node) return "";
    xmlChar *content = xmlNodeGetContent(node); // Gets content of node and descendants
    if (!content) return "";
    std::string out = reinterpret_cast<char *>(content);
    xmlFree(content);
    return out;
}

std::string Normalize(const std::string &text) {
    const size_t start = text.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";

    const size_t end = text.find_last_not_of(" \t\n\r");
    std::string trimmed = text.substr(start, end - start + 1);

    std::ranges::transform(trimmed, trimmed.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return trimmed;
}

xmlNodePtr FindDescendantWithAttr(const xmlNodePtr &node, const char *tag_name, const char *attr_name,
                                  const char *attr_value) {
    for (xmlNodePtr current = node; current; current = current->next) {
        if (current->type == XML_ELEMENT_NODE) {
            if (!tag_name || xmlStrcasecmp(current->name, reinterpret_cast<const xmlChar *>(tag_name)) == 0) {
                if (std::string attr = GetAttribute(current, attr_name); !attr.empty() && attr == attr_value) {
                    return current;
                }
            }
        }
        // Recurse into children
        if (current->children) {
            if (const xmlNodePtr found = FindDescendantWithAttr(current->children, tag_name, attr_name, attr_value)) {
                return found;
            }
        }
    }
    return nullptr;
}

xmlNodePtr FindDivWithClass(const xmlNodePtr &node, const std::string &class_value) {
    for (xmlNodePtr current = node; current; current = current->next) {
        if (current->type == XML_ELEMENT_NODE && xmlStrcasecmp(current->name, reinterpret_cast<const xmlChar *>("div"))
            == 0) {
            if (std::string classes = GetAttribute(current, "class");
                !classes.empty() && classes.find(class_value) != std::string::npos) {
                return current;
            }
        }
        if (current->children) {
            if (const xmlNodePtr found = FindDivWithClass(current->children, class_value)) {
                return found;
            }
        }
    }
    return nullptr;
}

bool TrackMatches(const xmlNodePtr &li_node, const std::string &target_title, const std::string &target_artist) {
    const xmlNodePtr title_node = FindDescendantWithAttr(li_node, nullptr, "data-testid", "track-lockup-title");
    const xmlNodePtr artist_node = FindDescendantWithAttr(li_node, "span", "data-testid", "track-lockup-subtitle");

    if (!title_node || !artist_node) return false;

    const std::string found_title = Normalize(GetText(title_node));
    const std::string found_artist = Normalize(GetText(artist_node));

    if (found_title.empty() || found_artist.empty()) return false;

    const bool title_match = FuzzyMatch(found_title, target_title) || found_title.find(Normalize(target_title)) !=
                             std::string::npos;
    const bool artist_match = FuzzyMatch(found_artist, target_artist) || found_artist.find(Normalize(target_artist)) !=
                              std::string::npos;

    return title_match && artist_match;
}

xmlNodePtr FindMatchingListItem(const xmlNodePtr &node, const std::string &title, const std::string &artist) {
    for (xmlNodePtr current = node; current; current = current->next) {
        if (current->type == XML_ELEMENT_NODE && xmlStrcasecmp(current->name, reinterpret_cast<const xmlChar *>("li"))
            == 0) {
            if (TrackMatches(current, title, artist)) {
                return current;
            }
        }
        if (current->children) {
            if (const xmlNodePtr found = FindMatchingListItem(current->children, title, artist)) {
                return found;
            }
        }
    }
    return nullptr;
}