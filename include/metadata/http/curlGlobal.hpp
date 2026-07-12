/**
 * @file CurlGlobal.hpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#pragma once

#include <curl/curl.h>
#include <stdexcept>

class CurlGlobal {
public:
    static void initialize() {
        static CurlGlobal instance;
    }

    CurlGlobal(const CurlGlobal &) = delete;

    CurlGlobal &operator=(const CurlGlobal &) = delete;

    CurlGlobal(CurlGlobal &&) = delete;

    CurlGlobal &operator=(CurlGlobal &&) = delete;

private:
    CurlGlobal() {
        if (const CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT); rc != CURLE_OK) {
            throw std::runtime_error(
                std::string("curl_global_init failed: ") + curl_easy_strerror(rc));
        }
    }

    ~CurlGlobal() {
        curl_global_cleanup();
    }
};