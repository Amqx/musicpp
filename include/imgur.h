//
// Created by Jonathan on 26-Sep-25.
//

#ifndef MUSICPP_IMGUR_H
#define MUSICPP_IMGUR_H

#include <string>
#include <winrt/Windows.Storage.Streams.h>
#include <spdlog/spdlog.h>

using namespace std;
using namespace winrt;
using namespace Windows::Storage::Streams;

class ImgurAPI {
public:
    ImgurAPI(const string &apikey, spdlog::logger *logger = nullptr);

    ~ImgurAPI();

    ImgurAPI(const ImgurAPI &) = delete;

    ImgurAPI &operator=(const ImgurAPI &) = delete;

    string uploadImage(const IRandomAccessStreamReference &streamRef) const;

private:
    string clientID;

    spdlog::logger* logger;
};


#endif //MUSICPP_IMGUR_H
