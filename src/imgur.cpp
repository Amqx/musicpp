//
// Created by Jonathan on 26-Sep-25.
//

#include <imgur.h>
#include <curl/curl.h>
#include <string>
#include <nlohmann/json.hpp>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/windows.foundation.collections.h>
#include <utils.h>

using namespace Windows;
using namespace std;
using namespace winrt;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;

using json = nlohmann::json;

ImgurAPI::ImgurAPI(const string &apikey, spdlog::logger *logger) {
    clientID = apikey;
    this -> logger = logger;
}

ImgurAPI::~ImgurAPI() {
    if (logger) {
        logger -> info("ImgurAPI Killed");
    }
}

string ImgurAPI::uploadImage(const IRandomAccessStreamReference &streamRef) const {
    auto stream = streamRef.OpenReadAsync().get();
    uint64_t size = stream.Size();
    vector<uint8_t> imageData(size);

    if (logger) {
        logger -> info("Performing Imgur upload with size: {} kB", size/1024);
    }

    DataReader reader(stream);
    (void) reader.LoadAsync(static_cast<uint32_t>(size)).get();
    reader.ReadBytes(array_view(imageData));
    reader.Close();

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger -> warn("Failed to initialize CURL for Imgur upload.");
        }
        return "default";
    }

    string url = "https://api.imgur.com/3/image";
    string readBuffer;
    string bearer = "Authorization: Client-ID " + clientID;

    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, bearer.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    curl_mime *mime = curl_mime_init(curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "image");
    curl_mime_data(part, reinterpret_cast<const char *>(imageData.data()), imageData.size());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    CURLcode res = curl_easy_perform(curl);

    curl_mime_free(mime);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger) {
            logger -> warn("Failed to upload image to Imgur: {}", curl_easy_strerror(res));
        }
        return "default";
    }

    try {
        json j = json::parse(readBuffer);

        if (j["success"] == true) {
            if (!j["data"].contains("link") || !j["data"]["link"].is_string()) {
                if (logger) {
                    logger->warn("Imgur response missing 'link' field.");
                }
                return "default";
            }
            return j["data"]["link"];
        }

        if (logger) {
            logger -> warn("Imgur upload failed, success flag false.");
        }

        return "default";
    } catch (json::parse_error &e) {
        if (logger) {
            logger -> warn("JSON parse error in Imgur uploadImage: {}", e.what());
        }
        return "default";
    } catch (exception &e) {
        if (logger) {
            logger -> warn("Other error in Imgur uploadImage: {}", e.what());
        }
        return "default";
    }
}
