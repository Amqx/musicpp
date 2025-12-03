//
// Created by Jonathan on 25-Sep-25.
//

#ifndef MUSICPP_MEDIAPLAYER_H
#define MUSICPP_MEDIAPLAYER_H

#include <string>
#include <winrt/windows.media.control.h>
#include <leveldb/db.h>
#include <shared_mutex>

#include "spotify.h"
#include "imgur.h"
#include "amscraper.h"
#include "constants.h"
#include "lfm.h"

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
    bool am_link_available = false;
    bool lastfm_link_available = false;
    bool spotify_link_available = false;
    std::string db_url;
    std::string final_url;
    std::string final_source;
};

struct Snapshot {
    wstring title = L"";
    wstring artist = L"";
    wstring album = L"";
    wstring image = L"";
    wstring image_source = L"";
    wstring amlink = L"";
    wstring lfmlink = L"";
    wstring splink = L"";
    uint64_t start_ts = kInvalidTime;
    uint64_t end_ts = kInvalidTime;
    bool state = false;
    bool has_session = false;
    uint64_t pause_timer = kInvalidTime;
    uint64_t duration = kInvalidTime;
    uint64_t elapsed = kInvalidTime;
};

class MediaPlayer {
public:
    MediaPlayer(Amscraper *scraper, SpotifyApi *sapi, ImgurApi *iapi, Lfm *lastfm, leveldb::DB *database,
                spdlog::logger *logger = nullptr);

    ~MediaPlayer();

    [[nodiscard]] Snapshot GetSnapshot(int type);

    void UpdateInfo();

    void ImageRefresh();

private:
    leveldb::DB *db_;
    wstring title_;
    wstring artist_;
    wstring album_;
    wstring image_;
    IRandomAccessStreamReference image_raw_;
    wstring spotify_link_;
    wstring amlink_;
    wstring lastfmlink_;
    uint64_t total_time_ = std::numeric_limits<uint64_t>::max();
    uint64_t start_ts_ = std::numeric_limits<uint64_t>::max();
    uint64_t end_ts_ = std::numeric_limits<uint64_t>::max();
    uint64_t pause_time_ = std::numeric_limits<uint64_t>::max();
    bool playing_ = false;
    bool scrobbled_ = false;
    bool set_now_playing_ = false;
    wstring image_source_;
    SpotifyApi *spotify_client_;
    ImgurApi *imgur_client_;
    Amscraper *scraper_;
    spdlog::logger *logger_;
    Lfm *lastfm_client_;
    int scrobbleattempts_ = 0;
    int nowplayingattempts_ = 0;
    Windows::Media::Control::GlobalSystemMediaTransportControlsSession session_ = nullptr;
    Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager smtcsm_ = nullptr;

    shared_mutex state_mutex_;
    atomic_bool refresh_in_progress_ = false;

    bool HasActiveSession() const;

    uint64_t GetElapsedSeconds() const;

    void pause();

    void play();

    void reset();

    void FindRunning();

    void UpdatePlaybackState(
        const Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackInfo &info);

    void UpdateTimeline(
        const Windows::Media::Control::GlobalSystemMediaTransportControlsSessionTimelineProperties &info);

    bool UpdateMetadata(
        const Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties &properties);

    void LoadDbImage(const uint64_t &curr_time, const string &song_key, ArtworkLog &log);

    void LoadAmLink(const uint64_t &curr_time, const string &song_key, ArtworkLog &log);

    void LoadLastFmLink(const uint64_t &curr_time, const string &song_key, ArtworkLog &log);

    void LoadSpotifyLink(const uint64_t &curr_time, const string &song_key, ArtworkLog &log);

    void FetchArtworkAm(const uint64_t &curr_time, const string &song_key, const string &stitle, const string &sartist,
                        const string &salbum, ArtworkLog &log);

    void FetchLastFmLink(const uint64_t &curr_time, const string &song_key, const string &stitle, const string &sartist,
                         ArtworkLog &log);

    void FetchArtworkSpotify(const uint64_t &curr_time, const string &song_key, const string &stitle,
                             const string &sartist,
                             const string &salbum, ArtworkLog &log);

    void FetchArtworkImgur(const uint64_t &curr_time, const string &song_key, const IRandomAccessStreamReference &thumb,
                           ArtworkLog &log);

    void log_artwork(const ArtworkLog &log) const;
};


#endif //MUSICPP_MEDIAPLAYER_H