//
// Created by Jonathan on 26-Sep-25.
//

#include "../include/imgur.h"
#include <curl/curl.h>
#include <string>
#include <nlohmann/json.hpp>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/windows.foundation.collections.h>

using namespace Windows;
using namespace std;
using namespace winrt;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;

using json = nlohmann::json;

ImgurAPI::ImgurAPI(const string apikey) {
    clientID = apikey;
}

ImgurAPI::~ImgurAPI() {
}

string ImgurAPI::uploadImage(IRandomAccessStreamReference const &streamRef) {
    auto stream = streamRef.OpenReadAsync().get();
    uint64_t size = stream.Size();
    vector<uint8_t> imageData(static_cast<size_t>(size));

    DataReader reader(stream);
    reader.LoadAsync(static_cast<uint32_t>(size)).get();
    reader.ReadBytes(array_view<uint8_t>(imageData));
    reader.Close();

    CURL *curl = curl_easy_init();
    if (!curl) return "default";

    string url = "https://api.imgur.com/3/image";
    string readBuffer;
    string bearer = "Authorization: Client-ID " + clientID;

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, bearer.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    curl_mime *mime = curl_mime_init(curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "image");
    curl_mime_data(part, reinterpret_cast<const char *>(imageData.data()), static_cast<size_t>(imageData.size()));
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    CURLcode res = curl_easy_perform(curl);

    curl_mime_free(mime);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return "default";
    }

    try {
        json j = json::parse(readBuffer);

        if (j["success"] == true) {
            return j["data"]["link"];
        }

        return "default";
    } catch (const json::exception &e) {
        return "default";
    } catch (...) {
        return "default";
    }
}

size_t ImgurAPI::WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    static_cast<string *>(userp)->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}
