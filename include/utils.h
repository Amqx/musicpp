//
// Created by Jonathan on 17-Nov-25.
//

#ifndef MUSICPP_UTILS_H
#define MUSICPP_UTILS_H

#include <string>

namespace spdlog {
    class logger;
}

struct SearchResult {
    std::string url;
    std::string image;
};

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);

std::string UrlEncode(const std::string &value, spdlog::logger *logger);

enum ValidationInputType {
    SAPI_CLIENT_ID = 32,
    SAPI_SECRET = 32,
    LFM_KEY = 32,
    LFM_SECRET = 32,
    IMGUR = 15
};

bool ValidateInput(const std::wstring &value, ValidationInputType type);

#endif //MUSICPP_UTILS_H
