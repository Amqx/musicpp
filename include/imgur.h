//
// Created by Jonathan on 26-Sep-25.
//

#ifndef MUSICPP_IMGUR_H
#define MUSICPP_IMGUR_H

#include <string>
#include <curl/curl.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace std;
using namespace winrt;
using namespace Windows::Storage::Streams;

class ImgurAPI {
public:
    ImgurAPI(const string apikey);

    ~ImgurAPI();

    ImgurAPI(const ImgurAPI &) = delete;

    ImgurAPI &operator=(const ImgurAPI &) = delete;

    string uploadImage(IRandomAccessStreamReference const &streamRef);

private:
    string clientID;

    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);
};


#endif //MUSICPP_IMGUR_H