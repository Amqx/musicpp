/**
 * @file lastfm.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 26-Jun-26
 */

#include "metadata/sources/lastfm.hpp"
#include "metadata/matching.hpp"
#include "metadata/http/curlWrapper.hpp"
#include "security/credentials.hpp"
#include "log/log.hpp"

#include <iostream>
#include <thread>
#include <wincrypt.h>
#include <nlohmann/json.hpp>
#include <conio.h>

using Json = nlohmann::json;

/**
 * Computes the MD5 hash of a given input string.
 * @param input Input to hash.
 * @return Hashed output.
 */
std::string Md5(const std::string &input) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE digest[16] = {};
    DWORD digest_len = 16;

    if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return "";
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }

    CryptHashData(hHash, reinterpret_cast<const BYTE *>(input.data()), input.size(), 0);
    CryptGetHashParam(hHash, HP_HASHVAL, digest, &digest_len, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);

    for (const unsigned char b : digest) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0x0F]);
    }

    return out;
}

bool LastFm::scrobble(const Track &track) const {
    if (!_authenticated.load(std::memory_order::memory_order_relaxed))
        return false;
    const auto curr = track.timing.current();
    const auto total = track.timing.total();
    if (curr <= kTrackLengthForScrobble || (
            curr < total && std::chrono::duration<double>(curr) / std::chrono::duration<
                double>(total) <= kTrackLengthPercentageForScrobble))
        return false;

    auto trimAlbumName = [](const std::string &input) {
        auto pos = input.rfind(" — Single");
        if (pos == std::string::npos) {
            pos = input.rfind(" — EP");
            if (pos == std::string::npos) {
                return input;
            }
        }
        return input.substr(0, pos);
    };

    std::string body;

    const std::string timestamp = std::to_string(track.timing.start());
    const std::string artist = CurlWrapper::escape(track.identity.artist);
    const std::string title = CurlWrapper::escape(track.identity.title);
    const std::string album = CurlWrapper::escape(trimAlbumName(track.identity.album));
    const std::string hash = Md5(
        "album" + trimAlbumName(track.identity.album) + "api_key" + _apikey + "artist" + track.
        identity.artist + "method" + "track.scrobble" + "sk" + _sessionKey + "timestamp" + timestamp
        + "track" + track.identity.title + _apiSecret);

    body += "method=track.scrobble";
    body += "&api_key=" + _apikey;
    body += "&artist=" + artist;
    body += "&track=" + title;
    body += "&album=" + album;
    body += "&timestamp=" + timestamp;
    body += "&sk=" + _sessionKey;
    body += "&api_sig=" + hash;

    const std::string url = "https://ws.audioscrobbler.com/2.0/";

    std::unique_ptr<CurlWrapper> curl = nullptr;
    try {
        curl = std::make_unique<CurlWrapper>(url);
    } catch (const std::exception &e) {
        (void)e;
        return false;
    }

    curl->usePost(body);
    const auto r = curl->performCall();
    if (r.curlcode != CURLE_OK)
        return false;

    if (r.output.find("lfm status=\"ok\"") != std::string::npos) {
        return true;
    }
    return false;
}

LastFm::LastFm(const std::string &apikey, const std::string &apiSecret) {
    const auto sessionKey = readCredential(kSESSION_STORAGE_KEY);
    // try to read an existing credential
    _apikey = apikey;
    _apiSecret = apiSecret;

    if (sessionKey.empty() || !testSessionKey(sessionKey) || apikey.empty() || apiSecret.empty()) {
        _authenticated.store(false, std::memory_order::memory_order_relaxed);
        return;
    }

    _authenticated.store(true, std::memory_order::memory_order_relaxed);
    _sessionKey = sessionKey;
}

std::string LastFm::getNewSession(const std::string &token) const {
    std::string url = "https://ws.audioscrobbler.com/2.0/?method=auth.getSession&api_key=" + _apikey
                      + "&token=" + token;
    const std::string hash = Md5(
        "api_key" + _apikey + "method" + "auth.getSession" + "token" + token + _apiSecret);
    if (hash.empty())
        return {};
    url += "&api_sig=" + hash + "&format=json";

    std::unique_ptr<CurlWrapper> curl = nullptr;
    try {
        curl = std::make_unique<CurlWrapper>(url);
    } catch (const std::exception &e) {
        (void)e;
        return {};
    }

    const auto r = curl->performCall();
    if (r.curlcode != CURLE_OK)
        return {};
    try {
        Json j = Json::parse(r.output);
        if (j.contains("session") && j["session"].contains("key")) {
            const std::string session_key = j["session"]["key"];
            return session_key;
        }
        return {};
    } catch (const std::exception &e) {
        (void)e;
        return {};
    }
}

std::string LastFm::requestAuthToken() const {
    const std::string url = "https://ws.audioscrobbler.com/2.0/?method=auth.gettoken&api_key=" +
                            _apikey +
                            "&format=json";
    std::unique_ptr<CurlWrapper> curl = nullptr;
    try {
        curl = std::make_unique<CurlWrapper>(url);
    } catch (const std::exception &e) {
        (void)e;
        return {};
    }
    const auto r = curl->performCall();
    if (r.curlcode != CURLE_OK)
        return {};

    try {
        Json j = Json::parse(r.output);
        if (!j.contains("token")) {
            return {};
        }

        return j["token"].get<std::string>();
    } catch (const std::exception &e) {
        (void)e;
        return {};
    }
}

bool LastFm::testSessionKey(const std::string &key) const {
    std::string url = "https://ws.audioscrobbler.com/2.0/?method=user.getInfo&api_key=" + _apikey +
                      "&sk=" + key;
    const std::string hash = Md5(
        "api_key" + _apikey + "method" + "user.getInfo" + "sk" + key + _apiSecret);
    if (hash.empty()) {
        return false;
    }
    url += "&api_sig=" + hash + "&format=json";

    std::unique_ptr<CurlWrapper> curl = nullptr;
    try {
        curl = std::make_unique<CurlWrapper>(url);
    } catch (const std::exception &e) {
        (void)e;
        return false;
    }
    const auto r = curl->performCall();
    if (r.curlcode != CURLE_OK)
        return false;

    try {
        if (const Json j = Json::parse(r.output); j.contains("user")) {
            return true;
        }
        return false;
    } catch (const std::exception &e) {
        (void)e;
        return false;
    }
}

bool LastFm::authenticateUser() {
    const auto token = requestAuthToken();
    if (token.empty()) {
        std::cout << "Failed to get a LastFm auth token!\n";
        std::cout <<
            "This error is most likely due to an incorrect API key and secret combination.\n";
        std::cout << "LastFm will be disabled.\n";
        logging::get("lastfm")->warn(
            "Failed to get auth token (likely bad API key/secret); LastFm disabled");
        return false;
    }

    const std::string auth_url = "https://www.last.fm/api/auth/?api_key=" + _apikey + "&token=" +
                                 token;
    std::cout << "Go to the following link to login with LastFm:\n";
    std::cout << "\033[34m\x1b]8;;" + auth_url + "\x1b\\" + auth_url + "\x1b]8;;\x1b\\\x1b[0m\n";
    std::cout << "MusicPP will check for success every 5 seconds. Press `Esc` to cancel.";

    time_t timer = 0;
    std::atomic cancelled{false};
    std::jthread watcher([&](const std::stop_token &st) {
        while (!st.stop_requested()) {
            if (_kbhit()) {
                if (const int ch = _getch(); ch == 27) {
                    cancelled.store(true, std::memory_order::memory_order_relaxed);
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    while (!cancelled) {
        const auto key = getNewSession(token);
        if (!key.empty()) {
            watcher.request_stop();
            _sessionKey = key;
            _authenticated.store(true, std::memory_order::memory_order_relaxed);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
        timer += 5;
        if (timer >= 300) {
            std::cout << "Authentication timed out after 5 minutes.\n";
            logging::get("lastfm")->warn("LastFm authentication timed out after 5 minutes");
            cancelled.store(true, std::memory_order::memory_order_relaxed);
            watcher.request_stop();
        }
    }
    watcher.join();
    return false;
}

LastFm::~LastFm() {
    if (_authenticated.load(std::memory_order::memory_order_relaxed)) {
        writeCredential(kSESSION_STORAGE_KEY, _sessionKey);
    }
}

SearchResult LastFm::searchTrack(const Track &track) {
    if (_apikey.empty())
        return {};
    const std::string kNumSearchResults = "5";
    std::unique_ptr<CurlWrapper> curl = nullptr;
    try {
        const std::string url = "https://ws.audioscrobbler.com/2.0/?method=track.search&track=" +
                                CurlWrapper::escape(track.identity.title) + "&artist=" +
                                CurlWrapper::escape(track.identity.artist) + "&limit=" +
                                kNumSearchResults + "&format=json" + "&api_key=" + _apikey;
        curl = std::make_unique<CurlWrapper>(url);
    } catch (const std::exception &e) {
        (void)e;
        return {};
    }
    const auto r = curl->performCall();
    if (r.curlcode != CURLE_OK)
        return {};
    try {
        Json j = Json::parse(r.output)["results"];
        Json tracks = j["trackmatches"].value("track", Json::array());

        for (const auto &found : tracks) {
            const std::string title = found.value("name", "");
            const std::string artist = found.value("artist", "");
            const std::string url = found.value("url", "");

            const auto title_sim = fuzzyMatch(title, track.identity.title);
            const auto artist_sim = fuzzyMatch(artist, track.identity.artist);

            if (title_sim && artist_sim) {
                return {{}, url};
            }
        }
        return {};
    } catch (const std::exception &e) {
        (void)e;
        return {};
    }
}

bool LastFm::authed() const {
    return _authenticated.load(std::memory_order::memory_order_relaxed);
}

std::string LastFm::identify() {
    return kIDENTITY;
}
