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

wstring FriendlyTime(const uint64_t &seconds) {
    const uint64_t weeks = seconds / 604800;
    const uint64_t days = (seconds % 604800) / 86400;
    const uint64_t hours = (seconds % 86400) / 3600;
    const uint64_t minutes = (seconds % 3600) / 60;

    std::wstringstream ss;
    if (weeks > 0) ss << weeks << L"w ";
    if (days > 0) ss << days << L"d ";
    if (hours > 0) ss << hours << L"h ";
    if (minutes > 0 && weeks == 0) ss << minutes << L"m";

    std::wstring result = ss.str();
    if (result.empty()) return L"0s";
    if (result.back() == L' ') result.pop_back();
    return result;
}

uint64_t UnixSecondsNow() {
    return static_cast<uint64_t>(
        chrono::duration_cast<chrono::seconds>(
            chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}
