//
// Created by Jonathan on 25-Sep-25.
//

#include <chrono>
#include <thread>
#include <iostream>
#include <Windows.h>
#include <conio.h>
#include <leveldb/db.h>
#include <shlobj_core.h>
#include <filesystem>
#include <shellapi.h>
#include <sstream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <unordered_set>
#include <vector>
#include <iomanip>
#include "lfm.h"
#include "timeutils.h"
#include "consoleutils.h"
#include "stringutils.h"
#include "constants.h"
#include "mediaPlayer.h"
#include "imgur.h"
#include "spotify.h"
#include "discordrp.h"
#include "credhelper.h"

using namespace std;

namespace {
    struct AppContext {
        std::unique_ptr<leveldb::DB> db;

        std::shared_ptr<spdlog::logger> logger;

        std::unique_ptr<Amscraper> scraper;

        std::unique_ptr<Lfm> lastfm;

        std::unique_ptr<SpotifyApi> spotify;

        std::unique_ptr<ImgurApi> imgur;

        std::unique_ptr<MediaPlayer> player;

        std::unique_ptr<Discordrp> discord;

        NOTIFYICONDATAW nid{};

        HWND hWnd = nullptr;

        wstring last_tip;
    };

    UINT g_wm_taskbar_restart = 0;
    constexpr UINT WM_TRAYICON = WM_APP + 1;
    constexpr UINT ID_TRAY_EXIT = 1001;
    constexpr UINT ID_COPY_TITLE = 1002;
    constexpr UINT ID_COPY_ARTIST = 1003;
    constexpr UINT ID_COPY_ALBUM = 1004;
    constexpr UINT ID_TRAY_DISCORD_TOGGLE = 1005;
    constexpr UINT ID_TRAY_LASTFM_TOGGLE = 1006;
    constexpr UINT_PTR DATA_TIMER_ID = 1;
    const std::unordered_set<std::string> kValidRegions{kRegionList.begin(), kRegionList.end()};
} // Constants
namespace {
    string MakeLogName() {
        const auto t = UnixSecondsNow();

        const chrono::zoned_time local{chrono::current_zone(), chrono::sys_time(chrono::seconds{t})};

        const auto lt = chrono::floor<chrono::seconds>(local.get_local_time());
        const auto dp = chrono::floor<chrono::days>(lt);
        const chrono::year_month_day ymd{dp};
        const auto tod = chrono::hh_mm_ss{lt - dp};

        return std::format("musicpp_{:04}-{:02}-{:02}_{:02}-{:02}-{:02}.log",
                           static_cast<int>(ymd.year()),
                           static_cast<unsigned>(ymd.month()),
                           static_cast<unsigned>(ymd.day()),
                           tod.hours().count(),
                           tod.minutes().count(),
                           tod.seconds().count()
        );
    }

    void PruneLogs(const filesystem::path &folder) {
        std::vector<filesystem::directory_entry> files;
        for (auto &entry: filesystem::directory_iterator(folder)) {
            if (entry.is_regular_file()) files.push_back(entry);
        }
        ranges::sort(files, [](auto &a, auto &b) {
            return a.last_write_time() > b.last_write_time();
        });

        if (files.size() > kMaxFiles) {
            for (size_t i = kMaxFiles; i < files.size(); i++) {
                error_code ec;
                filesystem::remove(files[i], ec);
                if (ec) {
                    wcout << L"Failed to remove old log file: " << ConvertToWString(files[i].path().string()) << endl;;
                }
            }
        }
    }

    bool IsValidRegion(const std::string &region) {
        return kValidRegions.contains(region);
    }

    void PrintRegionList() {
        wcout << L"\nAvailable Apple Music regions (country codes):" << endl;
        int count = 0;
        for (const auto &region: kRegionList) {
            wcout << setw(4) << region.c_str();
            ++count;
            if (count % 12 == 0) {
                wcout << endl;
            }
        }
        if (count % 12 != 0) {
            wcout << endl;
        }
        wcout << endl;
    }

    void SaveRegion(const AppContext &ctx, const std::string &region) {
        if (!ctx.db) return;
        if (const leveldb::Status status = ctx.db->Put(leveldb::WriteOptions(), kRegionDbKey, region); !status.ok()) {
            if (ctx.logger) ctx.logger->warn("Failed to persist region {}: {}", region, status.ToString());
        } else {
            if (ctx.logger) ctx.logger->info("Set region to {}", region);
        }
    }

    string LoadRegion(const AppContext &ctx) {
        if (!ctx.db) return "";
        string region;
        if (const leveldb::Status status = ctx.db->Get(leveldb::ReadOptions(), kRegionDbKey, &region); !status.ok()) {
            if (ctx.logger) ctx.logger->warn("Failed to read saved region: {}", status.ToString());
        }
        return region;
    }

    string GetRegion() {
        wstring input;
        while (true) {
            wcout << L"Enter Apple Music region code (type 'list' to view options): ";
            wcin >> input;

            string region_input = ToLowerCase(ConvertWString(input));
            if (region_input == "list") {
                PrintRegionList();
                continue;
            }

            if (IsValidRegion(region_input)) {
                return region_input;
            }

            wcout << L"Region not recognized. Please choose a code from the list below." << endl;
            PrintRegionList();
        }
    }

    string EnsureRegion(const AppContext &ctx) {
        string region = ToLowerCase(LoadRegion(ctx));
        if (region.empty()) {
            region = kDefaultRegion;
            SaveRegion(ctx, region);
        }
        return region;
    }

    bool InitializeDatabase(AppContext &ctx) {
        PWSTR path = nullptr;

        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
            wcout << L"Fatal: could not get local appdata path" << endl;

            return false;
        }

        wcout << L"Configuration files: " << path << L'\n' << endl;

        const filesystem::path base_path(path);
        const filesystem::path db_path = base_path / "musicpp" / "song_db";
        const filesystem::path log_path = base_path / "musicpp" / "logs";
        CoTaskMemFree(path);

        std::error_code ec1, ec2;

        create_directories(db_path, ec1);
        create_directories(log_path, ec2);

        if (ec1) {
            wcout << L"Fatal: Could not create db folder: " << ec1.message().c_str() << endl;
            return false;
        }

        if (ec2) {
            wcout << L"Could not create log folder: " << ec2.message().c_str() << endl;
            wcout << L"The program will not have logs. Continuing..." << endl;
        } else {
            PruneLogs(log_path);
            const string name = MakeLogName();
            const auto logger = spdlog::basic_logger_mt("MusicPP Logger", (log_path / name).string());
#ifdef LOG_LEVEL_DEBUG
            spdlog::set_level(spdlog::level::debug);
            logger->flush_on(spdlog::level::debug);
#elif defined(LOG_LEVEL_INFO)
            spdlog::set_level(spdlog::level::info);
            spdlog::flush_every(kLogFlushInterval);
#else
            spdlog::set_level(spdlog::level::debug);
            logger->flush_on(spdlog::level::debug);
#endif
            spdlog::set_pattern("(%Y-%m-%d %H:%M:%S %z, [%8l]) Thread %t: %v");
            ctx.logger = logger;
        }

        if (ctx.logger) {
            ctx.logger->info("Logger initialized at path: {}", log_path.string());
            ctx.logger->info("MusicPP starting up...");
        }

        leveldb::DB *temp_db = nullptr;

        leveldb::Options options;

        options.create_if_missing = true;

        const leveldb::Status status = leveldb::DB::Open(options, db_path.string(), &temp_db);

        if (status.ok()) {
            ctx.db.reset(temp_db);
            if (ctx.logger) ctx.logger->info("Database initialized at path: {}", db_path.string());
            return true;
        }

        wcout << L"Fatal: Could not initialize database!" << endl;
        if (ctx.logger) ctx.logger->error("Could not initialize database: {}", status.ToString());

        return false;
    }

    bool RunConfigurationMode(const AppContext &ctx, string &region) {
        wcout << L"------------------------------------------------\n" << endl;

        wcout << L"Press ANY KEY to enter Configuration/Reset mode." << endl;


        bool interaction = false;

        for (int i = 3; i > 0; --i) {
            wcout << L"Launching in " << i << L" seconds... \r" << flush;

            for (int j = 0; j < 10; ++j) {
                if (_kbhit()) {
                    (void) _getch(); // consume key

                    interaction = true;

                    break;
                }

                this_thread::sleep_for(chrono::milliseconds(50));
            }

            if (interaction) break;
        }


        if (interaction) {
            if (ctx.logger) ctx.logger->info("User interaction detected, entering Configuration Mode.");
            wcout << L"\n\n[Configuration Mode]" << endl;

            wcout << L"Enter 1 to force reset all API Keys" << endl;

            wcout << L"Enter 2 to change Apple Music region" << endl;

            wcout << L"Enter anything else to exit" << endl;


            wstring input;

            wcin >> input;

            if (input == L"1") {
                if (ctx.logger) ctx.logger->info("User initiated API key reset.");
                wcout << L"Deleted all keys." << endl;

                return true;
            }
            if (input == L"2") {
                wcout << L"Current Apple Music region: " << region.c_str() << endl;
                PrintRegionList();

                if (const string new_region = GetRegion(); new_region != region) {
                    region = new_region;
                    SaveRegion(ctx, region);
                    wcout << L"Apple Music region updated to " << region.c_str() << endl;
                } else {
                    wcout << L"Region unchanged" << endl;
                }
            }
        } else {
            if (ctx.logger) ctx.logger->info("Auto-Start mode, no user interaction");
            wcout << L"\n\n[Auto-Start] No interaction detected. Loading keys..." << endl;
        }

        return false;
    }

    void LoadCredentials(AppContext &ctx, const bool &force_reset, const string &region) {
        wcout << L"\nChecking API Keys..." << endl;

        const wstring s_cid = EnsureCredential(kSpotifyDbClientIdKey, L"Spotify Client ID",
                                               L"https://developer.spotify.com/documentation/web-api/tutorials/getting-started",
                                               force_reset, ctx.logger.get());

        const wstring s_sec = EnsureCredential(kSpotifyDbClientSecretKey, L"Spotify Client Secret",
                                               L"https://developer.spotify.com/documentation/web-api/tutorials/getting-started",
                                               force_reset, ctx.logger.get());

        const wstring i_cid = EnsureCredential(kImgurDbClientIdKey, L"Imgur Client ID",
                                               L"https://api.imgur.com/oauth2/addclient", force_reset,
                                               ctx.logger.get());

        const wstring lfm_key = EnsureCredential(kLastFmDbApikey, L"LastFM API Key",
                                                 L"https://www.last.fm/api/account/create",
                                                 force_reset, ctx.logger.get());

        const wstring lfm_secret = EnsureCredential(kLastFmDbSecret, L"LastFM Secret",
                                                    L"https://www.last.fm/api/account/create", force_reset,
                                                    ctx.logger.get());

        wcout << "\nAll APIKeys found" << endl;
        if (ctx.logger) {
            ctx.logger->info("All API keys loaded successfully");
        }

        // Initialize API and Player objects
        wcout << L"Apple Music region: " << region.c_str() << endl;
        ctx.scraper = std::make_unique<Amscraper>(region, ctx.logger.get());
        if (ctx.logger) ctx.logger->info("AMScraper initialized with region {}", region);

        ctx.lastfm = std::make_unique<Lfm>(ConvertWString(lfm_key), ConvertWString(lfm_secret), ctx.db.get(),
                                           ctx.logger.get());
        if (ctx.logger) {
            ctx.logger->info("LastFM initialized");
        }

        ctx.spotify = std::make_unique<SpotifyApi>(ConvertWString(s_cid), ConvertWString(s_sec), ctx.logger.get());
        if (ctx.logger) {
            ctx.logger->info("SpotifyAPI initialized");
        }

        ctx.imgur = std::make_unique<ImgurApi>(ConvertWString(i_cid), ctx.logger.get());
        if (ctx.logger) ctx.logger->info("ImgurAPI initialized");

        ctx.player = std::make_unique<MediaPlayer>(ctx.scraper.get(), ctx.spotify.get(), ctx.imgur.get(),
                                                   ctx.lastfm.get(), ctx.db.get(), ctx.logger.get());
        if (ctx.logger) ctx.logger->info("mediaPlayer initialized");

        ctx.discord = std::make_unique<Discordrp>(ctx.player.get(), kDiscordApikey, ctx.db.get(), ctx.logger.get());
        if (ctx.logger) ctx.logger->info("discordrp initialized");
    }

    void UpdateTrayTooltip(AppContext *ctx) {
        if (ctx->nid.cbSize == 0) return;

        const wstring image_source = ctx->player->GetImageSource();

        const bool playing = ctx->player->GetState();

        wstringstream tip_stream;

        if (ctx->player->GetArtist().empty()) {
            tip_stream << L"MusicPP\nNo track playing";
        } else {
            tip_stream << L"MusicPP\nPresence Active\n\n";
            tip_stream << L"Image Source: " << (!image_source.empty() ? image_source : L"unknown") << L"\n";

            if (playing) {
                if (const uint64_t duration = ctx->player->GetDurationSeconds(); duration > 0) {
                    const uint64_t elapsed = ctx->player->GetElapsedSeconds();

                    tip_stream << L"" << FormatTimestamp(elapsed) << L" / " << FormatTimestamp(duration) << L"\n";
                }
            } else {
                tip_stream << L"Paused" << L"\n";
            }
        }

        const wstring tip = tip_stream.str();

        if (tip == ctx->last_tip) {
            return;
        }

        ctx->last_tip = tip;

        if (ctx->logger) ctx->logger->debug("Updating tray tooltip to: \n{}", ConvertWString(tip));

        lstrcpynW(ctx->nid.szTip, tip.c_str(), ARRAYSIZE(ctx->nid.szTip));

        ctx->nid.uFlags |= NIF_TIP | NIF_ICON | NIF_MESSAGE;

        Shell_NotifyIcon(NIM_MODIFY, &ctx->nid);
    }

    void CopyToClipboard(const AppContext *ctx, const std::wstring &text) {
        if (!OpenClipboard(nullptr)) {
            if (ctx->logger) ctx->logger->error("Failed to open clipboard for text: {}", ConvertWString(text));
            return;
        }
        EmptyClipboard();

        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        const HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!hMem) {
            if (ctx->logger)
                ctx->logger->error("Failed to allocate memory for clipboard text: {}",
                                   ConvertWString(text));
            CloseClipboard();
            return;
        }

        memcpy(GlobalLock(hMem), text.c_str(), bytes);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
        if (ctx->logger) ctx->logger->debug("Set clipboard to: {}", ConvertWString(text));
        CloseClipboard();
    }

    LRESULT CALLBACK WndProc(const HWND hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam) {
        auto *ctx = reinterpret_cast<AppContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

        if (msg == g_wm_taskbar_restart) {
            if (ctx && ctx->nid.cbSize != 0) {
                // Explorer restarted
                ctx->nid.uFlags |= NIF_MESSAGE | NIF_ICON | NIF_TIP;
                if (!ctx->nid.hIcon) {
                    ctx->nid.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APPICON));
                    if (!ctx->nid.hIcon) ctx->nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION); // Fallback
                }
                Shell_NotifyIcon(NIM_ADD, &ctx->nid);
                if (ctx->logger) ctx->logger->info("Shell restarted, tray icon recreated");
            }

            UpdateTrayTooltip(ctx);
            return 0;
        }

        switch (msg) {
            case WM_CREATE: {
                // Setup Tray Icon

                auto *createStruct = reinterpret_cast<CREATESTRUCT *>(lParam);

                ctx = static_cast<AppContext *>(createStruct->lpCreateParams);

                SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

                ctx->hWnd = hWnd;


                ctx->nid.cbSize = sizeof(ctx->nid);

                ctx->nid.hWnd = hWnd;

                ctx->nid.uID = 1;

                ctx->nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;

                ctx->nid.uCallbackMessage = WM_TRAYICON;

                ctx->nid.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APPICON));

                if (!ctx->nid.hIcon) ctx->nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION); // Fallback


                lstrcpynW(ctx->nid.szTip, L"MusicPP", ARRAYSIZE(ctx->nid.szTip));

                Shell_NotifyIcon(NIM_ADD, &ctx->nid);

                if (ctx->logger) ctx->logger->info("Window created and tray icon added");

                // Start Timers

                SetTimer(hWnd, DATA_TIMER_ID, kLoopRefreshInterval, nullptr);

                if (ctx->logger)
                    ctx->logger->debug("Timer started: Data ({} ms)",
                                       kLoopRefreshInterval);
                return 0;
            }


            case WM_TRAYICON: {
                if (LOWORD(lParam) == WM_RBUTTONUP) {
                    POINT pt{};

                    GetCursorPos(&pt);

                    SetForegroundWindow(hWnd); // Required for menu behavior


                    if (const HMENU hMenu = CreatePopupMenu()) {
                        const wstring v = L"MusicPP V" + kVersion;
                        AppendMenu(hMenu, MF_STRING | MF_DISABLED, 0, v.c_str());
                        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
                        const wstring title = ctx->player->GetTitle();
                        const wstring artist = ctx->player->GetArtist();
                        const wstring album = ctx->player->GetAlbum();
                        if (ctx->player->HasActiveSession() && !title.empty() && !artist.empty() && !album.empty()) {
                            AppendMenu(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, L"Now Playing");
                            AppendMenu(hMenu, MF_STRING, ID_COPY_TITLE, EscapeAmpersands(Truncate(title)).c_str());
                            AppendMenu(hMenu, MF_STRING, ID_COPY_ARTIST, EscapeAmpersands(Truncate(artist)).c_str());
                            AppendMenu(hMenu, MF_STRING, ID_COPY_ALBUM, EscapeAmpersands(Truncate(album)).c_str());
                            AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
                        }

                        const wstring discord_state =
                                ctx->discord->GetState() ? L"Discord active" : L"Discord disabled";
                        AppendMenu(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, discord_state.c_str());
                        AppendMenu(hMenu, MF_STRING, ID_TRAY_DISCORD_TOGGLE, TEXT("Toggle Discord"));

                        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);

                        AppendMenu(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, ctx->lastfm->GetReason().c_str());
                        AppendMenu(hMenu, MF_STRING, ID_TRAY_LASTFM_TOGGLE, TEXT("Toggle LastFM"));

                        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
                        AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, TEXT("Exit"));
                        const UINT cmd = TrackPopupMenu(
                            hMenu,
                            TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD,
                            pt.x,
                            pt.y,
                            0,
                            hWnd,
                            nullptr);

                        DestroyMenu(hMenu);

                        if (cmd != 0) {
                            PostMessage(hWnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
                        }
                    }

                    PostMessage(hWnd, WM_NULL, 0, 0);
                }

                return 0;
            }


            case WM_COMMAND: {
                switch (LOWORD(wParam)) {
                    case ID_TRAY_EXIT: {
                        if (ctx->logger) ctx->logger->info("User requested application exit via tray menu.");
                        SendMessage(hWnd, WM_CLOSE, 0, 0);
                        return 0;
                    }

                    case ID_TRAY_LASTFM_TOGGLE: {
                        if (ctx->logger) ctx->logger->info("LastFM state toggled");
                        ctx->lastfm->toggle();
                        return 0;
                    }

                    case ID_TRAY_DISCORD_TOGGLE: {
                        if (ctx->logger) ctx->logger->info("Discord state toggled");
                        ctx->discord->toggle();
                        return 0;
                    }

                    case ID_COPY_TITLE: {
                        CopyToClipboard(ctx, ctx->player->GetTitle());
                        return 0;
                    }

                    case ID_COPY_ARTIST: {
                        CopyToClipboard(ctx, ctx->player->GetArtist());
                        return 0;
                    }

                    case ID_COPY_ALBUM: {
                        CopyToClipboard(ctx, ctx->player->GetAlbum());
                        return 0;
                    }

                    default: {
                        return 0;
                    }


                }
            }


            case WM_TIMER: {
                if (!ctx || !ctx->player || !ctx->discord) {
                    if (ctx && ctx->logger) ctx->logger->error("Timer triggered but AppContext/dependencies are null.");
                    return 0;
                }

                ctx->player->UpdateInfo();
                ctx->discord->update();
                UpdateTrayTooltip(ctx);
                if (ctx->logger) ctx->logger->debug("Data timer update cycle completed.");
                return 0;
            }


            case WM_CLOSE:

                if (ctx) {
                    KillTimer(hWnd, DATA_TIMER_ID);

                    Shell_NotifyIcon(NIM_DELETE, &ctx->nid);

                    if (ctx->nid.hIcon) {
                        DestroyIcon(ctx->nid.hIcon);
                    }
                    if (ctx->logger) ctx->logger->info("Timers and icon cleaned up");
                }

                DestroyWindow(hWnd);

                return 0;


            case WM_DESTROY:

                PostQuitMessage(0);

                return 0;

            default: ;
        }

        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
} // Helpers

int WINAPI WinMain(const HINSTANCE hInstance, const HINSTANCE hPrevInstance, const LPSTR lpCmdLine,
                   const int nCmdShow) {
    SetupConsole();

    AppContext ctx;

    if (!InitializeDatabase(ctx)) {
        // Let user see error before closing
        std::wcout << L"Press Enter to continue..." << std::endl;
        std::wcin.get();
        return 1;
    }

    std::string region = EnsureRegion(ctx);
    const bool force_reset = RunConfigurationMode(ctx, region);

    LoadCredentials(ctx, force_reset, region);
    if (ctx.logger) ctx.logger->info("Load credentials finished");

    wcout << "\nThis console window will automatically close in 3 seconds" << endl;

    for (int i = 3; i > 0; --i) {
        wcout << L"Closing in " << i << L" seconds... \r" << flush;

        this_thread::sleep_for(chrono::seconds(1));
    }

    CleanupConsole();
    if (ctx.logger) ctx.logger->info("Console cleanup complete, moving to message loop");

    g_wm_taskbar_restart = RegisterWindowMessage(TEXT("TaskbarCreated"));

    if (g_wm_taskbar_restart == 0) {
        if (ctx.logger) ctx.logger->error("Failed to register TaskbarCreated message");
    }

    WNDCLASSW wc{};

    wc.lpfnWndProc = WndProc;

    wc.hInstance = hInstance;

    wc.lpszClassName = L"MusicPPTrayClass";

    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);


    if (!RegisterClassW(&wc)) return 0;


    ctx.hWnd = CreateWindowExW(0, L"MusicPPTrayClass", L"TrayOnlyWindow",

                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,

                               300, 200, nullptr, nullptr, hInstance, &ctx);


    if (!ctx.hWnd) return 0;

    ShowWindow(ctx.hWnd, SW_HIDE);


    MSG msg{};

    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);

        DispatchMessage(&msg);
    }
    if (ctx.logger) ctx.logger->info("Application message loop ended. Shutting down.");

    return static_cast<int>(msg.wParam);
}
