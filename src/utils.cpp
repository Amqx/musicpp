//
// Created by Jonathan on 17-Nov-25.
//

#include <string>
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include "utils.h"

using namespace std;

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    static_cast<string *>(userp)->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}

string UrlEncode(const string &value, spdlog::logger *logger) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger->warn("Failed to initialize CURL for urlEncode.");
        }
        return "";
    }
    char *output = curl_easy_escape(curl, value.c_str(), value.length());
    string encoded = output ? output : "";
    curl_free(output);
    curl_easy_cleanup(curl);
    return encoded;
}