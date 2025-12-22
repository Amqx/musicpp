//
// Created by Jonathan on 26-Sep-25.
//

#include <Windows.h>
#include <sstream>
#include <chrono>
#include <curl/curl.h>
#include <string>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "credhelper.h"
#include "utils.h"
#include "stringutils.h"
#include "timeutils.h"
#include "spotify.h"
#include "constants.h"

using namespace std::chrono;

using Json = nlohmann::json;

SpotifyApi::SpotifyApi(const string &apikey, const string &apisecret, spdlog::logger *logger) {
    client_id_ = apikey;
    client_secret_ = apisecret;
    last_refresh_time_ = 0;
    this->logger_ = logger;
    RequestToken();
    running_ = true;
    if (logger) {
        logger->debug("Refresh thread started for SpotifyAPI.");
    }
    refresh_thread_ = thread(&SpotifyApi::RefreshLoop, this);
}

SpotifyApi::~SpotifyApi() {
    running_ = false;
    cv_.notify_all();
    if (refresh_thread_.joinable()) refresh_thread_.join();
    if (logger_) {
        logger_->info("SpotifyAPI Killed");
    }
}

string SpotifyApi::GetAccessToken() {
    lock_guard lock(token_mutex_);
    return access_token_;
}

SearchResult SpotifyApi::SearchTracks(const string &title, const string &artist, const string &album) {
    string read_buffer;

    ostringstream query;
    if (!title.empty()) query << UrlEncode(title, logger_) << "%20";
    if (!artist.empty()) query << "artist%3A" << UrlEncode(artist, logger_) << "%20";
    if (!album.empty()) query << "album%3A" << UrlEncode(album, logger_);

    string url = "https://api.spotify.com/v1/search?q=" + query.str() + "&type=track&limit=1";

    if (logger_) {
        logger_->debug("Performing Spotify search with query: {}", url);
    }

    SearchResult result = {"", ""};

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger_) {
            logger_->warn("Failed to initialize CURL for Spotify searchTracks.");
        }
        return result;
    }

    curl_slist *headers = nullptr;
    string bearer = "Authorization: Bearer " + GetAccessToken();
    headers = curl_slist_append(headers, bearer.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kCurlTimeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kCurlConnectTimeout);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger_) {
            logger_->warn("Failed to perform Spotify search: {}", curl_easy_strerror(res));
        }
        return result;
    }

    try {
        Json j = Json::parse(read_buffer);
        if (!j.contains("tracks") || !j["tracks"].contains("items") ||
            j["tracks"]["items"].empty()) {
            return result;
        }

        Json track = j["tracks"]["items"][0];
        string track_name, artist_name, album_name;
        if (track.contains("name")) {
            track_name = track["name"];
        }
        if (track.contains("artists") && !track["artists"].empty() &&
            track["artists"][0].contains("name")) {
            artist_name = track["artists"][0]["name"];
        }
        if (track.contains("album") && track["album"].contains("name")) {
            album_name = track["album"]["name"];
        }

        double title_similarity = -1;
        double artist_similarity = -1;
        double album_similarity = -1;

        if (track_name.length() > kMinSubstrLen && title.length() > kMinSubstrLen && (
                ToLowerCase(title).find(ToLowerCase(track_name)) != std::string::npos ||
                ToLowerCase(track_name).find(ToLowerCase(title)) != std::string::npos)) {
            title_similarity = 100.0;
        }
        if (artist_name.length() > kMinSubstrLen && artist.length() > kMinSubstrLen && (
                ToLowerCase(artist).find(ToLowerCase(artist_name)) !=
                std::string::npos ||
                ToLowerCase(artist_name).find(ToLowerCase(artist)) !=
                std::string::npos)) {
            artist_similarity = 100.0;
        }
        if (album_name.length() > kMinSubstrLen && album.length() > kMinSubstrLen && (
                ToLowerCase(album).find(ToLowerCase(album_name)) !=
                std::string::npos ||
                ToLowerCase(album_name).find(ToLowerCase(album)) !=
                std::string::npos)) {
            album_similarity = 100.0;
        }

        if (title_similarity < 0) {
            title_similarity = CalculateSimilarity(track_name, title);
        }
        if (artist_similarity < 0) {
            artist_similarity = CalculateSimilarity(artist_name, artist);
        }
        if (album_similarity < 0) {
            album_similarity = CalculateSimilarity(album_name, album);
        }

        if (double weighted_score = (kTitleWeight * title_similarity) + (kArtistWeight * artist_similarity) + (
                                        kAlbumWeight * album_similarity);
            weighted_score < kMatchGenerosity || artist_similarity < kMatchGenerosity || title_similarity <
            kMatchGenerosity) {
            return result;
        }

        if (track.contains("album")) {
            if (track["album"].contains("images")) {
                Json images = track["album"]["images"];

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
                if (string temp = track["album"]["external_urls"]["spotify"]; !temp.empty()) {
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
    } catch (Json::parse_error &e) {
        if (logger_) {
            logger_->warn("JSON parse error in Spotify searchTracks: {}", e.what());
        }
        return result;
    } catch (exception &e) {
        if (logger_) {
            logger_->warn("Other error in Spotify searchTracks: {}", e.what());
        }
        return result;
    }
}

bool SpotifyApi::RequestToken() {
    if (wstring spotify_client_token = ReadGenericCredential(kSpotifyDbClientToken); !spotify_client_token.empty()) {
        if (const auto split = spotify_client_token.find(L'|'); split != string::npos) {
            const auto timestamp = stoi(spotify_client_token.substr(split + 1));

            if (const auto curr_time = UnixSecondsNow();
                curr_time - timestamp < kSpotifyTokenValidity) {
                access_token_ = ConvertWString(spotify_client_token.substr(0, split));
                last_refresh_time_ = timestamp;
                if (logger_) {
                    logger_->info("Loaded previous Spotify token, valid for {} seconds",
                                  curr_time - last_refresh_time_);
                }
                return true;
            }
            if (logger_) {
                logger_->info("Previous Spotify token expired, requesting new token");
            }
        } else {
            if (logger_) {
                logger_->warn("Failed to parse previous Spotify token, requesting new token.");
            }
        }
    }

    string read_buffer;

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger_) {
            logger_->warn("Failed to initialize CURL for Spotify requestToken.");
        }
        return false;
    }
    const string auth = client_id_ + ":" + client_secret_;

    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, "https://accounts.spotify.com/api/token");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "grant_type=client_credentials");
    curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kCurlTimeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kCurlConnectTimeout);

    const CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger_) {
            logger_->warn("Failed to request Spotify token: {}", curl_easy_strerror(res));
        }
        return false;
    }
    const auto curr_time = UnixSecondsNow();

    if (size_t pos = read_buffer.find(R"("access_token":")"); pos != string::npos) {
        pos += kSpotifyTokenLen;
        const size_t end = read_buffer.find('\"', pos);
        const string token = read_buffer.substr(pos, end - pos);
        lock_guard lock(token_mutex_);
        access_token_ = token;
        last_refresh_time_ = curr_time;
        const wstring saved = ConvertToWString(access_token_) + L"|" + to_wstring(curr_time);
        WriteGenericCredential(kSpotifyDbClientToken, saved);
        if (logger_) {
            logger_->info("Successfully requested new Spotify token.");
        }
        return true;
    }
    if (logger_) {
        logger_->warn("Failed to parse Spotify token from response.");
    }

    return false;
}

void SpotifyApi::RefreshLoop() {
    while (running_) {
        const uint64_t now = UnixSecondsNow();
        const uint64_t next_refresh = last_refresh_time_ + kSpotifyRefreshInterval;
        uint64_t wait = next_refresh > now ? next_refresh - now : 0;

        std::unique_lock lock(cv_mutex_);

        if (cv_.wait_for(lock, hours(wait), [this] { return !running_.load(); })) {
            return;
        }

        if (!RequestToken()) {
            if (logger_) logger_->warn("Failed to refresh Spotify token.");
        }
    }
}