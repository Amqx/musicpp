/**
 * @file CurlWrapper.hpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#pragma once

#include <curl/curl.h>
#include <stdexcept>

#include "types/track.hpp"

class CurlInitError : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override {
        return "Failed to init curl!";
    }
};

struct CurlResult {
    CURLcode curlcode = CURLE_OK;
    std::string curlErrorString;
    long HTTPCode = 0;
    std::string output;
};

class CurlWrapper {
protected:
    CURL* curl = nullptr;
    curl_slist* headers = nullptr;
    curl_mime* mime = nullptr;
    char errbuf[CURL_ERROR_SIZE] = {0};
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
        static_cast<std::string *>(userp)->append(static_cast<char *>(contents), size * nmemb);
        return size * nmemb;
    }

public:
    explicit CurlWrapper(const std::string& endpoint);

    ~CurlWrapper();

    CurlWrapper(const CurlWrapper &) = delete;

    CurlWrapper &operator=(const CurlWrapper &) = delete;

    CurlWrapper(CurlWrapper &&) = delete;

    CurlWrapper &operator=(CurlWrapper &&) = delete;

    /**
     * URL-encodes a single component (e.g. a query parameter value).
     * @param value Raw value to encode.
     * @return Percent-encoded value.
     */
    [[nodiscard]] static std::string escape(const std::string &value);

    void addHeader(const std::string& header);

    void setUserAgent(const std::string& useragent = "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");

    /**
     * Attaches a single MIME part to the request body.
     * @param bytes Raw part contents.
     * @param name MIME part field name (e.g. "image").
     */
    void addMime(const std::vector<unsigned char> &bytes, const std::string& name);

    void usePost(const std::string &fields) const;

    [[nodiscard]] CurlResult performCall();
};
