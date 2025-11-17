//
// Created by Jonathan on 26-Sep-25.
//

#include <Windows.h>

#include <spotify.h>
#include <iostream>
#include <sstream>
#include <chrono>
#include <curl/curl.h>
#include <string>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <credhelper.h>

using namespace std::chrono;

using json = nlohmann::json;

size_t SpotifyAPI::WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    static_cast<string *>(userp)->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}

string SpotifyAPI::convertWString(const wstring &wstr) {
    if (wstr.empty()) {
        return "";
    }

    const int required_size = WideCharToMultiByte(
        CP_UTF8, // CodePage: Convert to UTF-8
        0, // dwFlags
        wstr.data(), // lpWideCharStr: Pointer to wide string data
        static_cast<int>(wstr.length()), // cchWideChar: Length of the wide string (excluding null)
        nullptr, // lpMultiByteStr: Output buffer (NULL to get size)
        0, // cbMultiByte: Output buffer size (0 to get size)
        nullptr, nullptr // lpDefaultChar, lpUsedDefaultChar
    );

    if (required_size <= 0) {
        return ""; // Conversion error
    }
    std::string narrow_str(required_size, '\0');

    WideCharToMultiByte(
        CP_UTF8, // CodePage: Convert to UTF-8
        0, // dwFlags
        wstr.data(), // lpWideCharStr
        static_cast<int>(wstr.length()), // cchWideChar
        &narrow_str[0], // lpMultiByteStr: Use the internal buffer (C++11+)
        required_size, // cbMultiByte: Size of the buffer
        nullptr, nullptr
    );

    return narrow_str;
}

bool SpotifyAPI::requestToken() {
    wstring spotify_client_token = ReadGenericCredential(L"spotify_client_token");
    if (!spotify_client_token.empty()) {
        auto split = spotify_client_token.find(L'|');
        if (split != string::npos) {
            const auto timestamp = stoi(spotify_client_token.substr(split + 1));

            if (auto currTime = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
                currTime - timestamp < 3600) {
                accessToken = convertWString(spotify_client_token.substr(0, split));
                lastRefreshTime = currTime - timestamp;
                return true;
            }
        }
    }

    string readBuffer;

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    const string auth = clientId + ":" + clientSecret;

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, "https://accounts.spotify.com/api/token");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "grant_type=client_credentials");
    curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);
    delete headers;
    auto currTime = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return false;

    size_t pos = readBuffer.find("\"access_token\":\"");
    if (pos != string::npos) {
        pos += 16;
        const size_t end = readBuffer.find('\"', pos);
        const string token = readBuffer.substr(pos, end - pos);
        lock_guard<mutex> lock(tokenMutex);
        accessToken = token;
        lastRefreshTime = 0;
        wstring saved = wstring(accessToken.begin(), accessToken.end()) + L"|" + to_wstring(currTime);
        WriteGenericCredential(L"spotify_client_token", saved);
        return true;
    }
    return false;
}

void SpotifyAPI::refreshLoop() {
    while (running) {
        // counts till 1 hour 100ms at a time
        for (int i = 0; i < 36000; i++) {
            if (!running) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!requestToken()) {
            cerr << "Failed to refresh token.\n";
        }
    }
}

string SpotifyAPI::urlEncode(const string &value) {
    CURL *curl = curl_easy_init();
    if (!curl) return "";
    char *output = curl_easy_escape(curl, value.c_str(), value.length());
    string encoded = output ? output : "";
    curl_free(output);
    curl_easy_cleanup(curl);
    return encoded;
}

SpotifyAPI::SpotifyAPI(const string &apikey, const string &apisecret) {
    clientId = apikey;
    clientSecret = apisecret;
    lastRefreshTime = 0;
    if (!requestToken()) {
        throw runtime_error("Failed to obtain initial access token");
    }
    running = true;
    refreshThread = thread(&SpotifyAPI::refreshLoop, this);
}

SpotifyAPI::~SpotifyAPI() {
    running = false;
    if (refreshThread.joinable()) refreshThread.join();
}

string SpotifyAPI::getAccessToken() {
    lock_guard<mutex> lock(tokenMutex);
    return accessToken;
}

string SpotifyAPI::searchTracks(const string &title,
                                const string &artist,
                                const string &album) {
    string readBuffer;

    ostringstream query;
    if (!title.empty()) query << urlEncode(title) << "%20";
    if (!artist.empty()) query << "artist%3A" << urlEncode(artist) << "%20";
    if (!album.empty()) query << "album%3A" << urlEncode(album);

    string fullQuery = "q=" + query.str() + "&type=track&limit=1";

    string url = "https://api.spotify.com/v1/search?" + fullQuery;

    CURL *curl = curl_easy_init();
    if (!curl) return "failed";

    struct curl_slist *headers = nullptr;
    string bearer = "Authorization: Bearer " + getAccessToken();
    headers = curl_slist_append(headers, bearer.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);
    delete headers;
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return "failed";
    }
    return getAlbumImageUrl(readBuffer, toLowerCase(title), toLowerCase(artist), toLowerCase(album));
}

string SpotifyAPI::getAlbumImageUrl(const string &jsonString, const string &inputTitle, const string &inputArtist,
                                    const string &inputAlbum) {
    try {
        json j = json::parse(jsonString);

        // Check if tracks exist and items array is not empty
        if (!j.contains("tracks") || !j["tracks"].contains("items") ||
            j["tracks"]["items"].empty()) {
            return "failed";
        }

        // Get the first track item
        json track = j["tracks"]["items"][0];

        // Extract track name, artist name, and album name from JSON
        string trackName;
        string artistName;
        string albumName;

        // Get track name
        if (track.contains("name")) {
            trackName = track["name"];
        }

        // Get artist name (first artist)
        if (track.contains("artists") && !track["artists"].empty() &&
            track["artists"][0].contains("name")) {
            artistName = track["artists"][0]["name"];
        }

        // Get album name
        if (track.contains("album") && track["album"].contains("name")) {
            albumName = track["album"]["name"];
        }

        double titleSimilarity = -1;
        double artistSimilarity = -1;
        double albumSimilarity = -1;

        if (trackName.length() > 5 && inputTitle.length() > 5 && inputTitle.find(toLowerCase(trackName)) !=
            std::string::npos ||
            trackName.find(toLowerCase(inputTitle)) != std::string::npos) {
            titleSimilarity = 100.0;
        }

        if (artistName.length() > 5 && inputArtist.length() > 5 && inputArtist.find(toLowerCase(artistName)) !=
            std::string::npos ||
            artistName.find(toLowerCase(inputArtist)) != std::string::npos) {
            artistSimilarity = 100.0;
        }

        if (albumName.length() > 5 && inputAlbum.length() > 5 && inputAlbum.find(toLowerCase(albumName)) !=
            std::string::npos ||
            albumName.find(toLowerCase(inputAlbum)) != std::string::npos) {
            artistSimilarity = 100.0;
        }

        // Calculate similarities
        if (titleSimilarity < 0) {
            titleSimilarity = calculateSimilarity(trackName, inputTitle);
        }

        if (artistSimilarity < 0) {
            artistSimilarity = calculateSimilarity(artistName, inputArtist);
        }

        if (albumSimilarity < 0) {
            albumSimilarity = calculateSimilarity(albumName, inputAlbum);
        }

        // Check if all similarities are at least 50%
        if (titleSimilarity < 50.0 || artistSimilarity < 50.0 || albumSimilarity < 50.0) {
            return "failed";
        }

        // Look for images in the album object
        if (track.contains("album") && track["album"].contains("images")) {
            json images = track["album"]["images"];

            string url640;
            string url300;

            // Iterate through images to find 640x640 and 300x300
            for (const auto &image: images) {
                if (image.contains("height") && image.contains("width") &&
                    image.contains("url")) {
                    int height = image["height"];
                    int width = image["width"];

                    if (height == 640 && width == 640) {
                        url640 = image["url"];
                    }
                }
            }

            // Return 640x640 if available, otherwise 300x300, otherwise "failed"
            if (!url640.empty()) {
                return url640;
            }
        }

        return "failed";
    } catch (...) {
        // Any other error
        return "failed";
    }
}

double SpotifyAPI::calculateSimilarity(const string &str1, const string &str2) {
    if (str1.empty() && str2.empty()) return 100.0;
    if (str1.empty() || str2.empty()) return 0.0;

    string s1 = toLowerCase(str1);
    string s2 = toLowerCase(str2);

    double distance = levenshteinDistance(s1, s2);
    double maxLen = max(s1.length(), s2.length());

    return (maxLen - distance) / maxLen * 100.0;
}


string SpotifyAPI::toLowerCase(const string &str) {
    string result = str;
    ranges::transform(result, result.begin(),
                      [](const unsigned char c) { return tolower(c); });
    return result;
}

int SpotifyAPI::levenshteinDistance(const string &s1, const string &s2) {
    const size_t len1 = s1.size();
    const size_t len2 = s2.size();
    vector<vector<int> > dp(len1 + 1, vector<int>(len2 + 1));

    for (size_t i = 0; i <= len1; i++) {
        dp[i][0] = i;
    }
    for (size_t j = 0; j <= len2; j++) {
        dp[0][j] = j;
    }

    for (size_t i = 1; i <= len1; i++) {
        for (size_t j = 1; j <= len2; j++) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            dp[i][j] = min({
                dp[i - 1][j] + 1, // deletion
                dp[i][j - 1] + 1, // insertion
                dp[i - 1][j - 1] + cost // substitution
            });
        }
    }

    return dp[len1][len2];
}
