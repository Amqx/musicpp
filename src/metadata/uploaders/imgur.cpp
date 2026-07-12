/**
 * @file imgur.cpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#include <memory>
#include <nlohmann/json.hpp>
#include "metadata/uploaders/imgur.hpp"

#include "metadata/http/curlWrapper.hpp"
#include "log/log.hpp"

using Json = nlohmann::json;

Imgur::Imgur(const std::string &apikey) {
    _apikey = apikey;
}

std::string Imgur::identify() {
    return kIDENTITY;
}

UploadResult Imgur::uploadImage(const std::vector<unsigned char> &bytes, ImageType type) {
    const auto &logger = logging::get("imgur");

    std::unique_ptr<CurlWrapper> curl = nullptr;
    try {
        curl = std::make_unique<CurlWrapper>("https://api.imgur.com/3/image");
    } catch (const CurlInitError &e) {
        logger->error("Skipping upload of {} byte(s): {}", bytes.size(), e.what());
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
                logger->warn("Upload succeeded but the response carried no image link");
                return {};
            }
            return UploadResult{j["data"]["link"]};
        }

        // Most often a bad/rate-limited client ID; the enricher falls back to no image.
        logger->warn("Imgur rejected the upload (HTTP {})", r.HTTPCode);
        return {};
    } catch (const Json::exception &e) {
        logger->warn("Malformed upload response: {}", e.what());
        return {};
    }
}