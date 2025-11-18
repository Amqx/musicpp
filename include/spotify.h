//
// Created by Jonathan on 26-Sep-25.
//

#ifndef MUSICPP_SPOTIFY_H
#define MUSICPP_SPOTIFY_H

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <spdlog/spdlog.h>

using namespace std;

class SpotifyAPI {
public:
    SpotifyAPI(const string &apikey, const string &apisecret, spdlog::logger *logger = nullptr);

    ~SpotifyAPI();

    // Delete copy constructor and copy assignment operator
    SpotifyAPI(const SpotifyAPI &) = delete;

    SpotifyAPI &operator=(const SpotifyAPI &) = delete;

    string getAccessToken();

    string searchTracks(const string &title = "",
                        const string &artist = "",
                        const string &album = "");

private:
    spdlog::logger* logger;
    string clientId;
    string clientSecret;
    string accessToken;
    atomic<bool> running{false};
    thread refreshThread;
    mutex tokenMutex;
    uint64_t lastRefreshTime;

    bool requestToken();

    void refreshLoop();

    string getAlbumImageUrl(const string &jsonString,
                                   const string &inputTitle,
                                   const string &inputArtist,
                                   const string &inputAlbum) const;
};
#endif //MUSICPP_SPOTIFY_H
