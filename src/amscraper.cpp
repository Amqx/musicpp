//
// Created by Jonathan on 19-Nov-25.
//

#include "amscraper.h"
#include <utils.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/tree.h>
#include <regex>
#include <algorithm>
#include "stringutils.h"

namespace {
    bool FuzzyMatch(const std::string &a, const std::string &b) {
        double similarity = calculateSimilarity(a, b);
        return similarity >= 0.6;
    }

    std::string getAttribute(xmlNodePtr node, const char *attribute) {
        if (!node) return "";
        xmlChar *value = xmlGetProp(node, BAD_CAST attribute);
        if (!value) return "";
        std::string out = reinterpret_cast<char *>(value);
        xmlFree(value);
        return out;
    }

    std::string getText(xmlNodePtr node) {
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
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return trimmed;
    }

    xmlNodePtr findDescendantWithAttr(xmlNodePtr node, const char *tagName, const char *attrName,
                                      const char *attrValue) {
        for (xmlNodePtr current = node; current; current = current->next) {
            if (current->type == XML_ELEMENT_NODE) {
                if (!tagName || xmlStrcasecmp(current->name, BAD_CAST tagName) == 0) {
                    std::string attr = getAttribute(current, attrName);
                    if (!attr.empty() && attr == attrValue) {
                        return current;
                    }
                }
            }
            // Recurse into children
            if (current->children) {
                if (xmlNodePtr found = findDescendantWithAttr(current->children, tagName, attrName, attrValue)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    xmlNodePtr findDivWithClass(xmlNodePtr node, const std::string &classValue) {
        for (xmlNodePtr current = node; current; current = current->next) {
            if (current->type == XML_ELEMENT_NODE && xmlStrcasecmp(current->name, BAD_CAST "div") == 0) {
                std::string classes = getAttribute(current, "class");
                if (!classes.empty() && classes.find(classValue) != std::string::npos) {
                    return current;
                }
            }
            if (current->children) {
                if (xmlNodePtr found = findDivWithClass(current->children, classValue)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    bool trackMatches(xmlNodePtr liNode, const std::string &targetTitle, const std::string &targetArtist) {
        xmlNodePtr titleNode = findDescendantWithAttr(liNode, nullptr, "data-testid", "track-lockup-title");
        xmlNodePtr artistNode = findDescendantWithAttr(liNode, "span", "data-testid", "track-lockup-subtitle");

        if (!titleNode || !artistNode) return false;

        std::string foundTitle = normalize(getText(titleNode));
        std::string foundArtist = normalize(getText(artistNode));

        if (foundTitle.empty() || foundArtist.empty()) return false;

        bool titleMatch = FuzzyMatch(foundTitle, targetTitle) || foundTitle.find(normalize(targetTitle)) !=
                          std::string::npos;
        bool artistMatch = FuzzyMatch(foundArtist, targetArtist) || foundArtist.find(normalize(targetArtist)) !=
                           std::string::npos;

        return titleMatch && artistMatch;
    }

    xmlNodePtr findMatchingListItem(xmlNodePtr node, const std::string &title, const std::string &artist) {
        for (xmlNodePtr current = node; current; current = current->next) {
            if (current->type == XML_ELEMENT_NODE && xmlStrcasecmp(current->name, BAD_CAST "li") == 0) {
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
} // Helpers

amscraper::amscraper(const std::string &region, spdlog::logger *logger) {
    this->region = region;
    this->logger = logger;
}

amscraper::~amscraper() {
    if (logger) {
        logger->info("amScraper Killed");
    }
}

searchResult amscraper::searchTracks(const std::string &title, const std::string &artist,
                                     const std::string &album) const {
    const std::string url = "https://music.apple.com/" + region + "/search?term=" + urlEncode(
                                title + " " + album + " " + artist, logger);

    if (logger) {
        logger->debug("Performing Apple Music search with query {}", url);
    }

    searchResult results = {"", ""};

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger->warn("Failed to initialize CURL for Apple Music searchTracks.");
        }
        return results;
    }
    std::string readBuffer;

    curl_slist *headers = nullptr;
    headers = curl_slist_append(
        headers,
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger) {
            logger->warn("Failed to perform Apple Music search: {}", curl_easy_strerror(res));
        }
        return results;
    }

    htmlDocPtr doc = htmlReadMemory(readBuffer.c_str(), static_cast<int>(readBuffer.size()), nullptr, nullptr,
                                    HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    if (!doc) {
        if (logger) logger->warn("Failed to parse Apple Music HTML");
        return results;
    }

    if (xmlNodePtr root = xmlDocGetRootElement(doc)) {
        if (xmlNodePtr searchRoot = findDivWithClass(root, "desktop-search-page")) {
            if (xmlNodePtr liNode = findMatchingListItem(searchRoot, title, artist)) {
                xmlNodePtr anchorNode = findDescendantWithAttr(liNode, "a", "data-testid", "click-action");
                results.url = getAttribute(anchorNode, "href");

                xmlNodePtr sourceNode = findDescendantWithAttr(liNode, "source", "type", "image/jpeg");
                std::string srcset = getAttribute(sourceNode, "srcset");
                if (!srcset.empty()) {
                    std::regex urlRegex(R"((https?://[^ ,]+))");
                    std::smatch match;
                    if (std::regex_search(srcset, match, urlRegex)) {
                        std::string imageUrl = match[1];

                        std::regex sizeRegex(R"((\d+x\d+bb-\d+))");
                        std::smatch sizeMatch;
                        if (std::regex_search(imageUrl, sizeMatch, sizeRegex)) {
                            const std::string targetSize = "800x800bb-60";
                            results.image = std::regex_replace(imageUrl, sizeRegex, targetSize);
                        } else {
                            // If no size component is found, use the original URL
                            results.image = imageUrl;
                        }
                    }
                }
            } else {
                if (logger) logger->debug("No matching track found in Apple Music HTML list");
            }
        } else {
            if (logger) logger->debug("Could not find 'desktop-search-page' div in Apple Music HTML response");
        }
    }

    xmlFreeDoc(doc);
    return results;
}
