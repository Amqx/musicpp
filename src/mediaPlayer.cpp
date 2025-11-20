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

mediaPlayer::mediaPlayer(amscraper *scraper, SpotifyAPI *sapi, ImgurAPI *iapi, leveldb::DB *database, spdlog::logger *logger) {
    this->scraper = scraper;
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

wstring mediaPlayer::getSpotifyLink() {
    return this->spotify_link;
}

wstring mediaPlayer::getAMLink() {
    return this->amlink;
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
        auto trackTitle = properties.Title();
        this->title = trackTitle.c_str();

        wstring s1 = albumArtistValue.c_str();

        // if any two are empty we ignore
        if (albumArtistValue.empty() || trackTitle.empty()) {
            if (this->logger) {
                this->logger->debug("albumArtist or title empty; assuming nothing is playing. Resetting.");
            }
            reset();
            return;
        }

        size_t sep = s1.find(L'\u2014');
        if (sep == wstring::npos) {
            if (this -> logger) {
                this->logger->debug("Custom song deteccted/ No EM dash found.");
            }
            this -> album = s1;
            this -> artist = s1;
        } else {
            wstring p1, p2;
            this -> artist = s1.substr(0, sep-1);
            this -> album = s1.substr(sep+2);

            // save down multiple EM dash songs for later investigation
            if (this -> album.find(L'\u2014') != wstring::npos) {
                if (this -> logger) {
                    this -> logger -> warn("Multiple EM dashes detected: {}", convertWString(s1));
                }
            }
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
            this->amlink = L"";
            this->spotify_link = L"";
            string stitle = convertWString(title);
            string sartist = convertWString(artist);
            string salbum = convertWString(album);
            string songKey = sanitizeKeys(stitle) + '|' + sanitizeKeys(sartist) + '|' + sanitizeKeys(salbum);

            // check database
            if (db) {
                // finding image
                string result;
                leveldb::Status status = db->Get(leveldb::ReadOptions(), songKey, &result);
                if (status.ok()) {
                    logInfo.db_hit_image = true;
                    try {
                        size_t firstSep = result.find('|');
                        if (firstSep == string::npos) {
                            throw invalid_argument("Missing first separator");
                        }
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
                                logInfo.db_image_expired = true;
                                this->image = L"";
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
                    } catch (const exception& e) {
                        if (this->logger) {
                            this->logger->warn("DB Image Parse Error for '{}': {}", songKey, e.what());
                        }
                        db->Delete(leveldb::WriteOptions(), songKey);
                        logInfo.db_parse_error = true;
                        this->image.clear();
                    }
                }

                // finding apple music link
                string AMResult;
                string amKey = "musicppAM" + songKey;
                leveldb::Status AMstatus = db->Get(leveldb::ReadOptions(), amKey, &AMResult);
                if (AMstatus.ok()) {
                    logInfo.db_hit_amlink = true;
                    try {
                        size_t AMsep = AMResult.find('|');
                        if (AMsep == string::npos) {
                            throw invalid_argument("Missing AM separator");
                        }
                        time_t timestamp = stoll(AMResult.substr(0, AMsep));
                        string url = AMResult.substr(AMsep + 1);

                        if (!url.empty()) {
                            if (currTime - timestamp > 24*7*60*60) {
                                db->Delete(leveldb::WriteOptions(), amKey);
                                logInfo.db_amlink_expired = true;
                            } else {
                                this->amlink = wstring(url.begin(), url.end());
                                logInfo.AM_link_available = true;
                            }
                        }
                    } catch (const exception &e) {
                        if (this->logger) {
                            this->logger->warn("DB AM Link Parse Error: {}. Deleting key.", e.what());
                        }
                        db->Delete(leveldb::WriteOptions(), amKey);
                        logInfo.db_parse_error = true;
                        this->amlink.clear();
                    }

                }

                // finding spotify link
                string SpotifyResult;
                string spKey = "musicppSP" + songKey;
                leveldb::Status SpotifyStatus = db->Get(leveldb::ReadOptions(), "musicppSP" + songKey, &SpotifyResult);
                if (SpotifyStatus.ok()) {
                    logInfo.db_hit_spotifylink = true;
                    try {
                        size_t SPsep = SpotifyResult.find('|');
                        if (SPsep == string::npos) {
                            throw invalid_argument("Missing Spotify separator");
                        }

                        time_t timestamp = stoll(SpotifyResult.substr(0, SPsep));
                        string url = SpotifyResult.substr(SPsep + 1);

                        if (!url.empty()) {
                            if (currTime - timestamp > 24 * 7 * 60 * 60) {
                                db->Delete(leveldb::WriteOptions(), spKey);
                                logInfo.db_spotifylink_expired = true;
                            } else {
                                this->spotify_link = wstring(url.begin(), url.end());
                                logInfo.Spotify_link_available = true;
                            }
                        }
                    } catch (const exception &e) {
                        if (this->logger) {
                            this->logger->warn("DB Spotify Link Parse Error: {}. Deleting key.", e.what());
                        }
                        db->Delete(leveldb::WriteOptions(), spKey);
                        logInfo.db_parse_error = true;
                        this->spotify_link.clear();
                    }
                }
            }

            // Apple Music search,  on fail the fields of res will be empty
            // Should run if we are missing either the image or amlink
            if (this->scraper && (this -> image.empty() || this->amlink.empty())) {
                scraperResult res = this->scraper->searchTracks(stitle, sartist, salbum);

                // Only update image if we don't have one already
                if (!res.image.empty() && this->image.empty()) {
                    string value = to_string(currTime) + "|" + res.image + "|AppleMusic";
                    logInfo.scraper_used = true;
                    if (db) {
                        db->Put(leveldb::WriteOptions(), songKey, value);
                        logInfo.cache_written = true;
                    }
                    this ->image = wstring(res.image.begin(), res.image.end());
                    this->image_source = L"AppleMusic";
                }

                // Always update the link if we have it
                if (!res.url.empty()) {
                    string urlKey = "musicppAM" + songKey;
                    string value = to_string(currTime) + "|" + res.url;
                    logInfo.AM_link_available = true;
                    if (db) {
                        db->Put(leveldb::WriteOptions(), urlKey,value);
                    }
                    this->amlink = wstring(res.url.begin(), res.url.end());
                }
            }

            // Spotify search, on fail the fields of res will be empty
            // Should run if we are missing either the image or spotify_link
            if (this->spotify_client && (this->image.empty() || this->spotify_link.empty())) {
                spotifyResult res = this->spotify_client->searchTracks(stitle, sartist, salbum);

                // Similarily, only update the image if we don't have it
                if (!res.image.empty() && this->image.empty()) {
                    string value = to_string(currTime) + "|" + res.image + "|Spotify";
                    logInfo.spotify_used = true;
                    if (db) {
                        db->Put(leveldb::WriteOptions(), songKey, value);
                        logInfo.cache_written = true;
                    }
                    this->image = wstring(res.image.begin(), res.image.end());
                    this->image_source = L"Spotify";
                }

                // Once again, always update the link if available
                if (!res.url.empty()) {
                    string urlKey = "musicppSP" + songKey;
                    string value = to_string(currTime) + "|" + res.url;
                    logInfo.Spotify_link_available = true;
                    if (db) {
                        db->Put(leveldb::WriteOptions(), urlKey,value);
                    }
                    this->spotify_link = wstring(res.url.begin(), res.url.end());
                }
            }

            // imgur upload, if failed imgur will always return default
            if (this->image.empty() && imgur_client) {
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
                    "db_hit_image={} db_image_expired={} "
                    "db_hit_amlink={} db_amlink_expired={} "
                    "db_hit_spotifylink={} db_spotifylink_expired={} db_parse_error={} "
                    "scraper_used={} spotify_used={} imgur_used={} cache_written={} "
                    "AM_link_available={} Spotify_link_available={}",
                    convertWString(title),
                    convertWString(artist),
                    convertWString(album),
                    logInfo.final_url,
                    logInfo.final_source,
                    logInfo.db_hit_image,
                    logInfo.db_image_expired,
                    logInfo.db_hit_amlink,
                    logInfo.db_amlink_expired,
                    logInfo.db_hit_spotifylink,
                    logInfo.db_spotifylink_expired,
                    logInfo.db_parse_error,
                    logInfo.scraper_used,
                    logInfo.spotify_used,
                    logInfo.imgur_used,
                    logInfo.cache_written,
                    logInfo.AM_link_available,
                    logInfo.Spotify_link_available
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
    wcout << L"Apple Music link: " << amlink << endl;
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