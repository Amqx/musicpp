/**
 * @file results.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 17-Jul-26
 */

#include "types/results.hpp"
#include <ostream>

std::ostream &operator<<(std::ostream &os, const SearchResult &result) {
    os << "SearchResult { image_url: " << result.image_url << ", web_url: " << result.web_url
        << ", image_type: " << result.image_type << " }";
    return os;
}

std::ostream &operator<<(std::ostream &os, const UploadResult &result) {
    os << "UploadResult { image_url: " << result.image_url << " }";
    return os;
}
