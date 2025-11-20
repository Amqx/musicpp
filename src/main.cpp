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
#include <Windows.h>
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
#include "stringutils.h"
#include <unordered_set>
#include <vector>
#include <iomanip>

using namespace std;
namespace {
    struct AppContext {
        std::unique_ptr<leveldb::DB> db;

        std::shared_ptr<spdlog::logger> logger;

        std::unique_ptr<amscraper> scraper;

        std::unique_ptr<SpotifyAPI> spotify;

        std::unique_ptr<ImgurAPI> imgur;

        std::unique_ptr<mediaPlayer> player;

        std::unique_ptr<discordrp> discord;

        NOTIFYICONDATAW nid{};

        HWND hWnd = nullptr;

        wstring lastTip;
    };
    constexpr int INTERVAL = 5;
    constexpr UINT WM_TRAYICON = WM_APP + 1;
    constexpr UINT ID_TRAY_EXIT = 1001;
    constexpr UINT_PTR DATA_TIMER_ID = 1;
    constexpr UINT_PTR TOOLTIP_TIMER_ID = 2;
    constexpr UINT DATA_TIMER_INTERVAL = INTERVAL * 1000;
    const vector<string> kRegionList = {
        "ae", "ag", "ai", "am", "ar", "at", "au", "az", "bb", "be",
        "bg", "bh", "bm", "bo", "br", "bs", "bw", "by", "bz", "ca",
        "cf", "ch", "ci", "cl", "cm", "cn", "co", "cr", "cz", "de",
        "dk", "dm", "do", "ec", "ee", "eg", "es", "fi", "fr", "gb",
        "gd", "ge", "gn", "gq", "gr", "gt", "gw", "gy", "hk", "hn",
        "hr", "hu", "id", "ie", "il", "in", "it", "jm", "jo", "jp",
        "kg", "kn", "kr", "kw", "ky", "kz", "la", "lc", "li", "lt",
        "lu", "lv", "ma", "md", "me", "mg", "mk", "ml", "mo", "ms",
        "mt", "mu", "mx", "my", "mz", "ne", "ng", "ni", "nl", "no",
        "nz", "om", "pa", "pe", "ph", "pl", "pr", "pt", "py", "qa",
        "ro", "ru", "sa", "se", "sg", "si", "sk", "sn", "sr", "sv",
        "tc", "th", "tj", "tm", "tn", "tr", "tt", "tw", "ua", "ug",
        "us", "uy", "uz", "vc", "ve", "vg", "vn", "za"
    };
    const unordered_set<string> kValidRegions{kRegionList.begin(), kRegionList.end()};
    constexpr char kRegionDbKey[] = "config:region";
    constexpr const char *kDefaultRegion = "ca";
} // Constants
namespace {
    bool isValidRegion(const std::string &region) {
        return kValidRegions.contains(region);
    }
    void printRegionList() {
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
    void saveRegion(const AppContext &ctx, const std::string &region) {
        if (!ctx.db) return;
        const leveldb::Status status = ctx.db->Put(leveldb::WriteOptions(), kRegionDbKey, region);
        if (!status.ok()) {
            if (ctx.logger) ctx.logger->warn("Failed to persist region {}: {}", region, status.ToString());
        } else {
            if (ctx.logger) ctx.logger->info("Set region to {}", region);
        }
    }
    string loadRegion(const AppContext &ctx) {
        if (!ctx.db) return "";
        string region;
        const leveldb::Status status = ctx.db->Get(leveldb::ReadOptions(), kRegionDbKey, &region);
        if (!status.ok()) {
            if (ctx.logger) ctx.logger->warn("Failed to read saved region: {}", status.ToString());
        }
        return region;
    }
    string getRegion() {
        wstring input;
        while (true) {
            wcout << L"Enter Apple Music region code (type 'list' to view options): ";
            wcin >> input;

            string regionInput = toLowerCase(convertWString(input));
            if (regionInput == "list") {
                printRegionList();
                continue;
            }

            if (isValidRegion(regionInput)) {
                return regionInput;
            }

            wcout << L"Region not recognized. Please choose a code from the list below." << endl;
            printRegionList();
        }
    }
    string ensureRegion(const AppContext &ctx) {
        string region = toLowerCase(loadRegion(ctx));
        if (region.empty()) {
            region = kDefaultRegion;
            saveRegion(ctx, region);
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

        const filesystem::path basePath(path);
        const filesystem::path dbPath = basePath / "musicpp" /"song_db";
        const filesystem::path logPath = basePath  / "musicpp" / "logs" / "musicpp.log";
        CoTaskMemFree(path);

        std::error_code ec1, ec2;

        filesystem::create_directories(dbPath, ec1);
        filesystem::create_directories(logPath.parent_path(), ec2);

        if (ec1) {
            wcout << L"Fatal: Could not create db folder: " << ec1.message().c_str() << endl;
            return false;
        }

        if (ec2) {
            wcout << L"Could not create log folder: " << ec2.message().c_str() << endl;
            wcout << L"The program will not have logs. Continuing..." << endl;
        } else {
            auto max_size = 1048576 * 5; // 5mb
            auto max_files = 3; // 3 files
            auto logger = spdlog::rotating_logger_mt("MusicPP Logger", logPath.string(), max_size, max_files);
    #ifdef LOG_LEVEL_DEBUG
            spdlog::set_level(spdlog::level::debug);
            logger->flush_on(spdlog::level::debug);
    #elif defined(LOG_LEVEL_INFO)
            spdlog::set_level(spdlog::level::info);
            spdlog::flush_every(std::chrono::seconds(30));
    #else
            spdlog::set_level(spdlog::level::debug);
            logger->flush_on(spdlog::level::debug);
    #endif
            spdlog::set_pattern("[%H:%M:%S %z] [%n] [%^---%L---%$] [thread %t] %v");
            ctx.logger = logger;
        }

        if (ctx.logger) {
            ctx.logger -> info("Logger initialized at path: {}", logPath.string());
            ctx.logger -> info("MusicPP starting up...");
        }

        leveldb::DB *tempDB = nullptr;

        leveldb::Options options;

        options.create_if_missing = true;

        const leveldb::Status status = leveldb::DB::Open(options, dbPath.string(), &tempDB);

        if (status.ok()) {
            ctx.db.reset(tempDB);
            if (ctx.logger) ctx.logger -> info("Database initialized at path: {}", dbPath.string());
            return true;
        }

        wcout << L"Fatal: Could not initialize database!" << endl;
        if (ctx.logger) ctx.logger -> error("Could not initialize database: {}", status.ToString());

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

                this_thread::sleep_for(chrono::milliseconds(100));
            }

            if (interaction) break;
        }


        if (interaction) {
            if (ctx.logger) ctx.logger->info("User interaction detected, entering Configuration Mode.");
            wcout << L"\n\n[Configuration Mode]" << endl;

            wcout << L"Enter 1 to force reset all API Keys." << endl;

            wcout << L"Enter 2 to change Apple Music region" << endl;

            wcout << L"Enter anything else to exit" << endl;


            wstring input;

            wcin >> input;

            if (input == L"1") {
                if (ctx.logger) ctx.logger -> info("User initiated API key reset.");
                wcout << L"Deleted all keys." << endl;

                return true; // forceReset = true
            }
            if (input == L"2") {
                wcout << L"Current Apple Music region: " << region.c_str() << endl;
                printRegionList();
                string newRegion = getRegion();

                if (newRegion != region) {
                    region = newRegion;
                    saveRegion(ctx, region);
                    wcout << L"Apple Music region updated to " << region.c_str() << L"." << endl;
                } else {
                    wcout << L"Region unchanged." << endl;
                }
            }
        } else {
            if (ctx.logger) ctx.logger -> info("Auto-Start mode, no user interaction.");
            wcout << L"\n\n[Auto-Start] No interaction detected. Loading keys..." << endl;
        }

        return false; // forceReset = false
    }
    void LoadCredentials(AppContext &ctx, bool forceReset, const string &region) {
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
        wcout << L"Apple Music region: " << region.c_str() << endl;
        ctx.scraper = std::make_unique<amscraper>(region, ctx.logger.get());
        if (ctx.logger) ctx.logger -> info("AMScraper initialized with region {}", region);

        ctx.spotify = std::make_unique<SpotifyAPI>(convertWString(s_cid), convertWString(s_sec), ctx.logger.get());
        if (ctx.logger) {
            ctx.logger -> info("SpotifyAPI initialized");
        }

        ctx.imgur = std::make_unique<ImgurAPI>(convertWString(i_cid), ctx.logger.get());
        if (ctx.logger) ctx.logger -> info("ImgurAPI initialized");

        ctx.player = std::make_unique<mediaPlayer>(ctx.scraper.get(), ctx.spotify.get(), ctx.imgur.get(), ctx.db.get(), ctx.logger.get());
        if (ctx.logger) ctx.logger -> info("mediaPlayer initialized");

        ctx.discord = std::make_unique<discordrp>(ctx.player.get(), 1358389458956976128, ctx.logger.get());
        if (ctx.logger) ctx.logger -> info("discordrp initialized");
    }
    void UpdateTrayTooltip(AppContext *ctx) {
        if (ctx->nid.cbSize == 0) return;

        const wstring imageSource = ctx->player->getImageSource();

        const bool playing = ctx->player->getState();

        wstringstream tipStream;

        if (ctx->player->getArtist().empty()) {
            tipStream << L"MusicPP\nNo track playing";
        } else {
            tipStream << L"MusicPP\nPresence Active\n\n";
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

        if (tip == ctx->lastTip) {
            return;
        }

        ctx->lastTip = tip;

        if (ctx -> logger) ctx->logger->debug("Updating tray tooltip to: {}", convertWString(tip.c_str()));

        lstrcpynW(ctx->nid.szTip, tip.c_str(), ARRAYSIZE(ctx->nid.szTip));

        ctx->nid.uFlags = NIF_TIP;

        Shell_NotifyIcon(NIM_MODIFY, &ctx->nid);
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

                if (ctx->logger) ctx -> logger -> debug("Timer started: Data ({}) ms",
                                                      DATA_TIMER_INTERVAL);
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

                ctx->player->getInfo();
                ctx->discord->update();
                UpdateTrayTooltip(ctx);
                if (ctx -> logger) ctx->logger->debug("Data timer update cycle completed.");
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
} // Helpers

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

    SetupConsole();

    AppContext ctx;

    if (!InitializeDatabase(ctx)) {
        std::wcout << L"Press Enter to continue..." << std::endl;
        std::wcin.get(); // Let user see errorMode();
        return 1;
    }

    std::string region = ensureRegion(ctx);
    bool forceReset = RunConfigurationMode(ctx, region);

    LoadCredentials(ctx, forceReset, region);
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

