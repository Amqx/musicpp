/**
 * @file CurlWrapper.cpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#include "metadata/http/curlWrapper.hpp"
#include "metadata/http/curlGlobal.hpp"

CurlWrapper::CurlWrapper(const std::string &endpoint) {
    CurlGlobal::initialize();
    curl = curl_easy_init();
    if (!curl) {
        throw CurlInitError();
    }
    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    errbuf[0] = '\0';
}

CurlWrapper::~CurlWrapper() {
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

void CurlWrapper::addHeader(const std::string &header) {
    headers = curl_slist_append(headers, header.c_str());
}

void CurlWrapper::setUserAgent(const std::string &useragent) {
    headers = curl_slist_append(headers, useragent.c_str());
}

CurlResult CurlWrapper::performCall() {
    CurlResult r;
    errbuf[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.output);

    r.curlcode = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.HTTPCode);

    if (r.curlcode != CURLE_OK) {
        if (errbuf[0] != '\0') r.curlErrorString = errbuf;
        else r.curlErrorString = curl_easy_strerror(r.curlcode);
    }

    return r;
}
