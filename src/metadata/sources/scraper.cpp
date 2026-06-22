/**
 * @file scraper.cpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#include "metadata/sources/scraper.hpp"

#include <regex>

#include "metadata/http/curlWrapper.hpp"
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>

Scraper::Scraper(const std::string &region) {
    _region = region;
}

std::string Scraper::identify() {
    return "Apple Music Web Scraper";
}

constexpr double kMatchGenerosity = 0.6; // How close fuzzy matches should be to each other
const std::string kTargetSize = "1000x1000bb-60";

std::string toLowerCase(const std::string &str) {
    std::string result = str;
    std::ranges::transform(result, result.begin(),
                      [](const unsigned char c) { return tolower(c); });
    return result;
}

int levenshteinDistance(const std::string &s1, const std::string &s2) {
    const size_t len1 = s1.size();
    const size_t len2 = s2.size();
    std::vector dp(len1 + 1, std::vector<int>(len2 + 1));

    for (size_t i = 0; i <= len1; i++) {
        dp[i][0] = i;
    }
    for (size_t j = 0; j <= len2; j++) {
        dp[0][j] = j;
    }

    for (size_t i = 1; i <= len1; i++) {
        for (size_t j = 1; j <= len2; j++) {
            const int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({
                dp[i - 1][j] + 1, // deletion
                dp[i][j - 1] + 1, // insertion
                dp[i - 1][j - 1] + cost // substitution
            });
        }
    }

    return dp[len1][len2];
}

double calculateSimilarity(const std::string &str1, const std::string &str2) {
    if (str1.empty() && str2.empty()) return 100.0;
    if (str1.empty() || str2.empty()) return 0.0;

    const std::string s1 = toLowerCase(str1);
    const std::string s2 = toLowerCase(str2);

    const double distance = levenshteinDistance(s1, s2);
    const double max_len = std::ranges::max(s1.length(), s2.length());

    return (max_len - distance) / max_len * 100.0;
}

bool fuzzyMatch(const std::string &a, const std::string &b) {
    return calculateSimilarity(a, b) >= kMatchGenerosity;
}

std::string getAttribute(const xmlNodePtr &node, const char *attribute) {
    if (!node) return "";
    xmlChar *value = xmlGetProp(node, reinterpret_cast<const xmlChar *>(attribute));
    if (!value) return "";
    std::string out = reinterpret_cast<char *>(value);
    xmlFree(value);
    return out;
}

std::string getText(const xmlNodePtr &node) {
    if (!node) return "";
    xmlChar *content = xmlNodeGetContent(node); // Gets content of node and descendants
    if (!content) return "";
    std::string out = reinterpret_cast<char *>(content);
    xmlFree(content);
    return out;
}

std::string normalize(const std::string &text) {
    const size_t start = text.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";

    const size_t end = text.find_last_not_of(" \t\n\r");
    std::string trimmed = text.substr(start, end - start + 1);

    std::ranges::transform(trimmed, trimmed.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return trimmed;
}

xmlNodePtr findDescendantWithAttr(const xmlNodePtr &node, const char *tag_name, const char *attr_name,
                                  const char *attr_value) {
    for (xmlNodePtr current = node; current; current = current->next) {
        if (current->type == XML_ELEMENT_NODE) {
            if (!tag_name || xmlStrcasecmp(current->name, reinterpret_cast<const xmlChar *>(tag_name)) == 0) {
                if (std::string attr = getAttribute(current, attr_name); !attr.empty() && attr == attr_value) {
                    return current;
                }
            }
        }
        // Recurse into children
        if (current->children) {
            if (const xmlNodePtr found = findDescendantWithAttr(current->children, tag_name, attr_name, attr_value)) {
                return found;
            }
        }
    }
    return nullptr;
}

xmlNodePtr findDivWithClass(const xmlNodePtr &node, const std::string &class_value) {
    for (xmlNodePtr current = node; current; current = current->next) {
        if (current->type == XML_ELEMENT_NODE && xmlStrcasecmp(current->name, reinterpret_cast<const xmlChar *>("div"))
            == 0) {
            if (std::string classes = getAttribute(current, "class");
                !classes.empty() && classes.find(class_value) != std::string::npos) {
                return current;
            }
        }
        if (current->children) {
            if (const xmlNodePtr found = findDivWithClass(current->children, class_value)) {
                return found;
            }
        }
    }
    return nullptr;
}

bool trackMatches(const xmlNodePtr &li_node, const std::string &target_title, const std::string &target_artist) {
    const xmlNodePtr title_node = findDescendantWithAttr(li_node, nullptr, "data-testid", "track-lockup-title");
    const xmlNodePtr artist_node = findDescendantWithAttr(li_node, "span", "data-testid", "track-lockup-subtitle");

    if (!title_node || !artist_node) return false;

    const std::string found_title = normalize(getText(title_node));
    const std::string found_artist = normalize(getText(artist_node));

    if (found_title.empty() || found_artist.empty()) return false;

    const bool title_match = fuzzyMatch(found_title, target_title) || found_title.find(normalize(target_title)) !=
                             std::string::npos;
    const bool artist_match = fuzzyMatch(found_artist, target_artist) || found_artist.find(normalize(target_artist)) !=
                              std::string::npos;

    return title_match && artist_match;
}

xmlNodePtr findMatchingListItem(const xmlNodePtr &node, const std::string &title, const std::string &artist) {
    for (xmlNodePtr current = node; current; current = current->next) {
        if (current->type == XML_ELEMENT_NODE && xmlStrcasecmp(current->name, reinterpret_cast<const xmlChar *>("li"))
            == 0) {
            if (trackMatches(current, title, artist)) {
                return current;
            }
        }
        if (current->children) {
            if (const xmlNodePtr found = findMatchingListItem(current->children, title, artist)) {
                return found;
            }
        }
    }
    return nullptr;
}

SearchResult Scraper::searchTrack(const Track &track) {
    const std::string url = "https://music.apple.com/" + _region + "/search?term=" + track.identity.title + " " + track.identity.album + " " + track.identity.artist;
    std::unique_ptr<CurlWrapper> curl = nullptr;
    try {
        curl.reset(new CurlWrapper(url));
    } catch (const std::exception& e) {
        (void)e;
        return {};
    }
    curl->setUserAgent();
    const auto result = curl->performCall();
    if (result.curlcode != CURLE_OK) {
        return {};
    }

    htmlDocPtr doc = htmlReadMemory(result.output.c_str(), static_cast<int>(result.output.size()), nullptr, nullptr, HTML_PARSE_NOERROR / HTML_PARSE_NOWARNING);
    if (!doc) return {};

    SearchResult r;

    if (xmlNodePtr root = xmlDocGetRootElement(doc)) {
        if (xmlNodePtr search_root = findDivWithClass(root, "desktop-search-page")) {
            if (xmlNodePtr li_node = findMatchingListItem(search_root, track.identity.title, track.identity.artist)) {
                xmlNodePtr anchor_node = findDescendantWithAttr(li_node, "a", "data-testid", "click-action");
                r.web_url = getAttribute(anchor_node, "href");

                xmlNodePtr source_node = findDescendantWithAttr(li_node, "source", "type", "image/jpeg");
                if (std::string srcset = getAttribute(source_node, "srcset"); !srcset.empty()) {
                    std::regex url_regex(R"((https?://[^ ,]+))");
                    if (std::smatch match; std::regex_search(srcset, match, url_regex)) {
                        std::string image_url = match[1];

                        std::regex size_regex(R"((\d+x\d+bb-\d+))");
                        if (std::smatch size_match; std::regex_search(image_url, size_match, size_regex)) {
                            r.image_url = std::regex_replace(image_url, size_regex, kTargetSize);
                        } else {
                            // If no size component is found, use the original URL
                            r.image_url = image_url;
                        }
                    }
                }
            }
        }
    }

    xmlFreeDoc(doc);
    return r;
}
