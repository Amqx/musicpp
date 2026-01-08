//
// Created by Jonathan on 25-Sep-25.
//

#include <leveldb/db.h>
#include <winrt/windows.media.control.h>
#include <winrt/base.h>
#include <spdlog/spdlog.h>
// ReSharper disable once CppUnusedIncludeDirective - Needed for get() deduced types
#include <winrt/windows.foundation.collections.h>
#include "mediaPlayer.h"
#include "constants.h"
#include "spotify.h"
#include "timeutils.h"
#include "stringutils.h"

MediaPlayer::MediaPlayer(Amscraper *scraper, M3U8Processor *processor, SpotifyApi *sapi, ImgurApi *iapi, Lfm *lastfm,
                         leveldb::DB *database,
                         spdlog::logger *logger) {
    this->scraper_ = scraper;
    this->processor_ = processor;
    this->spotify_client_ = sapi;
    this->imgur_client_ = iapi;
    this->db_ = database;
    this->logger_ = logger;
    this->lastfm_client_ = lastfm;

    init_apartment();
    smtcsm_ = Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    if (smtcsm_ != nullptr) {
        if (this->logger_) this->logger_->debug("SMTC Session Manager initialized");
        FindRunning();
        if (session_ != nullptr) {
            if (this->logger_) this->logger_->info("Apple Music session found at construction; calling getInfo()");
            UpdateInfo();
        } else {
            if (this->logger_) this->logger_->info("No Apple Music session found at construction");
        }
    } else if (this->logger_) {
        this->logger_->error("SMTC Session Manager is null; media controls will not work");
    }
}

MediaPlayer::~MediaPlayer() {
    this->session_ = nullptr;
    uninit_apartment();
    if (this->logger_) {
        logger_->info("mediaPlayer Killed");
    }
}

Snapshot MediaPlayer::GetSnapshot(const int type) const {
    Snapshot info = {};

    if (type == kSnapshotTypeDiscord) {
        info.title = title_;
        info.artist = artist_;
        info.album = album_;

        if (!image_.empty()) info.image = image_;
        else info.image = kDefaultImage;

        info.image_source = image_source_;
        info.amlink = amlink_;
        info.lfmlink = lastfmlink_;
        info.splink = spotify_link_;
        info.start_ts = start_ts_;
        info.end_ts = end_ts_;
        info.state = playing_;
        info.pause_timer = pause_time_;
    }

    if (type == kSnapshotTypeTime) {
        info.album = album_; // needed to show whether there is valid data
        info.image_source = image_source_;
        info.elapsed = GetElapsedSeconds();
        info.duration = total_time_;
        info.state = playing_;
    }

    if (type == kSnapshotTypeTray) {
        info.title = title_;
        info.artist = artist_;
        info.album = album_;
        info.image = image_;
        info.image_source = image_source_;
        info.has_session = session_ != nullptr;
    }

    return info;
}

uint64_t MediaPlayer::GetElapsedSeconds() const {
    if (this->start_ts_ == kInvalidTime) {
        return 0;
    }

    uint64_t reference_time;
    if (!this->playing_ && this->pause_time_ != kInvalidTime) {
        reference_time = this->pause_time_;
    } else {
        reference_time = UnixSecondsNow();
    }

    if (reference_time <= this->start_ts_) {
        return 0;
    }

    uint64_t elapsed = reference_time - this->start_ts_;
    if (total_time_ > 0 && elapsed > total_time_) {
        elapsed = total_time_;
    }
    return elapsed;
}

void MediaPlayer::UpdateInfo() {
    if (!smtcsm_) {
        smtcsm_ = Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        if (!smtcsm_) {
            if (logger_) {
                logger_->error("Failed to init SMTCSM! Nothing will work");
            }
            reset();
            return;
        }
        if (logger_) {
            logger_->info("Managed to revive SMTCSM. Tracking started");
        }
    }
    FindRunning();
    if (!HasActiveSession()) {
        reset();
        if (logger_) {
            logger_->debug("getInfo called but no Apple Music session found; resetting");
        }
        return;
    }

    try {
        UpdatePlaybackState(session_.GetPlaybackInfo());
        UpdateTimeline(session_.GetTimelineProperties());
    } catch (exception &e) {
        if (logger_) {
            logger_->warn("Failed to update playback state: {}", e.what());
        }
        reset();
        session_ = nullptr;
        return;
    }

    if (!playing_ && pause_time_ != kInvalidTime) {
        const auto paused_for = UnixSecondsNow() - pause_time_;
        if (paused_for >= kLfmNowPlayingPauseResetSeconds) {
            if (set_now_playing_ || nowplayingattempts_ > 0) {
                set_now_playing_ = false;
                nowplayingattempts_ = 0;
                if (logger_) {
                    logger_->debug("Reset LastFM now playing state after {}s paused", paused_for);
                }
            }
        }
    }

    wstring old_title = title_;
    wstring old_album = album_;
    wstring old_artist = artist_;

    // ReSharper disable once CppDFAUnusedValue
    Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties properties{nullptr};

    try {
        properties = session_.TryGetMediaPropertiesAsync().get();
    } catch (exception &e) {
        if (logger_) {
            logger_->warn("Failed to update get media properties: {}", e.what());
        }
        reset();
        session_ = nullptr;
        return;
    }

    if (!UpdateMetadata(properties)) {
        reset();
        return;
    }

    const string stitle = ConvertWString(this->title_);
    const string sartist = ConvertWString(artist_);
    const string salbum = ConvertWString(album_);
    const auto start_time = this->start_ts_;
    const auto duration = total_time_;

    if (!(old_title != title_ || old_album != album_ || old_artist != artist_)) {
        if (!set_now_playing_ && playing_ && cycle_num_ >= kLfmNowPlayingMinCycles) {
            this->set_now_playing_ = true;
            thread([this, stitle, sartist, salbum, duration] {
                if (nowplayingattempts_ < kMaxSetNowPlayingAttempts) {
                    const bool ok = lastfm_client_->UpdateNowPlaying(stitle, sartist, salbum, duration);
                    nowplayingattempts_ += 1;
                    if (!ok && nowplayingattempts_ < kMaxSetNowPlayingAttempts) {
                        this->set_now_playing_ = false;
                    }
                } else {
                    this->set_now_playing_ = true;
                }
            }).detach();
        }
        if (const auto elapsed_time = GetElapsedSeconds();
            !scrobbled_ && total_time_ > kLfmMinTime && ((
                static_cast<double>(elapsed_time) / static_cast<double>(this->total_time_) >=
                kLfmPercentage || elapsed_time > kLfmElapsedTime))) {
            this->scrobbled_ = true;
            std::thread([this, stitle, sartist, salbum, start_time] {
                if (scrobbleattempts_ < kMaxScrobbleAttempts) {
                    const bool ok = lastfm_client_->scrobble(stitle, sartist, salbum, start_time);
                    scrobbleattempts_ += 1;
                    if (!ok && scrobbleattempts_ < kMaxScrobbleAttempts) {
                        this->scrobbled_ = false;
                    }
                } else {
                    this->scrobbled_ = true;
                }
            }).detach();
        }
        if (cycle_num_ >= kGifMinCyclesBeforeProcess && !tried_animated_ && !amlink_.empty()) {
            tried_animated_ = true;
            processor_->start();
            processor_->Submit(ConvertWString(amlink_));
        } else if (tried_animated_) {
            if (!animated_complete_) {
                if (const auto status = processor_->Status(); status == Finished || status == Failed) {
                    animated_complete_ = true;
                    if (status == Finished) {
                        const auto gif = processor_->Get();
                        if (gif.empty()) return;

                        const auto url = imgur_client_->UploadImage(gif);
                        if (!url.empty()) {
                            this->image_ = ConvertToWString(url);

                            const string url_key =
                                    "musicppAMAnim" + SanitizeKeys(stitle) + '|' + SanitizeKeys(sartist) + '|' +
                                    SanitizeKeys(salbum);
                            const string value = to_string(UnixSecondsNow()) + "|" + url + "|Apple Music (Anim)";
                            // gifs get twice the expiry time
                            if (db_) {
                                db_->Put(leveldb::WriteOptions(), url_key, value);
                            }
                        }
                    } else {
                        if (logger_) logger_->info("M3U8 processor unsuccessful");
                    }
                    processor_->exit();
                }
            }
        }
        cycle_num_++;
        return;
    }

    ArtworkLog log;
    try {
        const auto curr_time = UnixSecondsNow();

        if (logger_)
            logger_->info("Track changed: '{}' by '{}' on '{}'",
                          ConvertWString(this->title_),
                          ConvertWString(this->artist_),
                          ConvertWString(this->album_));

        this->image_.clear();
        this->tried_animated_ = false;
        this->animated_complete_ = false;
        this->image_raw_ = properties.Thumbnail();
        this->image_source_.clear();
        this->amlink_.clear();
        this->spotify_link_.clear();
        this->lastfmlink_.clear();
        this->set_now_playing_ = false;
        this->scrobbled_ = false;
        this->scrobbleattempts_ = 0;
        this->nowplayingattempts_ = 0;
        this->cycle_num_ = 0;
        this->processor_->exit();

        const string song_key = SanitizeKeys(stitle) + '|' + SanitizeKeys(sartist) + '|' + SanitizeKeys(salbum);

        if (db_) {
            LoadDbImage(curr_time, song_key, log);
            LoadAmLink(curr_time, song_key, log);
            LoadLastFmLink(curr_time, song_key, log);
            LoadSpotifyLink(curr_time, song_key, log);
        }

        FetchArtworkAm(curr_time, song_key, stitle, sartist, salbum, log);
        FetchLastFmLink(curr_time, song_key, stitle, sartist, log);
        FetchArtworkSpotify(curr_time, song_key, stitle, sartist, salbum, log);
        FetchArtworkImgur(curr_time, song_key, image_raw_, log);
    } catch (exception &e) {
        if (logger_) logger_->error("Unknown error occured: {}", e.what());
    }

    log.final_source = ConvertWString(this->image_source_);
    log.final_url = ConvertWString(this->image_);

    log_artwork(log);
}

void MediaPlayer::ImageRefresh() {
    ArtworkLog log;
    try {
        const string stitle = ConvertWString(this->title_);
        const string sartist = ConvertWString(artist_);
        const string salbum = ConvertWString(album_);
        const string song_key = SanitizeKeys(stitle) + '|' + SanitizeKeys(sartist) + '|' + SanitizeKeys(salbum);
        const auto curr_time = UnixSecondsNow();


        if (logger_)
            logger_->info("Forced image refresh requested: '{}' by '{}' on '{}'",
                          ConvertWString(this->title_),
                          ConvertWString(this->artist_),
                          ConvertWString(this->album_));

        this->image_.clear();
        this->image_source_.clear();
        this->amlink_.clear();
        this->spotify_link_.clear();
        this->lastfmlink_.clear();
        this->tried_animated_ = false;
        this->animated_complete_ = false;

        if (db_) {
            db_->Delete(leveldb::WriteOptions(), song_key);
            db_->Delete(leveldb::WriteOptions(), "musicppAMAnim" + song_key);
            db_->Delete(leveldb::WriteOptions(), "musicppAM" + song_key);
            db_->Delete(leveldb::WriteOptions(), "musicppLFM" + song_key);
            db_->Delete(leveldb::WriteOptions(), "musicppSP" + song_key);
        }

        FetchArtworkAm(curr_time, song_key, stitle, sartist, salbum, log);
        FetchLastFmLink(curr_time, song_key, stitle, sartist, log);
        FetchArtworkSpotify(curr_time, song_key, stitle, sartist, salbum, log);
        FetchArtworkImgur(curr_time, song_key, image_raw_, log);
        if (cycle_num_ >= kGifMinCyclesBeforeProcess && !amlink_.empty()) {
            tried_animated_ = true;
            processor_->start();
            processor_->Submit(ConvertWString(amlink_));
        }
    } catch (exception &e) {
        if (logger_) logger_->error("Unknown error occured: {}", e.what());
    }

    log.final_source = ConvertWString(this->image_source_);
    log.final_url = ConvertWString(this->image_);

    log_artwork(log);
}

void MediaPlayer::pause() {
    this->playing_ = false;
    if (this->pause_time_ == kInvalidTime) {
        this->pause_time_ = UnixSecondsNow();
    }
    if (this->logger_) {
        this->logger_->debug("Paused playback at ts={}, elapsed={}s.",
                             this->pause_time_, GetElapsedSeconds());
    }
}

void MediaPlayer::play() {
    const bool prev_state = this->playing_;
    this->playing_ = true;
    this->pause_time_ = kInvalidTime;
    if (this->logger_) {
        if (prev_state != this->playing_) {
            this->logger_->debug("Resumed playback. start_ts={}, end_ts={}.",
                                 this->start_ts_, this->end_ts_);
        } else {
            this->logger_->debug("Retained playing state");
        }
    }
}

void MediaPlayer::reset() {
    if (this->logger_) {
        this->logger_->debug("Resetting mediaPlayer state. Previous track: '{}' by '{}'.",
                             ConvertWString(this->title_),
                             ConvertWString(this->artist_));
    }
    this->title_ = L"";
    this->artist_ = L"";
    this->album_ = L"";
    this->image_ = L"";
    this->tried_animated_ = false;
    this->animated_complete_ = false;
    this->image_source_ = L"";
    this->spotify_link_ = L"";
    this->amlink_ = L"";
    this->lastfmlink_ = L"";
    this->start_ts_ = kInvalidTime;
    this->end_ts_ = kInvalidTime;
    this->pause_time_ = kInvalidTime;
    this->playing_ = false;
    this->total_time_ = kInvalidTime;
    this->scrobbled_ = false;
    this->set_now_playing_ = false;
    this->scrobbleattempts_ = 0;
    this->nowplayingattempts_ = 0;
    cycle_num_ = 0;
    this->processor_->exit();
}

void MediaPlayer::FindRunning() {
    for (const auto &s: this->smtcsm_.GetSessions()) {
        if (*s.SourceAppUserModelId().c_str() == *L"AppleInc.AppleMusicWin_nzyj5cx40ttqa!App") {
            this->session_ = s;
            if (this->logger_) {
                this->logger_->debug("Apple Music session found: {}",
                                     ConvertWString(s.SourceAppUserModelId().c_str()));
            }
            return;
        }
    }
    if (this->logger_) {
        this->logger_->debug("No Apple Music session currently running");
    }
    this->session_ = nullptr;
}

bool MediaPlayer::HasActiveSession() const {
    return this->session_ != nullptr;
}

void MediaPlayer::UpdatePlaybackState(
    const Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackInfo &info) {
    auto status = info.PlaybackStatus();
    if (logger_) logger_->debug("Playback Status: {}", static_cast<int>(status));
    if (status == Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
        const bool prev_state = playing_;
        play();
        if (!prev_state && logger_) {
            logger_->info("Playback transitioned to playing");
        }
    } else {
        const bool prev_state = playing_;
        pause();
        if (prev_state && logger_) {
            logger_->info("Playback transitioned to paused/stopped");
        }
    }
}

void MediaPlayer::UpdateTimeline(
    const Windows::Media::Control::GlobalSystemMediaTransportControlsSessionTimelineProperties &info) {
    const auto curr_time = UnixSecondsNow();
    auto position = info.Position().count() / kWindowsTsConversion;
    auto end_time = info.EndTime().count() / kWindowsTsConversion;
    start_ts_ = curr_time - position;
    end_ts_ = start_ts_ + end_time;
    total_time_ = end_ts_ - start_ts_;
    if (logger_) {
        logger_->debug("Timeline: pos={}s, end={}s, total={}s, start_ts={}, end_ts={}",
                       position, end_time, total_time_, start_ts_, end_ts_);
    }
}

bool MediaPlayer::UpdateMetadata(
    const Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties &properties) {
    wstring album_artist_value = properties.AlbumArtist().c_str();
    const wstring track_title = properties.Title().c_str();
    this->title_ = track_title;

    if (album_artist_value.empty() || track_title.empty()) {
        if (logger_) {
            logger_->debug("albumArtist or title empty; assuming nothing is playing");
        }
        return false;
    }

    const size_t sep = album_artist_value.find(L'\u2014');
    if (sep == wstring::npos) {
        if (logger_) {
            logger_->debug("Custom song detected/ no EM dash found");
        }
        album_ = album_artist_value;
        artist_ = album_artist_value;
        return true;
    }
    this->artist_ = album_artist_value.substr(0, sep - 1);
    this->album_ = album_artist_value.substr(sep + 2);
    return true;
}

void MediaPlayer::LoadDbImage(const uint64_t &curr_time, const string &song_key, ArtworkLog &log) {
    string result;
    bool anim = true;
    if (const auto status = db_->Get(leveldb::ReadOptions(), "musicppAMAnim" + song_key, &result); !status.ok()) {
        anim = false;
        if (const auto status2 = db_->Get(leveldb::ReadOptions(), song_key, &result); !status2.ok()) return;
    }

    log.db_hit_image = true;
    try {
        const size_t first_sep = result.find('|');
        if (first_sep == string::npos) {
            throw invalid_argument("Missing first separator");
        }

        const time_t timestamp = stoll(result.substr(0, first_sep));

        const size_t second_sep = result.find('|', first_sep + 1);
        string url, source;
        if (second_sep == string::npos) {
            url = result.substr(first_sep + 1);
        } else {
            url = result.substr(first_sep + 1, second_sep - first_sep - 1);
            source = result.substr(second_sep + 1);
        }

        if (anim) {
            if (curr_time - timestamp > kDbExpireTimeAnim) {
                db_->Delete(leveldb::WriteOptions(), "musicppAMAnim" + song_key);
                log.db_image_expired = true;
                image_source_ = L"DB - expired";
                return;
            }
        } else {
            if (curr_time - timestamp > kDbExpireTime) {
                db_->Delete(leveldb::WriteOptions(), song_key);
                log.db_image_expired = true;
                image_source_ = L"DB - expired";
                return;
            }
        }

        image_ = ConvertToWString(url);
        if (anim) {
            this->animated_complete_ = true;
            this->tried_animated_ = true;
        }
        if (source.empty()) {
            image_source_ = L"DB - unknown";
        } else {
            image_source_ = L"DB, " + ConvertToWString(source);
        }
        log.db_url = url;
    } catch (const exception &e) {
        if (this->logger_) {
            this->logger_->warn("DB Image Parse Error for '{}': {}", song_key, e.what());
        }
        if (anim) {
            db_->Delete(leveldb::WriteOptions(), "musicppAMAnim" + song_key);
        } else {
            db_->Delete(leveldb::WriteOptions(), song_key);
        }
        log.db_parse_error = true;
        this->image_.clear();
        this->image_source_.clear();
    }
}

void MediaPlayer::LoadAmLink(const uint64_t &curr_time, const string &song_key, ArtworkLog &log) {
    const string full_key = "musicppAM" + song_key;
    string value;
    if (const auto status = db_->Get(leveldb::ReadOptions(), full_key, &value); !status.ok()) return;

    log.db_hit_amlink = true;
    try {
        const size_t sep = value.find('|');
        if (sep == string::npos) {
            throw invalid_argument("Missing AM separator");
        }

        const time_t ts = stoll(value.substr(0, sep));
        const string url = value.substr(sep + 1);

        if (curr_time - ts > kDbExpireTime) {
            db_->Delete(leveldb::WriteOptions(), full_key);
            log.db_amlink_expired = true;
        } else {
            amlink_ = ConvertToWString(url);
            log.am_link_available = true;
        }
    } catch (exception &e) {
        if (logger_) {
            logger_->warn("DB AM Link Parse Error: {}. Deleting key.", e.what());
        }
        db_->Delete(leveldb::WriteOptions(), full_key);
        log.db_parse_error = true;
        this->amlink_.clear();
    }
}

void MediaPlayer::LoadLastFmLink(const uint64_t &curr_time, const string &song_key, ArtworkLog &log) {
    const string full_key = "musicppLFM" + song_key;
    string value;
    if (const auto status = db_->Get(leveldb::ReadOptions(), full_key, &value); !status.ok()) return;

    log.db_hit_lastfmlink = true;

    try {
        const size_t sep = value.find('|');
        if (sep == string::npos) throw invalid_argument("Missing LastFM separator");

        const time_t ts = stoll(value.substr(0, sep));
        const string url = value.substr(sep + 1);

        if (curr_time - ts > kDbExpireTime) {
            db_->Delete(leveldb::WriteOptions(), full_key);
            log.db_lastfmlink_expired = true;
        } else {
            lastfmlink_ = ConvertToWString(url);
            log.lastfm_link_available = true;
        }
    } catch (exception &e) {
        if (logger_) {
            logger_->warn("DB LastFM Link Parse Error: {}. Deleting Key.", e.what());
        }
        db_->Delete(leveldb::WriteOptions(), full_key);
        log.db_parse_error = true;
    }
}

void MediaPlayer::LoadSpotifyLink(const uint64_t &curr_time, const string &song_key, ArtworkLog &log) {
    const string full_key = "musicppSP" + song_key;
    string value;
    if (const auto status = db_->Get(leveldb::ReadOptions(), full_key, &value); !status.ok()) return;

    log.db_hit_spotifylink = true;

    try {
        const size_t sep = value.find('|');
        if (sep == string::npos) throw invalid_argument("Missing Spotify separator");

        const time_t ts = stoll(value.substr(0, sep));
        const string url = value.substr(sep + 1);

        if (curr_time - ts > kDbExpireTime) {
            db_->Delete(leveldb::WriteOptions(), full_key);
            log.db_spotifylink_expired = true;
        } else {
            spotify_link_ = ConvertToWString(url);
            log.spotify_link_available = true;
        }
    } catch (exception &e) {
        if (logger_) {
            logger_->warn("DB Spotify Link Parse Error: {}. Deleting key.", e.what());
        }
        db_->Delete(leveldb::WriteOptions(), full_key);
        log.db_parse_error = true;
    }
}

void MediaPlayer::FetchArtworkAm(const uint64_t &curr_time, const string &song_key, const string &stitle,
                                 const string &sartist, const string &salbum, ArtworkLog &log) {
    if (!scraper_) return;
    if (!image_.empty() && !amlink_.empty()) return;
    log.scraper_used = true;
    const auto [url, image] = scraper_->SearchTracks(stitle, sartist, salbum);

    if (!image.empty() && image_.empty()) {
        const string value = to_string(curr_time) + "|" + image + "|Apple Music";
        if (db_) {
            db_->Put(leveldb::WriteOptions(), song_key, value);
            log.cache_written = true;
        }
        image_ = ConvertToWString(image);
        image_source_ = L"Apple Music";
    }

    if (!url.empty()) {
        const string url_key = "musicppAM" + song_key;
        const string value = to_string(curr_time) + "|" + url;
        log.am_link_available = true;
        if (db_) {
            db_->Put(leveldb::WriteOptions(), url_key, value);
        }
        amlink_ = ConvertToWString(url);
    }
}

void MediaPlayer::FetchLastFmLink(const uint64_t &curr_time, const string &song_key, const string &stitle,
                                  const string &sartist, ArtworkLog &log) {
    if (!lastfm_client_) return;
    if (!lastfmlink_.empty()) return;

    log.lastfm_used = true;

    if (const string res = lastfm_client_->SearchTracks(stitle, sartist); !res.empty()) {
        const string url_key = "musicppLFM" + song_key;
        const string value = to_string(curr_time) + "|" + res;
        log.lastfm_link_available = true;
        if (db_) {
            db_->Put(leveldb::WriteOptions(), url_key, value);
        }
        lastfmlink_ = ConvertToWString(res);
    }
}

void MediaPlayer::FetchArtworkSpotify(const uint64_t &curr_time, const string &song_key, const string &stitle,
                                      const string &sartist, const string &salbum, ArtworkLog &log) {
    if (!spotify_client_) return;

    // should only run if:
    // 1. image is empty
    // 2. lastfm's link is also empty
    // 3. spotify's link is ALSO empty
    if (!image_.empty() && !lastfmlink_.empty() && !amlink_.empty()) {
        return;
    }
    log.spotify_used = true;
    const auto [url, image] = spotify_client_->SearchTracks(stitle, sartist, salbum);

    if (!image.empty() && image_.empty()) {
        const string value = to_string(curr_time) + "|" + image + "|Spotify";
        if (db_) {
            db_->Put(leveldb::WriteOptions(), song_key, value);
            log.cache_written = true;
        }
        image_ = ConvertToWString(image);
        image_source_ = L"Spotify";
    }

    if (!url.empty()) {
        const string url_key = "musicppSP" + song_key;
        const string value = to_string(curr_time) + "|" + url;
        log.spotify_link_available = true;
        if (db_) {
            db_->Put(leveldb::WriteOptions(), url_key, value);
        }
        spotify_link_ = ConvertToWString(url);
    }
}

void MediaPlayer::FetchArtworkImgur(const uint64_t &curr_time, const string &song_key,
                                    const IRandomAccessStreamReference &thumb, ArtworkLog &log) {
    if (!imgur_client_) return;
    if (!image_.empty()) return;

    const string url = imgur_client_->UploadImage(thumb);
    if (!url.empty() && url != "default") {
        const string value = to_string(curr_time) + "|" + url + "|Imgur";
        log.imgur_used = true;
        if (db_) {
            db_->Put(leveldb::WriteOptions(), song_key, value);
            log.cache_written = true;
        }
    }

    image_ = ConvertToWString(url);
    if (image_ == L"default") {
        image_source_ = L"none";
    } else {
        image_source_ = L"Imgur";
    }
}

void MediaPlayer::log_artwork(const ArtworkLog &log) const {
    if (!logger_) return;

    string flags_str;

    auto append_flag = [&](const char *name, const bool value) {
        if (value) {
            if (!flags_str.empty()) flags_str += " ";
            flags_str += name;
        }
    };

    // database hits
    append_flag("db_hit_image", log.db_hit_image);
    append_flag("db_hit_am", log.db_hit_amlink);
    append_flag("db_hit_lfm", log.db_hit_lastfmlink);
    append_flag("db_hit_sp", log.db_hit_spotifylink);

    // expirations
    append_flag("img_expired", log.db_image_expired);
    append_flag("amlink_expired", log.db_amlink_expired);
    append_flag("lfmlink_expired", log.db_lastfmlink_expired);
    append_flag("splink_expired", log.db_spotifylink_expired);

    // sources used
    append_flag("parse_error", log.db_parse_error);
    append_flag("am_used", log.scraper_used);
    append_flag("lfm_used", log.lastfm_used);
    append_flag("spotify_used", log.spotify_used);
    append_flag("imgur_used", log.imgur_used);
    append_flag("cache_written", log.cache_written);

    // links available
    append_flag("am_avail", log.am_link_available);
    append_flag("lfm_avail", log.lastfm_link_available);
    append_flag("sp_avail", log.spotify_link_available);

    logger_->info(
        R"(ArtworkLog | Flags: [{}] | DB URL: "{}" | Final URL: "{}" | Source: "{}")",
        flags_str.empty() ? "NONE" : flags_str,
        log.db_url,
        log.final_url,
        log.final_source
    );
}
