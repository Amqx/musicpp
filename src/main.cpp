//
// Created by Jonathan on 25-Sep-25.
//

#include <include/mediaPlayer.h>
#include <include/imgur.h>
#include <include/spotify.h>
#include <include/discordrp.h>
#include <chrono>
#include <thread>
#include <include/credhelper.h>
#include <iostream>
#include <windows.h>
#include <conio.h>
#include <fcntl.h>
#include "leveldb/db.h"
#include <shlobj_core.h>
#include <io.h>
#include <filesystem>

#define INTERVAL 5

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONIN$", "r", stdin);
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    SetConsoleOutputCP(CP_UTF8); // or CP_UTF8 for UTF-8 output
    _setmode(_fileno(stdout), _O_U8TEXT); // for UTF-8 wide output
    _setmode(_fileno(stdin), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);

    wcout << L"MusicPP V1.0" << endl;

    // init database
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path);
    leveldb::DB* database;

    if (SUCCEEDED(hr)) {
        wcout << L"Configuration files are stored at: " << path << L'\n' << endl;
        string DBPath = wstr_to_str(path) + "/musicpp/song_db";
        CoTaskMemFree(path);

        filesystem::path newFolder(DBPath);
        error_code ec;
        filesystem::create_directories(newFolder, ec);

        if (!ec) {
            leveldb::DB *newDB;
            leveldb::Options options;
            options.create_if_missing = true;
            leveldb::Status status = leveldb::DB::Open(options, DBPath, &newDB);
            if (status.ok()) {
                database = newDB;
            } else {
                wcout << L"Fatal: Could not initialize database!" << endl;
                return 1;
            }
        } else {
            wcout << L"Fatal: Could not create/ access local appdata folder" << endl;
            return 1;
        }
    } else {
        wcout << L"Fatal: could not get local appdata path" << endl;
        return 1;
    }

    bool forceReset = false;
    wcout << L"Enter 1 to force reset all API Keys" << endl;
    wcout << L"Enter anything else (0,a, etc.) to skip" << endl;
    wstring input;
    wcin >> input;
    if (input == L"1") {
        forceReset = true;
        wcout << L"Deleted all keys" << endl;
    }

    wcout << L"\nChecking API Keys..." << endl;
    // initialize spotify
    std::wstring spotify_client_id = ReadGenericCredential(L"musicpp/spotify_client_id");
    if (spotify_client_id.empty() || forceReset) {
        wcout << L"\nSpotify Client ID not set! Please get one here: https://developer.spotify.com/documentation/web-api/tutorials/getting-started" << endl;
        wstring newSpotifyKey;
        wcout << L"Enter your Spotify Client ID" << endl;
        wcin >> newSpotifyKey;
        WriteGenericCredential(L"musicpp/spotify_client_id", newSpotifyKey);
        spotify_client_id = ReadGenericCredential(L"musicpp/spotify_client_id");
    }

    std::wstring spotify_client_secret = ReadGenericCredential(L"musicpp/spotify_client_secret");
    if (spotify_client_secret.empty() || forceReset) {
        wcout << L"\nSpotify Client Secret not set! Please get one here: https://developer.spotify.com/documentation/web-api/tutorials/getting-started" << endl;
        wstring newSpotifySecret;
        wcout << L"Enter your Spotify Secret" << endl;
        wcin >> newSpotifySecret;
        WriteGenericCredential(L"musicpp/spotify_client_secret", newSpotifySecret);
        spotify_client_secret = ReadGenericCredential(L"musicpp/spotify_client_secret");
    }
    string scid = wstr_to_str(spotify_client_id.c_str());
    string scs = wstr_to_str(spotify_client_secret.c_str());

    // initialize imgur
    std::wstring imgur_client_id = ReadGenericCredential(L"musicpp/imgur_client_id");
    if (imgur_client_id.empty() || forceReset) {
        wcout << L"\nImgur Client ID not set! Please get one here: https://api.imgur.com/oauth2/addclient" << endl;
        wstring newImgurKey;
        wcin >> newImgurKey;
        WriteGenericCredential(L"musicpp/imgur_client_id", newImgurKey);
        imgur_client_id = ReadGenericCredential(L"musicpp/imgur_client_id");
    }
    string icid = wstr_to_str(imgur_client_id.c_str());

    wcout << "\nAll APIKeys found!" << endl;
    SpotifyAPI spotify(scid, scs);
    ImgurAPI imgur(icid);
    mediaPlayer appleMusic(&spotify, &imgur, database);
    discordrp discord(&appleMusic, 1358389458956976128);

    wcout << "\nThis console window will automatically close in 5 seconds." << endl;
    this_thread::sleep_for(chrono::seconds(5));
    HWND hConsole = GetConsoleWindow();
    FreeConsole();
    if (hConsole != nullptr) {
        PostMessageW(hConsole, WM_CLOSE, 0, 0);
    }

    while (true) {
        appleMusic.getInfo();
        discord.update();
        this_thread::sleep_for(chrono::seconds(INTERVAL));
    }
}
