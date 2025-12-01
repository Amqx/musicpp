//
// Created by Jonathan on 26-Sep-25.
//

#ifndef MUSICPP_SPOTIFY_H
#define MUSICPP_SPOTIFY_H

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include "utils.h"

using namespace std;

namespace spdlog {
    class logger;
};

class SpotifyApi {
public:
    SpotifyApi(const string &apikey, const string &apisecret, spdlog::logger *logger = nullptr);

    ~SpotifyApi();

    // Delete copy constructor and copy assignment operator
    SpotifyApi(const SpotifyApi &) = delete;

    SpotifyApi &operator=(const SpotifyApi &) = delete;

    string GetAccessToken();

    SearchResult SearchTracks(const string &title = "", const string &artist = "", const string &album = "");

private:
    spdlog::logger *logger_;
    string client_id_;
    string client_secret_;
    string access_token_;
    atomic<bool> running_{false};
    thread refresh_thread_;
    mutex token_mutex_;
    uint64_t last_refresh_time_;
    std::condition_variable cv_;
    std::mutex cv_mutex_;

    bool RequestToken();

    void RefreshLoop();
};
#endif //MUSICPP_SPOTIFY_H