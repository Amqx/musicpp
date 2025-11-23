//
// Created by Jonathan on 23-Nov-25.
//

#include "lfm.h"
#include <curl/curl.h>
#include <utils.h>
#include <nlohmann/json.hpp>
#include <stringutils.h>
#include <timeutils.h>
#include <credhelper.h>
#include <conio.h>

// TEMP
#include <iostream>

using namespace std;

using json = nlohmann::json;

bool userExit() {
    if (_kbhit()) {
        if (const int ch = _getch(); ch == 27 || ch == '\r') {
            return true;
        }
    }
    return false;
}

lfm::lfm(const std::string &apikey, const std::string &apisecret, spdlog::logger *logger) {
    this->apikey = apikey;
    this->apisecret = apisecret;
    this->logger = logger;
    this->tokenValidity = -1;
    this->enabled = false;

    const string session = convertWString(ReadGenericCredential(L"lastfm_sessionkey", logger));

    if (!session.empty() && authTestSession(session)) {
        if (logger) {
            logger -> info("Successfully verified with LastFM using saved session key");
        }
        sessionKey = session;
        enabled = true;
        return;
    }
    DeleteGenericCredential(L"lastfm_sessionkey", logger);

    string token = authRequestToken();
    if (token.empty()) {
        wcout << L"Failed to get token! LastFM will not function." << endl;
        wcout << L"This error is most likely due to an incorrect API Key and Secret. Please check the logs for more info" << endl;
        wcout << L"Press Enter or ESC to confirm: " << endl;
        while (!userExit());
    }
    wcout << L"Go to the following link to authorize LastFM:\n" << L"https://www.last.fm/api/auth/?api_key=" +
        wstring(convertToWString(apikey)) + L"&token=" + convertToWString(token) << endl;
    wcout << L"\nThis program checks for success every 15 seconds.";
    wcout << L"You can press Enter or ESC at any time to cancel." << endl;
    time_t timer = 0;
    std::atomic cancelled{false};

    jthread watcher([&](const std::stop_token &st){
        while (!st.stop_requested()) {
            if (userExit()) {
                cancelled = true;
                return;
            }
            this_thread::sleep_for(chrono::milliseconds(50));
        }
    });

    while (!cancelled) {
        if (authGetSession(token)) {
            watcher.request_stop();
            break;
        }

        this_thread::sleep_for(chrono::seconds(15));
        timer += 15;

        if (timer >= 300) {
            wcout << L"\nAuthentication timed out after 300s" << endl;
            cancelled = true;
        }
    }
    watcher.join();
}

lfm::~lfm() = default;

std::string lfm::authRequestToken() {
    std::string url = "https://ws.audioscrobbler.com/2.0/?method=auth.gettoken&api_key=" + apikey + "&format=json";
    this->tokenValidity = -1;

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger -> warn("Failed to initialize CURL for LastFM authentication");
        }
        return "";
    }

    std::string readBuffer;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        if (logger) {
            logger -> warn("Failed to request LastFM token: {}", curl_easy_strerror(res));
        }
        return "";
    }

    try {
        json j = json::parse(readBuffer);
        if (!j.contains("token")) {
            if (logger) {
                logger -> warn("Failed to retrieve auth token");
            }
            return "";
        }

        this->tokenValidity = unix_seconds_now();
        return j["token"];
    } catch (json::parse_error &e) {
        if (logger) {
            logger -> warn("JSON parse error in LastFM token request: {}", e.what());
        }
        return "";
    } catch (exception &e) {
        if (logger) {
            logger -> warn("Other error in LastFM token request: {}", e.what());
        }
        return "";
    }
}

bool lfm::authTestSession(const std::string &testKey) const {
    string url = "https://ws.audioscrobbler.com/2.0/?method=user.getInfo&api_key=" + apikey + "&sk=" + testKey;
    const string hash = md5("api_key" + apikey + "method" + "user.getInfo" + "sk" + testKey + apisecret);
    if (hash.empty()) {
        return false;
    }
    url += "&api_sig=" + hash + "&format=json";

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger -> warn("Failed to initialize curl for LastFM session test");
        }
        return false;
    }

    string readBuffer;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        if (logger) {
            logger -> warn("Failed to perform LastFM session test");
        }
        return false;
    }
    try {
        if (json j = json::parse(readBuffer); j.contains("user")) {
            return true;
        }
        return false;
    } catch (json::parse_error &e) {
        if (logger) {
            logger -> warn("JSON parse error in LastFM session test: {}", e.what());
        }
        return false;
    } catch (exception &e) {
        if (logger) {
            logger -> warn("Other error in LastFM session test: {}", e.what());
        }
        return false;
    }
}

bool lfm::authGetSession(const std::string &token) {
    string url = "https://ws.audioscrobbler.com/2.0/?method=auth.getSession&api_key=" + apikey + "&token=" + token;
    const string hash = md5("api_key" + apikey + "method" + "auth.getSession" + "token" + token + apisecret);
    if (hash.empty()) {
        return false;
    }
    url += "&api_sig=" + hash + "&format=json";

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger -> warn("Failed to initialize curl for LastFM session get");
        }
        return false;
    }

    string readBuffer;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        if (logger) {
            logger -> warn("Failed to perform LastFM session get");
        }
        return false;
    }

    try {
        json j = json::parse(readBuffer);
        if (j.contains("session") && j["session"].contains("key")) {
            string name = j["session"]["name"];
            wcout << L"Authenticated as: " << convertToWString(name) << endl;
            sessionKey = j["session"]["key"];
            enabled = true;
            WriteGenericCredential(L"lastfm_sessionkey", wstring(convertToWString(sessionKey)));
            if (logger) {
                logger -> info("Successfully grabbed new LastFM SessionKey");
                wcout << L"Grabbed new LastFM session key" << endl;
            }
            return true;
        }
        return false;
    } catch (json::parse_error &e) {
        if (logger) {
            logger -> warn("JSON parse error in LastFM session get: {}", e.what());
        }
        return false;
    } catch (exception &e) {
        if (logger) {
            logger -> warn("Other error in LastFM session get: {}", e.what());
        }
        return false;
    }

}

string lfm::searchTracks(const std::string &title, const std::string &artist) const {

    string readBuffer;

    string url = "https://ws.audioscrobbler.com/2.0/?method=track.search&track=" + urlEncode(title, logger) + "&artist=" + urlEncode(artist, logger) + "&limit=5&format=json";

    if (logger) {
        logger -> debug("Performing LastFM search with query: {}", url);
    }

    url += "&api_key=" + apikey;

    string result;

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger -> warn("Failed to initialize CURL for LastFM searchTracks");
        }
        return result;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger) {
            logger -> warn("Failed to perform LastFM search: {}", curl_easy_strerror(res));
        }
        return result;
    }

    try {
        json j = json::parse(readBuffer)["results"];
        json tracks = j["trackmatches"].value("track", json::array());
        if (!tracks.is_array() || tracks.empty()) {
            return result;
        }

        const string trackName = tracks[0].value("name", "");
        const string artistName = tracks[0].value("artist", "");
        const string trackURL = tracks[0].value("url", "");

        double titleSimilarity = -1;
        double artistSimilarity = -1;

        if (trackName.length() > 5 && title.length() > 5 && (toLowerCase(title).find(toLowerCase(trackName)) != std::string::npos ||
            toLowerCase(trackName).find(toLowerCase(title)) != std::string::npos)) {
            titleSimilarity = 100.0;
        }
        if (artistName.length() > 5 && artist.length() > 5 && (toLowerCase(artist).find(toLowerCase(artistName)) !=
            std::string::npos ||
            toLowerCase(artistName).find(toLowerCase(artist)) != std::string::npos)) {
            artistSimilarity = 100.0;
        }

        if (titleSimilarity < 0) {
            titleSimilarity = calculateSimilarity(trackName, title);
        }
        if (artistSimilarity < 0) {
            artistSimilarity = calculateSimilarity(artistName, artist);
        }

        if (artistSimilarity < 50.0 || titleSimilarity < 50.0) {
            return result;
        }

        if (!trackURL.empty()) {
            result = trackURL;
        } else {
            if (logger) {
                logger -> warn("Found Last.fm results with title: {} and artist: {}, but missing URL", trackName, artistName);
            }
        }

        return result;
    } catch (json::parse_error &e) {
        if (logger) {
            logger -> warn("JSON parse error in LastFM searchTracks: {}", e.what());
        }
        return result;
    } catch (exception &e) {
        if (logger) {
            logger -> warn("Other error in LastFM searchTracks: {}", e.what());
        }
        return result;
    }
}

bool lfm::updateNowPlaying(const std::string &title, const std::string &artist, const std::string &album, const time_t& duration) const {

    if (!enabled) return false;
    const string url = "https://ws.audioscrobbler.com/2.0/";
    string body;
    const string dur = to_string(duration);
    const string art = urlEncode(artist, logger);
    const string tit = urlEncode(title, logger);
    const string alb = urlEncode(cleanAlbumName(album), logger);
    const string hash = md5("album" + cleanAlbumName(album) + "api_key" + apikey + "artist" + artist + "duration" + dur + "method" + "track.updateNowPlaying" + "sk" + sessionKey + "track" + title + apisecret);
    body += "method=track.updateNowPlaying";
    body += "&api_key=" + apikey;
    body += "&artist="  + art;
    body += "&track="   + tit;
    body += "&album="   + alb;
    body += "&duration=" + dur;
    body += "&sk="      + sessionKey;
    body += "&api_sig=" + hash;
    if (logger) logger -> debug("Updating LastFM Now playing to: {} by {} on {}, with duration {}", tit, art, alb, dur);

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger -> warn("Failed to initialize curl for LastFM update now playing");
        }
        return false;
    }

    string readBuffer;

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger) {
            logger -> warn("Failed to perform LastFM update now playing");
        }
        return false;
    }

    if (readBuffer.find("lfm status=\"ok\"") != string::npos) {
        if (logger) logger -> info("Successfully set new LastFM now playing");
        return true;
    }
    if (logger) logger -> warn("Failed to set new LastFM now playing: {}", readBuffer);
    return false;
}

bool lfm::scrobble(const std::string &title, const std::string &artist, const std::string &album,
    const time_t& start) const {

    if (!enabled) return false;

    const string ts = to_string(start);
    const string art = urlEncode(artist, logger);
    const string tit = urlEncode(title, logger);
    const string alb = urlEncode(cleanAlbumName(album), logger);
    const string hash = md5("album" + cleanAlbumName(album) + "api_key" + apikey + "artist" + artist + "method" + "track.scrobble" + "sk" + sessionKey + "timestamp" + ts + "track" + title + apisecret);
    const string url = "https://ws.audioscrobbler.com/2.0/";

    string body;
    body += "method=track.scrobble";
    body += "&api_key=" + apikey;
    body += "&artist="  + art;
    body += "&track="   + tit;
    body += "&album="   + alb;
    body += "&timestamp=" + ts;
    body += "&sk="      + sessionKey;
    body += "&api_sig=" + hash;

    if (logger) logger -> debug("Scrobbling: {} by {} on {}, with start time {}", tit, art, alb, ts);

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger -> warn("Failed to initialize curl for LastFM scrobble");
        }
        return false;
    }

    string readBuffer;

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        if (logger) {
            logger -> warn("Failed to perform scrobble");
        }
        return false;
    }
    if (readBuffer.find("lfm status=\"ok\"") != string::npos) {
        if (logger) logger -> info("Successfully scrobbled track");
        return true;
    }
    if (logger) logger -> warn("Failed to scrobble track: {}", readBuffer);
    return false;
}



