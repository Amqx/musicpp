//
// Created by Jonathan on 26-Sep-25.
//

#include <curl/curl.h>
#include <string>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/windows.foundation.collections.h>
#include "utils.h"
#include "constants.h"
#include "imgur.h"

using Json = nlohmann::json;

ImgurApi::ImgurApi(const string &apikey, spdlog::logger *logger) {
    client_id_ = apikey;
    this->logger_ = logger;
}

ImgurApi::~ImgurApi() {
    if (logger_) {
        logger_->info("ImgurAPI Killed");
    }
}

string ImgurApi::UploadImage(const IRandomAccessStreamReference &stream_ref) const {
    const auto stream = stream_ref.OpenReadAsync().get();
    const uint64_t size = stream.Size();
    vector<uint8_t> image_data(size);

    if (logger_) {
        logger_->info("Performing Imgur upload with size: {} kB", size / 1024);
    }

    const DataReader reader(stream);
    (void) reader.LoadAsync(static_cast<uint32_t>(size)).get();
    reader.ReadBytes(array_view(image_data));
    reader.Close();

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger_) {
            logger_->warn("Failed to initialize CURL for Imgur upload.");
        }
        return "default";
    }

    const string url = "https://api.imgur.com/3/image";
    string read_buffer;
    const string bearer = "Authorization: Client-ID " + client_id_;

    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, bearer.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kCurlTimeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kCurlConnectTimeout);

    curl_mime *mime = curl_mime_init(curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "image");
    curl_mime_data(part, reinterpret_cast<const char *>(image_data.data()), image_data.size());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    const CURLcode res = curl_easy_perform(curl);

    curl_mime_free(mime);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger_) {
            logger_->warn("Failed to upload image to Imgur: {}", curl_easy_strerror(res));
        }
        return "default";
    }

    try {
        if (Json j = Json::parse(read_buffer); j["success"] == true) {
            if (!j["data"].contains("link") || !j["data"]["link"].is_string()) {
                if (logger_) {
                    logger_->warn("Imgur response missing 'link' field.");
                }
                return "default";
            }
            return j["data"]["link"];
        }

        if (logger_) {
            logger_->warn("Imgur upload failed, success flag false.");
        }

        return "default";
    } catch (Json::parse_error &e) {
        if (logger_) {
            logger_->warn("JSON parse error in Imgur uploadImage: {}", e.what());
        }
        return "default";
    } catch (exception &e) {
        if (logger_) {
            logger_->warn("Other error in Imgur uploadImage: {}", e.what());
        }
        return "default";
    }
}