//
// Created by Jonathan on 26-Sep-25.
//

#include <Windows.h>
#include <spotify.h>
#include <sstream>
#include <chrono>
#include <curl/curl.h>
#include <string>
#include <nlohmann/json.hpp>
#include <credhelper.h>
#include <utils.h>
#include <stringutils.h>
#include <timeutils.h>

using namespace std::chrono;

using json = nlohmann::json;

SpotifyAPI::SpotifyAPI(const string &apikey, const string &apisecret, spdlog::logger *logger) {
    clientId = apikey;
    clientSecret = apisecret;
    lastRefreshTime = 0;
    this -> logger = logger;
    requestToken();
    running = true;
    if (logger) {
        logger->debug("Refresh thread started for SpotifyAPI.");
    }
    refreshThread = thread(&SpotifyAPI::refreshLoop, this);
}

SpotifyAPI::~SpotifyAPI() {
    running = false;
    cv.notify_all();
    if (refreshThread.joinable()) refreshThread.join();
    if (logger) {
        logger -> info("SpotifyAPI Killed");
    }
}

string SpotifyAPI::getAccessToken() {
    lock_guard lock(tokenMutex);
    return accessToken;
}

searchResult SpotifyAPI::searchTracks(const string &title, const string &artist, const string &album) {
    string readBuffer;

    ostringstream query;
    if (!title.empty()) query << urlEncode(title, logger) << "%20";
    if (!artist.empty()) query << "artist%3A" << urlEncode(artist, logger) << "%20";
    if (!album.empty()) query << "album%3A" << urlEncode(album, logger);

    string url = "https://api.spotify.com/v1/search?q=" + query.str() + "&type=track&limit=1";

    if (logger) {
        logger -> debug("Performing Spotify search with query: {}", url);
    }

    searchResult result = {"", ""};

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger -> warn("Failed to initialize CURL for Spotify searchTracks.");
        }
        return result;
    }

    curl_slist *headers = nullptr;
    string bearer = "Authorization: Bearer " + getAccessToken();
    headers = curl_slist_append(headers, bearer.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger) {
            logger -> warn("Failed to perform Spotify search: {}", curl_easy_strerror(res));
        }
        return result;
    }

    try {
        json j = json::parse(readBuffer);
        if (!j.contains("tracks") || !j["tracks"].contains("items") ||
            j["tracks"]["items"].empty()) {
            return result;
        }

        json track = j["tracks"]["items"][0];
        string trackName, artistName, albumName;
        if (track.contains("name")) {
            trackName = track["name"];
        }
        if (track.contains("artists") && !track["artists"].empty() &&
            track["artists"][0].contains("name")) {
            artistName = track["artists"][0]["name"];
        }
        if (track.contains("album") && track["album"].contains("name")) {
            albumName = track["album"]["name"];
        }

        double titleSimilarity = -1;
        double artistSimilarity = -1;
        double albumSimilarity = -1;

        if (trackName.length() > 5 && title.length() > 5 && (toLowerCase(title).find(toLowerCase(trackName)) != std::string::npos ||
            toLowerCase(trackName).find(toLowerCase(title)) != std::string::npos)) {
            titleSimilarity = 100.0;
            }
        if (artistName.length() > 5 && artist.length() > 5 && (toLowerCase(artist).find(toLowerCase(artistName)) !=
            std::string::npos ||
            toLowerCase(artistName).find(toLowerCase(artist)) != std::string::npos)) {
            artistSimilarity = 100.0;
            }
        if (albumName.length() > 5 && album.length() > 5 && (toLowerCase(album).find(toLowerCase(albumName)) !=
            std::string::npos ||
            toLowerCase(albumName).find(toLowerCase(album)) != std::string::npos)) {
            albumSimilarity = 100.0;
        }

        if (titleSimilarity < 0) {
            titleSimilarity = calculateSimilarity(trackName, title);
        }
        if (artistSimilarity < 0) {
            artistSimilarity = calculateSimilarity(artistName, artist);
        }
        if (albumSimilarity < 0) {
            albumSimilarity = calculateSimilarity(albumName, album);
        }

        double weightedScore = (0.4 * titleSimilarity) + (0.4 * artistSimilarity) + (0.2 * albumSimilarity);
        if (weightedScore < 50.0 || artistSimilarity < 50.0 || titleSimilarity < 50.0) {
            return result;
        }

        if (track.contains("album")) {
            if (track["album"].contains("images")) {
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

                        if (height == 300 && width == 300) {
                            url300 = image["url"];
                        }
                        }
                }

                // Return 640x640 if available, otherwise 300x300, otherwise "failed"
                if (!url640.empty()) {
                    result.image = url640;
                } else if (!url300.empty()) {
                    result.image = url300;
                }
            }

            if (track["album"].contains("external_urls")) {
                string temp = track["album"]["external_urls"]["spotify"];
                if (!temp.empty()) {
                    result.url = temp;
                    return result;
                }
            }

            if (track["album"].contains("id")) {
                string temp = "https://open.spotify.com/album/";
                string p2 = track["album"]["id"];
                temp += p2;
                if (!temp.empty()) {
                    result.url = temp;
                    return result;
                }
            }
            return result;
        }
        return result;
    } catch (json::parse_error &e) {
        if (logger) {
            logger -> warn("JSON parse error in Spotify searchTracks: {}", e.what());
        }
        return result;
    } catch (exception &e) {
        if (logger) {
            logger -> warn("Other error in Spotify searchTracks: {}", e.what());
        }
        return result;
    }
}

bool SpotifyAPI::requestToken() {
    wstring spotify_client_token = ReadGenericCredential(L"spotify_client_token");
    if (!spotify_client_token.empty()) {
        auto split = spotify_client_token.find(L'|');
        if (split != string::npos) {
            const auto timestamp = stoi(spotify_client_token.substr(split + 1));

            if (auto currTime = unix_seconds_now();
                currTime - timestamp < 3600) {
                accessToken = convertWString(spotify_client_token.substr(0, split));
                lastRefreshTime = timestamp;
                if (logger) {
                    logger -> info("Loaded previous Spotify token, valid for {} seconds", currTime - lastRefreshTime);
                }
                return true;
            }
            if (logger) {
                logger -> info("Previous Spotify token expired, requesting new token");
            }
        } else {
            if (logger) {
                logger -> warn("Failed to parse previous Spotify token, requesting new token.");
            }
        }

    }

    string readBuffer;

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger -> warn("Failed to initialize CURL for Spotify requestToken.");
        }
        return false;
    }
    const string auth = clientId + ":" + clientSecret;

    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, "https://accounts.spotify.com/api/token");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "grant_type=client_credentials");
    curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    const CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger) {
            logger -> warn("Failed to request Spotify token: {}", curl_easy_strerror(res));
        }
        return false;
    }
    auto currTime = unix_seconds_now();

    size_t pos = readBuffer.find(R"("access_token":")");
    if (pos != string::npos) {
        pos += 16;
        const size_t end = readBuffer.find('\"', pos);
        const string token = readBuffer.substr(pos, end - pos);
        lock_guard lock(tokenMutex);
        accessToken = token;
        lastRefreshTime = currTime;
        wstring saved = convertToWString(accessToken) + L"|" + to_wstring(currTime);
        WriteGenericCredential(L"spotify_client_token", saved);
        if (logger) {
            logger -> info("Successfully requested new Spotify token.");
        }
        return true;
    }
    if (logger) {
        logger -> warn("Failed to parse Spotify token from response.");
    }

    return false;
}

void SpotifyAPI::refreshLoop() {
    while (running) {
        uint64_t now = unix_seconds_now();
        uint64_t nextRefresh = lastRefreshTime + 3550;
        uint64_t wait = nextRefresh > now ? nextRefresh - now : 0;

        std::unique_lock lock(cvMutex);

        if (cv.wait_for(lock, hours(wait), [this] {return !running.load(); })) {
            return;
        }

        if (!requestToken()) {
            if (logger) logger -> warn("Failed to refresh Spotify token.");
        }
    }
}