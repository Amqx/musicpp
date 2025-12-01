//
// Created by Jonathan on 19-Nov-25.
//

#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/tree.h>
#include <regex>
#include <spdlog/spdlog.h>
#include "stringutils.h"
#include "amscraper.h"
#include "utils.h"
#include "constants.h"

Amscraper::Amscraper(const std::string &region, spdlog::logger *logger) {
    this->region_ = region;
    this->logger_ = logger;
}

Amscraper::~Amscraper() {
    if (logger_) {
        logger_->info("amScraper Killed");
    }
}

SearchResult Amscraper::SearchTracks(const std::string &title, const std::string &artist,
                                     const std::string &album) const {
    const std::string url = "https://music.apple.com/" + region_ + "/search?term=" + UrlEncode(
                                title + " " + album + " " + artist, logger_);

    if (logger_) {
        logger_->debug("Performing Apple Music search with query {}", url);
    }

    SearchResult results = {"", ""};

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger_) {
            logger_->warn("Failed to initialize CURL for Apple Music searchTracks.");
        }
        return results;
    }
    std::string read_buffer;

    curl_slist *headers = nullptr;
    headers = curl_slist_append(
        headers,
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kCurlTimeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kCurlConnectTimeout);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger_) {
            logger_->warn("Failed to perform Apple Music search: {}", curl_easy_strerror(res));
        }
        return results;
    }

    htmlDocPtr doc = htmlReadMemory(read_buffer.c_str(), static_cast<int>(read_buffer.size()), nullptr, nullptr,
                                    HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    if (!doc) {
        if (logger_) logger_->warn("Failed to parse Apple Music HTML");
        return results;
    }

    if (xmlNodePtr root = xmlDocGetRootElement(doc)) {
        if (xmlNodePtr search_root = FindDivWithClass(root, "desktop-search-page")) {
            if (xmlNodePtr li_node = FindMatchingListItem(search_root, title, artist)) {
                xmlNodePtr anchor_node = FindDescendantWithAttr(li_node, "a", "data-testid", "click-action");
                results.url = GetAttribute(anchor_node, "href");

                xmlNodePtr source_node = FindDescendantWithAttr(li_node, "source", "type", "image/jpeg");
                if (std::string srcset = GetAttribute(source_node, "srcset"); !srcset.empty()) {
                    std::regex url_regex(R"((https?://[^ ,]+))");
                    if (std::smatch match; std::regex_search(srcset, match, url_regex)) {
                        std::string image_url = match[1];

                        std::regex size_regex(R"((\d+x\d+bb-\d+))");
                        if (std::smatch size_match; std::regex_search(image_url, size_match, size_regex)) {
                            results.image = std::regex_replace(image_url, size_regex, kTargetSize);
                        } else {
                            // If no size component is found, use the original URL
                            results.image = image_url;
                        }
                    }
                }
            } else {
                if (logger_) logger_->debug("No matching track found in Apple Music HTML list");
            }
        } else {
            if (logger_) logger_->debug("Could not find 'desktop-search-page' div in Apple Music HTML response");
        }
    }

    xmlFreeDoc(doc);
    return results;
}