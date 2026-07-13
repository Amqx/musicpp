/**
 * @file cache_codec.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <vector>
#include "types/track.hpp"

/**
 * The on-disk format of the metadata cache.
 */
namespace cache_codec {
/**
 * How long a cached image URL is trusted before it must be re-fetched.
 * Song URLs will never expire.
 */
inline constexpr std::chrono::days kImageTtl{14};

/**
 * A cached image together with the wall-clock instant it was written.
 */
struct CachedImage {
    ImageUrl image;
    std::chrono::sys_seconds written_at;
};

/**
 * The current wall-clock time, truncated to seconds.
 */
[[nodiscard]] std::chrono::sys_seconds nowSeconds();

/**
 * Whether a cached image is still within kImageTtl. A timestamp in the future counts as
 * fresh, so a backwards jump of the system clock cannot invalidate the whole cache.
 */
[[nodiscard]] bool isFresh(std::chrono::sys_seconds written_at, std::chrono::sys_seconds now);

/**
 * Derives the image storage key for a track.
 * @param track Track's information.
 * @return Prefixed database key for the track's image.
 */
[[nodiscard]] std::string imageKey(const Track &track);

/**
 * Derives the song-URL list storage key for a track.
 * @param track Track's information.
 * @return Prefixed database key for the track's song URLs.
 */
[[nodiscard]] std::string urlKey(const Track &track);

/**
 * Serializes a track's image into the image value format:
 * [written_at: i64][type: 1 byte][source_len: u32][source][url: remainder].
 */
[[nodiscard]] std::string createImageValue(const ImageUrl &image,
                                           std::chrono::sys_seconds written_at);

/**
 * Parses an image value back into an image and the instant it was written.
 * @return The cached image, or nullopt if the buffer is empty or malformed.
 */
[[nodiscard]] std::optional<CachedImage> parseImageValue(const std::string &raw);

/**
 * Serializes a song-URL list into the url value format:
 * [count: u32]{ [source_len: u32][source][url_len: u32][url] }*.
 */
[[nodiscard]] std::string createUrlValue(const std::vector<SongUrl> &urls);

/**
 * Parses a url value back into a list of SongUrls. Stops cleanly at the first malformed
 * entry rather than throwing.
 */
[[nodiscard]] std::vector<SongUrl> parseUrlValue(const std::string &raw);

/**
 * Unions incoming song URLs into the existing list, deduping by (url, source) and skipping
 * entries with an empty url.
 */
[[nodiscard]] std::vector<SongUrl> mergeSongUrls(std::vector<SongUrl> existing,
                                                 const std::vector<SongUrl> &incoming);
}
