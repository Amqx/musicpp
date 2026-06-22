/**
 * @file results.hpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#pragma once
#include <string>

class SearchResult {
public:
    std::string image_url;
    std::string web_url;
};

class UploadResult {
public:
    std::string image_url;
};
