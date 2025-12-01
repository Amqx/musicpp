//
// Created by Jonathan on 17-Nov-25.
//

#include <chrono>
#include "timeutils.h"

using namespace std;

wstring FormatTimestamp(const uint64_t &seconds) {
    wstringstream stream;

    stream << seconds / 60 << L":" << std::setw(2) << std::setfill(L'0') << (seconds % 60);

    return stream.str();
}

uint64_t UnixSecondsNow() {
    return static_cast<uint64_t>(
        chrono::duration_cast<chrono::seconds>(
            chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}