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
#include "system/winrt.hpp"

#include <iostream>
#include <thread>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>
#include <nlohmann/json.hpp>
#include <conio.h>

using Json = nlohmann::json;

namespace {
/**
* Computes the MD5 hash of a given input string.
* @param input Input to hash.
* @return Hashed output.
*/
std::string Md5(const std::string &input) {
    WinRtInit::initialize();
    namespace crypto = winrt::Windows::Security::Cryptography;
    try {
        // Held per thread
        static thread_local const auto provider =
            crypto::Core::HashAlgorithmProvider::OpenAlgorithm(
                crypto::Core::HashAlgorithmNames::Md5());

        const auto *inBytes = reinterpret_cast<const uint8_t *>(input.data());
        const auto inBuffer = crypto::CryptographicBuffer::CreateFromByteArray(
            {inBytes, inBytes + input.size()});
        const auto outBuffer = provider.HashData(inBuffer);
        return winrt::to_string(crypto::CryptographicBuffer::EncodeToHexString(outBuffer));
    } catch (const winrt::hresult_error &e) {
        logging::get("lastfm")->error("MD5 hashing failed: {}", winrt::to_string(e.message()));
        return {};
    } catch (...) {
        logging::get("lastfm")->error("MD5 hashing failed with an unknown error");
        return {};
    }
}

/**
 * Trims trailing album identifiers.
 * @param input Album name.
 * @return Trimmed name.
 */
std::string trimAlbumName(const std::string &input) {
    auto pos = input.rfind(" — Single");
    if (pos == std::string::npos) {
        pos = input.rfind(" — EP");
        if (pos == std::string::npos) {
            return input;
        }
    }
    return input.substr(0, pos);
}

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

    std::string body;

    const std::string timestamp = std::to_string(track.timing.start());
    const std::string artist = CurlWrapper::escape(track.identity.artist);
    const std::string title = CurlWrapper::escape(track.identity.title);
    const std::string album = CurlWrapper::escape(trimAlbumName(track.identity.album));
    const std::string hash = Md5(
        "album" + trimAlbumName(track.identity.album) + "api_key" + _apikey + "artist" + track.
        identity.artist + "method" + "track.scrobble" + "sk" + _sessionKey + "timestamp" + timestamp
        + "track" + track.identity.title + _apiSecret);
    if (hash.empty()) {
        logging::get("lastfm")->error("Skipping scrobble of '{} - {}': could not sign the request",
                                      track.identity.artist, track.identity.title);
        return false;
    }

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
    } catch (const CurlInitError &e) {
        logging::get("lastfm")->error("Skipping scrobble of '{} - {}': {}", track.identity.artist,
                                      track.identity.title, e.what());
        return false;
    }

    curl->usePost(body);
    const auto r = curl->performCall();
    if (!r.okOrWarn("lastfm", "track.scrobble for '{} - {}'", track.identity.artist,
                    track.identity.title))
        return false;

    if (r.output.find("lfm status=\"ok\"") != std::string::npos) {
        return true;
    }
    logging::get("lastfm")->warn("track.scrobble rejected for '{} - {}': {}", track.identity.artist,
                                 track.identity.title, r.briefBody());
    return false;
}

LastFm::LastFm(const std::string &apikey, const std::string &apiSecret) {
    const auto sessionKey = readCredential(kSESSION_STORAGE_KEY);
    // try to read an existing credential
    _apikey = apikey;
    _apiSecret = apiSecret;

    if (!sessionKey.empty() && !apikey.empty() && !apiSecret.empty()) {
        if (const auto user = testSessionKey(sessionKey); !user.empty()) {
            logging::get("lastfm")->info(
                "authenticated with last.fm as {} using previous session key", user);
            _authenticated.store(true, std::memory_order::memory_order_relaxed);
            _sessionKey = sessionKey;
            return;
        }
    }
    _authenticated.store(false, std::memory_order::memory_order_relaxed);
}

LastFm::Session LastFm::getNewSession(const std::string &token) const {
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
    } catch (const CurlInitError &e) {
        logging::get("lastfm")->error("auth.getSession failed: {}", e.what());
        return {};
    }

    // An unapproved token answers 403 on every 5s poll, so only a transport failure is worth
    // warning about. The success body carries the session key and is never logged.
    const auto r = curl->performCall();
    if (!r.transferredOrWarn("lastfm", "auth.getSession"))
        return {};
    try {
        Json j = Json::parse(r.output);
        if (j.contains("session") && j["session"].contains("key")) {
            Session session;
            session.key = j["session"]["key"].get<std::string>();
            // The response already names the user, so no user.getInfo call is needed.
            if (j["session"].contains("name"))
                session.name = j["session"]["name"].get<std::string>();
            return session;
        }
        // Expected while the user has not yet approved the token; polled every 5s.
        return {};
    } catch (const Json::exception &e) {
        // The body carries the session key on success, so it is never logged.
        logging::get("lastfm")->warn("Malformed auth.getSession response: {}", e.what());
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
    } catch (const CurlInitError &e) {
        logging::get("lastfm")->error("auth.getToken failed: {}", e.what());
        return {};
    }
    const auto r = curl->performCall();
    if (!r.okOrWarn("lastfm", "auth.getToken"))
        return {};

    try {
        Json j = Json::parse(r.output);
        if (!j.contains("token")) {
            logging::get("lastfm")->warn("auth.getToken response contained no token");
            return {};
        }

        return j["token"].get<std::string>();
    } catch (const Json::exception &e) {
        logging::get("lastfm")->warn("Malformed auth.getToken response: {}", e.what());
        return {};
    }
}

std::string LastFm::testSessionKey(const std::string &key) const {
    std::string url = "https://ws.audioscrobbler.com/2.0/?method=user.getInfo&api_key=" + _apikey +
                      "&sk=" + key;
    const std::string hash = Md5(
        "api_key" + _apikey + "method" + "user.getInfo" + "sk" + key + _apiSecret);
    if (hash.empty()) {
        return {};
    }
    url += "&api_sig=" + hash + "&format=json";

    std::unique_ptr<CurlWrapper> curl = nullptr;
    try {
        curl = std::make_unique<CurlWrapper>(url);
    } catch (const CurlInitError &e) {
        logging::get("lastfm")->error("Could not validate stored session key: {}", e.what());
        return {};
    }
    const auto r = curl->performCall();
    if (!r.okOrWarn("lastfm", "user.getInfo while validating the stored session key"))
        return {};

    try {
        if (const Json j = Json::parse(r.output); j.contains("user")) {
            return j.at("user").at("name").get<std::string>();
        }
        // No "user" object means the stored key was rejected; re-auth is required.
        logging::get("lastfm")->warn("Stored session key was rejected by user.getInfo");
        return {};
    } catch (const Json::exception &e) {
        // The request is signed with the session key, so the body is never logged.
        logging::get("lastfm")->warn("Malformed user.getInfo response: {}", e.what());
        return {};
    }
}

bool LastFm::authenticateUser() {
    if (_apikey.empty() || _apiSecret.empty()) {
        std::cout << "LastFm apikey or apisecret is empty, not attempting auth.\n";
        return false;
    }
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
    std::cout << "MusicPP will check for success every 5 seconds. Press `Esc` to cancel.\n";

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
        if (const auto session = getNewSession(token); !session.key.empty()) {
            watcher.request_stop();
            _sessionKey = session.key;
            _authenticated.store(true, std::memory_order::memory_order_relaxed);
            std::cout << "Successfully logged in"
                << (session.name.empty() ? std::string{} : " as " + session.name) << ".\n";
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
    } catch (const CurlInitError &e) {
        logging::get("lastfm")->error("Search for '{} - {}' failed: {}", track.identity.artist,
                                      track.identity.title, e.what());
        return {};
    }
    const auto r = curl->performCall();
    if (!r.okOrWarn("lastfm", "track.search for '{} - {}'", track.identity.artist,
                    track.identity.title))
        return {};
    try {
        Json j = Json::parse(r.output)["results"];
        Json tracks = j["trackmatches"].value("track", Json::array());

        for (const auto &found : tracks) {
            const std::string title = found.value("name", "");
            const std::string artist = found.value("artist", "");
            const std::string url = found.value("url", "");

            // Titles carry source-appended decoration ("… (Remastered)"), so allow a substring
            // match there; artists don't, so hold them to the ratio to avoid pulling in "X"
            // against "X Tribute".
            const auto title_sim = fuzzyMatch(title, track.identity.title, /*allowSubstring=*/true);
            const auto artist_sim = fuzzyMatch(artist, track.identity.artist);

            if (title_sim && artist_sim) {
                return {{}, url};
            }
        }
        // No result cleared the fuzzy-match threshold
        logging::get("lastfm")->debug("No match for '{} - {}' among {} result(s)",
                                      track.identity.artist, track.identity.title, tracks.size());
        return {};
    } catch (const Json::exception &e) {
        logging::get("lastfm")->warn("Malformed track.search response for '{} - {}': {}",
                                     track.identity.artist, track.identity.title, e.what());
        return {};
    }
}

bool LastFm::authed() const {
    return _authenticated.load(std::memory_order::memory_order_relaxed);
}

std::string LastFm::identify() {
    return kIDENTITY;
}

bool LastFm::setPlaying(const Track &track) const {
    if (!_authenticated.load(std::memory_order::memory_order_relaxed))
        return false;

    const std::string url = "https://ws.audioscrobbler.com/2.0/";
    std::string body;
    const std::string duration = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(track.timing.total()).count());
    const std::string artist = CurlWrapper::escape(track.identity.artist);
    const std::string album = CurlWrapper::escape(trimAlbumName(track.identity.album));
    const std::string title = CurlWrapper::escape(track.identity.title);

    const std::string hash = Md5(
        "album" + trimAlbumName(track.identity.album) + "api_key" + _apikey + "artist" + track.
        identity.artist + "duration" + duration + "method" + "track.updateNowPlaying" + "sk" +
        _sessionKey + "track" + track.identity.title + _apiSecret);
    if (hash.empty()) {
        logging::get("lastfm")->error(
            "Skipping now-playing update for '{} - {}': could not sign the request",
            track.identity.artist, track.identity.title);
        return false;
    }

    body += "method=track.updateNowPlaying";
    body += "&api_key=" + _apikey;
    body += "&artist=" + artist;
    body += "&track=" + title;
    body += "&album=" + album;
    body += "&duration=" + duration;
    body += "&sk=" + _sessionKey;
    body += "&api_sig=" + hash;

    std::unique_ptr<CurlWrapper> curl = nullptr;
    try {
        curl = std::make_unique<CurlWrapper>(url);
    } catch (const CurlInitError &e) {
        logging::get("lastfm")->error("Set now playing for '{} - {}' failed: {}",
                                      track.identity.artist, track.identity.title, e.what());
        return false;
    }

    curl->addHeader("Content-Type: application/x-www-form-urlencoded");
    curl->usePost(body);
    const auto &r = curl->performCall();
    if (!r.okOrWarn("lastfm", "track.updateNowPlaying for '{} - {}'", track.identity.artist,
                    track.identity.title))
        return false;

    if (r.output.find("lfm status=\"ok\"") != std::string::npos) {
        logging::get("lastfm")->info("Successfully set new LastFm now playing");
        return true;
    }
    logging::get("lastfm")->warn("track.updateNowPlaying rejected for '{} - {}': {}",
                                 track.identity.artist, track.identity.title, r.briefBody());
    return false;
}
