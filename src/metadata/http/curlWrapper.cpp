/**
 * @file CurlWrapper.cpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#include "metadata/http/curlWrapper.hpp"

#include <algorithm>
#include <memory>

#include "metadata/http/curlGlobal.hpp"

namespace {
constexpr size_t kMaxLoggedBody = 200;
}

bool CurlResult::transferred() const {
    return curlcode == CURLE_OK;
}

bool CurlResult::httpOk() const {
    return HTTPCode >= 200 && HTTPCode < 300;
}

bool CurlResult::ok() const {
    return transferred() && httpOk();
}

std::string CurlResult::briefBody() const {
    if (output.empty())
        return "<empty body>";

    std::string brief = output.substr(0, kMaxLoggedBody);
    // Never cut a UTF-8 sequence in half
    while (!brief.empty() && (static_cast<unsigned char>(brief.back()) & 0xC0) == 0x80)
        brief.pop_back();
    if (!brief.empty() && (static_cast<unsigned char>(brief.back()) & 0x80) != 0)
        brief.pop_back();

    std::ranges::replace(brief, '\n', ' ');
    std::ranges::replace(brief, '\r', ' ');

    if (output.size() > brief.size())
        brief += "...";
    return brief;
}

bool CurlResult::checkTransfer(const std::string_view &logger,
                               const std::string &description) const {
    if (transferred())
        return true;
    logging::get(logger)->warn("{} failed: {}", description, curlErrorString);
    return false;
}

bool CurlResult::checkResponse(const std::string_view &logger,
                               const std::string &description) const {
    if (!checkTransfer(logger, description))
        return false;
    if (httpOk())
        return true;
    logging::get(logger)->warn("{} returned HTTP {}: {}", description, HTTPCode, briefBody());
    return false;
}

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

std::string CurlWrapper::escape(const std::string &value) {
    CurlGlobal::initialize();
    const auto encoded = std::unique_ptr<char, decltype(&curl_free)>(
        curl_easy_escape(nullptr, value.c_str(), static_cast<int>(value.length())), curl_free);
    return encoded ? std::string(encoded.get()) : std::string();
}

void CurlWrapper::addMime(const std::vector<unsigned char> &bytes, const std::string &name) {
    if (mime)
        curl_mime_free(mime);
    mime = curl_mime_init(curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, name.c_str());
    curl_mime_data(part, reinterpret_cast<const char *>(bytes.data()), bytes.size());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
}

void CurlWrapper::usePost(const std::string &fields) const {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, fields.c_str());
}

CurlWrapper::~CurlWrapper() {
    if (headers)
        curl_slist_free_all(headers);
    if (mime)
        curl_mime_free(mime);
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
        if (errbuf[0] != '\0')
            r.curlErrorString = errbuf;
        else
            r.curlErrorString = curl_easy_strerror(r.curlcode);
    }

    return r;
}
