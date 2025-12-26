//
// Created by Jonathan on 23-Dec-25.
//

#ifndef M3U8_M3U8_H
#define M3U8_M3U8_H

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <random>

namespace spdlog {
    class logger;
}


enum M3U8Status {
    Finished,
    InProgress,
    Failed
};

class M3U8Processor {
public:
    struct M3U8Result {
        bool ok = false;

        std::vector<int> result;
    };

    M3U8Processor(spdlog::logger *logger);

    ~M3U8Processor();

    M3U8Processor(const M3U8Processor &) = delete;

    M3U8Processor &operator=(M3U8Processor &) = delete;

    void Submit(std::string url);

    // start/ stop setup functions
    void start();

    void exit();

    [[nodiscard]] M3U8Status Status() const;

    [[nodiscard]] std::vector<uint8_t> Get();

private:
    spdlog::logger *logger_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::condition_variable cv_;
    std::mutex mu_;
    std::atomic<bool> stop_requested_ = false;
    std::atomic<bool> has_job_ = false;
    std::string current_url_;
    std::optional<std::vector<uint8_t> > result_;
    std::atomic<bool> failed_{false};

    struct M3U8Variant {
        std::string url;
        uint64_t bandwidth;
        int resolution;
    };

    // looper
    void loop();

    // generations
    std::atomic<std::uint64_t> next_{0};
    std::atomic<std::uint64_t> current_{0}; // updated on submit
    std::atomic<std::uint64_t> finished_{0}; // set when result is ready
    bool IsCancelled(uint64_t gen) const;

    // Function to get HLS playlist
    std::string GetHls(const std::string &url) const;

    // Function to get variants
    std::vector<M3U8Variant> GetVariants(const std::string &url) const;

    // Function to pick variant
    static M3U8Variant PickVariant(const std::vector<M3U8Variant> &Variants);

    // Function to download variant
    std::vector<uint8_t> Download(const M3U8Variant &v, uint64_t loop_gen) const;

    static void ffmpeg_log_callback(void *ptr, int level, const char *fmt, va_list vl);
};


#endif //M3U8_M3U8_H
