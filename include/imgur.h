//
// Created by Jonathan on 26-Sep-25.
//

#ifndef MUSICPP_IMGUR_H
#define MUSICPP_IMGUR_H

#include <string>
#include <winrt/Windows.Storage.Streams.h>

namespace spdlog {
    class logger;
}

using namespace std;
using namespace winrt;
using namespace Windows::Storage::Streams;

class ImgurApi {
public:
    explicit ImgurApi(const string &apikey, spdlog::logger *logger = nullptr);

    ~ImgurApi();

    ImgurApi(const ImgurApi &) = delete;

    ImgurApi &operator=(const ImgurApi &) = delete;

    [[nodiscard]] string UploadImage(const IRandomAccessStreamReference &stream_ref) const;

    [[nodiscard]] string UploadImage(const vector<uint8_t> &gif) const;

private:
    string client_id_;

    spdlog::logger *logger_;
};


#endif //MUSICPP_IMGUR_H
