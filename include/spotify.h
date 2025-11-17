//
// Created by Jonathan on 26-Sep-25.
//

#ifndef MUSICPP_SPOTIFY_H
#define MUSICPP_SPOTIFY_H

#include <string>
#include <thread>
#include <mutex>
#include <atomic>

using namespace std;

class SpotifyAPI {
public:
    SpotifyAPI(const string &apikey, const string &apisecret);

    ~SpotifyAPI();

    // Delete copy constructor and copy assignment operator
    SpotifyAPI(const SpotifyAPI &) = delete;

    SpotifyAPI &operator=(const SpotifyAPI &) = delete;

    string getAccessToken();

    string searchTracks(const string &title = "",
                        const string &artist = "",
                        const string &album = "");

private:
    string clientId;
    string clientSecret;
    string accessToken;
    atomic<bool> running{false};
    thread refreshThread;
    mutex tokenMutex;
    uint64_t lastRefreshTime;

    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);

    bool requestToken();

    void refreshLoop();

    static string urlEncode(const string &value);

    static string toLowerCase(const string &str);

    static int levenshteinDistance(const string &s1, const string &s2);

    static string getAlbumImageUrl(const string &jsonString,
                                   const string &inputTitle,
                                   const string &inputArtist,
                                   const string &inputAlbum);

    static double calculateSimilarity(const string &str1, const string &str2);

    static string convertWString(const wstring &wstr);
};
#endif //MUSICPP_SPOTIFY_H
