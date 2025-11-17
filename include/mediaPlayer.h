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
#include "leveldb/db.h"

class mediaPlayer {
public:
    mediaPlayer(SpotifyAPI *sapi, ImgurAPI *iapi, leveldb::DB *database);

    ~mediaPlayer();

    wstring getTitle();

    wstring getArtist();

    wstring getAlbum();

    wstring getImage();

    wstring getImageSource();

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
    wstring itunes_link;
    uint64_t start_ts = std::numeric_limits<uint64_t>::max();
    uint64_t end_ts = std::numeric_limits<uint64_t>::max();
    uint64_t pauseTime = std::numeric_limits<uint64_t>::max();
    bool playing = false;
    wstring image_source;
    SpotifyAPI *spotify_client;
    ImgurAPI *imgur_client;

    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession session = nullptr;
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager smtcsm =
            winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

    void findRunning();

    static string convertWString(const wstring &wstr);
};


#endif //MUSICPP_MEDIAPLAYER_H
