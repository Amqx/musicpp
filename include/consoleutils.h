//
// Created by Jonathan on 17-Nov-25.
//

#ifndef MUSICPP_CONSOLEUTILS_H
#define MUSICPP_CONSOLEUTILS_H

namespace console {
    // Standard Colors
    const std::wstring Red = L"\x1b[31m";
    const std::wstring Green = L"\x1b[32m";
    const std::wstring Yellow = L"\x1b[33m";
    const std::wstring Blue = L"\x1b[34m";
    const std::wstring Reset = L"\x1b[0m";

    // Link Helpers
    const std::wstring LinkStart = L"\x1b]8;;";
    const std::wstring LinkST = L"\x1b\\";
    const std::wstring LinkEnd = L"\x1b]8;;\x1b\\";
}


void SetupConsole();

void CleanupConsole();

#endif //MUSICPP_CONSOLEUTILS_H