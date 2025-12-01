//
// Created by Jonathan on 17-Nov-25.
//

#ifndef MUSICPP_TIMEUTILS_H
#define MUSICPP_TIMEUTILS_H

#include <string>

std::wstring FormatTimestamp(const uint64_t &seconds);

uint64_t UnixSecondsNow();

#endif //MUSICPP_TIMEUTILS_H