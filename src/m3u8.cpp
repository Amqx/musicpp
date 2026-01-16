//
// Created by Jonathan on 23-Dec-25.
//

#include "m3u8.h"
#include "utils.h"
#include "stringutils.h"
#include "m3u8_internals.h"
#include <libavutil/log.h>
#include <libxml/HTMLparser.h>
#include <spdlog/spdlog.h>
#include <curl/curl.h>
#include <sstream>

using namespace std;

M3U8Processor::M3U8Processor(spdlog::logger *logger) {
    logger_ = logger;
    av_log_set_level(AV_LOG_INFO);
    av_log_set_callback(ffmpeg_log_callback);
}

M3U8Processor::~M3U8Processor() {
    if (this->logger_) {
        logger_->info("M3U8Processor Killed");
    }
    exit();
    av_log_set_callback(av_log_default_callback);
}

void M3U8Processor::Submit(string url) {
    const uint64_t gen = next_.fetch_add(1, memory_order_relaxed) + 1;
    lock_guard lk(mu_);
    current_url_ = std::move(url);
    current_ = gen;
    finished_ = 0;
    failed_ = false;
    result_.reset();
    has_job_ = true;
    cv_.notify_one();
    if (logger_) logger_->debug("M3U8Processor recieved new job");
}

M3U8Status M3U8Processor::Status() const {
    const auto fg = finished_.load(memory_order_acquire);
    const auto cg = current_.load(memory_order_acquire);
    if (failed_) return Failed;
    if (fg != 0 && fg == cg) return Finished;
    return InProgress;
}

vector<uint8_t> M3U8Processor::Get() {
    lock_guard lk(mu_);
    if (!result_) return {};
    vector<uint8_t> out = std::move(*result_);
    result_.reset();
    return out;
}

void M3U8Processor::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return; {
        lock_guard lk(mu_);
        stop_requested_ = false;
        has_job_ = false;
    }
    worker_ = thread([this] { loop(); });
    if (logger_) logger_->debug("M3U8Processor thread started");
}

void M3U8Processor::exit() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) return; {
        lock_guard lk(mu_);
        stop_requested_ = true;
        cv_.notify_all();
    }
    if (logger_) logger_->debug("M3U8Processor thread stopped");
    if (worker_.joinable()) worker_.join();
}

void M3U8Processor::loop() {
    for (;;) {
        string url;
        uint64_t loop_gen = 0; {
            unique_lock lk(mu_);
            cv_.wait(lk, [&] {
                return stop_requested_ || has_job_;
            });

            if (stop_requested_) break;

            url = current_url_;
            loop_gen = current_.load(memory_order_relaxed);
            has_job_ = false;
            failed_ = false;
        }

        const auto hls = GetHls(url);
        if (IsCancelled(loop_gen)) continue;
        if (hls.empty()) {
            lock_guard lk(mu_);
            result_ = {};
            failed_ = true;
            finished_.store(loop_gen, memory_order_release);
            has_job_ = false;
            if (logger_) logger_->info("M3U8Processor couldn't find an animated image");
            continue;
        }

        const auto v = GetVariants(hls);
        if (v.empty()) {
            lock_guard lk(mu_);
            result_ = {};
            failed_ = true;
            finished_.store(loop_gen, memory_order_release);
            has_job_ = false;
            if (logger_) logger_->info("M3U8Processor could not get any valid variants");
            continue;
        }
        if (logger_) logger_->debug("M3U8Processor found {} possible variants", v.size());
        if (IsCancelled(loop_gen)) continue;

        const auto picked = PickVariant(v);
        if (logger_)
            logger_->debug("Picked variant: resolution {}, bandwidth {}, url {}", picked.resolution,
                           picked.bandwidth, picked.url);
        if (IsCancelled(loop_gen)) continue;

        vector<uint8_t> computed = Download(picked, loop_gen);
        if (IsCancelled(loop_gen)) continue; {
            lock_guard lk(mu_);
            result_ = std::move(computed);
        }
        finished_.store(loop_gen, memory_order_release);
        if (logger_) logger_->info("Successfully downloaded gif");
    }
}

bool M3U8Processor::IsCancelled(const uint64_t gen) const {
    return current_.load(memory_order_acquire) != gen || !running_.load(memory_order_acquire);
}

string M3U8Processor::GetHls(const std::string &url) const {
    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger_) logger_->warn("Failed to initialize CURL for M3U8 GetHLS");
        return {};
    }

    string read_buffer;
    curl_slist *headers = nullptr;
    headers = curl_slist_append(
        headers,
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kCurlTimeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kCurlConnectTimeout);

    const CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger_) logger_->warn("Failed to perform M3U8 GetHLS: {}", curl_easy_strerror(res));
        return {};
    }

    if (read_buffer.empty()) return {};

    htmlDocPtr doc = htmlReadMemory(read_buffer.c_str(), static_cast<int>(read_buffer.size()), nullptr, nullptr,
                                    HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);

    if (!doc) {
        if (logger_) logger_->warn("Failed to parse Apple Music HTML for M3U8");
        return "";
    }

    std::string r;

    if (xmlNodePtr root = xmlDocGetRootElement(doc)) {
        if (xmlNodePtr search_root = FindDivWithClass(root, "content-container")) {
            if (xmlNodePtr video_node = FindDivWithClass(search_root, "video-artwork__container")) {
                xmlNodePtr image_node = video_node->children;
                std::string m3u8 = GetAttribute(image_node, "src");
                r = m3u8;
            } else {
                if (logger_) logger_->debug("Could not find animated image");
            }
        } else {
            if (logger_) logger_->debug("Could not find 'desktop-search-page' div in Apple Music HTML response");
        }
    }

    xmlFreeDoc(doc);
    return r;
}

vector<M3U8Processor::M3U8Variant> M3U8Processor::GetVariants(const string &url) const {
    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger_) logger_->warn("Failed to initialize CURL for M3U8 GetVariants");
        return {};
    }

    string read_buffer;
    curl_slist *headers = nullptr;
    headers = curl_slist_append(
        headers,
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kCurlTimeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kCurlConnectTimeout);

    const CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (logger_) logger_->warn("Failed to perform M3U8 GetVariants: {}", curl_easy_strerror(res));
        return {};
    }

    if (read_buffer.empty()) {
        if (logger_)
            logger_->error("No available variants. Please post this as an issue on the Git repo page: {}",
                           url);
        return {};
    }

    stringstream ss(read_buffer);
    vector<M3U8Variant> variants;
    string line;
    string current_attr_line;
    auto GetAttribute = [&](const string &input, const string &key) {
        auto pos = input.find(key + "=");
        if (pos == string::npos) return string{};

        pos += key.length() + 1;
        const char delimiter = (input[pos] == '"') ? '"' : ',';
        if (input[pos] == '"') pos++;

        const size_t endPos = input.find(delimiter, pos);
        return input.substr(pos, endPos - pos);
    };

    while (getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line.find("#EXT-X-STREAM-INF:") == 0) {
            current_attr_line = line;
        } else if (line.find('#') != 0 && !current_attr_line.empty()) {
            M3U8Variant v;
            v.url = line;

            string codec = GetAttribute(current_attr_line, "CODECS");
            if (codec.find("avc1") == string::npos) {
                continue;
            }

            string resol = GetAttribute(current_attr_line, "RESOLUTION");
            size_t xPos = resol.find('x');
            if (xPos != string::npos) {
                const auto res1 = stoi(resol.substr(xPos + 1));
                const auto res2 = stoi(resol.substr(0, xPos));
                if (res1 != res2) continue;
                if (res1 > 1200 || res1 < 800) continue;
                v.resolution = res1;
            } else continue;

            string bw = GetAttribute(current_attr_line, "BANDWIDTH");
            if (!bw.empty()) {
                v.bandwidth = stoull(bw);
            } else continue;

            variants.push_back(v);
            current_attr_line.clear();
        }
    }

    return variants;
}

M3U8Processor::M3U8Variant M3U8Processor::PickVariant(const vector<M3U8Variant> &Variants) {
    // pick the one with the lowest bandwidth and the highest resolution
    const auto it = ranges::max_element(Variants, [](const M3U8Variant &a, const M3U8Variant &b) {
        if (a.resolution != b.resolution) {
            return a.resolution < b.resolution;
        }

        return a.bandwidth < b.bandwidth;
    });
    return *it;
}

vector<uint8_t> M3U8Processor::Download(const M3U8Variant &v, const uint64_t loop_gen) const {
    auto iv_r = OpenInputAndFindVideostream(v.url);
    if (holds_alternative<Err>(iv_r)) {
        if (logger_) logger_->error("M3U8Processor download error: {}", std::get<Err>(iv_r).msg);
        return {};
    }
    auto iv = UNWRAP(std::move(iv_r));
    AVStream *ist = iv.fmt->streams[iv.vstream];
    if (IsCancelled(loop_gen)) return {};

    auto dec_r = BuildDecoder(ist);
    if (holds_alternative<Err>(dec_r)) {
        if (logger_) logger_->error("M3U8Processor download error: {}", std::get<Err>(dec_r).msg);
        return {};
    }
    CodecCtxPtr dec_ctx = UNWRAP(std::move(dec_r));
    if (IsCancelled(loop_gen)) return {};

    auto go_r = CreateGifOutput(v.resolution);
    if (holds_alternative<Err>(go_r)) {
        if (logger_) logger_->error("M3U8Processor download error: {}", std::get<Err>(go_r).msg);
        return {};
    }
    GifOutput go = UNWRAP(std::move(go_r));
    if (IsCancelled(loop_gen)) return {};

    auto sws_r = CreateSws(dec_ctx.get(), go.enc_ctx.get(), v.resolution);
    if (holds_alternative<Err>(sws_r)) {
        if (logger_) logger_->error("M3U8Processor download error: {}", std::get<Err>(sws_r).msg);
        return {};
    }
    SwsBundle sws = UNWRAP(std::move(sws_r));
    if (IsCancelled(loop_gen)) return {};

    auto wb_r = CreateWorkBuffers(go.enc_ctx.get(), v.resolution);
    if (holds_alternative<Err>(wb_r)) {
        if (logger_) logger_->error("M3U8Processor download error: {}", std::get<Err>(wb_r).msg);
        return {};
    }
    WorkBuffers wb = UNWRAP(std::move(wb_r));
    if (IsCancelled(loop_gen)) return {};

    auto loop_r = TranscodeLoop(iv, dec_ctx.get(), go, sws, wb, [this](const uint64_t g) { return IsCancelled(g); },
                                loop_gen);
    if (holds_alternative<Err>(loop_r)) {
        if (logger_) logger_->error("M3U8Processor download error: {}", std::get<Err>(loop_r).msg);
        return {};
    }
    if (IsCancelled(loop_gen)) return {};

    auto fin_r = FinalizeGifOutput(go);
    if (holds_alternative<Err>(fin_r)) {
        if (logger_) logger_->error("M3U8Processor download error: {}", std::get<Err>(fin_r).msg);
        return {};
    }

    return std::move(go.ob->data);
}

void M3U8Processor::ffmpeg_log_callback(void *ptr, int level, const char *fmt, const va_list vl) {
    if (level > av_log_get_level()) return;
    if (!fmt) return;

    const AVClass *avc = ptr ? *static_cast<const AVClass **>(ptr) : nullptr;
    const char *name = avc ? avc->item_name(ptr) : "ffmpeg";

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, vl);
    string msg(buffer);

    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();

    spdlog::log(
        level <= AV_LOG_ERROR
            ? spdlog::level::err
            : level <= AV_LOG_WARNING
                  ? spdlog::level::warn
                  : level <= AV_LOG_INFO
                        ? spdlog::level::info
                        : spdlog::level::debug,
        "[ffmpeg] [{}] {}",
        name,
        msg
    );
}
