/**
 * @file cache.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#include "metadata/cache.hpp"
#include <filesystem>
#include "shlobj_core.h"
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <algorithm>

namespace {
    /**
 * Resolves the default cache directory under LocalAppData.
 */
std::filesystem::path defaultDbPath() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        throw std::exception("Failed to get LocalAppData folder");
    }
    const std::filesystem::path base_path(path);
    CoTaskMemFree(path);
    return base_path / "musicppv2" / "song_db";
}

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

/**
 * Derives the image storage key for a track.
 * @param track Track's information.
 * @return Prefixed database key for the track's image.
 */
std::string imageKey(const Track &track) {
    return "img|" + getKey(track);
}

/**
 * Derives the song-URL list storage key for a track.
 * @param track Track's information.
 * @return Prefixed database key for the track's song URLs.
 */
std::string urlKey(const Track &track) {
    return "url|" + getKey(track);
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

/**
 * Serializes a track's image into the image value format:
 * [type: 1 byte][source_len: u32][source][url: remainder].
 */
std::string createImageValue(const ImageUrl &image) {
    std::string val;
    val.push_back(static_cast<char>(image.type));
    putU32(val, static_cast<uint32_t>(image.source.size()));
    val += image.source;
    val += image.url;
    return val;
}

/**
 * Parses an image value back into an ImageUrl.
 * @return The image, or nullopt if the buffer is empty or malformed.
 */
std::optional<ImageUrl> parseImageValue(const std::string &raw) {
    if (raw.empty())
        return std::nullopt;
    size_t offset = 0;
    ImageUrl image;
    image.type = static_cast<ImageType>(static_cast<uint8_t>(raw[offset++]));
    uint32_t sourceLen = 0;
    if (!readU32(raw, offset, sourceLen))
        return std::nullopt;
    if (!readBytes(raw, offset, sourceLen, image.source))
        return std::nullopt;
    image.url = raw.substr(offset);
    return image;
}

/**
 * Serializes a song-URL list into the url value format:
 * [count: u32]{ [source_len: u32][source][url_len: u32][url] }*.
 */
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

/**
 * Parses a url value back into a list of SongUrls. Stops cleanly at the first
 * malformed entry rather than throwing.
 */
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

/**
 * Unions incoming song URLs into the existing list, deduping by (url, source)
 * and skipping entries with an empty url.
 */
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

MetadataCache::MetadataCache() {
    open(defaultDbPath());
}

MetadataCache::MetadataCache(const std::filesystem::path &dbPath) {
    open(dbPath);
}

void MetadataCache::open(const std::filesystem::path &dbPath) {
    std::error_code ec;
    create_directories(dbPath, ec);
    if (ec) {
        const std::string error = "Couldn't create db folder: " + ec.message();
        throw std::exception(error.c_str());
    }

    leveldb::DB *temp = nullptr;
    leveldb::Options options;
    options.create_if_missing = true;

    if (const leveldb::Status status = leveldb::DB::Open(options, dbPath.string(), &temp); status.
        ok()) {
        _db.reset(temp);
    } else {
        const std::string error = "Couldn't initialize db: " + status.ToString();
        throw std::exception(error.c_str());
    }
}

MetadataCache::~MetadataCache() = default;

void MetadataCache::writeEntry(const EnrichedTrack &track) const {
    leveldb::WriteBatch batch;
    bool hasWrite = false;

    if (!track.image.url.empty()) {
        batch.Put(imageKey(track.track), createImageValue(track.image));
        hasWrite = true;
    }

    if (!track.songUrls.empty()) {
        std::vector<SongUrl> existing;
        if (std::string rawExisting; _db->Get(leveldb::ReadOptions(), urlKey(track.track),
                                              &rawExisting).ok()) {
            existing = parseUrlValue(rawExisting);
        }
        if (const auto merged = mergeSongUrls(std::move(existing), track.songUrls); !merged.
            empty()) {
            batch.Put(urlKey(track.track), createUrlValue(merged));
            hasWrite = true;
        }
    }

    if (hasWrite) {
        _db->Write(leveldb::WriteOptions(), &batch);
    }
}

std::optional<EnrichedTrack> MetadataCache::findEntry(const Track &track) const {
    std::string rawImage;
    std::string rawUrls;
    const bool hasImage = _db->Get(leveldb::ReadOptions(), imageKey(track), &rawImage).ok();
    const bool hasUrls = _db->Get(leveldb::ReadOptions(), urlKey(track), &rawUrls).ok();

    if (!hasImage && !hasUrls)
        return std::nullopt;

    EnrichedTrack out;
    out.track = track;
    if (hasImage) {
        if (const auto image = parseImageValue(rawImage)) {
            out.image = *image;
        }
    }
    if (hasUrls) {
        out.songUrls = parseUrlValue(rawUrls);
    }
    return out;
}
