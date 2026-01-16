//
// Created by Jonathan on 16-Jan-26.
//

#ifndef MUSICPP_H
#define MUSICPP_H

#include "amscraper.h"
#include "m3u8.h"
#include "lfm.h"
#include "spotify.h"
#include "imgur.h"
#include "mediaPlayer.h"
#include "discordrp.h"
#include <Windows.h>
#include <shellapi.h>
#include <filesystem>
#include <variant>

namespace spdlog {
    class logger;
}

namespace leveldb {
    class DB;
}

struct AppContext {
    std::unique_ptr<leveldb::DB> db;

    std::shared_ptr<spdlog::logger> logger;

    std::unique_ptr<Amscraper> scraper;

    std::unique_ptr<M3U8Processor> processor;

    std::unique_ptr<Lfm> lastfm;

    std::unique_ptr<SpotifyApi> spotify;

    std::unique_ptr<ImgurApi> imgur;

    std::unique_ptr<MediaPlayer> player;

    std::unique_ptr<Discordrp> discord;

    NOTIFYICONDATAW nid{};

    HWND hWnd = nullptr;

    wstring last_tip;
};

namespace setup {
    string MakeLogName();

    void PruneLogs(const filesystem::path &folder);

    bool IsValidRegion(const std::string &region);

    void PrintRegionList();

    void SaveRegion(const AppContext &ctx, const std::string &region);

    string LoadRegion(const AppContext &ctx);

    string GetRegion();

    string EnsureRegion(const AppContext &ctx);

    void PurgeDatabase(const AppContext *ctx);

    void CleanDatabase(const AppContext &ctx);

    bool InitializeDatabase(AppContext &ctx);

    bool RunConfigurationMode(const AppContext &ctx, string &region);

    void LoadCredentials(AppContext &ctx, const bool &force_reset, const string &region);

    variant<int, AppContext> setup();
}

namespace loop {
    void UpdateTrayTooltip(AppContext *ctx);

    void CopyToClipboard(const AppContext *ctx, const std::wstring &text, const wstring &label);

    LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    int loop(AppContext &ctx, HINSTANCE hInstance);
}

#endif //MUSICPP_H
