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

mediaPlayer::mediaPlayer(SpotifyAPI *sapi, ImgurAPI *iapi, leveldb::DB *database, spdlog::logger *logger) {
    this->spotify_client = sapi;
    this->imgur_client = iapi;
    this->db = database;
    this -> logger = logger;

    init_apartment();
    if (smtcsm != nullptr) {
        if (this->logger) this->logger->debug("SMTC Session Manager initialized.");
        findRunning();
        if (session != nullptr) {
            if (this->logger) this->logger->info("Apple Music session found at construction; calling getInfo().");
            getInfo();
        } else {
            if (this->logger) this->logger->info("No Apple Music session found at construction.");
        }
    } else if (this -> logger) {
        this->logger->error("SMTC Session Manager is null; media controls will not work.");
    }
}

mediaPlayer::~mediaPlayer() {
    this->session = nullptr;
    uninit_apartment();
    if (this -> logger) {
        logger -> info("mediaPlayer Killed");
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
    if (this->image.empty() != 1) {
        return this->image;
    }
    return L"default";
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
    if (this->start_ts == INVALID_TIME || this->end_ts == INVALID_TIME || this->end_ts <= this->start_ts) {
        return 0;
    }
    return this->end_ts - this->start_ts;
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
    findRunning();

    // if apple music running, then we go!
    if (this->session != nullptr) {
        // playback information - current playing status
        auto pb_info = session.GetPlaybackInfo();
        auto pb_status = pb_info.PlaybackStatus();
        if (this->logger) {
            this->logger->debug("PlaybackStatus: {}", static_cast<int>(pb_status));
        }
        if (pb_status == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused
            || pb_status ==
            winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped) {
            bool prev_state = this -> playing;
            pause();
            if (prev_state && this -> logger) {
                this->logger->info("Playback transitioned to paused/stopped.");
            }

        } else if (pb_status ==
                   winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
            bool prev_state = this -> playing;
            play();
            if (!prev_state && this -> logger) {
                this->logger->info("Playback transitioned to playing.");
            }
        }

        // timeline information - stop/ start time
        auto tl_info = session.GetTimelineProperties();
        auto currTime = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
        auto position = tl_info.Position().count() / 10000000;
        auto endtime = tl_info.EndTime().count() / 10000000;
        this->start_ts = currTime - position;
        this->end_ts = currTime + endtime - position;
        if (this->logger) {
            this->logger->debug("Timeline: pos={}s, end={}s, start_ts={}, end_ts={}",
                                position, endtime, start_ts, end_ts);
        }

        // init song properties
        auto properties = session.TryGetMediaPropertiesAsync().get();

        // thumbnail information - spotify search, imgur upload
        // ONLY UPDATE IF SONG INFO IS DIFFERENT!
        bool refresh = false;
        wstring oldArtist = artist;
        wstring oldAlbum = album;
        wstring oldTitle = title;

        // song information - album, artist, title
        auto albumArtistValue = properties.AlbumArtist();
        bool albumArtistProvided = !albumArtistValue.empty();
        wstring albumArtist = albumArtistValue.c_str();
        auto albumTitle = properties.AlbumTitle();
        auto trackArtist = properties.Artist();

        // if any two are empty we can assume nothing is playing
        if (albumArtistValue.empty() || properties.Title().empty()) {
            if (this->logger) {
                this->logger->debug("albumArtist or title empty; assuming nothing is playing. Resetting.");
            }
            reset();
            return;
        }

        this->title = properties.Title().c_str();

        if (!albumTitle.empty()) {
            this->album = trim_copy(albumTitle.c_str());
        } else {
            this->album.clear();
        }

        if (!trackArtist.empty()) {
            this->artist = trim_copy(trackArtist.c_str());
        } else {
            this->artist.clear();
        }

        if ((this->artist.empty() || this->album.empty() || this->artist == albumArtist || this->album == albumArtist)
            && albumArtistProvided) {
            wstring parsedArtist;
            wstring parsedAlbum;
            if (split_artist_album(albumArtist, parsedArtist, parsedAlbum)) {
                wstring trimmedAlbumArtist = trim_copy(albumArtist);
                if (this->artist.empty() || this->artist == albumArtist || this->artist == trimmedAlbumArtist) {
                    this->artist = parsedArtist;
                }
                if (this->album.empty() || this->album == albumArtist || this->album == trimmedAlbumArtist) {
                    this->album = parsedAlbum;
                }
            }
        }

        if (this->artist.empty() && albumArtistProvided) {
            this->artist = trim_copy(albumArtist);
        }
        if (this->album.empty() && albumArtistProvided) {
            this->album = trim_copy(albumArtist);
        }

        // compare new info against old, if anything changed, update photo
        if (this->artist != oldArtist || this->album != oldAlbum || this->title != oldTitle) {
            refresh = true;
            this -> image.clear();
            this -> image_source.clear();
            if (this->logger) {
                this->logger->debug("Track changed: '{}' by '{}' on '{}'",
                                   convertWString(this->title),
                                   convertWString(this->artist),
                                   convertWString(this->album));
            }
        }
        if (refresh) {
            ArtworkLog logInfo;
            this->image_source = L"";
            string stitle = convertWString(title);
            string sartist = convertWString(artist);
            string salbum = convertWString(album);
            string songKey = stitle + '|' + sartist + '|' + salbum;

            // check database
            if (db) {
                string result;
                leveldb::Status status = db->Get(leveldb::ReadOptions(), songKey, &result);
                if (status.ok()) {
                    logInfo.db_hit = true;
                    size_t firstSep = result.find('|');
                    if (firstSep == string::npos) {
                        logInfo.db_parse_error = true;
                    } else {
                        time_t timestamp = stoll(result.substr(0, firstSep));
                        size_t secondSep = result.find('|', firstSep + 1);
                        string url;
                        string source;
                        if (secondSep == string::npos) {
                            url = result.substr(firstSep + 1);
                        } else {
                            url = result.substr(firstSep + 1, secondSep - firstSep - 1);
                            source = result.substr(secondSep + 1);
                        }

                        if (!url.empty()) {
                            if (currTime - timestamp > 24 * 7 * 60 * 60) {
                                db->Delete(leveldb::WriteOptions(), songKey);
                                logInfo.db_expired = true;
                                this->image = L"failed";
                                this->image_source = L"DB, expired";
                            } else {
                                this->image = wstring(url.begin(), url.end());
                                if (source.empty()) {
                                    this->image_source = L"DB, unknown";
                                } else {
                                    wstring wsource(source.begin(), source.end());
                                    this->image_source = L"DB, " + wsource;
                                }
                                logInfo.db_url = url;
                            }
                        }
                    }
                }
            }

            // Spotify search
            if ((this->image == L"failed" || this->image.empty()) && this->spotify_client != nullptr) {
                string tn = this->spotify_client->searchTracks(stitle, sartist, salbum);
                if (!tn.empty() && tn != "failed") {
                    string value = to_string(currTime) + "|" + tn + "|Spotify";
                    logInfo.spotify_used = true;
                    if (db) {
                        db->Put(leveldb::WriteOptions(), songKey, value);
                        logInfo.cache_written = true;
                    }
                }
                this->image = wstring(tn.begin(), tn.end());
                this->image_source = L"Spotify";
            }

            // imgur upload, if failed returns "default"
            if ((this->image == L"failed" || this->image.empty()) && imgur_client != nullptr) {
                auto thumb = properties.Thumbnail();
                string tn = this->imgur_client->uploadImage(thumb);
                if (tn != "default" && !tn.empty()) {
                    string value = to_string(currTime) + "|" + tn + "|Imgur";
                    logInfo.imgur_used = true;
                    if (db) {
                        db->Put(leveldb::WriteOptions(), songKey, value);
                        logInfo.cache_written = true;
                    }
                }
                this->image = wstring(tn.begin(), tn.end());
                this->image_source = L"Imgur";
            }

            if (this->image.empty()) {
                this->image = L"default";
                this->image_source = L"none";
            }

            logInfo.final_url = convertWString(this->image);
            logInfo.final_source = convertWString(this->image_source);

            if (this -> logger) {
                this->logger->info(
                    "Artwork refresh: title='{}' | artist='{}' | album='{}' | "
                    "final_url='{}' | source='{}' | "
                    "db_hit={} db_expired={} db_parse_error={} "
                    "spotify_used={} imgur_used={} cache_written={}",
                    convertWString(title),
                    convertWString(artist),
                    convertWString(album),
                    logInfo.final_url,
                    logInfo.final_source,
                    logInfo.db_hit,
                    logInfo.db_expired,
                    logInfo.db_parse_error,
                    logInfo.spotify_used,
                    logInfo.imgur_used,
                    logInfo.cache_written
                );
            }

        }
    } else {
        if (this -> logger) {
            this -> logger -> debug("getInfo called but no Apple Music session found; resetting mediaPlayer state.");
        }
        reset();
    }
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
    this->playing = true;
    this->pauseTime = INVALID_TIME;
    if (this->logger) {
        this->logger->debug("Resumed playback. start_ts={}, end_ts={}.",
                           this->start_ts, this->end_ts);
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
    this->itunes_link = L"";
    this->start_ts = INVALID_TIME;
    this->end_ts = INVALID_TIME;
    this->pauseTime = INVALID_TIME;
    this->playing = false;
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
    wcout << L"Itunes link: " << itunes_link << endl;
    wcout << L"Start timestamp (UNIX): " << start_ts << endl;
    wcout << L"End timestamp (UNIX): " << end_ts << endl;
    wcout << L"Currently playing: " << playing << endl;
    wcout << L"Pause timer (UNIX): " << pauseTime << endl;
}

void mediaPlayer::findRunning() {
    for (auto const &s: this->smtcsm.GetSessions()) {
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
        this->logger->debug("No Apple Music session currently running.");
    }
    this->session = nullptr;
}