/**
 * @file imgur.hpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#pragma once

#include "metadata/uploaders/uploader.hpp"
#include "types/track.hpp"

class Imgur : public Uploader {
public:
    explicit Imgur(const std::string &apikey);

    std::string identify() override;

    UploadResult uploadImage(const std::vector<unsigned char> &bytes, ImageType type) override;

private:
    std::string _apikey;
    const std::string kIDENTITY = "Imgur Image Host";
};