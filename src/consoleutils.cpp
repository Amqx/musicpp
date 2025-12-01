//
// Created by Jonathan on 17-Nov-25.
//

#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <io.h>
#include <fcntl.h>
#include <iostream>
#include "consoleutils.h"
#include "constants.h"

using namespace std;
using namespace std::filesystem;

void SetupConsole() {
    AllocConsole();

    FILE *fp;

    freopen_s(&fp, "CONIN$", "r", stdin);

    freopen_s(&fp, "CONOUT$", "w", stdout);

    freopen_s(&fp, "CONOUT$", "w", stderr);

    SetConsoleOutputCP(CP_UTF8);

    _setmode(_fileno(stdout), _O_U8TEXT);

    _setmode(_fileno(stdin), _O_U8TEXT);

    _setmode(_fileno(stderr), _O_U8TEXT);

    wcout << L"MusicPP V" << kVersion << endl;
}


void CleanupConsole() {
    if (const HWND consoleWnd = GetConsoleWindow(); consoleWnd != nullptr) {
        ShowWindow(consoleWnd, SW_HIDE);
    }

    FreeConsole();
}
