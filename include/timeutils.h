//
// Created by Jonathan on 17-Nov-25.
//

#ifndef MUSICPP_TIMEUTILS_H
#define MUSICPP_TIMEUTILS_H

#include <string>



constexpr uint64_t INVALID_TIME = std::numeric_limits<uint64_t>::max();

std::wstring FormatTimestamp(uint64_t seconds);

uint64_t unix_seconds_now();

#endif //MUSICPP_TIMEUTILS_H