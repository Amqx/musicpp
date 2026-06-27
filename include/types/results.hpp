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
};

class UploadResult {
public:
    std::string image_url;
};
