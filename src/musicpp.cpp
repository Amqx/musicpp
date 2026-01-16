//
// Created by Jonathan on 16-Jan-26.
//

#include "musicpp.h"
#include "timeutils.h"
#include "stringutils.h"
#include "globals.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <consoleutils.h>
#include <credhelper.h>
#include <conio.h>
#include <shlobj_core.h>

using namespace std;

string setup::MakeLogName() {
    const auto t = UnixSecondsNow();

    const chrono::zoned_time local{chrono::current_zone(), chrono::sys_time(chrono::seconds{t})};

    const auto lt = chrono::floor<chrono::seconds>(local.get_local_time());
    const auto dp = chrono::floor<chrono::days>(lt);
    const chrono::year_month_day ymd{dp};
    const auto tod = chrono::hh_mm_ss{lt - dp};

    return format("musicpp_{:04}-{:02}-{:02}_{:02}-{:02}-{:02}.log",
                  static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()),
                  static_cast<unsigned>(ymd.day()),
                  tod.hours().count(),
                  tod.minutes().count(),
                  tod.seconds().count()
    );
}

void setup::PruneLogs(const filesystem::path &folder, wstringstream &out) {
    vector<filesystem::directory_entry> files;
    for (auto &entry: filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file()) files.push_back(entry);
    }
    ranges::sort(files, [](auto &a, auto &b) {
        return a.last_write_time() > b.last_write_time();
    });

    if (files.size() > kMaxFiles) {
        for (size_t i = kMaxFiles; i < files.size(); i++) {
            error_code ec;
            filesystem::remove(files[i], ec);
            if (ec) {
                out << console::Yellow << L"Failed to remove old log file: " << ConvertToWString(
                    files[i].path().string()) << console::Reset << endl;
            }
        }
    }
}

bool setup::IsValidRegion(const string &region) {
    return kValidRegions.contains(region);
}

void setup::PrintRegionList() {
    wcout << L"\nAvailable Apple Music regions (country codes):" << endl;
    int count = 0;
    for (const auto &region: kRegionList) {
        wcout << setw(4) << region.c_str();
        ++count;
        if (count % 12 == 0) {
            wcout << endl;
        }
    }
    if (count % 12 != 0) {
        wcout << endl;
    }
    wcout << endl;
}

void setup::SaveRegion(const AppContext &ctx, const string &region) {
    if (!ctx.db) return;
    if (const leveldb::Status status = ctx.db->Put(leveldb::WriteOptions(), kRegionDbKey, region); !status.ok()) {
        if (ctx.logger) ctx.logger->warn("Failed to persist region {}: {}", region, status.ToString());
    } else {
        if (ctx.logger) ctx.logger->info("Set region to {}", region);
    }
}

string setup::LoadRegion(const AppContext &ctx) {
    if (!ctx.db) return "";
    string region;
    if (const leveldb::Status status = ctx.db->Get(leveldb::ReadOptions(), kRegionDbKey, &region); !status.ok()) {
        if (ctx.logger) ctx.logger->warn("Failed to read saved region: {}", status.ToString());
    }
    return region;
}

string setup::GetRegion() {
    wstring input;
    while (true) {
        wcout << L"Enter Apple Music region code (type 'list' to view options): ";
        wcin >> input;

        string region_input = ToLowerCase(ConvertWString(input));
        if (region_input == "list") {
            PrintRegionList();
            continue;
        }

        if (IsValidRegion(region_input)) {
            return region_input;
        }

        wcout << L"Region not recognized. Please choose a code from the list below." << endl;
        PrintRegionList();
    }
}

string setup::EnsureRegion(const AppContext &ctx) {
    string region = ToLowerCase(LoadRegion(ctx));
    if (region.empty()) {
        return "null";
    }
    return region;
}

void setup::PurgeDatabase(const AppContext *ctx) {
    if (!ctx->db) {
        if (ctx->logger) ctx->logger->warn("Attempted to purge unavailable database");
        return;
    }

    leveldb::WriteBatch batch;
    leveldb::ReadOptions ro;
    ro.fill_cache = false;
    unique_ptr<leveldb::Iterator> it(ctx->db->NewIterator(ro));
    if (!it) {
        if (ctx->logger) ctx->logger->warn("Failed to get iterator for db purge");
        return;
    }

    int count = 0;
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        const leveldb::Slice key = it->key();

        if (key == kDiscordStateKey || key == kLfmStateKey || key == kRegionDbKey) {
            continue;
        }

        batch.Delete(key);
        count++;
    }

    if (!it->status().ok()) {
        if (ctx->logger) ctx->logger->error("Iterator error during purge: {}", it->status().ToString());
        return;
    }

    const leveldb::Status s = ctx->db->Write(leveldb::WriteOptions(), &batch);
    if (s.ok()) {
        if (ctx->logger) ctx->logger->info("Successfully wiped {} items from the cache", count);
        if (count > 0) ctx->db->CompactRange(nullptr, nullptr);
    } else {
        if (ctx->logger) ctx->logger->error("Failed to delete items from cache! Error: {}", s.ToString());
    }
    ctx->player->ImageRefresh();
}

bool setup::CleanDatabase(const AppContext &ctx, wstringstream &out) {
    if (ctx.logger)
        ctx.logger->info(
            "Beginning database purge: checking for keys older than {}s (static) and keys older than {}s (animated)",
            kDbExpireTime, kDbExpireTimeAnim);
    out << L"Cleaning database of static images older than " << FriendlyTime(kDbExpireTime) << endl;
    out << L"Cleaning database of animated images older than " << FriendlyTime(kDbExpireTimeAnim) << endl;
    const uint64_t now = UnixSecondsNow();

    leveldb::ReadOptions ro;
    leveldb::WriteOptions wo;
    ro.fill_cache = false;
    wo.sync = true;

    unique_ptr<leveldb::Iterator> it(ctx.db->NewIterator(ro));
    if (!it) {
        if (ctx.logger) ctx.logger->warn("Failed to get iterator for db purge");
        out << console::Yellow << L"Error during db cleanup, check logs" << console::Reset << endl;
        return false;
    }

    leveldb::WriteBatch batch;

    size_t deleted = 0;
    size_t malformed = 0;

    auto ParseEpochPrefix = [&](const string &value, uint64_t &out_seconds) {
        const size_t sep = value.find('|');
        if (sep == string::npos || sep == 0) return false;

        out_seconds = stoull(value.substr(0, sep));
        return true;
    };

    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        const leveldb::Slice key = it->key();
        if (key == kDiscordStateKey || key == kLfmStateKey || key == kRegionDbKey) {
            continue;
        }

        const leveldb::Slice val = it->value();
        bool should_delete = false;

        try {
            uint64_t stored;
            if (!ParseEpochPrefix(val.ToString(), stored)) {
                should_delete = true;
                malformed++;
            } else {
                if (stored > now) {
                    should_delete = true;
                    malformed++;
                } else if (key.ToString().find("musicppAMAnim") != string::npos) {
                    if (now - stored > kDbExpireTimeAnim) {
                        should_delete = true;
                    }
                } else if (now - stored > kDbExpireTime) {
                    should_delete = true;
                }
            }
        } catch (exception &e) {
            should_delete = true;
            if (ctx.logger)
                ctx.logger->warn("Found extremely malformed value (error: {}): {}", e.what(),
                                 val.ToString());
        }

        if (should_delete) {
            batch.Delete(key);
            deleted++;
        }
    }

    if (!it->status().ok()) {
        if (ctx.logger) ctx.logger->error("Iterator error during database cleaning: {}", it->status().ToString());
        out << console::Yellow << L"Error during db cleanup, check logs" << console::Reset << endl;
        return false;
    }

    if (deleted > 0) {
        const leveldb::Status s = ctx.db->Write(wo, &batch);
        if (!s.ok()) {
            if (ctx.logger) ctx.logger->error("Failed to delete items from cache! Error: {}", s.ToString());
            out << console::Yellow << L"Error during db cleanup, check logs" << console::Reset << endl;
            return false;
        }
        if (ctx.logger) ctx.logger->info("Purged {} keys ({} malformed)", deleted, malformed);
        out << L"Purged " << deleted << L" keys from the database (" << malformed << L" malformed keys)" << endl;
        ctx.db->CompactRange(nullptr, nullptr);
    } else {
        if (ctx.logger) ctx.logger->info("Nothing to purge");
        wcout << L"Nothing to purge!" << endl;
    }

    wcout << endl;
    return true;
}

bool setup::InitializeDatabase(AppContext &ctx, wstringstream &out) {
    PWSTR path = nullptr;

    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        wcout << console::Red << L"Fatal: could not get local appdata path" << console::Reset << endl;

        return false;
    }

    out << L"Configuration files: " << path << L'\n' << endl;

    const filesystem::path base_path(path);
    const filesystem::path db_path = base_path / "musicpp" / "song_db";
    const filesystem::path log_path = base_path / "musicpp" / "logs";
    CoTaskMemFree(path);

    error_code ec1, ec2;

    create_directories(db_path, ec1);
    create_directories(log_path, ec2);

    if (ec1) {
        out << console::Red << L"Fatal: Could not create db folder: " << ec1.message().c_str() << console::Reset
                << endl;
        return false;
    }

    if (ec2) {
        out << console::Red << L"Could not create log folder: " << ec2.message().c_str() << console::Reset << endl;
        out << L"The program will not have logs. Continuing..." << endl;
    } else {
        PruneLogs(log_path, out);
        const string name = setup::MakeLogName();
        const auto logger = spdlog::basic_logger_mt("MusicPP Logger", (log_path / name).string());
#ifdef LOG_LEVEL_DEBUG
        spdlog::set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::debug);
#elif defined(LOG_LEVEL_INFO)
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_every(kLogFlushInterval);
#else
        spdlog::set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::debug);
#endif
        spdlog::set_pattern("(%Y-%m-%d %H:%M:%S %z, [%8l]) Thread %t: %v");
        // ReSharper disable once CppRedundantQualifierADL
        spdlog::set_default_logger(logger);
        ctx.logger = logger;
    }

    if (ctx.logger) {
        ctx.logger->info("Logger initialized at path: {}", log_path.string());
        ctx.logger->info("MusicPP starting up...");
    }

    leveldb::DB *temp_db = nullptr;

    leveldb::Options options;

    options.create_if_missing = true;

    const leveldb::Status status = leveldb::DB::Open(options, db_path.string(), &temp_db);

    if (status.ok()) {
        ctx.db.reset(temp_db);
        if (ctx.logger) ctx.logger->info("Database initialized at path: {}", db_path.string());
        return CleanDatabase(ctx, out);
    }

    out << console::Red << L"Fatal: Could not initialize database!" << console::Reset << endl;
    if (ctx.logger) ctx.logger->error("Could not initialize database: {}", status.ToString());

    return false;
}

bool setup::LoadCredentials(AppContext &ctx, const string &region, bool& console_created) {

    const wstring s_cid = EnsureCredential(kSpotifyDbClientIdKey, L"Spotify Client ID",
                                           L"https://developer.spotify.com/documentation/web-api/tutorials/getting-started",
                                           console_created, ctx.logger.get());

    const wstring s_sec = EnsureCredential(kSpotifyDbClientSecretKey, L"Spotify Client Secret",
                                           L"https://developer.spotify.com/documentation/web-api/tutorials/getting-started",
                                           console_created, ctx.logger.get());

    const wstring i_cid = EnsureCredential(kImgurDbClientIdKey, L"Imgur Client ID",
                                           L"https://api.imgur.com/oauth2/addclient", console_created,
                                           ctx.logger.get());

    const wstring lfm_key = EnsureCredential(kLastFmDbApikey, L"LastFM API Key",
                                             L"https://www.last.fm/api/account/create",
                                             console_created, ctx.logger.get());

    const wstring lfm_secret = EnsureCredential(kLastFmDbSecret, L"LastFM Secret",
                                                L"https://www.last.fm/api/account/create", console_created,
                                                ctx.logger.get());
    if (console_created) {
        wcout << L"All APIKeys found" << endl;
        if (ctx.logger) {
            ctx.logger->info("All API keys loaded successfully");
        }
        wcout << L"\nApple Music region: " << region.c_str() << endl;
    }


    // Initialize API and Player objects
    ctx.scraper = make_unique<Amscraper>(region, ctx.logger.get());
    if (ctx.logger) ctx.logger->info("AMScraper initialized with region {}", region);

    ctx.processor = make_unique<M3U8Processor>(ctx.logger.get());
    if (ctx.logger) ctx.logger->info("M3U8Processor initialized");

    ctx.lastfm = make_unique<Lfm>(ConvertWString(lfm_key), ConvertWString(lfm_secret), ctx.db.get(),
                                       ctx.logger.get());
    if (ctx.logger) {
        ctx.logger->info("LastFM initialized");
    }

    ctx.spotify = make_unique<SpotifyApi>(ConvertWString(s_cid), ConvertWString(s_sec), ctx.logger.get());
    if (ctx.logger) {
        ctx.logger->info("SpotifyAPI initialized");
    }

    ctx.imgur = make_unique<ImgurApi>(ConvertWString(i_cid), ctx.logger.get());
    if (ctx.logger) ctx.logger->info("ImgurAPI initialized");

    ctx.player = make_unique<MediaPlayer>(ctx.scraper.get(), ctx.processor.get(), ctx.spotify.get(),
                                               ctx.imgur.get(),
                                               ctx.lastfm.get(), ctx.db.get(), ctx.logger.get());
    if (ctx.logger) ctx.logger->info("mediaPlayer initialized");

    ctx.discord = make_unique<Discordrp>(ctx.player.get(), kDiscordApikey, ctx.db.get(), ctx.logger.get());
    if (ctx.logger) ctx.logger->info("discordrp initialized");

    return console_created;
}

variant<int, AppContext> setup::setup() {

    AppContext ctx;

    if (wstringstream out; !InitializeDatabase(ctx, out)) {
        SetupConsole();
        wcout << out.str();
        wcout << L"Press Enter to continue..." << endl;
        wcin.get();
        return 1;
    }

    bool console_created = false;

    string region = EnsureRegion(ctx);
    if (region == "null" || !IsValidRegion(region)) {
        SetupConsole();
        console_created = true;
        if (region == "null") {
            wcout << L"Currently no region set!" << endl;
        } else {
            wcout << L"Invalid region detected!" << endl;
        }

        wcout << L"Enter 1 to use default region (Canada) or enter 2 to select a region: " << endl;

        wstring input;
        while (wcin >> input) {
            if (input == L"1") {
                region = kDefaultRegion;
                SaveRegion(ctx, region);
                wcout << L"Set default region of Canada" << endl;
            } else if (input == L"2") {
                PrintRegionList();
                region = GetRegion();
                SaveRegion(ctx, region);
                wcout << L"Region set to " << ConvertToWString(region) << L"!" << endl;
            } else {
                wcout << L"Invalid input!" << endl;
                wcout << L"Enter 1 to use default region (Canada) or enter 2 to select a region: " << endl;
            }
        }
    }

    LoadCredentials(ctx, region, console_created);

    if (ctx.logger) ctx.logger->info("Load credentials finished");

    if (console_created) {
        wcout << "\nThis console window will close automatically" << endl;

        for (int i = 3; i > 0; --i) {
            wcout << L"Closing in " << i << L" seconds... \r" << flush;

            this_thread::sleep_for(chrono::seconds(1));
        }

        CleanupConsole();
        if (ctx.logger) ctx.logger->info("Console cleanup complete, moving to message loop");
    }
    return ctx;
}

void loop::UpdateTrayTooltip(AppContext *ctx) {
    if (ctx->nid.cbSize == 0) return;

    const Snapshot metadata = ctx->player->GetSnapshot(kSnapshotTypeTime);

    const wstring image_source = metadata.image_source;

    const bool playing = metadata.state;

    wstringstream tip_stream;

    if (metadata.album.empty()) {
        tip_stream << L"MusicPP - No track playing";
    } else {
        tip_stream << L"MusicPP - ";
        wstring states = L"";
        if (ctx->discord->GetState()) {
            states += L"Presence enabled\n";
        }
        if (ctx->lastfm->GetState()) {
            states += L"LastFM enabled\n";
        }
        if (states == L"") {
            states = L"All features disabled\n";
        } else if (states == L"Presence enabled\nLastFM enabled\n") {
            states = L"All features enabled\n";
        }
        tip_stream << states;
        tip_stream << L"Image Source: " << (!image_source.empty() ? image_source : L"Unknown") << L"\n";

        if (playing) {
            if (const uint64_t duration = metadata.duration; duration > 0) {
                const uint64_t elapsed = metadata.elapsed;

                tip_stream << L"Timestamp: " << FormatTimestamp(elapsed) << L" / " << FormatTimestamp(duration) << L"\n";
            }
        } else {
            tip_stream << L"Track Paused" << L"\n";
        }
    }

    const wstring tip = tip_stream.str();

    if (tip == ctx->last_tip) {
        return;
    }

    ctx->last_tip = tip;

    if (ctx->logger) ctx->logger->debug("Updating tray tooltip to: \n{}", ConvertWString(tip));

    lstrcpynW(ctx->nid.szTip, tip.c_str(), ARRAYSIZE(ctx->nid.szTip));

    ctx->nid.uFlags |= NIF_TIP | NIF_ICON | NIF_MESSAGE;

    Shell_NotifyIcon(NIM_MODIFY, &ctx->nid);
}

void loop::CopyToClipboard(const AppContext *ctx, const wstring &text, const wstring &label) {
    if (!OpenClipboard(nullptr)) {
        if (ctx->logger) ctx->logger->error("Failed to open clipboard for text: {}", ConvertWString(text));
        return;
    }
    EmptyClipboard();

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    const HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        if (ctx->logger)
            ctx->logger->error("Failed to allocate memory for clipboard text: {}",
                               ConvertWString(text));
        CloseClipboard();
        return;
    }

    memcpy(GlobalLock(hMem), text.c_str(), bytes);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();

    NOTIFYICONDATAW tempNid = {sizeof(tempNid)};
    tempNid.hWnd = ctx->hWnd;
    tempNid.uID = ctx->nid.uID;
    tempNid.uFlags = NIF_INFO;
    tempNid.dwInfoFlags = NIIF_INFO;
    lstrcpynW(tempNid.szInfoTitle, L"Copied to clipboard", ARRAYSIZE(tempNid.szInfoTitle));
    lstrcpynW(tempNid.szInfo, label.c_str(), ARRAYSIZE(tempNid.szInfo));

    Shell_NotifyIcon(NIM_MODIFY, &tempNid);
    if (ctx->logger) ctx->logger->debug("Set clipboard to: {}", ConvertWString(text));
}

static bool GetDatabasePath(filesystem::path &db_path) {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        return false;
    }

    db_path = filesystem::path(path) / "musicpp" / "song_db";
    CoTaskMemFree(path);
    return true;
}

static uintmax_t GetDirectorySize(const filesystem::path &dir) {
    error_code ec;
    if (!exists(dir, ec)) return 0;

    uintmax_t total = 0;
    for (auto it = filesystem::recursive_directory_iterator(
             dir, filesystem::directory_options::skip_permission_denied, ec);
         it != filesystem::recursive_directory_iterator() && !ec;
         it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            total += it->file_size(ec);
        }
    }

    return total;
}

static wstring FormatBytes(const uintmax_t bytes) {
    static constexpr const wchar_t *kUnits[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double size = static_cast<double>(bytes);
    size_t unit = 0;
    constexpr size_t kUnitsCount = std::size(kUnits);

    while (size >= 1024.0 && unit + 1 < kUnitsCount) {
        size /= 1024.0;
        ++unit;
    }

    wstringstream out;
    if (unit == 0) {
        out << bytes << L' ' << kUnits[unit];
    } else {
        out << fixed << setprecision(size < 10.0 ? 1 : 0) << size << L' ' << kUnits[unit];
    }
    return out.str();
}

static void UpdateDbSizeLabel(const HWND hDlg) {
    filesystem::path db_path;
    if (!GetDatabasePath(db_path)) {
        SetDlgItemTextW(hDlg, IDC_TEXT_DB_SIZE, L"Unavailable");
        return;
    }

    const uintmax_t size = GetDirectorySize(db_path);
    const wstring label = FormatBytes(size);
    SetDlgItemTextW(hDlg, IDC_TEXT_DB_SIZE, label.c_str());
}

static wstring loop::GetDlgItemTextWStr(const HWND hDlg, const int id) {
    const HWND hEdit = GetDlgItem(hDlg, id);
    if (!hEdit) return L"";

    const int len = GetWindowTextLengthW(hEdit);
    if (len <= 0) return L"";

    wstring s;
    s.resize(static_cast<size_t>(len + 1));

    GetWindowTextW(hEdit, s.data(), len + 1);
    s.resize(wcslen(s.c_str()));
    return s;
}

static bool loop::IsValidOrEmpty(const wstring &value, const ValidationInputType type) {
    if (value.empty()) return true;
    return ValidateInput(value, type);
}

static bool loop::AreSettingsInputsValid(const HWND hDlg) {
    const wstring s_id = GetDlgItemTextWStr(hDlg, IDC_EDIT_SPOTIFY_ID);
    if (!IsValidOrEmpty(s_id, SAPI_CLIENT_ID)) return false;

    const wstring s_secret = GetDlgItemTextWStr(hDlg, IDC_EDIT_SPOTIFY_SECRET);
    if (!IsValidOrEmpty(s_secret, SAPI_SECRET)) return false;

    const wstring lfm_key = GetDlgItemTextWStr(hDlg, IDC_EDIT_LFM_KEY);
    if (!IsValidOrEmpty(lfm_key, LFM_KEY)) return false;

    const wstring lfm_secret = GetDlgItemTextWStr(hDlg, IDC_EDIT_LFM_SECRET);
    if (!IsValidOrEmpty(lfm_secret, LFM_SECRET)) return false;

    const wstring imgur_id = GetDlgItemTextWStr(hDlg, IDC_EDIT_IMGUR_ID);
    if (!IsValidOrEmpty(imgur_id, IMGUR)) return false;

    return true;
}

static void loop::UpdateSettingsActionState(const HWND hDlg) {
    const bool valid = AreSettingsInputsValid(hDlg);
    EnableWindow(GetDlgItem(hDlg, ID_APPLY), valid);
    EnableWindow(GetDlgItem(hDlg, IDOK), valid);
}

static void loop::PopulateRegionCombo(const HWND hDlg, const string &selected_region) {
    const HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_REGION);
    if (!hCombo) return;

    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);

    int selected_index = -1;
    int default_index = -1;
    for (const auto &region : kRegionList) {
        const wstring wregion = ConvertToWString(region);
        const int idx = static_cast<int>(SendMessageW(hCombo, CB_ADDSTRING, 0,
                                                      reinterpret_cast<LPARAM>(wregion.c_str())));
        if (region == selected_region) {
            selected_index = idx;
        }
        if (region == kDefaultRegion) {
            default_index = idx;
        }
    }

    if (selected_index == -1) {
        selected_index = default_index;
    }

    if (selected_index != -1) {
        SendMessageW(hCombo, CB_SETCURSEL, selected_index, 0);
    }
}

static string loop::GetComboSelection(const HWND hCombo) {
    if (!hCombo) return "";
    const LRESULT idx = SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR) return "";

    wchar_t buffer[16]{};
    if (SendMessageW(hCombo, CB_GETLBTEXT, idx, reinterpret_cast<LPARAM>(buffer)) == CB_ERR) {
        return "";
    }
    return ConvertWString(buffer);
}

void loop::ApplySettings(const HWND hDlg, const AppContext *ctx) {
    const bool discord_enabled = IsDlgButtonChecked(hDlg, IDC_CHECK_DISCORD) == BST_CHECKED;
    const bool lastfm_enabled = IsDlgButtonChecked(hDlg, IDC_CHECK_LASTFM) == BST_CHECKED;

    ctx->discord->SetState(discord_enabled);
    ctx->lastfm->SetState(lastfm_enabled);

    const HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_REGION);
    const string selected_region = ToLowerCase(GetComboSelection(hCombo));
    if (!selected_region.empty() && setup::IsValidRegion(selected_region)) {
        if (ctx->scraper && ctx->scraper->GetRegion() != selected_region) {
            ctx->scraper->SetRegion(selected_region);
            setup::SaveRegion(*ctx, selected_region);
        }
    }

    const wstring new_s_id = GetDlgItemTextWStr(hDlg, IDC_EDIT_SPOTIFY_ID);
    if (ValidateInput(new_s_id, SAPI_CLIENT_ID)) {
        WriteGenericCredential(kSpotifyDbClientIdKey, new_s_id, ctx->logger.get());
    }
    const wstring new_s_secret = GetDlgItemTextWStr(hDlg, IDC_EDIT_SPOTIFY_SECRET);
    if (ValidateInput(new_s_secret, SAPI_SECRET)) {
        WriteGenericCredential(kSpotifyDbClientSecretKey, new_s_secret, ctx->logger.get());
    }
    const wstring new_lfm_key = GetDlgItemTextWStr(hDlg, IDC_EDIT_LFM_KEY);
    if (ValidateInput(new_lfm_key, LFM_KEY)) {
        WriteGenericCredential(kLastFmDbApikey, new_lfm_key, ctx->logger.get());
    }
    const wstring new_lfm_secret = GetDlgItemTextWStr(hDlg, IDC_EDIT_LFM_SECRET);
    if (ValidateInput(new_lfm_secret, LFM_SECRET)) {
        WriteGenericCredential(kLastFmDbSecret, new_lfm_secret, ctx->logger.get());
    }
    const wstring new_imgur_id = GetDlgItemTextWStr(hDlg, IDC_EDIT_IMGUR_ID);
    if (ValidateInput(new_imgur_id, IMGUR)) {
        WriteGenericCredential(kImgurDbClientIdKey, new_imgur_id, ctx->logger.get());
    }

    MessageBoxW(hDlg, L"Settings applied. If you changed any API keys, please restart the app.", L"MusicPP", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK loop::WndProc(const HWND hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam) {
    auto *ctx = reinterpret_cast<AppContext *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    if (msg == g_wm_taskbar_restart) {
        if (ctx && ctx->nid.cbSize != 0) {
            ctx->nid.uFlags |= NIF_MESSAGE | NIF_ICON | NIF_TIP;
            if (!ctx->nid.hIcon) {
                ctx->nid.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APPICON));
                if (!ctx->nid.hIcon) ctx->nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION); // Fallback
            }
            Shell_NotifyIcon(NIM_ADD, &ctx->nid);
            if (ctx->logger) ctx->logger->info("Shell restarted, tray icon recreated");
        }

        UpdateTrayTooltip(ctx);
        return 0;
    }

    switch (msg) {
        case WM_CREATE: {
            const auto *create_struct = reinterpret_cast<CREATESTRUCT *>(lParam);

            ctx = static_cast<AppContext *>(create_struct->lpCreateParams);

            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

            ctx->hWnd = hWnd;

            ctx->nid.cbSize = sizeof(ctx->nid);

            ctx->nid.hWnd = hWnd;

            ctx->nid.uID = 1;

            ctx->nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;

            ctx->nid.uCallbackMessage = WM_TRAYICON;

            ctx->nid.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APPICON));

            if (!ctx->nid.hIcon) ctx->nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION); // Fallback

            lstrcpynW(ctx->nid.szTip, L"MusicPP", ARRAYSIZE(ctx->nid.szTip));

            Shell_NotifyIcon(NIM_ADD, &ctx->nid);

            if (ctx->logger) ctx->logger->info("Window created and tray icon added");

            NOTIFYICONDATAW notify = {sizeof(notify)};
            notify.hWnd = ctx->hWnd;
            notify.uID = ctx->nid.uID;
            notify.uFlags = NIF_INFO;
            notify.dwInfoFlags = NIIF_INFO;
            lstrcpynW(notify.szInfoTitle, L"MusicPP", ARRAYSIZE(notify.szInfoTitle));
            lstrcpynW(notify.szInfo, L"MusicPP has been minimized to the tray.", ARRAYSIZE(notify.szInfo));
            Shell_NotifyIcon(NIM_MODIFY, &notify);

            SetTimer(hWnd, DATA_TIMER_ID, kLoopRefreshInterval, nullptr);

            if (ctx->logger)
                ctx->logger->debug("Timer started: Data ({} ms)",
                                   kLoopRefreshInterval);
            return 0;
        }

        case WM_TRAYICON: {
            if (LOWORD(lParam) == WM_RBUTTONUP) {
                POINT pt{};

                GetCursorPos(&pt);

                SetForegroundWindow(hWnd); // Required for menu behavior

                if (const HMENU hMenu = CreatePopupMenu()) {

                    // Version
                    const wstring v = L"MusicPP V" + kVersion;
                    AppendMenu(hMenu, MF_STRING | MF_DISABLED, 0, v.c_str());
                    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);

                    // Metadata
                    const Snapshot metadata = ctx->player->GetSnapshot(kSnapshotTypeTray);
                    const wstring title = metadata.title;
                    const wstring artist = metadata.artist;
                    const wstring album = metadata.album;
                    if (metadata.has_session && !title.empty() && !artist.empty() && !album.empty()) {
                        AppendMenu(hMenu, MF_STRING, ID_COPY_TITLE_ARTIST_ALBUM, L"Now Playing");
                        AppendMenu(hMenu, MF_STRING, ID_COPY_TITLE, EscapeAmpersands(Truncate(title)).c_str());
                        AppendMenu(hMenu, MF_STRING, ID_COPY_ARTIST, EscapeAmpersands(Truncate(artist)).c_str());
                        AppendMenu(hMenu, MF_STRING, ID_COPY_ALBUM, EscapeAmpersands(Truncate(album)).c_str());
                        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
                        AppendMenu(hMenu, MF_STRING, ID_COPY_IMAGE, L"Copy Image");
                        AppendMenu(hMenu, MF_STRING, ID_OPEN_IMAGE, L"Open Image");
                        AppendMenu(hMenu, MF_STRING, ID_TRAY_FORCE_REFRESH, L"Force Refresh");
                        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
                    }

                    // Settings
                    AppendMenu(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"Settings...");
                    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);

                    // Exit
                    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, TEXT("Exit"));

                    const UINT cmd = TrackPopupMenu(
                        hMenu,
                        TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD,
                        pt.x,
                        pt.y,
                        0,
                        hWnd,
                        nullptr);

                    DestroyMenu(hMenu);

                    if (cmd != 0) {
                        PostMessage(hWnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
                    }
                }

                PostMessage(hWnd, WM_NULL, 0, 0);
            }

            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_TRAY_EXIT: {
                    if (ctx->logger) ctx->logger->info("User requested application exit via tray menu.");
                    SendMessage(hWnd, WM_CLOSE, 0, 0);
                    return 0;
                }

                case ID_COPY_TITLE: {
                    const Snapshot metadata = ctx->player->GetSnapshot(kSnapshotTypeTray);
                    CopyToClipboard(ctx, metadata.title, L"Copied title: " + metadata.title);
                    return 0;
                }

                case ID_COPY_ARTIST: {
                    const Snapshot metadata = ctx->player->GetSnapshot(kSnapshotTypeTray);
                    CopyToClipboard(ctx, metadata.artist, L"Copied artist: " + metadata.artist);
                    return 0;
                }

                case ID_COPY_ALBUM: {
                    const Snapshot metadata = ctx->player->GetSnapshot(kSnapshotTypeTray);
                    CopyToClipboard(ctx, metadata.album, L"Copied album: " + metadata.album);
                    return 0;
                }

                case ID_COPY_IMAGE: {
                    const Snapshot metadata = ctx->player->GetSnapshot(kSnapshotTypeTray);
                    CopyToClipboard(ctx, metadata.image,
                                    L"Copied image URL\nSourced from: " + metadata.image_source);
                    return 0;
                }

                case ID_OPEN_IMAGE: {
                    const Snapshot metadata = ctx->player->GetSnapshot(kSnapshotTypeTray);
                    ShellExecuteW(nullptr, L"open", metadata.image.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }

                case ID_TRAY_FORCE_REFRESH: {
                    ctx->player->ImageRefresh();
                    return 0;
                }

                case ID_COPY_TITLE_ARTIST_ALBUM: {
                    const Snapshot metadata = ctx->player->GetSnapshot(kSnapshotTypeTray);
                    const wstring clipboard_text =
                            metadata.title + L"\r\n" + metadata.artist + L"\r\n" + metadata.album;
                    const wstring label = L"Copied metadata\nTitle: " + metadata.title + L"\nArtist: " + metadata.
                                          artist + L"\nAlbum: " + metadata.album;
                    CopyToClipboard(ctx, clipboard_text, label);
                    return 0;
                }

                case ID_TRAY_SETTINGS: {
                    DialogBoxParamW(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDD_SETTINGS_DIALOG), ctx->hWnd, SettingsProc, reinterpret_cast<LPARAM>(ctx));
                }

                default: {
                    return 0;
                }
            }
        }

        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
                ctx->spotify->NotifyAwake();
            }

        case WM_TIMER: {
            if (!ctx || !ctx->player || !ctx->discord) {
                if (ctx && ctx->logger) ctx->logger->error("Timer triggered but AppContext/dependencies are null.");
                return 0;
            }

            ctx->player->UpdateInfo();
            ctx->discord->update();
            UpdateTrayTooltip(ctx);
            if (ctx->logger) ctx->logger->debug("Data timer update cycle completed.");
            return 0;
        }


        case WM_CLOSE:

            if (ctx) {
                KillTimer(hWnd, DATA_TIMER_ID);

                Shell_NotifyIcon(NIM_DELETE, &ctx->nid);

                if (ctx->nid.hIcon) {
                    DestroyIcon(ctx->nid.hIcon);
                }
                if (ctx->logger) ctx->logger->info("Timers and icon cleaned up");
            }

            DestroyWindow(hWnd);

            return 0;


        case WM_DESTROY:

            PostQuitMessage(0);

            return 0;

        default: ;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

INT_PTR loop::SettingsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto *ctx = reinterpret_cast<AppContext *>(lParam);
            SetWindowLongPtr(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));

            if (!ctx) {
                return FALSE;
            }

            if (const HICON hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APPICON))) {
                SendMessage(hDlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
                SendMessage(hDlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            }

            CheckDlgButton(hDlg, IDC_CHECK_DISCORD, ctx->discord->GetState() ? BST_CHECKED : BST_UNCHECKED);

            if (ctx->lastfm->GetReason() == L"Invalid API Credentials!") {
                CheckDlgButton(hDlg, IDC_CHECK_LASTFM, BST_UNCHECKED);
                EnableWindow(GetDlgItem(hDlg, IDC_CHECK_LASTFM), FALSE);
            } else {
                CheckDlgButton(hDlg, IDC_CHECK_LASTFM, ctx->lastfm->GetState() ? BST_CHECKED : BST_UNCHECKED);
            }

            if (ctx->scraper) {
                PopulateRegionCombo(hDlg, ctx->scraper->GetRegion());
            } else {
                PopulateRegionCombo(hDlg, kDefaultRegion);
            }

            SetDlgItemTextW(hDlg, IDC_EDIT_SPOTIFY_ID, L"");
            SetDlgItemTextW(hDlg, IDC_EDIT_SPOTIFY_SECRET, L"");
            SetDlgItemTextW(hDlg, IDC_EDIT_LFM_KEY, L"");
            SetDlgItemTextW(hDlg, IDC_EDIT_LFM_SECRET, L"");
            SetDlgItemTextW(hDlg, IDC_EDIT_IMGUR_ID, L"");
            UpdateSettingsActionState(hDlg);
            UpdateDbSizeLabel(hDlg);

            return TRUE;
        }

        case WM_COMMAND: {
            const auto *ctx = reinterpret_cast<AppContext *>(GetWindowLongPtr(hDlg, DWLP_USER));
            if (!ctx) {
                return FALSE;
            }

            switch (LOWORD(wParam)) {
                case ID_APPLY: {
                    if (!AreSettingsInputsValid(hDlg)) {
                        return TRUE;
                    }
                    ApplySettings(hDlg, ctx);
                    return TRUE;
                }
                case IDOK: {
                    if (!AreSettingsInputsValid(hDlg)) {
                        return TRUE;
                    }
                    ApplySettings(hDlg, ctx);
                    EndDialog(hDlg, IDOK);
                    return TRUE;
                }
                case ID_PURGE_DATABASE: {
                    const int result = MessageBoxW(
                        hDlg,
                        L"Are you sure you want to purge the database cache?",
                        L"MusicPP",
                        MB_YESNO | MB_ICONWARNING);
                    if (result == IDYES) {
                        setup::PurgeDatabase(ctx);
                        UpdateDbSizeLabel(hDlg);
                    }
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;

                default:
                    break;
            }

            if (HIWORD(wParam) == EN_CHANGE) {
                switch (LOWORD(wParam)) {
                    case IDC_EDIT_SPOTIFY_ID:
                    case IDC_EDIT_SPOTIFY_SECRET:
                    case IDC_EDIT_LFM_KEY:
                    case IDC_EDIT_LFM_SECRET:
                    case IDC_EDIT_IMGUR_ID:
                        UpdateSettingsActionState(hDlg);
                        return TRUE;
                    default:
                        break;
                }
            }
        }

        default: {
            return FALSE;
        }
    }
}

int loop::loop(AppContext &ctx, const HINSTANCE hInstance) {
    g_wm_taskbar_restart = RegisterWindowMessage(TEXT("TaskbarCreated"));
    if (g_wm_taskbar_restart == 0) {
        if (ctx.logger) ctx.logger->error("Failed to register TaskbarCreated message");
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MusicPPTrayClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) return 0;
    ctx.hWnd = CreateWindowExW(0, L"MusicPPTrayClass", L"TrayOnlyWindow",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               300, 200, nullptr, nullptr, hInstance, &ctx);
    if (!ctx.hWnd) return 0;
    ShowWindow(ctx.hWnd, SW_HIDE);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (ctx.logger) ctx.logger->info("Application message loop ended. Shutting down.");

    return static_cast<int>(msg.wParam);
}
