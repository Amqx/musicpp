//
// Created by Jonathan on 25-Sep-25.
//

#include <mediaPlayer.h>
#include <iostream>
#include <fcntl.h>
#include <spotify.h>
#include <utility>
#include <leveldb/db.h>
#include <limits>

namespace {
    constexpr uint64_t INVALID_TIME = std::numeric_limits<uint64_t>::max();

    uint64_t unix_seconds_now() {
        return static_cast<uint64_t>(
            chrono::duration_cast<chrono::seconds>(
                chrono::system_clock::now().time_since_epoch()
            ).count()
        );
    }

    bool is_extended_whitespace(wchar_t ch) {
        if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r' || ch == L'\f' || ch == L'\v') {
            return true;
        }
        if (ch == 0x00A0 || ch == 0x2007 || ch == 0x202F) {
            return true;
        }
        if (ch >= 0x2000 && ch <= 0x200A) {
            return true;
        }
        return false;
    }

    wstring trim_copy(const wstring &value) {
        if (value.empty()) {
            return L"";
        }

        size_t first = 0;
        while (first < value.size() && is_extended_whitespace(value[first])) {
            ++first;
        }
        if (first == value.size()) {
            return L"";
        }

        size_t last = value.size() - 1;
        while (last > first && is_extended_whitespace(value[last])) {
            --last;
        }
        return value.substr(first, last - first + 1);
    }

    bool split_artist_album(const wstring &input, wstring &outArtist, wstring &outAlbum) {
        auto is_dash = [](wchar_t ch) {
            return ch == L'-' || ch == 0x2013 || ch == 0x2014 || ch == 0x2015 || ch == 0x2212;
        };

        for (size_t dashPos = 0; dashPos < input.size(); ++dashPos) {
            wchar_t dashChar = input[dashPos];
            if (!is_dash(dashChar)) {
                continue;
            }

            size_t artistEnd = dashPos;
            while (artistEnd > 0 && is_extended_whitespace(input[artistEnd - 1])) {
                --artistEnd;
            }

            size_t albumStart = dashPos + 1;
            while (albumStart < input.size()) {
                wchar_t ch = input[albumStart];
                if (ch == dashChar || is_extended_whitespace(ch)) {
                    ++albumStart;
                } else {
                    break;
                }
            }

            wstring candidateArtist = trim_copy(input.substr(0, artistEnd));
            wstring candidateAlbum = trim_copy(albumStart < input.size() ? input.substr(albumStart) : L"");

            if (!candidateArtist.empty() && !candidateAlbum.empty()) {
                outArtist = std::move(candidateArtist);
                outAlbum = std::move(candidateAlbum);
                return true;
            }
        }

        outArtist.clear();
        outAlbum.clear();
        return false;
    }
}

mediaPlayer::mediaPlayer(SpotifyAPI *sapi, ImgurAPI *iapi, leveldb::DB *database) {
    this->spotify_client = sapi;
    this->imgur_client = iapi;
    this->db = database;

    init_apartment();
    if (smtcsm != nullptr) {
        findRunning();
        if (session != nullptr) {
            getInfo();
        }
    }
}

mediaPlayer::~mediaPlayer() {
    this->session = nullptr;
    delete db;
    uninit_apartment();
}


void mediaPlayer::findRunning() {
    for (auto const &s: this->smtcsm.GetSessions()) {
        if (*s.SourceAppUserModelId().c_str() == *L"AppleInc.AppleMusicWin_nzyj5cx40ttqa!App") {
            this->session = s;
            return;
        }
    }
    this->session = nullptr;
}

void mediaPlayer::getInfo() {
    findRunning();

    // if apple music running, then we go!
    if (this->session != nullptr) {
        // playback information - current playing status
        auto pb_info = session.GetPlaybackInfo();
        auto pb_status = pb_info.PlaybackStatus();
        if (pb_status == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused
            || pb_status ==
            winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped) {
            pause();
        } else if (pb_status ==
                   winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
            play();
        }

        // timeline information - stop/ start time
        auto tl_info = session.GetTimelineProperties();
        auto currTime = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
        auto position = tl_info.Position().count() / 10000000;
        auto endtime = tl_info.EndTime().count() / 10000000;
        this->start_ts = currTime - position;
        this->end_ts = currTime + endtime - position;

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
        }
        if (refresh) {
            this->image_source = L"";
            string stitle = convertWString(title);
            string sartist = convertWString(artist);
            string salbum = convertWString(album);
            string songKey = stitle + '|' + sartist + '|' + salbum;

            // check database
            if (db) {
                string result;
                leveldb::Status status = db->Get(leveldb::ReadOptions(), songKey, &result);
                if (!status.ok()) {
                    this->image = L"failed";
                    this->image_source = L"failed";
                } else {
                    size_t firstSep = result.find('|');
                    time_t timestamp = 0;
                    string url;
                    string source;
                    if (firstSep == string::npos) {
                        this->image = L"failed";
                        this->image_source = L"failed";
                    } else {
                        timestamp = stoll(result.substr(0, firstSep));
                        size_t secondSep = result.find('|', firstSep + 1);
                        if (secondSep == string::npos) {
                            url = result.substr(firstSep + 1);
                        } else {
                            url = result.substr(firstSep + 1, secondSep - firstSep - 1);
                            source = result.substr(secondSep + 1);
                        }
                    }

                    if (!url.empty()) {
                        if (currTime - timestamp > 24 * 7 * 60 * 60) {
                            db->Delete(leveldb::WriteOptions(), songKey);
                            this->image = L"failed";
                            this->image_source = L"DB, expired";
                        } else {
                            this->image = wstring(url.begin(), url.end());
                            if (source.empty()) {
                                this->image_source = L"DB, cached";
                            } else {
                                wstring wsource(source.begin(), source.end());
                                this->image_source = L"DB, cached from " + wsource;
                            }
                        }
                    }
                }
            }

            // Spotify search
            if ((this->image == L"failed" || this->image.empty()) && this->spotify_client != nullptr) {
                string tn = this->spotify_client->searchTracks(stitle, sartist, salbum);
                if (tn != "failed" && !tn.empty()) {
                    string value = to_string(currTime) + "|" + tn + "|Spotify";
                    if (db) {
                        leveldb::Status putstat = db->Put(leveldb::WriteOptions(), songKey, value);
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
                    if (db) {
                        db->Put(leveldb::WriteOptions(), songKey, value);
                    }
                }
                this->image = wstring(tn.begin(), tn.end());
                this->image_source = L"Imgur";
            }

            if (this->image.empty()) {
                this->image = L"default";
            }
        }
    } else {
        // if apple music not running, then we reset :(
        reset();
    }
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

bool mediaPlayer::getState() const {
    return this->playing;
}

uint64_t mediaPlayer::getStartTS() const {
    return this->start_ts;
}

uint64_t mediaPlayer::getEndTS() const {
    return this->end_ts;
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
    if (this->playing) {
        referenceTime = unix_seconds_now();
    } else if (this->pauseTime != INVALID_TIME) {
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

void mediaPlayer::pause() {
    this->playing = false;
    if (this->pauseTime == INVALID_TIME) {
        this->pauseTime = unix_seconds_now();
    }
}

void mediaPlayer::play() {
    this->playing = true;
    this->pauseTime = INVALID_TIME;
}

void mediaPlayer::reset() {
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

string mediaPlayer::convertWString(const wstring &wstr) {
    if (wstr.empty()) {
        return "";
    }

    const int required_size = WideCharToMultiByte(
        CP_UTF8, // CodePage: Convert to UTF-8
        0, // dwFlags
        wstr.data(), // lpWideCharStr: Pointer to wide string data
        static_cast<int>(wstr.length()), // cchWideChar: Length of the wide string (excluding null)
        nullptr, // lpMultiByteStr: Output buffer (NULL to get size)
        0, // cbMultiByte: Output buffer size (0 to get size)
        nullptr, nullptr // lpDefaultChar, lpUsedDefaultChar
    );

    if (required_size <= 0) {
        return ""; // Conversion error
    }
    std::string narrow_str(required_size, '\0');

    WideCharToMultiByte(
        CP_UTF8, // CodePage: Convert to UTF-8
        0, // dwFlags
        wstr.data(), // lpWideCharStr
        static_cast<int>(wstr.length()), // cchWideChar
        &narrow_str[0], // lpMultiByteStr: Use the internal buffer (C++11+)
        required_size, // cbMultiByte: Size of the buffer
        nullptr, nullptr
    );

    return narrow_str;
}
