/**
 * @file scraper.cpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#include "metadata/sources/scraper.hpp"
#include "metadata/matching.hpp"
#include "log/log.hpp"

#include <memory>
#include <regex>
#include <array>

#include "metadata/http/curlWrapper.hpp"
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>

namespace {
constexpr std::array<std::string_view, static_cast<size_t>(ScraperRegions::_COUNT)> kRegions = {
    "ae", "ag", "ai", "am", "ar", "at", "au", "az", "bb", "be",
    "bg", "bh", "bm", "bo", "br", "bs", "bw", "by", "bz", "ca",
    "cf", "ch", "ci", "cl", "cm", "cn", "co", "cr", "cz", "de",
    "dk", "dm", "do", "ec", "ee", "eg", "es", "fi", "fr", "gb",
    "gd", "ge", "gn", "gq", "gr", "gt", "gw", "gy", "hk", "hn",
    "hr", "hu", "id", "ie", "il", "in", "it", "jm", "jo", "jp",
    "kg", "kn", "kr", "kw", "ky", "kz", "la", "lc", "li", "lt",
    "lu", "lv", "ma", "md", "me", "mg", "mk", "ml", "mo", "ms",
    "mt", "mu", "mx", "my", "mz", "ne", "ng", "ni", "nl", "no",
    "nz", "om", "pa", "pe", "ph", "pl", "pr", "pt", "py", "qa",
    "ro", "ru", "sa", "se", "sg", "si", "sk", "sn", "sr", "sv",
    "tc", "th", "tj", "tm", "tn", "tr", "tt", "tw", "ua", "ug",
    "us", "uy", "uz", "vc", "ve", "vg", "vn", "za"
};
}

constexpr bool isValidRegion(const std::string_view &region) {
    if (region.size() != 2)
        return false;
    char buf[2] = {static_cast<char>(std::tolower(static_cast<unsigned char>(region[0]))),
                   static_cast<char>(std::tolower(static_cast<unsigned char>(region[1])))};
    const std::string_view lower{buf, 2};
    return std::ranges::binary_search(kRegions, lower);
}

std::string to_string(ScraperRegions region) {
    const auto index = static_cast<size_t>(region);
    if (index >= static_cast<size_t>(ScraperRegions::_COUNT)) {
        throw std::out_of_range("Unknown region enum value");
    }
    return std::string(kRegions[index]);
}

Scraper::Scraper(const std::string &region) {
    _region = region;
}

std::string Scraper::identify() {
    return kIDENTITY;
}

namespace {
std::string getAttribute(const xmlNodePtr &node, const char *attribute) {
    if (!node)
        return "";
    xmlChar *value = xmlGetProp(node, reinterpret_cast<const xmlChar *>(attribute));
    if (!value)
        return "";
    std::string out = reinterpret_cast<char *>(value);
    xmlFree(value);
    return out;
}

std::string getText(const xmlNodePtr &node) {
    if (!node)
        return "";
    xmlChar *content = xmlNodeGetContent(node); // Gets content of node and descendants
    if (!content)
        return "";
    std::string out = reinterpret_cast<char *>(content);
    xmlFree(content);
    return out;
}

std::string normalize(const std::string &text) {
    const size_t start = text.find_first_not_of(" \t\n\r");
    if (start == std::string::npos)
        return "";

    const size_t end = text.find_last_not_of(" \t\n\r");
    std::string trimmed = text.substr(start, end - start + 1);

    std::ranges::transform(trimmed, trimmed.begin(),
                           [](const unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
    return trimmed;
}

xmlNodePtr findDescendantWithAttr(const xmlNodePtr &node, const char *tag_name,
                                  const char *attr_name,
                                  const char *attr_value) {
    for (xmlNodePtr current = node; current; current = current->next) {
        if (current->type == XML_ELEMENT_NODE) {
            if (!tag_name || xmlStrcasecmp(current->name,
                                           reinterpret_cast<const xmlChar *>(tag_name)) == 0) {
                if (std::string attr = getAttribute(current, attr_name);
                    !attr.empty() && attr == attr_value) {
                    return current;
                }
            }
        }
        // Recurse into children
        if (current->children) {
            if (xmlNodePtr found = findDescendantWithAttr(
                current->children, tag_name, attr_name, attr_value)) {
                return found;
            }
        }
    }
    return nullptr;
}

xmlNodePtr findDivWithClass(const xmlNodePtr &node, const std::string &class_value) {
    for (xmlNodePtr current = node; current; current = current->next) {
        if (current->type == XML_ELEMENT_NODE && xmlStrcasecmp(
                current->name, reinterpret_cast<const xmlChar *>("div"))
            == 0) {
            if (std::string classes = getAttribute(current, "class");
                !classes.empty() && classes.find(class_value) != std::string::npos) {
                return current;
            }
        }
        if (current->children) {
            if (xmlNodePtr found = findDivWithClass(current->children, class_value)) {
                return found;
            }
        }
    }
    return nullptr;
}

bool trackMatches(const xmlNodePtr &li_node, const std::string &target_title,
                  const std::string &target_artist) {
    xmlNodePtr title_node = findDescendantWithAttr(li_node, nullptr, "data-testid",
                                                   "track-lockup-title");
    xmlNodePtr artist_node = findDescendantWithAttr(li_node, "span", "data-testid",
                                                    "track-lockup-subtitle");

    if (!title_node || !artist_node)
        return false;

    const std::string found_title = normalize(getText(title_node));
    const std::string found_artist = normalize(getText(artist_node));

    if (found_title.empty() || found_artist.empty())
        return false;

    // Titles carry source-appended decoration ("… (Remastered)"), so allow a substring match there;
    // artists don't, so hold them to the ratio to avoid pulling in "X" against "X Tribute".
    return fuzzyMatch(found_title, target_title, /*allowSubstring=*/true) &&
           fuzzyMatch(found_artist, target_artist);
}

xmlNodePtr findMatchingListItem(const xmlNodePtr &node, const std::string &title,
                                const std::string &artist) {
    for (xmlNodePtr current = node; current; current = current->next) {
        if (current->type == XML_ELEMENT_NODE && xmlStrcasecmp(
                current->name, reinterpret_cast<const xmlChar *>("li"))
            == 0) {
            if (trackMatches(current, title, artist)) {
                return current;
            }
        }
        if (current->children) {
            if (xmlNodePtr found = findMatchingListItem(current->children, title, artist)) {
                return found;
            }
        }
    }
    return nullptr;
}

}

SearchResult Scraper::searchTrack(const Track &track) {
    const std::string term = CurlWrapper::escape(
        track.identity.title + " " + track.identity.album + " " + track.identity.artist);
    const std::string url = "https://music.apple.com/" + _region + "/search?term=" + term;
    std::unique_ptr<CurlWrapper> curl = nullptr;
    try {
        curl = std::make_unique<CurlWrapper>(url);
    } catch (const CurlInitError &e) {
        logging::get("scraper")->error("Search for '{} - {}' failed: {}", track.identity.artist,
                                       track.identity.title, e.what());
        return {};
    }
    curl->setUserAgent();
    const auto result = curl->performCall();
    if (!result.okOrWarn("scraper", "Search for '{} - {}'", track.identity.artist,
                         track.identity.title)) {
        return {};
    }

    const std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> doc{
        htmlReadMemory(result.output.c_str(), static_cast<int>(result.output.size()), nullptr,
                       nullptr, HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING),
        &xmlFreeDoc
    };
    if (!doc) {
        logging::get("scraper")->warn("Could not parse the Apple Music page for '{} - {}'",
                                      track.identity.artist, track.identity.title);
        return {};
    }

    SearchResult r;
    const std::string kTargetSize = "1000x1000bb-60";

    if (xmlNodePtr root = xmlDocGetRootElement(doc.get())) {
        if (xmlNodePtr search_root = findDivWithClass(root, "desktop-search-page")) {
            if (xmlNodePtr li_node = findMatchingListItem(search_root, track.identity.title,
                                                          track.identity.artist)) {
                xmlNodePtr anchor_node = findDescendantWithAttr(
                    li_node, "a", "data-testid", "click-action");
                r.web_url = getAttribute(anchor_node, "href");

                xmlNodePtr source_node = findDescendantWithAttr(
                    li_node, "source", "type", "image/jpeg");
                if (std::string srcset = getAttribute(source_node, "srcset"); !srcset.empty()) {
                    std::regex url_regex(R"((https?://[^ ,]+))");
                    if (std::smatch match; std::regex_search(srcset, match, url_regex)) {
                        std::string image_url = match[1];

                        std::regex size_regex(R"((\d+x\d+bb-\d+))");
                        if (std::smatch size_match; std::regex_search(
                            image_url, size_match, size_regex)) {
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

    return r;
}
