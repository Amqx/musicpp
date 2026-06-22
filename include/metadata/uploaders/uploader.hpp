/**
 * @file uploader.hpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#pragma once

#include <vector>

#include "types/results.hpp"
#include "types/track.hpp"

class Uploader {
public:
    virtual ~Uploader() = default;
    virtual UploadResult uploadImage(const std::vector<unsigned char>& bytes, ImageType type) = 0;
    virtual std::string identify() = 0;
};
