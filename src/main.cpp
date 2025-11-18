//

// Created by Jonathan on 25-Sep-25.

//

#include <mediaPlayer.h>
#include <imgur.h>
#include <spotify.h>
#include <discordrp.h>
#include <chrono>
#include <thread>
#include <credhelper.h>
#include <iostream>
#include <windows.h>
#include <conio.h>
#include "leveldb/db.h"
#include <shlobj_core.h>
#include <filesystem>
#include <shellapi.h>
#include <resource.h>
#include <sstream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <timeutils.h>
#include <consoleutils.h>

using namespace std;

struct AppContext {
    std::unique_ptr<leveldb::DB> db;

    std::shared_ptr<spdlog::logger> logger;

    std::unique_ptr<SpotifyAPI> spotify;

    std::unique_ptr<ImgurAPI> imgur;

    std::unique_ptr<mediaPlayer> player;

    std::unique_ptr<discordrp> discord;

    NOTIFYICONDATAW nid{};

    HWND hWnd = nullptr;

    wstring lastTip;
};

#define INTERVAL 5
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT ID_TRAY_EXIT = 1001;
constexpr UINT_PTR DATA_TIMER_ID = 1;
constexpr UINT_PTR TOOLTIP_TIMER_ID = 2;
constexpr UINT DATA_TIMER_INTERVAL = INTERVAL * 1000;
constexpr UINT TOOLTIP_TIMER_INTERVAL = 1000;

bool InitializeDatabase(AppContext &ctx);
bool RunConfigurationMode(const AppContext &ctx);
void LoadCredentials(AppContext &ctx, bool forceReset);
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void UpdateTrayTooltip(AppContext *ctx);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    SetupConsole();

    AppContext ctx;

    if (!InitializeDatabase(ctx)) {
        std::wcout << L"Press Enter to continue..." << std::endl;
        std::wcin.get(); // Let user see errorMode();
        return 1;
    }

    LoadCredentials(ctx, RunConfigurationMode(ctx));
    if (ctx.logger) ctx.logger->info("Load credentials finished");


    wcout << "\nThis console window will automatically close in 3 seconds." << endl;

    for (int i = 3; i > 0; --i) {
        wcout << L"Closing in " << i << L" seconds... \r" << flush;

        this_thread::sleep_for(chrono::seconds(1));
    }

    CleanupConsole();
    if (ctx.logger) ctx.logger->info("Console cleanup complete. Moving to message loop.");

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

bool InitializeDatabase(AppContext &ctx) {
    PWSTR path = nullptr;

    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        wcout << L"Fatal: could not get local appdata path" << endl;

        return false;
    }

    wcout << L"Configuration files: " << path << L'\n' << endl;

    const string dbPath = wstr_to_str(path) + "/musicpp/song_db";
    const string logPath = wstr_to_str(path) + "/musicpp/logs/musicpp.log";
    CoTaskMemFree(path);

    filesystem::path newFolderLog(logPath);

    auto max_size = 1048576 * 5; // 5mb
    auto max_files = 3; // 3 files
    #ifdef LOG_LEVEL_DEBUG
        spdlog::set_level(spdlog::level::debug);
    #elif defined(LOG_LEVEL_INFO)
        spdlog::set_level(spdlog::level::info);
    #else
        spdlog::set_level(spdlog::level::warn);
    #endif
    spdlog::set_pattern("[%H:%M:%S %z] [%n] [%^---%L---%$] [thread %t] %v");
    auto logger = spdlog::rotating_logger_mt("MusicPP Logger", logPath, max_size, max_files);
    logger->flush_on(spdlog::level::err);
    ctx.logger = logger;

    if (ctx.logger) {
        ctx.logger -> info("Logger initialized at path: {}", logPath);
        ctx.logger -> info("MusicPP starting up...");
    }

    filesystem::path newFolder(dbPath);

    std::error_code ec;

    filesystem::create_directories(newFolder, ec);


    if (ec) {
        wcout << L"Fatal: Could not create/access local appdata folder" << endl;
        if (ctx.logger) ctx.logger -> error("Could not create/access local appdata folder: {}", ec.message());

        return false;
    }


    leveldb::DB *tempDB = nullptr;

    leveldb::Options options;

    options.create_if_missing = true;


    leveldb::Status status = leveldb::DB::Open(options, dbPath, &tempDB);

    if (status.ok()) {
        ctx.db.reset(tempDB);
        if (ctx.logger) ctx.logger -> info("Database initialized at path: {}", dbPath);
        return true;
    }

    wcout << L"Fatal: Could not initialize database!" << endl;
    if (ctx.logger) ctx.logger -> error("Could not initialize database: {}", status.ToString());

    return false;
}
bool RunConfigurationMode(const AppContext &ctx) {
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

            this_thread::sleep_for(chrono::milliseconds(100));
        }

        if (interaction) break;
    }


    if (interaction) {
        if (ctx.logger) ctx.logger->info("User interaction detected, entering Configuration Mode.");
        wcout << L"\n\n[Configuration Mode]" << endl;

        wcout << L"Enter 1 to force reset all API Keys." << endl;

        wcout << L"Enter anything else to keep current keys." << endl;


        wstring input;

        wcin >> input;

        if (input == L"1") {
            if (ctx.logger) ctx.logger -> info("User initiated API key reset.");
            wcout << L"Deleted all keys." << endl;

            return true; // forceReset = true
        }
    } else {
        if (ctx.logger) ctx.logger -> info("Auto-Start mode, no user interaction.");
        wcout << L"\n\n[Auto-Start] No interaction detected. Loading keys..." << endl;
    }

    return false; // forceReset = false
}
void LoadCredentials(AppContext &ctx, bool forceReset) {
    wcout << L"\nChecking API Keys..." << endl;


    wstring s_cid = EnsureCredential(L"musicpp/spotify_client_id", L"Spotify Client ID",
                                     L"https://developer.spotify.com/documentation/web-api/tutorials/getting-started",
                                     forceReset, ctx.logger.get());

    wstring s_sec = EnsureCredential(L"musicpp/spotify_client_secret", L"Spotify Client Secret",
                                     L"https://developer.spotify.com/documentation/web-api/tutorials/getting-started",
                                     forceReset, ctx.logger.get());

    wstring i_cid = EnsureCredential(L"musicpp/imgur_client_id", L"Imgur Client ID",
                                     L"https://api.imgur.com/oauth2/addclient", forceReset, ctx.logger.get());


    wcout << "\nAll APIKeys found!" << endl;
    if (ctx.logger) {
        ctx.logger -> info("All API keys loaded successfully.");
    }


    // Initialize API and Player objects
    ctx.spotify = std::make_unique<SpotifyAPI>(wstr_to_str(s_cid.c_str()), wstr_to_str(s_sec.c_str()), ctx.logger.get());
    if (ctx.logger) {
        ctx.logger -> info("SpotifyAPI initialized");
    }

    ctx.imgur = std::make_unique<ImgurAPI>(wstr_to_str(i_cid.c_str()), ctx.logger.get());
    if (ctx.logger) ctx.logger -> info("ImgurAPI initialized");

    ctx.player = std::make_unique<mediaPlayer>(ctx.spotify.get(), ctx.imgur.get(), ctx.db.get(), ctx.logger.get());
    if (ctx.logger) ctx.logger -> info("mediaPlayer initialized");

    ctx.discord = std::make_unique<discordrp>(ctx.player.get(), 1358389458956976128, ctx.logger.get());
    if (ctx.logger) ctx.logger -> info("discordrp initialized");
}
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto *ctx = reinterpret_cast<AppContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));


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

            if (ctx->logger) ctx->logger->info("Window created and tray icon added.");

            // Start Timers

            SetTimer(hWnd, DATA_TIMER_ID, DATA_TIMER_INTERVAL, nullptr);

            SetTimer(hWnd, TOOLTIP_TIMER_ID, TOOLTIP_TIMER_INTERVAL, nullptr);

            if (ctx->logger) ctx -> logger -> debug("Timers started: Data ({}) ms, Tooltip ({} ms)",
                                                  DATA_TIMER_INTERVAL,
                                                  TOOLTIP_TIMER_INTERVAL);
            return 0;
        }


        case WM_TRAYICON: {
            if (LOWORD(lParam) == WM_RBUTTONUP) {
                POINT pt{};

                GetCursorPos(&pt);

                SetForegroundWindow(hWnd); // Required for menu behavior


                if (HMENU hMenu = CreatePopupMenu()) {
                    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, TEXT("Exit"));

                    UINT cmd = TrackPopupMenu(
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
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                if (ctx -> logger) ctx->logger->info("User requested application exit via tray menu.");
                SendMessage(hWnd, WM_CLOSE, 0, 0);
            }

            return 0;
        }


        case WM_TIMER: {
            if (!ctx || !ctx->player || !ctx->discord) {
                if (ctx && ctx -> logger) ctx->logger->error("Timer triggered but AppContext/dependencies are null.");
                return 0;
            }


            if (wParam == DATA_TIMER_ID) {
                ctx->player->getInfo();

                ctx->discord->update();

                UpdateTrayTooltip(ctx);
                if (ctx -> logger) ctx->logger->debug("Data timer update cycle completed.");
            } else if (wParam == TOOLTIP_TIMER_ID) {
                UpdateTrayTooltip(ctx);
            }

            return 0;
        }


        case WM_CLOSE:

            if (ctx) {
                KillTimer(hWnd, DATA_TIMER_ID);
                KillTimer(hWnd, TOOLTIP_TIMER_ID);

                Shell_NotifyIcon(NIM_DELETE, &ctx->nid);

                if (ctx->nid.hIcon) {
                    DestroyIcon(ctx->nid.hIcon);
                }
                if (ctx -> logger) ctx->logger->info("Timers and icon cleaned up");
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
void UpdateTrayTooltip(AppContext *ctx) {
    if (ctx->nid.cbSize == 0) return;


    const wstring title = ctx->player->getTitle();

    const wstring artist = ctx->player->getArtist();

    const wstring album = ctx->player->getAlbum();

    const wstring imageSource = ctx->player->getImageSource();

    const bool playing = ctx->player->getState();

    wstringstream tipStream;

    if (title.empty() && artist.empty() && album.empty()) {
        tipStream << L"MusicPP\nNo track playing";
    } else {
        tipStream << L"MusicPP\nPresence Active\n\n";
        tipStream << (!title.empty() ? title : L"(unknown title)") << L"\n";
        tipStream << (!artist.empty() ? artist : L"(unknown artist)") << L"\n";
        tipStream << (!album.empty() ? album : L"(unknown album)") << L"\n\n";
        tipStream << L"Image Source: " << (!imageSource.empty() ? imageSource : L"unknown") << L"\n";

        if (playing) {
            if (const uint64_t duration = ctx->player->getDurationSeconds(); duration > 0) {
                const uint64_t elapsed = ctx->player->getElapsedSeconds();

                tipStream << L"" << FormatTimestamp(elapsed) << L" / " << FormatTimestamp(duration) << L"\n";
            }
        } else {
            tipStream << L"Paused" << L"\n";
        }
    }


    wstring tip = tipStream.str();

    // Ensure we don't overflow the buffer

    if (tip.size() >= ARRAYSIZE(ctx->nid.szTip)) {
        tip = tip.substr(0, ARRAYSIZE(ctx->nid.szTip) - 1);
    }

    if (tip == ctx->lastTip) {
        return;
    }
    ctx->lastTip = tip;
    if (ctx -> logger) ctx->logger->debug("Updating tray tooltip to: {}", wstr_to_str(tip.c_str()));

    lstrcpynW(ctx->nid.szTip, tip.c_str(), ARRAYSIZE(ctx->nid.szTip));

    ctx->nid.uFlags = NIF_TIP;

    Shell_NotifyIcon(NIM_MODIFY, &ctx->nid);
}