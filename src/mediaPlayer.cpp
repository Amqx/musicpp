//
// Created by Jonathan on 25-Sep-25.
//

#include <mediaPlayer.h>
#include <iostream>
#include <fcntl.h>
#include <spotify.h>
#include <timeutils.h>
#include <leveldb/db.h>
#include <stringutils.h>

mediaPlayer::mediaPlayer(amscraper *scraper, SpotifyAPI *sapi, ImgurAPI *iapi, lfm *lastfm, leveldb::DB *database,
                         spdlog::logger *logger) {
    this->scraper = scraper;
    this->spotify_client = sapi;
    this->imgur_client = iapi;
    this->db = database;
    this->logger = logger;
    this->lastfm_client = lastfm;

    init_apartment();
    if (smtcsm != nullptr) {
        if (this->logger) this->logger->debug("SMTC Session Manager initialized");
        findRunning();
        if (session != nullptr) {
            if (this->logger) this->logger->info("Apple Music session found at construction; calling getInfo()");
            getInfo();
        } else {
            if (this->logger) this->logger->info("No Apple Music session found at construction");
        }
    } else if (this->logger) {
        this->logger->error("SMTC Session Manager is null; media controls will not work");
    }
}

mediaPlayer::~mediaPlayer() {
    this->session = nullptr;
    uninit_apartment();
    if (this->logger) {
        logger->info("mediaPlayer Killed");
    }
}

wstring mediaPlayer::getTitle() {
    return this->title;
}

wstring mediaPlayer::getArtist() {
    return this->artist;
}

wstring mediaPlayer::getAlbum() {
    return this->album;
}

wstring mediaPlayer::getImage() {
    if (!this->image.empty()) {
        return this->image;
    }
    return L"default";
}

wstring mediaPlayer::getAMLink() {
    return this->amlink;
}

wstring mediaPlayer::getLastFMLink() {
    return this->lastfmlink;
}

wstring mediaPlayer::getSpotifyLink() {
    return this->spotify_link;
}

wstring mediaPlayer::getImageSource() {
    if (this->image_source.empty()) {
        return L"unknown";
    }
    return this->image_source;
}

uint64_t mediaPlayer::getStartTS() const {
    return this->start_ts;
}

uint64_t mediaPlayer::getEndTS() const {
    return this->end_ts;
}

bool mediaPlayer::getState() const {
    return this->playing;
}

uint64_t mediaPlayer::getPauseTimer() const {
    return this->pauseTime;
}

uint64_t mediaPlayer::getDurationSeconds() const {
    return totalTime;
}

uint64_t mediaPlayer::getElapsedSeconds() const {
    if (this->start_ts == INVALID_TIME) {
        return 0;
    }

    uint64_t referenceTime;
    if (!this->playing && this->pauseTime != INVALID_TIME) {
        referenceTime = this->pauseTime;
    } else {
        referenceTime = unix_seconds_now();
    }

    if (referenceTime <= this->start_ts) {
        return 0;
    }

    uint64_t elapsed = referenceTime - this->start_ts;
    uint64_t duration = getDurationSeconds();
    if (duration > 0 && elapsed > duration) {
        elapsed = duration;
    }
    return elapsed;
}

void mediaPlayer::getInfo() {
    if (!smtcsm) {
        smtcsm = Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        if (!smtcsm) {
            if (logger) {
                logger->error("Failed to init SMTCSM! Nothing will work");
            }
            reset();
            return;
        }
        if (logger) {
            logger->info("Managed to revive SMTCSM. Tracking started");
        }
    }
    findRunning();
    if (!hasActiveSession()) {
        reset();
        if (logger) {
            logger->debug("getInfo called but no Apple Music session found; resetting");
        }
        return;
    }

    try {
        updatePlaybackState(session.GetPlaybackInfo());
        updateTimeline(session.GetTimelineProperties());
    } catch (exception &e) {
        if (logger) {
            logger->warn("Failed to update playback state: {}", e.what());
        }
        reset();
        session = nullptr;
        return;
    }

    wstring oldTitle = title;
    wstring oldAlbum = album;
    wstring oldArtist = artist;

    Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties properties{nullptr};

    try {
        properties = session.TryGetMediaPropertiesAsync().get();
    } catch (exception &e) {
        if (logger) {
            logger->warn("Failed to update get media properties: {}", e.what());
        }
        reset();
        session = nullptr;
        return;
    }

    if (!updateMetadata(properties)) {
        reset();
        return;
    }

    const string stitle = convertWString(this->title);
    const string sartist = convertWString(artist);
    const string salbum = convertWString(album);
    const time_t startTime = this->start_ts;
    const time_t duration = getDurationSeconds();

    if (!(oldTitle != title || oldAlbum != album || oldArtist != artist)) {
        if (const time_t elapsedTime = getElapsedSeconds();
            !scrobbled && totalTime > 30 && ((static_cast<double>(elapsedTime) / static_cast<double>(this->totalTime) >=
                                              0.5 || elapsedTime > 240))) {
            std::thread([this, stitle, sartist, salbum, startTime] {
                if (scrobbleattempts < 3) {
                    if (lastfm_client->scrobble(stitle, sartist, salbum, startTime)) {
                        this->scrobbled = true;
                    };
                    scrobbleattempts += 1;
                } else {
                    this->scrobbled = true;
                }
            }).detach();
        }
        return;
    }

    const auto currTime = unix_seconds_now();

    if (logger)
        logger->info("Track changed: '{}' by '{}' on '{}'",
                     convertWString(this->title),
                     convertWString(this->artist),
                     convertWString(this->album));

    this->image.clear();
    this->image_source.clear();
    this->amlink.clear();
    this->spotify_link.clear();
    this->lastfmlink.clear();
    this->setNowPlaying = false;
    this->scrobbled = false;
    this->scrobbleattempts = 0;
    this->nowplayingattempts = 0;

    ArtworkLog logInfo;
    const string songKey = sanitizeKeys(stitle) + '|' + sanitizeKeys(sartist) + '|' + sanitizeKeys(salbum);

    if (!setNowPlaying && playing) {
        thread([this, stitle, sartist, salbum, duration] {
            if (nowplayingattempts < 3) {
                if (lastfm_client->updateNowPlaying(stitle, sartist, salbum, duration)) {
                    this->setNowPlaying = true;
                };
                nowplayingattempts += 1;
            } else {
                this->setNowPlaying = true;
            }
        }).detach();
    }

    if (db) {
        loadDBImage(currTime, songKey, logInfo);
        loadAMLink(currTime, songKey, logInfo);
        loadLastFMLink(currTime, songKey, logInfo);
        loadSpotifyLink(currTime, songKey, logInfo);
    }

    fetchArtworkAM(currTime, songKey, stitle, sartist, salbum, logInfo);
    fetchLastFMLink(currTime, songKey, stitle, sartist, logInfo);
    fetchArtworkSpotify(currTime, songKey, stitle, sartist, salbum, logInfo);
    fetchArtworkImgur(currTime, songKey, properties.Thumbnail(), logInfo);

    logInfo.final_source = convertWString(this->image_source);
    logInfo.final_url = convertWString(this->image);

    log_artwork(logInfo);
}

void mediaPlayer::pause() {
    this->playing = false;
    if (this->pauseTime == INVALID_TIME) {
        this->pauseTime = unix_seconds_now();
    }
    if (this->logger) {
        this->logger->debug("Paused playback at ts={}, elapsed={}s.",
                            this->pauseTime, getElapsedSeconds());
    }
}

void mediaPlayer::play() {
    bool prev_state = this->playing;
    this->playing = true;
    this->pauseTime = INVALID_TIME;
    if (this->logger) {
        if (prev_state != this->playing) {
            this->logger->debug("Resumed playback. start_ts={}, end_ts={}.",
                                this->start_ts, this->end_ts);
        } else {
            this->logger->debug("Retained playing state");
        }
    }
}

void mediaPlayer::reset() {
    if (this->logger) {
        this->logger->debug("Resetting mediaPlayer state. Previous track: '{}' by '{}'.",
                            convertWString(this->title),
                            convertWString(this->artist));
    }
    this->title = L"";
    this->artist = L"";
    this->album = L"";
    this->image = L"";
    this->image_source = L"";
    this->spotify_link = L"";
    this->amlink = L"";
    this->lastfmlink = L"";
    this->start_ts = INVALID_TIME;
    this->end_ts = INVALID_TIME;
    this->pauseTime = INVALID_TIME;
    this->playing = false;
    this->totalTime = INVALID_TIME;
    this->scrobbled = false;
    this->setNowPlaying = false;
    this->scrobbleattempts = 0;
    this->nowplayingattempts = 0;
}

void mediaPlayer::printInfo() const {
    wcout << L"Player information" << endl;
    wcout << L"=================================" << endl;
    wcout << L"Title: " << title << endl;
    wcout << L"Artist: " << artist << endl;
    wcout << L"Album: " << album << endl;
    wcout << L"Image url: " << image << endl;
    wcout << L"Image source: " << image_source << endl;
    wcout << L"Spotify link: " << spotify_link << endl;
    wcout << L"Apple Music link: " << amlink << endl;
    wcout << L"LastFM link: " << lastfmlink << endl;
    wcout << L"Start timestamp (UNIX): " << start_ts << endl;
    wcout << L"End timestamp (UNIX): " << end_ts << endl;
    wcout << L"Total time: " << totalTime << endl;
    wcout << L"Currently playing: " << playing << endl;
    wcout << L"Pause timer (UNIX): " << pauseTime << endl;
}

void mediaPlayer::findRunning() {
    for (const auto &s: this->smtcsm.GetSessions()) {
        if (*s.SourceAppUserModelId().c_str() == *L"AppleInc.AppleMusicWin_nzyj5cx40ttqa!App") {
            this->session = s;
            if (this->logger) {
                this->logger->debug("Apple Music session found: {}",
                                    convertWString(s.SourceAppUserModelId().c_str()));
            }
            return;
        }
    }
    if (this->logger) {
        this->logger->debug("No Apple Music session currently running");
    }
    this->session = nullptr;
}

bool mediaPlayer::hasActiveSession() const {
    return this->session != nullptr;
}

void mediaPlayer::updatePlaybackState(
    const Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackInfo &info) {
    auto status = info.PlaybackStatus();
    if (logger) logger->debug("Playback Status: {}", static_cast<int>(status));
    if (status == Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
        bool prev_state = playing;
        play();
        if (!prev_state && logger) {
            logger->info("Playback transitioned to playing");
        }
    } else {
        bool prev_state = playing;
        pause();
        if (prev_state && logger) {
            logger->info("Playback transitioned to paused/stopped");
        }
    }
}

void mediaPlayer::updateTimeline(
    const Windows::Media::Control::GlobalSystemMediaTransportControlsSessionTimelineProperties &info) {
    auto currTime = unix_seconds_now();
    auto position = info.Position().count() / 10000000;
    auto endTime = info.EndTime().count() / 10000000;
    start_ts = currTime - position;
    end_ts = currTime + endTime - position;
    totalTime = end_ts - start_ts;
    if (logger) {
        logger->debug("Timeline: pos={}s, end={}s, total={}s, start_ts={}, end_ts={}",
                      position, endTime, totalTime, start_ts, end_ts);
    }
}

bool mediaPlayer::updateMetadata(
    const Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties &properties) {
    wstring albumArtistValue = properties.AlbumArtist().c_str();
    wstring trackTitle = properties.Title().c_str();
    this->title = trackTitle;

    if (albumArtistValue.empty() || trackTitle.empty()) {
        if (logger) {
            logger->debug("albumArtist or title empty; assuming nothing is playing and resetting");
        }
        reset();
        return false;
    }

    size_t sep = albumArtistValue.find(L'\u2014');
    if (sep == wstring::npos) {
        if (logger) {
            logger->debug("Custom song detected/ no EM dash found");
        }
        album = albumArtistValue;
        artist = albumArtistValue;
        return true;
    }
    this->artist = albumArtistValue.substr(0, sep - 1);
    this->album = albumArtistValue.substr(sep + 2);
    return true;
}

void mediaPlayer::loadDBImage(const time_t &currTime, const string &songKey, ArtworkLog &logInfo) {
    string result;
    const auto status = db->Get(leveldb::ReadOptions(), songKey, &result);
    if (!status.ok()) return;

    logInfo.db_hit_image = true;
    try {
        size_t firstSep = result.find('|');
        if (firstSep == string::npos) {
            throw invalid_argument("Missing first separator");
        }

        time_t timestamp = stoll(result.substr(0, firstSep));

        size_t secondSep = result.find('|', firstSep + 1);
        string url, source;
        if (secondSep == string::npos) {
            url = result.substr(firstSep + 1);
        } else {
            url = result.substr(firstSep + 1, secondSep - firstSep - 1);
            source = result.substr(secondSep + 1);
        }

        if (currTime - timestamp > 7 * 24 * 60 * 60) {
            db->Delete(leveldb::WriteOptions(), songKey);
            logInfo.db_image_expired = true;
            image_source = L"DB - expired";
            return;
        }

        image = convertToWString(url);
        if (source.empty()) {
            image_source = L"DB - unknown";
        } else {
            image_source = L"DB, " + convertToWString(source);
        }
        logInfo.db_url = url;
    } catch (const exception &e) {
        if (this->logger) {
            this->logger->warn("DB Image Parse Error for '{}': {}", songKey, e.what());
        }
        db->Delete(leveldb::WriteOptions(), songKey);
        logInfo.db_parse_error = true;
        this->image.clear();
        this->image_source.clear();
    }
}

void mediaPlayer::loadAMLink(const time_t &currTime, const string &songKey, ArtworkLog &logInfo) {
    const string fullKey = "musicppAM" + songKey;
    string value;
    const auto status = db->Get(leveldb::ReadOptions(), fullKey, &value);
    if (!status.ok()) return;

    logInfo.db_hit_amlink = true;
    try {
        size_t sep = value.find('|');
        if (sep == string::npos) {
            throw invalid_argument("Missing AM separator");
        }

        time_t ts = stoll(value.substr(0, sep));
        string url = value.substr(sep + 1);

        if (currTime - ts > 7 * 24 * 60 * 60) {
            db->Delete(leveldb::WriteOptions(), fullKey);
            logInfo.db_amlink_expired = true;
        } else {
            amlink = convertToWString(url);
            logInfo.AM_link_available = true;
        }
    } catch (exception &e) {
        if (logger) {
            logger->warn("DB AM Link Parse Error: {}. Deleting key.", e.what());
        }
        db->Delete(leveldb::WriteOptions(), fullKey);
        logInfo.db_parse_error = true;
        this->amlink.clear();
    }
}

void mediaPlayer::loadLastFMLink(const time_t &currTime, const string &songKey, ArtworkLog &logInfo) {
    const string fullKey = "musicppLFM" + songKey;
    string value;
    const auto status = db->Get(leveldb::ReadOptions(), fullKey, &value);
    if (!status.ok()) return;

    logInfo.db_hit_lastfmlink = true;

    try {
        size_t sep = value.find('|');
        if (sep == string::npos) throw invalid_argument("Missing LastFM separator");

        time_t ts = stoll(value.substr(0, sep));
        string url = value.substr(sep + 1);

        if (currTime - ts > 7 * 24 * 60 * 60) {
            db->Delete(leveldb::WriteOptions(), fullKey);
            logInfo.db_lastfmlink_expired = true;
        } else {
            lastfmlink = convertToWString(url);
            logInfo.lastfm_link_available = true;
        }
    } catch (exception &e) {
        if (logger) {
            logger->warn("DB LastFM Link Parse Error: {}. Deleting Key.", e.what());
        }
        db->Delete(leveldb::WriteOptions(), fullKey);
        logInfo.db_parse_error = true;
    }
}

void mediaPlayer::loadSpotifyLink(const time_t &currTime, const string &songKey, ArtworkLog &logInfo) {
    const string fullKey = "musicppSP" + songKey;
    string value;
    const auto status = db->Get(leveldb::ReadOptions(), fullKey, &value);
    if (!status.ok()) return;

    logInfo.db_hit_spotifylink = true;

    try {
        size_t sep = value.find('|');
        if (sep == string::npos) throw invalid_argument("Missing Spotify separator");

        time_t ts = stoll(value.substr(0, sep));
        string url = value.substr(sep + 1);

        if (currTime - ts > 7 * 24 * 60 * 60) {
            db->Delete(leveldb::WriteOptions(), fullKey);
            logInfo.db_spotifylink_expired = true;
        } else {
            spotify_link = convertToWString(url);
            logInfo.Spotify_link_available = true;
        }
    } catch (exception &e) {
        if (logger) {
            logger->warn("DB Spotify Link Parse Error: {}. Deleting key.", e.what());
        }
        db->Delete(leveldb::WriteOptions(), fullKey);
        logInfo.db_parse_error = true;
    }
}

void mediaPlayer::fetchArtworkAM(const time_t &currTime, const string &songKey, const string &stitle,
                                 const string &sartist, const string &salbum, ArtworkLog &logInfo) {
    if (!scraper) return;
    if (!image.empty() && !amlink.empty()) return;
    logInfo.scraper_used = true;
    searchResult res = scraper->searchTracks(stitle, sartist, salbum);

    if (!res.image.empty() && image.empty()) {
        const string value = to_string(currTime) + "|" + res.image + "|Apple Music";
        if (db) {
            db->Put(leveldb::WriteOptions(), songKey, value);
            logInfo.cache_written = true;
        }
        image = convertToWString(res.image);
        image_source = L"Apple Music";
    }

    if (!res.url.empty()) {
        const string urlKey = "musicppAM" + songKey;
        const string value = to_string(currTime) + "|" + res.url;
        logInfo.AM_link_available = true;
        if (db) {
            db->Put(leveldb::WriteOptions(), urlKey, value);
        }
        amlink = convertToWString(res.url);
    }
}

void mediaPlayer::fetchLastFMLink(const time_t &currTime, const string &songKey, const string &stitle,
                                  const string &sartist, ArtworkLog &logInfo) {
    if (!lastfm_client) return;
    if (!lastfmlink.empty()) return;

    logInfo.lastfm_used = true;
    string res = lastfm_client->searchTracks(stitle, sartist);

    if (!res.empty()) {
        const string urlKey = "musicppLFM" + songKey;
        string value = to_string(currTime) + "|" + res;
        logInfo.lastfm_link_available = true;
        if (db) {
            db->Put(leveldb::WriteOptions(), urlKey, value);
        }
        lastfmlink = convertToWString(res);
    }
}

void mediaPlayer::fetchArtworkSpotify(const time_t &currTime, const string &songKey, const string &stitle,
                                      const string &sartist, const string &salbum, ArtworkLog &logInfo) {
    if (!spotify_client) return;

    // should only run if:
    // 1. image is empty
    // 2. lastfm's link is also empty
    // 3. spotify's link is ALSO empty
    if (!image.empty() && !lastfmlink.empty() && !amlink.empty()) {
        return;
    }
    logInfo.spotify_used = true;
    searchResult res = spotify_client->searchTracks(stitle, sartist, salbum);

    if (!res.image.empty() && image.empty()) {
        string value = to_string(currTime) + "|" + res.image + "|Spotify";
        if (db) {
            db->Put(leveldb::WriteOptions(), songKey, value);
            logInfo.cache_written = true;
        }
        image = convertToWString(res.image);
        image_source = L"Spotify";
    }

    if (!res.url.empty()) {
        const string urlKey = "musicppSP" + songKey;
        const string value = to_string(currTime) + "|" + res.url;
        logInfo.Spotify_link_available = true;
        if (db) {
            db->Put(leveldb::WriteOptions(), urlKey, value);
        }
        spotify_link = convertToWString(res.url);
    }
}

void mediaPlayer::fetchArtworkImgur(const time_t &currTime, const string &songKey,
                                    const IRandomAccessStreamReference &thumb, ArtworkLog &logInfo) {
    if (!imgur_client) return;
    if (!image.empty()) return;

    string url = imgur_client->uploadImage(thumb);
    if (!url.empty() && url != "default") {
        const string value = to_string(currTime) + "|" + url + "|Imgur";
        logInfo.imgur_used = true;
        if (db) {
            db->Put(leveldb::WriteOptions(), songKey, value);
            logInfo.cache_written = true;
        }
    }

    image = convertToWString(url);
    if (image == L"default") {
        image_source = L"none";
    } else {
        image_source = L"Imgur";
    }
}

void mediaPlayer::log_artwork(const ArtworkLog &a) const {
    if (!logger) return;

    string flags_str;

    auto append_flag = [&](const char *name, bool value) {
        if (value) {
            if (!flags_str.empty()) flags_str += " ";
            flags_str += name;
        }
    };

    // database hits
    append_flag("db_hit_image", a.db_hit_image);
    append_flag("db_hit_am", a.db_hit_amlink);
    append_flag("db_hit_lfm", a.db_hit_lastfmlink);
    append_flag("db_hit_sp", a.db_hit_spotifylink);

    // expirations
    append_flag("img_expired", a.db_image_expired);
    append_flag("amlink_expired", a.db_amlink_expired);
    append_flag("lfmlink_expired", a.db_lastfmlink_expired);
    append_flag("splink_expired", a.db_spotifylink_expired);

    // sources used
    append_flag("parse_error", a.db_parse_error);
    append_flag("am_used", a.scraper_used);
    append_flag("lfm_used", a.lastfm_used);
    append_flag("spotify_used", a.spotify_used);
    append_flag("imgur_used", a.imgur_used);
    append_flag("cache_written", a.cache_written);

    // links available
    append_flag("am_avail", a.AM_link_available);
    append_flag("lfm_avail", a.lastfm_link_available);
    append_flag("sp_avail", a.Spotify_link_available);

    logger->info(
        R"(ArtworkLog | Flags: [{}] | DB URL: "{}" | Final URL: "{}" | Source: "{}")",
        flags_str.empty() ? "NONE" : flags_str,
        a.db_url,
        a.final_url,
        a.final_source
    );
}