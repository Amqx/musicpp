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
    if (refreshThread.joinable()) refreshThread.join();
    if (logger) {
        logger -> info("SpotifyAPI Killed");
    }
}

string SpotifyAPI::getAccessToken() {
    lock_guard<mutex> lock(tokenMutex);
    return accessToken;
}

string SpotifyAPI::searchTracks(const string &title, const string &artist, const string &album) {
    string readBuffer;

    ostringstream query;
    if (!title.empty()) query << urlEncode(title, logger) << "%20";
    if (!artist.empty()) query << "artist%3A" << urlEncode(artist, logger) << "%20";
    if (!album.empty()) query << "album%3A" << urlEncode(album, logger);

    string fullQuery = "q=" + query.str() + "&type=track&limit=1";

    if (logger) {
        logger -> debug("Performing Spotify search with query: {}", fullQuery);
    }

    string url = "https://api.spotify.com/v1/search?" + fullQuery;

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger -> warn("Failed to initialize CURL for Spotify searchTracks.");
        }
        return "failed";
    }

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
        if (logger) {
            logger -> warn("Failed to perform Spotify search: {}", curl_easy_strerror(res));
        }
        return "failed";
    }
    return getAlbumImageUrl(readBuffer, toLowerCase(title), toLowerCase(artist), toLowerCase(album));
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
                if (logger) {
                    logger -> info("Loaded Spotify token from Windows Credential Manager, valid for {} seconds", 3600 - lastRefreshTime);
                }
                return true;
            }
            if (logger) {
                logger -> info("Spotify token from Windows Credential Manager expired, requesting new token.");
            }
        } else {
            if (logger) {
                logger -> warn("Failed to parse Spotify token from Windows Credential Manager, requesting new token.");
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

    if (res != CURLE_OK) {
        if (logger) {
            logger -> warn("Failed to request Spotify token: {}", curl_easy_strerror(res));
        }
        return false;
    }

    size_t pos = readBuffer.find(R"("access_token":")");
    if (pos != string::npos) {
        pos += 16;
        const size_t end = readBuffer.find('\"', pos);
        const string token = readBuffer.substr(pos, end - pos);
        lock_guard<mutex> lock(tokenMutex);
        accessToken = token;
        lastRefreshTime = 0;
        wstring saved = wstring(accessToken.begin(), accessToken.end()) + L"|" + to_wstring(currTime);
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
        // counts till 1 hour 100ms at a time
        for (int i = 0; i < 36000; i++) {
            if (!running) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!requestToken()) {
            if (logger) logger -> warn("Failed to refresh Spotify token.");
        }
    }
}

string SpotifyAPI::getAlbumImageUrl(const string &jsonString, const string &inputTitle, const string &inputArtist,
    const string &inputAlbum) const {
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

        if (trackName.length() > 5 && inputTitle.length() > 5 && (inputTitle.find(toLowerCase(trackName)) !=
            std::string::npos ||
            trackName.find(toLowerCase(inputTitle)) != std::string::npos)) {
            titleSimilarity = 100.0;
        }

        if (artistName.length() > 5 && inputArtist.length() > 5 && (inputArtist.find(toLowerCase(artistName)) !=
            std::string::npos ||
            artistName.find(toLowerCase(inputArtist)) != std::string::npos)) {
            artistSimilarity = 100.0;
        }

        if (albumName.length() > 5 && inputAlbum.length() > 5 && (inputAlbum.find(toLowerCase(albumName)) !=
            std::string::npos ||
            albumName.find(toLowerCase(inputAlbum)) != std::string::npos)) {
                        albumSimilarity = 100.0;
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

        // move to a weighted score algorithm: place higher emphasis on title and artist similarity,
        // and be more forgiving if the album similarity is low
        double weightedScore = (0.4 * titleSimilarity) + (0.4 * artistSimilarity) + (0.2 * albumSimilarity);
        if (weightedScore < 50.0 || artistSimilarity < 30.0 || titleSimilarity < 30.0) {
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
    } catch (json::parse_error &e) {
        if (logger) {
            logger -> warn("JSON parse error in Spotify getAlbumImageUrl: {}", e.what());
        }
        return "failed";
    } catch (exception &e) {
        if (logger) {
            logger -> warn("Other error in Spotify getAlbumImageUrl: {}", e.what());
        }
        return "failed";
    }
}
