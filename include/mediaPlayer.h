//
// Created by Jonathan on 25-Sep-25.
//

#ifndef MUSICPP_MEDIAPLAYER_H
#define MUSICPP_MEDIAPLAYER_H

#include <string>
#include <chrono>
#include <winrt/windows.media.control.h>
#include <winrt/base.h>
#include <winrt/Windows.Media.h>
#include <winrt/windows.foundation.h>
#include <winrt/windows.foundation.collections.h>
#include <include/spotify.h>
#include <include/imgur.h>
#include "leveldb/db.h"

class mediaPlayer {
public:
    mediaPlayer(SpotifyAPI *sapi, ImgurAPI *iapi, leveldb::DB *database);

    ~mediaPlayer();

    wstring getTitle();

    wstring getArtist();

    wstring getAlbum();

    wstring getImage();

    uint64_t getStartTS() const;

    uint64_t getEndTS() const;

    bool getState() const;

    uint64_t getPauseTimer() const;

    void getInfo();

    void pause();

    void play();

    void reset();

    void printInfo() const;

private:
    leveldb::DB *db;
    wstring title = L"";
    wstring artist = L"";
    wstring album = L"";
    wstring image = L"";
    uint64_t start_ts = -1;
    uint64_t end_ts = -1;
    uint64_t pauseTime = -1;
    bool playing = false;
    SpotifyAPI *spotify_client;
    ImgurAPI *imgur_client;

    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession session = nullptr;
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager smtcsm =
            winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

    void findRunning();

    string convertWString(const wstring &wstr);
};


#endif //MUSICPP_MEDIAPLAYER_H
