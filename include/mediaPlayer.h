//
// Created by Jonathan on 25-Sep-25.
//

#ifndef MUSICPP_MEDIAPLAYER_H
#define MUSICPP_MEDIAPLAYER_H

#include <string>
#include <chrono>
#include <limits>
#include <winrt/windows.media.control.h>
#include <winrt/base.h>
#include <winrt/Windows.Media.h>
#include <winrt/windows.foundation.h>
#include <winrt/windows.foundation.collections.h>
#include <spotify.h>
#include <imgur.h>
#include <amscraper.h>
#include "leveldb/db.h"
#include <lfm.h>

namespace spdlog {
    class logger;
}

struct ArtworkLog {
    bool db_hit_image = false;
    bool db_hit_amlink = false;
    bool db_hit_lastfmlink = false;
    bool db_hit_spotifylink = false;
    bool db_image_expired = false;
    bool db_amlink_expired = false;
    bool db_lastfmlink_expired = false;
    bool db_spotifylink_expired = false;
    bool db_parse_error = false;
    bool scraper_used = false;
    bool lastfm_used = false;
    bool spotify_used = false;
    bool imgur_used = false;
    bool cache_written = false;
    bool AM_link_available = false;
    bool lastfm_link_available = false;
    bool Spotify_link_available = false;
    std::string db_url;
    std::string final_url;
    std::string final_source;
};

class mediaPlayer {
public:
    mediaPlayer(amscraper *scraper, SpotifyAPI *sapi, ImgurAPI *iapi, lfm *lastfm, leveldb::DB *database,
                spdlog::logger *logger = nullptr);

    ~mediaPlayer();

    wstring getTitle();

    wstring getArtist();

    wstring getAlbum();

    wstring getImage();

    wstring getImageSource();

    wstring getAMLink();

    wstring getLastFMLink();

    wstring getSpotifyLink();

    uint64_t getStartTS() const;

    uint64_t getEndTS() const;

    bool getState() const;

    uint64_t getPauseTimer() const;

    uint64_t getDurationSeconds() const;

    uint64_t getElapsedSeconds() const;

    void getInfo();

    void pause();

    void play();

    void reset();

    void printInfo() const;

private:
    leveldb::DB *db;
    wstring title;
    wstring artist;
    wstring album;
    wstring image;
    wstring spotify_link;
    wstring amlink;
    wstring lastfmlink;
    uint64_t totalTime = std::numeric_limits<uint64_t>::max();
    uint64_t start_ts = std::numeric_limits<uint64_t>::max();
    uint64_t end_ts = std::numeric_limits<uint64_t>::max();
    uint64_t pauseTime = std::numeric_limits<uint64_t>::max();
    bool playing = false;
    bool scrobbled = false;
    bool setNowPlaying = false;
    wstring image_source;
    SpotifyAPI *spotify_client;
    ImgurAPI *imgur_client;
    amscraper *scraper;
    spdlog::logger *logger;
    lfm *lastfm_client;

    int scrobbleattempts = 0;
    int nowplayingattempts = 0;


    Windows::Media::Control::GlobalSystemMediaTransportControlsSession session = nullptr;
    Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager smtcsm =
            Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

    void findRunning();

    bool hasActiveSession() const;

    void updatePlaybackState(
        const Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackInfo &info);

    void updateTimeline(
        const Windows::Media::Control::GlobalSystemMediaTransportControlsSessionTimelineProperties &info);

    bool updateMetadata(
        const Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties &properties);

    void loadDBImage(const time_t &currTime, const string &songKey, ArtworkLog &logInfo);

    void loadAMLink(const time_t &currTime, const string &songKey, ArtworkLog &logInfo);

    void loadLastFMLink(const time_t &currTime, const string &songKey, ArtworkLog &logInfo);

    void loadSpotifyLink(const time_t &currTime, const string &songKey, ArtworkLog &logInfo);

    void fetchArtworkAM(const time_t &currTime, const string &songKey, const string &stitle, const string &sartist,
                        const string &salbum, ArtworkLog &logInfo);

    void fetchLastFMLink(const time_t &currTime, const string &songKey, const string &stitle, const string &sartist,
                         ArtworkLog &logInfo);

    void fetchArtworkSpotify(const time_t &currTime, const string &songKey, const string &stitle, const string &sartist,
                             const string &salbum, ArtworkLog &logInfo);

    void fetchArtworkImgur(const time_t &currTime, const string &songKey, const IRandomAccessStreamReference &thumb,
                           ArtworkLog &logInfo);

    void clearNowPlaying();

    void log_artwork(const ArtworkLog &a) const;
};


#endif //MUSICPP_MEDIAPLAYER_H
