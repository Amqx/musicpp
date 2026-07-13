/**
 * @file cache_codec.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#include "metadata/cache_codec.hpp"
#include <algorithm>

namespace {
/**
 * Creates a database key for a given base track.
 * @param track Track's information.
 * @return Database key for the track.
 */
std::string getKey(const Track &track) {
    auto normalize = [](std::string str) {
        std::ranges::replace(str, '|', '-');
        return str;
    };
    const auto title = normalize(track.identity.title);
    const auto artist = normalize(track.identity.artist);
    const auto album = normalize(track.identity.album);
    return title + "|" + artist + "|" + album;
}

void putU32(std::string &buf, const uint32_t value) {
    buf.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

bool readU32(const std::string &buf, size_t &offset, uint32_t &out) {
    if (offset + sizeof(uint32_t) > buf.size())
        return false;
    std::memcpy(&out, buf.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    return true;
}

bool readBytes(const std::string &buf, size_t &offset, const uint32_t len, std::string &out) {
    if (offset + len > buf.size())
        return false;
    out.assign(buf.data() + offset, len);
    offset += len;
    return true;
}

void putI64(std::string &buf, const int64_t value) {
    buf.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

bool readI64(const std::string &buf, size_t &offset, int64_t &out) {
    if (offset + sizeof(int64_t) > buf.size())
        return false;
    std::memcpy(&out, buf.data() + offset, sizeof(int64_t));
    offset += sizeof(int64_t);
    return true;
}

}

namespace cache_codec {
std::chrono::sys_seconds nowSeconds() {
    return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
}

bool isFresh(const std::chrono::sys_seconds written_at, const std::chrono::sys_seconds now) {
    return now - written_at < kImageTtl;
}

std::string imageKey(const Track &track) {
    return "img|" + getKey(track);
}

std::string urlKey(const Track &track) {
    return "url|" + getKey(track);
}

std::string createImageValue(const ImageUrl &image, const std::chrono::sys_seconds written_at) {
    std::string val;
    putI64(val, written_at.time_since_epoch().count());
    val.push_back(static_cast<char>(image.type));
    putU32(val, static_cast<uint32_t>(image.source.size()));
    val += image.source;
    val += image.url;
    return val;
}

std::optional<CachedImage> parseImageValue(const std::string &raw) {
    size_t offset = 0;
    CachedImage cached;

    int64_t written = 0;
    if (!readI64(raw, offset, written))
        return std::nullopt;
    cached.written_at = std::chrono::sys_seconds{std::chrono::seconds{written}};

    if (offset >= raw.size())
        return std::nullopt;
    cached.image.type = static_cast<ImageType>(static_cast<uint8_t>(raw[offset++]));

    uint32_t sourceLen = 0;
    if (!readU32(raw, offset, sourceLen))
        return std::nullopt;
    if (!readBytes(raw, offset, sourceLen, cached.image.source))
        return std::nullopt;

    cached.image.url = raw.substr(offset);
    return cached;
}

std::string createUrlValue(const std::vector<SongUrl> &urls) {
    std::string val;
    putU32(val, static_cast<uint32_t>(urls.size()));
    for (const auto &[url, source] : urls) {
        putU32(val, static_cast<uint32_t>(source.size()));
        val += source;
        putU32(val, static_cast<uint32_t>(url.size()));
        val += url;
    }
    return val;
}

std::vector<SongUrl> parseUrlValue(const std::string &raw) {
    std::vector<SongUrl> urls;
    if (raw.empty())
        return urls;
    size_t offset = 0;
    uint32_t count = 0;
    if (!readU32(raw, offset, count))
        return urls;
    urls.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        SongUrl songUrl;
        if (uint32_t sourceLen = 0; !readU32(raw, offset, sourceLen) || !readBytes(
                                        raw, offset, sourceLen, songUrl.source))
            break;
        if (uint32_t urlLen = 0; !readU32(raw, offset, urlLen) || !readBytes(
                                     raw, offset, urlLen, songUrl.url))
            break;
        urls.push_back(std::move(songUrl));
    }
    return urls;
}

std::vector<SongUrl> mergeSongUrls(std::vector<SongUrl> existing,
                                   const std::vector<SongUrl> &incoming) {
    auto alreadyPresent = [&existing](const SongUrl &candidate) {
        return std::ranges::any_of(existing, [&candidate](const SongUrl &e) {
            return e.url == candidate.url && e.source == candidate.source;
        });
    };
    for (const auto &songUrl : incoming) {
        if (!songUrl.url.empty() && !alreadyPresent(songUrl)) {
            existing.push_back(songUrl);
        }
    }
    return existing;
}

}
