/**
 * @file scrobble_driver.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <stop_token>
#include <vector>

#include "log/log.hpp"
#include "metadata/scrobbler.hpp"
#include "orchestrator/worker.hpp"
#include "types/track.hpp"

/// A track must run longer than this to be scrobbled at all (Last.fm's rule).
constexpr std::chrono::seconds kScrobbleMinLength{30};

/// A play qualifies to be scrobbled once it has been played past this, or past half the track's
/// length, whichever comes first — measured against playback position, not wall time.
constexpr std::chrono::seconds kScrobblePlayCap{240};

/// Wall time between scrobble attempts after one was rejected.
constexpr std::chrono::seconds kScrobbleRetry{30};

/// Times a now-playing update is offered to a scrobbler before the play is given up on.
constexpr int kNowPlayingAttempts{3};

/// Wall time between now-playing attempts after one failed.
constexpr std::chrono::seconds kNowPlayingRetry{5};

/// Wall time after which a now-playing update is re-sent to keep it from going stale on the
/// scrobbler's end. A refresh that falls due mid-pause is held until playback resumes.
constexpr std::chrono::minutes kNowPlayingRefresh{10};

/**
 * When a play's calls fall due, and how a rejected one is followed up. Defaults to the constants
 * above; tests substitute a faster schedule.
 */
struct ScrobbleSchedule {
    std::chrono::milliseconds scrobbleRetry = kScrobbleRetry;
    std::chrono::milliseconds nowPlayingRetry = kNowPlayingRetry;
    std::chrono::milliseconds nowPlayingRefresh = kNowPlayingRefresh;
    int nowPlayingAttempts = kNowPlayingAttempts;
};

/**
 * Drives every registered scrobbler through the calls one play owes it.
 */
class ScrobbleDriver {
public:
    /**
     * @param schedule When the play's calls fall due. Defaults to the production schedule.
     */
    explicit ScrobbleDriver(const ScrobbleSchedule &schedule = {});

    ~ScrobbleDriver();

    ScrobbleDriver(const ScrobbleDriver &) = delete;

    ScrobbleDriver &operator=(const ScrobbleDriver &) = delete;

    ScrobbleDriver(ScrobbleDriver &&) = delete;

    ScrobbleDriver &operator=(ScrobbleDriver &&) = delete;

    /**
     * Registers a scrobbler to be driven from the next play onwards.
     * @param scrobbler Shared handle to the scrobbler.
     */
    void registerScrobbler(std::shared_ptr<Scrobbler> scrobbler);

    /**
     * Retires every attempt made for the play that just ended and arms the schedule for the next
     * one. Called when the track changes, when it starts over, and when playback stops.
     */
    void reset();

    /**
     * Advances one cycle: applies whatever the worker has finished since the last cycle, then hands it
     * every call now due.
     * @param current Track playing this cycle. A scrobble is judged against how far the track has
     * played, so the fresh timing is passed rather than the timing the play started with.
     */
    void tick(const Track &current);

private:
    /// The network calls a scrobbler is driven through over the course of one play.
    enum class Attempt { NowPlaying, Scrobble };

    /// Where one of those calls stands within the current play.
    enum class Phase {
        Waiting, ///< Due at nextAttempt, not yet handed to the worker.
        InFlight, ///< With the worker; its result has not come back yet.
        Done ///< Accepted, or failed often enough that the play gives up on it.
    };

    /**
     * The scheduling state of one attempt kind, for one scrobbler, within the current play.
     */
    struct Pending {
        Phase phase = Phase::Waiting;
        int attempts = 0;
        std::chrono::steady_clock::time_point nextAttempt{};
    };

    /**
     * A registered scrobbler plus the per-play state deciding when it is next called.
     */
    struct Target {
        std::shared_ptr<Scrobbler> scrobbler;
        Pending nowPlaying{};
        Pending scrobble{};
    };

    /**
     * The outcome of one attempt.
     */
    struct AttemptResult {
        /// Token of the play the attempt was made for. A result whose play has been retired is
        /// about a track the user has already moved on from, so its state is no longer there to
        /// apply to.
        std::stop_token play;

        /// Track the attempt was made for, carried so a result can be reported without asking the
        /// poll loop what is playing by the time it lands.
        TrackIdentity identity;

        std::size_t target = 0;
        Attempt kind = Attempt::NowPlaying;
        bool accepted = false;
    };

    /**
     * The state of one attempt kind within a target.
     * @param target Target holding the state.
     * @param kind Attempt kind wanted.
     * @return Reference to that kind's state within the target.
     */
    [[nodiscard]] static Pending &slotFor(Target &target, Attempt kind);

    /**
     * @param kind Attempt kind to name.
     * @return Name of the attempt kind, for logging.
     */
    [[nodiscard]] static const char *name(Attempt kind);

    /**
     * Whether a track has been played far enough to be scrobbled, per Last.fm's guideline: longer
     * than 30s, and played past half its length or past the 240s cap, whichever is sooner. Judged
     * against playback position, so a paused play stops accruing towards it.
     * @param track Track playing this cycle.
     * @return Whether a scrobble is warranted.
     */
    [[nodiscard]] static bool scrobbleThresholdMet(const Track &track);

    /**
     * Applies every result the worker has posted since the previous cycle.
     */
    void drainResults();

    /**
     * Applies one result: marks the attempt done, or schedules the retry that follows a rejection.
     * @param result Result to apply. Must belong to the current play.
     */
    void applyResult(const AttemptResult &result);

    /**
     * Hands the worker every attempt that is due this cycle. An attempt already in flight is not
     * submitted again, so a scrobbler is only ever asked one thing at a time.
     * @param current Track playing this cycle.
     */
    void driveAttempts(const Track &current);

    /**
     * Submits a single attempt to the worker, stamped with the current play.
     * @param target Index of the target to call.
     * @param kind Attempt kind to make.
     * @param track Track to make the attempt for.
     */
    void submitAttempt(std::size_t target, Attempt kind, const Track &track);

    std::shared_ptr<spdlog::logger> _log = logging::get("scrobbler");

    ScrobbleSchedule _schedule{};

    std::vector<Target> _targets{};

    /// The play being scrobbled. Retired and replaced whenever a new play begins
    std::stop_source _play{};

    std::mutex _resultsMutex{};

    /// Results posted by the worker, waiting to be applied at the top of a later cycle.
    std::vector<AttemptResult> _results{};

    /// Runs the attempts off the poll loop.
    Worker _worker{};
};
