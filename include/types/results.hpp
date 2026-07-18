/**
 * @file results.hpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#pragma once
#include <string>

#include "types/track.hpp"

class SearchResult {
public:
    std::string image_url;
    std::string web_url;
    ImageType image_type = Static;

    friend auto operator<=>(const SearchResult &, const SearchResult &) = default;
};

std::ostream &operator<<(std::ostream &os, const SearchResult &result);

class UploadResult {
public:
    std::string image_url;

    friend auto operator<=>(const UploadResult &, const UploadResult &) = default;
};

std::ostream &operator<<(std::ostream &os, const UploadResult &result);
