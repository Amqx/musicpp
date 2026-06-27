/**
 * @file imgur.cpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#include <memory>
#include <nlohmann/json.hpp>
#include "metadata/uploaders/imgur.hpp"

#include "metadata/http/curlWrapper.hpp"

using Json = nlohmann::json;

Imgur::Imgur(const std::string &apikey) {
    _apikey = apikey;
}

std::string Imgur::identify() {
    return kIDENTITY;
}

UploadResult Imgur::uploadImage(const std::vector<unsigned char>& bytes, ImageType type) {
    std::unique_ptr<CurlWrapper> curl = nullptr;
    try {
        curl = std::make_unique<CurlWrapper>("https://api.imgur.com/3/image");
    } catch (const std::exception& e) {
        (void)e;
        return {};
    }
    curl->addHeader("Authorization: Client-ID " + _apikey);
    curl->addMime(bytes, "image");
    const auto r = curl->performCall();

    if (r.curlcode != CURLE_OK) {
        return {};
    }

    try {
        if (Json j = Json::parse(r.output); j["success"] == true) {
            if (!j["data"].contains("link") || !j["data"]["link"].is_string()) {
                return {};
            }
            return UploadResult {j["data"]["link"]};
        }

        return {};
    } catch (const std::exception& e) {
        (void)e;
        return {};
    }
}
