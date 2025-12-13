import os
import re
import time
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

REFRESH_INTERVAL = 5  # seconds between updates


def parse_flags(flag_string):
    if flag_string.strip() == "NONE":
        return set()
    return set(flag_string.split())


def get_newest_log_file():
    """Find the most recently modified log file."""
    log_files = [
        os.path.join(LOG_DIR, f)
        for f in os.listdir(LOG_DIR)
        if f.lower().endswith(".log")
    ]
    if not log_files:
        return None
    return max(log_files, key=os.path.getmtime)


def process_log_file(path, stats, cache_stats, line_count_ref, start_pos=0):
    """Process log file from a given position, return new position."""
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        f.seek(start_pos)
        new_lines = 0
        
        for line in f:
            m = FLAG_RE.search(line)
            if not m:
                continue

            line_count_ref["count"] += 1
            new_lines += 1

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

        return f.tell(), new_lines


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


def print_stats(stats, cache_stats, line_count, current_file, new_lines=None):
    """Print current statistics."""
    os.system('cls' if os.name == 'nt' else 'clear')
    
    reliability = compute_reliability(stats)
    cache_result = compute_cache_stats(cache_stats)

    print("=== CONTINUOUS LOG MONITOR ===")
    print(f"Monitoring: {current_file}")
    print(f"Last update: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    if new_lines is not None:
        print(f"New entries: {new_lines}")
    print(f"Total lines processed: {line_count['count']}")
    print("=" * 50)
    print()

    print("Source Reliability")
    print("-" * 50)
    for src in SOURCES:
        used = stats[src]["used"]
        avail = stats[src]["available"]
        unavail = stats[src]["unavailable"]
        r = reliability[src]
        if r is None:
            print(f"{src:6s}: no usage found")
        else:
            if src == "imgur":
                print(f"{src:6s}: used={used}")
            else:
                print(
                    f"{src:6s}: reliability = {r:.5f} "
                    f"(used={used}, available={avail}, unavailable={unavail})"
                )

    print()
    print("Cache Statistics")
    print("-" * 50)
    if cache_stats["total_entries"] == 0:
        print("No log entries found")
    else:
        print(f"Hit rate        : {cache_result['hit_rate']:.5f}")
        print(f"Write rate      : {cache_result['write_rate']:.5f}")
        if cache_result["expiration_rate"] is None:
            print("Expiration rate : no cache hits found")
        else:
            print(f"Expiration rate : {cache_result['expiration_rate']:.5f}")

    print()
    print(f"(Refreshing every {REFRESH_INTERVAL} seconds, press Ctrl+C to stop)")


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

    current_file = get_newest_log_file()
    if not current_file:
        print("No log files found in directory")
        return

    # Initial full scan of all log files
    print("Performing initial scan of all log files...")
    for filename in os.listdir(LOG_DIR):
        if filename.lower().endswith(".log"):
            filepath = os.path.join(LOG_DIR, filename)
            process_log_file(filepath, stats, cache_stats, line_count)

    # Get position in newest file after initial scan
    file_pos = os.path.getsize(current_file)

    print_stats(stats, cache_stats, line_count, current_file)

    # Continuous monitoring loop
    try:
        while True:
            time.sleep(REFRESH_INTERVAL)

            # Check if log file has changed
            newest_file = get_newest_log_file()
            if newest_file != current_file:
                # New log file detected, process it from the beginning
                current_file = newest_file
                file_pos = 0

            # Process new entries
            new_pos, new_lines = process_log_file(
                current_file, stats, cache_stats, line_count, file_pos
            )
            file_pos = new_pos

            # Update display
            print_stats(stats, cache_stats, line_count, current_file, new_lines)

    except KeyboardInterrupt:
        print("\n\nMonitoring stopped by user")
        print("\nFinal Statistics:")
        print_stats(stats, cache_stats, line_count, current_file)


if __name__ == "__main__":
    main()