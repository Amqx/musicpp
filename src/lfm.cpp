//
// Created by Jonathan on 23-Nov-25.
//

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <conio.h>
#include <iostream>
#include <spdlog/spdlog.h>
#include "lfm.h"
#include "utils.h"
#include "constants.h"
#include "stringutils.h"
#include "consoleutils.h"
#include "timeutils.h"
#include "credhelper.h"

using namespace std;

using Json = nlohmann::json;

bool UserExit() {
    if (_kbhit()) {
        // 27 is esc, \r is enter
        if (const int ch = _getch(); ch == 27 || ch == '\r') {
            return true;
        }
    }
    return false;
}

Lfm::Lfm(const std::string &apikey, const std::string &apisecret, leveldb::DB *db, spdlog::logger *logger) {
    this->apikey_ = apikey;
    this->apisecret_ = apisecret;
    this->logger_ = logger;
    this->db_ = db;
    this->token_validity_ = -1;
    this->valid_ = false;
    this->enabled_ = true;

    if (db_) {
        string result;
        if (const auto status = db_->Get(leveldb::ReadOptions(), kLfmStateKey, &result); !status.ok()) {
            if (logger) logger->info("Did not find previous LastFM state, resetting to active");
        } else {
            if (result == "false") {
                enabled_ = false;
                if (logger) logger->info("DB LastFM state pulled: Set to false");
            } else {
                if (logger) logger->info("DB LastFM state pulled: Set to true");
            }
        }
    }

    if (const string session = ConvertWString(ReadGenericCredential(kLastFmDbSessionKey, logger));
        !session.empty() && AuthTestSession(session)) {
        if (logger) {
            logger->info("Successfully verified with LastFM using saved session key");
        }
        session_key_ = session;
        valid_ = true;
        return;
    }
    DeleteGenericCredential(kLastFmDbSessionKey, logger);

    const string token = AuthRequestToken();
    if (token.empty()) {
        wcout << L"Failed to get token! LastFM will not function." << endl;
        wcout <<
                L"This error is most likely due to an incorrect API Key and Secret. Please check the logs for more info"
                << endl;
        wcout << L"Press Enter or ESC to confirm: " << endl;
        while (!UserExit()) {
            this_thread::sleep_for(chrono::milliseconds(50));
        }
        return;
    }

    const wstring auth_url = L"https://www.last.fm/api/auth/?api_key=" +
                             wstring(ConvertToWString(apikey)) + L"&token=" + ConvertToWString(token);
    wcout << L"Go to the following link to authorize LastFM:\n";
    wcout << console::Blue << console::LinkStart << auth_url << console::LinkST << auth_url << console::LinkEnd <<
            console::Reset << endl;
    wcout << L"\nThis program checks for success every 15 seconds.";
    wcout << L"You can press Enter or ESC at any time to cancel." << endl;
    time_t timer = 0;
    std::atomic cancelled{false};

    jthread watcher([&](const std::stop_token &st) {
        while (!st.stop_requested()) {
            if (UserExit()) {
                cancelled = true;
                return;
            }
            this_thread::sleep_for(chrono::milliseconds(50));
        }
    });

    while (!cancelled) {
        if (AuthGetSession(token)) {
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

Lfm::~Lfm() {
    if (db_) {
        if (enabled_) {
            db_->Put(leveldb::WriteOptions(), kLfmStateKey, "true");
        } else {
            db_->Put(leveldb::WriteOptions(), kLfmStateKey, "false");
        }
    }
    if (this->logger_) {
        logger_->info("Saved LastFM state: "s + (enabled_ ? "true" : "false"));
        logger_->info("Lfm Killed");
    }
}

std::string Lfm::AuthRequestToken() {
    const std::string url = "https://ws.audioscrobbler.com/2.0/?method=auth.gettoken&api_key=" + apikey_ +
                            "&format=json";
    this->token_validity_ = -1;

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger_) {
            logger_->warn("Failed to initialize CURL for LastFM authentication");
        }
        return "";
    }

    std::string read_buffer;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kCurlTimeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kCurlConnectTimeout);

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        if (logger_) {
            logger_->warn("Failed to request LastFM token: {}", curl_easy_strerror(res));
        }
        return "";
    }

    try {
        Json j = Json::parse(read_buffer);
        if (!j.contains("token")) {
            if (logger_) {
                logger_->warn("Failed to retrieve auth token");
            }
            return "";
        }

        this->token_validity_ = UnixSecondsNow();
        return j["token"];
    } catch (Json::parse_error &e) {
        if (logger_) {
            logger_->warn("JSON parse error in LastFM token request: {}", e.what());
        }
        return "";
    } catch (exception &e) {
        if (logger_) {
            logger_->warn("Other error in LastFM token request: {}", e.what());
        }
        return "";
    }
}

bool Lfm::AuthTestSession(const std::string &test_key) const {
    string url = "https://ws.audioscrobbler.com/2.0/?method=user.getInfo&api_key=" + apikey_ + "&sk=" + test_key;
    const string hash = Md5("api_key" + apikey_ + "method" + "user.getInfo" + "sk" + test_key + apisecret_);
    if (hash.empty()) {
        return false;
    }
    url += "&api_sig=" + hash + "&format=json";

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger_) {
            logger_->warn("Failed to initialize curl for LastFM session test");
        }
        return false;
    }

    string read_buffer;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kCurlTimeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kCurlConnectTimeout);

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        if (logger_) {
            logger_->warn("Failed to perform LastFM session test");
        }
        return false;
    }
    try {
        if (const Json j = Json::parse(read_buffer); j.contains("user")) {
            return true;
        }
        return false;
    } catch (Json::parse_error &e) {
        if (logger_) {
            logger_->warn("JSON parse error in LastFM session test: {}", e.what());
        }
        return false;
    } catch (exception &e) {
        if (logger_) {
            logger_->warn("Other error in LastFM session test: {}", e.what());
        }
        return false;
    }
}

bool Lfm::AuthGetSession(const std::string &token) {
    string url = "https://ws.audioscrobbler.com/2.0/?method=auth.getSession&api_key=" + apikey_ + "&token=" + token;
    const string hash = Md5("api_key" + apikey_ + "method" + "auth.getSession" + "token" + token + apisecret_);
    if (hash.empty()) {
        return false;
    }
    url += "&api_sig=" + hash + "&format=json";

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger_) {
            logger_->warn("Failed to initialize curl for LastFM session get");
        }
        return false;
    }

    string read_buffer;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kCurlTimeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kCurlConnectTimeout);

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        if (logger_) {
            logger_->warn("Failed to perform LastFM session get");
        }
        return false;
    }

    try {
        Json j = Json::parse(read_buffer);
        if (j.contains("session") && j["session"].contains("key")) {
            const string name = j["session"]["name"];
            wcout << L"Authenticated as: " << ConvertToWString(name) << endl;
            session_key_ = j["session"]["key"];
            valid_ = true;
            WriteGenericCredential(kLastFmDbSessionKey, wstring(ConvertToWString(session_key_)));
            if (logger_) {
                logger_->info("Successfully grabbed new LastFM SessionKey");
                wcout << L"Grabbed new LastFM session key" << endl;
            }
            return true;
        }
        return false;
    } catch (Json::parse_error &e) {
        if (logger_) {
            logger_->warn("JSON parse error in LastFM session get: {}", e.what());
        }
        return false;
    } catch (exception &e) {
        if (logger_) {
            logger_->warn("Other error in LastFM session get: {}", e.what());
        }
        return false;
    }
}

string Lfm::SearchTracks(const std::string &title, const std::string &artist) const {
    if (!valid_) return "";

    string read_buffer;

    string url = "https://ws.audioscrobbler.com/2.0/?method=track.search&track=" + UrlEncode(title, logger_) +
                 "&artist="
                 + UrlEncode(artist, logger_) + "&limit=" + kNumSearchResults + "&format=json";

    if (logger_) {
        logger_->debug("Performing LastFM search with query: {}", url);
    }

    url += "&api_key=" + apikey_;

    string result;

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger_) {
            logger_->warn("Failed to initialize CURL for LastFM searchTracks");
        }
        return result;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kCurlTimeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kCurlConnectTimeout);

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger_) {
            logger_->warn("Failed to perform LastFM search: {}", curl_easy_strerror(res));
        }
        return result;
    }

    try {
        Json j = Json::parse(read_buffer)["results"];
        Json tracks = j["trackmatches"].value("track", Json::array());
        if (!tracks.is_array() || tracks.empty()) {
            return result;
        }

        const string track_name = tracks[0].value("name", "");
        const string artist_name = tracks[0].value("artist", "");
        const string track_url = tracks[0].value("url", "");

        double title_similarity = -1;
        double artist_similarity = -1;

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

        if (title_similarity < 0) {
            title_similarity = CalculateSimilarity(track_name, title);
        }
        if (artist_similarity < 0) {
            artist_similarity = CalculateSimilarity(artist_name, artist);
        }

        if (artist_similarity < kMatchGenerosity || title_similarity < kMatchGenerosity) {
            return result;
        }

        if (!track_url.empty()) {
            result = track_url;
        } else {
            if (logger_) {
                logger_->warn("Found Last.fm results with title: {} and artist: {}, but missing URL", track_name,
                              artist_name);
            }
        }

        return result;
    } catch (Json::parse_error &e) {
        if (logger_) {
            logger_->warn("JSON parse error in LastFM searchTracks: {}", e.what());
        }
        return result;
    } catch (exception &e) {
        if (logger_) {
            logger_->warn("Other error in LastFM searchTracks: {}", e.what());
        }
        return result;
    }
}

bool Lfm::UpdateNowPlaying(const std::string &title, const std::string &artist, const std::string &album,
                           const uint64_t &duration) const {
    // If LFM is disabled for any reason just return true so we don't keep attempting to scrobble or set now playing
    if (!valid_ || !enabled_) return true;

    const string url = "https://ws.audioscrobbler.com/2.0/";
    string body;
    const string dur = to_string(duration);
    const string art = UrlEncode(artist, logger_);
    const string tit = UrlEncode(title, logger_);
    const string alb = UrlEncode(CleanAlbumName(album), logger_);
    const string hash = Md5(
        "album" + CleanAlbumName(album) + "api_key" + apikey_ + "artist" + artist + "duration" + dur + "method" +
        "track.updateNowPlaying" + "sk" + session_key_ + "track" + title + apisecret_);
    body += "method=track.updateNowPlaying";
    body += "&api_key=" + apikey_;
    body += "&artist=" + art;
    body += "&track=" + tit;
    body += "&album=" + alb;
    body += "&duration=" + dur;
    body += "&sk=" + session_key_;
    body += "&api_sig=" + hash;
    if (logger_) logger_->debug("Updating LastFM Now playing to: {} by {} on {}, with duration {}", tit, art, alb, dur);

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger_) {
            logger_->warn("Failed to initialize curl for LastFM update now playing");
        }
        return false;
    }

    string read_buffer;

    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kCurlTimeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kCurlConnectTimeout);

    const CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger_) {
            logger_->warn("Failed to perform LastFM update now playing");
        }
        return false;
    }

    if (read_buffer.find("lfm status=\"ok\"") != string::npos) {
        if (logger_) logger_->info("Successfully set new LastFM now playing");
        return true;
    }
    if (logger_) logger_->warn("Failed to set new LastFM now playing: {}", read_buffer);
    return false;
}

bool Lfm::scrobble(const std::string &title, const std::string &artist, const std::string &album,
                   const uint64_t &start) const {
    // If LFM is disabled for any reason just return true so we don't keep attempting to scrobble or set now playing
    if (!valid_ || !enabled_) return true;

    const string ts = to_string(start);
    const string art = UrlEncode(artist, logger_);
    const string tit = UrlEncode(title, logger_);
    const string alb = UrlEncode(CleanAlbumName(album), logger_);
    const string hash = Md5(
        "album" + CleanAlbumName(album) + "api_key" + apikey_ + "artist" + artist + "method" + "track.scrobble" + "sk" +
        session_key_ + "timestamp" + ts + "track" + title + apisecret_);
    const string url = "https://ws.audioscrobbler.com/2.0/";

    string body;
    body += "method=track.scrobble";
    body += "&api_key=" + apikey_;
    body += "&artist=" + art;
    body += "&track=" + tit;
    body += "&album=" + alb;
    body += "&timestamp=" + ts;
    body += "&sk=" + session_key_;
    body += "&api_sig=" + hash;

    if (logger_) logger_->debug("Scrobbling: {} by {} on {}, with start time {}", tit, art, alb, ts);

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger_) {
            logger_->warn("Failed to initialize curl for LastFM scrobble");
        }
        return false;
    }

    string read_buffer;

    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kCurlTimeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kCurlConnectTimeout);

    const CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        if (logger_) {
            logger_->warn("Failed to perform scrobble");
        }
        return false;
    }
    if (read_buffer.find("lfm status=\"ok\"") != string::npos) {
        if (logger_) logger_->info("Successfully scrobbled track");
        return true;
    }
    if (logger_) logger_->warn("Failed to scrobble track: {}", read_buffer);
    return false;
}

bool Lfm::GetState() const {
    return enabled_;
}

std::wstring Lfm::GetReason() const {
    if (!valid_) {
        return L"Invalid API Credentials!";
    }
    if (!enabled_) {
        return L"LastFM disabled";
    }
    return L"LastFM active";
}

void Lfm::toggle() {
    enabled_ = !enabled_;
}
