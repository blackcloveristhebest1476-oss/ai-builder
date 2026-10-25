#pragma once

// In-game timing-window measurement.
//
// Strategy (chosen for reliability over speed): every trial restarts the level
// from the beginning and replays the whole macro with exactly one input moved
// by `shift` physics ticks, using the same injection point GDR bots use
// (after GJBaseGameLayer::processCommands, handleButton(down, button, !player2)).
// A trial succeeds if the player survives `horizon` ticks past the moved input
// (with every later input unchanged). Because each trial starts from the level
// start, earlier inputs, velocity, gamemode, gravity, speed portals and
// triggers are all simulated by Geometry Dash itself - nothing is approximated.
//
// The level is simulated faster than real time by stepping the game's own
// update() many times per rendered frame, within a time budget.
//
// Fast mode (default) adds three shortcuts, each verified before it is trusted:
//  - several ticks per update() call (like GD itself at lower FPS); turned off if
//    GD ever advances a different number of ticks than asked;
//  - trials restart from a checkpoint taken just before the tested input instead
//    of the level start, but only after a replay from that checkpoint matched a
//    full restart exactly (player state on every tick, and the outcome);
//  - a surviving trial ends as soon as the full player state is back to exactly
//    the unmodified run's for a few ticks after the moved click.

#include "../core/Analysis.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CheckpointObject.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fwa {

struct EngineEvent {
    uint64_t frame = 0;
    uint8_t button = 1;
    bool player2 = false;
    bool down = true;
    size_t recordIndex = 0;
};

struct PlayerSnapshot {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    double vy1 = 0, vy2 = 0;
    bool operator==(PlayerSnapshot const&) const = default;
};

class MeasurementEngine : public cocos2d::CCObject {
public:
    static MeasurementEngine* get();

    struct Config {
        int maxShift = 24;
        uint64_t horizonTicks = 360;
        bool recheck = true;
        bool determinism = true;
        bool wholeClick = true;  // move a press together with its release
        bool tap = false;        // replace the click with a short tap (frame-step style)
        int tapTicks = 1;
        int budgetMs = 14;
        bool fast = true;        // batching, checkpoint restarts, early convergence exit
        std::vector<size_t> targets; // record indices to measure, chronological
    };

    /// Checks whether measurement can run on this level with the loaded analysis.
    static std::optional<std::string> checkCanMeasure(PlayLayer* pl);
    std::optional<std::string> start(PlayLayer* pl, Config cfg, std::function<void(std::string const&)> onFinished);
    void cancel(std::string const& reason, bool restartLevel = true);

    bool active() const { return m_active; }
    bool controls(GJBaseGameLayer* layer) const { return m_active && layer == m_layer; }
    bool driving() const { return m_driving; }
    bool injecting() const { return m_injecting; }
    bool resetting() const { return m_resetting; }

    // Called from hooks
    void onProcessCommands(GJBaseGameLayer* layer);
    void onPlayerDestroyed(GJBaseGameLayer* layer);
    void onLevelEnd(GJBaseGameLayer* layer);

private:
    enum class Phase { Idle, Baseline, Determinism, Search };
    enum class Side { Early, Late };

    struct Run {
        std::vector<EngineEvent> events;
        size_t next = 0;
        uint64_t endFrame = 0;
        bool died = false;
        uint64_t deathFrame = 0;
        bool levelEnded = false;
        uint64_t levelEndFrame = 0;
        bool capture = false;
        std::map<uint64_t, PlayerSnapshot> snapshots;
        std::map<uint64_t, std::pair<std::string, std::string>> modes; // frame -> (p1 mode, p2 mode)
        uint64_t lastProgress = 0;
        uint64_t startProgress = 0;
        bool started = true;
        int stallCalls = 0;
        // Movement trace around the tested input, used to tell apart timings that
        // really change the outcome from ones GD buffers into the same tick.
        uint64_t traceFrom = 1, traceTo = 0;
        std::vector<double> trace;

        // fast mode
        geode::Ref<CheckpointObject> restore;      // start from this checkpoint instead of the level start
        uint64_t restoreFrame = 0;
        bool restoreFailed = false;
        uint64_t checkpointFrame = 0;               // take a checkpoint when this tick completes (0 = never)
        geode::Ref<CheckpointObject> checkpointTaken;
        uint64_t refFrom = 0;                       // record full player state from this tick (0 = never)
        std::vector<double> ref;
        std::vector<double> const* convergeRef = nullptr; // unmodified run's state, from convergeRefFrom
        uint64_t convergeRefFrom = 0;
        uint64_t convergeFrom = 0;                  // first tick a convergence may be accepted
        int convergeStreak = 0;
        bool converged = false;
        uint64_t successBy = 0;                     // a death after this tick still counts as survived (0 = off)
    };

    enum class Fast { Untested, Verifying, On, Off };

    struct TargetState {
        size_t record = 0;      // record index
        size_t eventIndex = 0;  // index into m_events
        std::optional<size_t> pairIndex; // the press's own release (whole-click mode)
        uint64_t frame = 0;
        int maxEarly = 0, maxLate = 0;
        std::string earlyBound, lateBound;
        Side side = Side::Early;
        int d = 1;              // next distance to try on the current side
        int early = 0, late = 0;
        bool earlyFailed = false, lateFailed = false;
        // recheck queue: shifts to re-run with their expected outcome
        std::vector<std::pair<int, bool>> rechecks;
        bool earlyRecheckQueued = false, lateRecheckQueued = false;
        bool zeroDone = false;  // the original timing has been replayed as a trial
        std::map<int, std::vector<double>> traces; // surviving shift -> movement trace
        std::map<int, double> log;                 // shift -> death time in s, or -1 if it survived
        bool inconsistent = false;
        int trials = 0;
        bool skip = false;      // baseline failed etc.
        std::string skipNote;
        // fast mode
        uint64_t cpFrame = 0;   // checkpoint tick (just before the earliest tested timing)
        geode::Ref<CheckpointObject> checkpoint;
        Fast fast = Fast::Untested;
        std::vector<double> ref; // unmodified (shift 0) player state per tick from cpFrame + 1
        uint64_t refAliveUntil = 0; // the unmodified run is known to survive up to this tick
    };

    MeasurementEngine() = default;
    void tick(float dt);
    void beginRun(std::vector<EngineEvent> events, uint64_t endFrame, bool capture);
    void beginRun(Run spec);
    void launchTrial(Run spec);
    void stateInto(GJBaseGameLayer* layer, std::vector<double>& out) const;
    void finishRun();
    void onRunFinished(bool success);
    bool planNextTrial();
    void startTrial(int shift);
    void finalize(bool cancelled, std::string const& reason);
    void updateHud();
    void cleanupHud();
    std::string modeName(PlayerObject* p) const;

    bool m_active = false;
    bool m_driving = false;
    bool m_injecting = false;
    bool m_runActive = false;
    bool m_resetting = false;
    bool m_cbfTouched = false;
    bool m_skipFinalReset = false;
    bool m_cbfPrevious = false;
    std::vector<uint64_t> m_targetFrames;
    PlayLayer* m_layer = nullptr;
    Config m_cfg;
    Phase m_phase = Phase::Idle;
    std::vector<EngineEvent> m_events;   // the macro, chronological
    Run m_run;
    Run m_baseline;
    std::vector<TargetState> m_targets;
    size_t m_current = 0;                // index into m_targets
    int m_currentShift = 0;
    bool m_currentIsRecheck = false;
    bool m_expectedRecheck = false;
    bool m_currentIsZero = false;
    bool m_currentIsVerify = false;
    Run m_runSpec;                       // how the current run was started (for restarts)
    int m_batch = 8;                     // ticks per update() call in fast mode
    size_t m_fastOn = 0, m_fastOff = 0;  // targets measured from a checkpoint / from the level start
    size_t m_converged = 0;              // trials ended early by convergence
    uint64_t m_capFrame = UINT64_MAX;    // trials never run past this (baseline death)
    std::function<void(std::string const&)> m_onFinished;

    // statistics for progress / ETA
    uint64_t m_ticksSimulated = 0;
    double m_secondsSimulating = 0.0;
    size_t m_trialsRun = 0;
    std::chrono::steady_clock::time_point m_startedAt;
    geode::Ref<cocos2d::CCLabelBMFont> m_hud;
    geode::Ref<cocos2d::CCLayerColor> m_hudBg;
    std::string m_status;
};

} // namespace fwa
