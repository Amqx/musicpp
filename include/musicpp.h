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

    void PruneLogs(const filesystem::path &folder, wstringstream &out);

    bool IsValidRegion(const std::string &region);

    void PrintRegionList();

    void SaveRegion(const AppContext &ctx, const std::string &region);

    string LoadRegion(const AppContext &ctx);

    string GetRegion();

    string EnsureRegion(const AppContext &ctx);

    void PurgeDatabase(const AppContext *ctx);

    bool CleanDatabase(const AppContext &ctx, wstringstream &out);

    bool InitializeDatabase(AppContext &ctx, wstringstream &out);

    bool LoadCredentials(AppContext &ctx, const string &region, bool &console_created);

    variant<int, AppContext> setup();
}

namespace loop {
    void UpdateTrayTooltip(AppContext *ctx);

    void CopyToClipboard(const AppContext *ctx, const std::wstring &text, const wstring &label);

    static std::wstring GetDlgItemTextWStr(HWND hDlg, int id);

    static bool IsValidOrEmpty(const wstring &value, ValidationInputType type);

    static bool AreSettingsInputsValid(HWND hDlg);

    static void UpdateSettingsActionState(HWND hDlg);

    static void PopulateRegionCombo(HWND hDlg, const std::string &selected_region);

    static std::string GetComboSelection(HWND hCombo);

    void ApplySettings(HWND hDlg, const AppContext* ctx);

    LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    INT_PTR CALLBACK SettingsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

    int loop(AppContext &ctx, HINSTANCE hInstance);
}

#endif //MUSICPP_H
