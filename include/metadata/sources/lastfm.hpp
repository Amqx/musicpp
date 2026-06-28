/**
 * @file lastfm.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 26-Jun-26
 */

#pragma once

#include "source.hpp"
#include <atomic>

class LastFm : public MetadataWebSource {
public:
    LastFm(const std::string &apikey, const std::string &apiSecret);

    ~LastFm() override;

    LastFm(const LastFm &) = delete;

    LastFm &operator=(const LastFm &) = delete;

    LastFm(const LastFm &&) = delete;

    LastFm &operator=(const LastFm &&) = delete;

    /**
     * Searching tracks with LastFm does not require a session key, but does require a valid API key.
     */
    SearchResult searchTrack(const Track &track) override;

    std::string identify() override;

    [[nodiscard]] bool authed() const;

    bool authenticateUser();

    bool scrobble(const Track &track) const;

private:
    std::string _sessionKey{};
    std::string _apikey{};
    std::string _apiSecret{};

    std::atomic<bool> _authenticated{};

    const std::chrono::seconds kTrackLengthForScrobble{30};
    const double kTrackLengthPercentageForScrobble{0.5};

    const std::string kIDENTITY = "LastFm API";
    const std::string kSESSION_STORAGE_KEY = "amqx_musicppv2_lastfm_apisecret";

    bool testSessionKey(const std::string &key) const;

    std::string requestAuthToken() const;

    std::string getNewSession(const std::string &token) const;
};
