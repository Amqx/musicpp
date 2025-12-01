import os
import re
from collections import defaultdict

# Directory containing the log files
LOG_DIR = os.path.join(os.getenv("LOCALAPPDATA"), "musicpp", "logs")

FLAG_RE = re.compile(r"Flags:\s*\[([^\]]*)\]")

SOURCES = {
    "am": ("am_used", "am_avail"),
    "lfm": ("lfm_used", "lfm_avail"),
    "sp": ("spotify_used", "sp_avail"),
    "imgur": ("imgur_used", None),
}

CACHE_FLAGS = {
    "hit": "db_hit_image",
    "expired": "img_expired",
    "written": "cache_written",
}


def parse_flags(flag_string):
    if flag_string.strip() == "NONE":
        return set()
    return set(flag_string.split())


def process_log_file(path, stats, cache_stats, line_count_ref):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = FLAG_RE.search(line)
            if not m:
                continue

            line_count_ref["count"] += 1

            flags = parse_flags(m.group(1))

            cache_stats["total_entries"] += 1

            # Cache-related counters
            if CACHE_FLAGS["hit"] in flags:
                cache_stats["hits"] += 1
            if CACHE_FLAGS["expired"] in flags:
                cache_stats["expired"] += 1
            if CACHE_FLAGS["written"] in flags:
                cache_stats["written"] += 1

            # Source reliability counters
            for src, (used_flag, avail_flag) in SOURCES.items():
                if used_flag in flags:
                    stats[src]["used"] += 1
                    if avail_flag and avail_flag in flags:
                        stats[src]["available"] += 1
                    else:
                        stats[src]["unavailable"] += 1


def compute_reliability(stats):
    result = {}
    for src, values in stats.items():
        used = values["used"]
        available = values["available"]
        result[src] = (available / used) if used > 0 else None
    return result


def compute_cache_stats(cache_stats):
    total = cache_stats["total_entries"]
    hits = cache_stats["hits"]
    written = cache_stats["written"]
    expired = cache_stats["expired"]

    return {
        "hit_rate": (hits / total) if total > 0 else None,
        "write_rate": (written / total) if total > 0 else None,
        "expiration_rate": (expired / hits) if hits > 0 else None,
    }


def main():
    if not os.path.isdir(LOG_DIR):
        print(f"Log directory does not exist: {LOG_DIR}")
        return

    stats = {src: {"used": 0, "available": 0, "unavailable": 0} for src in SOURCES}

    cache_stats = {
        "total_entries": 0,
        "hits": 0,
        "expired": 0,
        "written": 0,
    }

    line_count = {"count": 0}

    for filename in os.listdir(LOG_DIR):
        if filename.lower().endswith(".log"):
            process_log_file(os.path.join(LOG_DIR, filename), stats, cache_stats, line_count)

    reliability = compute_reliability(stats)
    cache_result = compute_cache_stats(cache_stats)

    print("Summary")
    print("Directory: ", LOG_DIR)
    print(f"Total lines considered: {line_count['count']}")
    print("-----------------------" + '-' * (line_count['count'] % 10))

    for src in SOURCES:
        used = stats[src]["used"]
        avail = stats[src]["available"]
        unavail = stats[src]["unavailable"]
        r = reliability[src]
        if r is None:
            print(f"{src}: no usage found")
        else:
            if src == "imgur":
                print(f"{src}: used={used}")
            else:
                print(
                    f"{src}: reliability = {r:.5f} "
                    f"(used={used}, available={avail}, unavailable={unavail})"
                )

    print("\nCache Statistics")
    print("----------------")
    if cache_stats["total_entries"] == 0:
        print("No log entries found")
    else:
        print(f"cache hit rate      = {cache_result['hit_rate']:.5f}")
        print(f"cache write rate    = {cache_result['write_rate']:.5f}")
        if cache_result["expiration_rate"] is None:
            print("cache expiration rate = no cache hits found")
        else:
            print(f"cache expiration rate = {cache_result['expiration_rate']:.5f}")


if __name__ == "__main__":
    main()
