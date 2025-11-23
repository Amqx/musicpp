//
// Created by Jonathan on 17-Nov-25.
//

#ifndef MUSICPP_UTILS_H
#define MUSICPP_UTILS_H

#include <string>
#include <spdlog/spdlog.h>

struct searchResult {
    std::string url;
    std::string image;
};

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);

std::string urlEncode(const std::string &value, spdlog::logger *logger);

#endif //MUSICPP_UTILS_H