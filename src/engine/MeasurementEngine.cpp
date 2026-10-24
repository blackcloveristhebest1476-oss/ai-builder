#include "MeasurementEngine.hpp"
#include "RecordEngine.hpp"
#include "../Session.hpp"
#include "../core/Util.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace fwa {

namespace {
constexpr float kStep = 1.f / 240.f;
constexpr int kStallLimit = 4000; // update() calls without the tick counter moving
} // namespace

MeasurementEngine* MeasurementEngine::get() {
    static MeasurementEngine* instance = [] {
        auto e = new MeasurementEngine();
        e->retain(); // lives for the whole session
        return e;
    }();
    return instance;
}

std::optional<std::string> MeasurementEngine::checkCanMeasure(PlayLayer* pl) {
    if (!pl) return "Open the level the macro was recorded on first. Measurement replays the macro inside the real level.";
    if (RecordEngine::get()->active()) return "Stop recording first (Record button).";
    auto const& session = Session::get();
    if (!session.hasData()) return "Import a macro first.";
    auto const& a = session.analysis();
    if (a.source.kind == SourceKind::Sample) return "Sample data is synthetic and cannot be measured. Import a real macro.";
    if (a.source.kind == SourceKind::WindowData) {
        return "Frame-window tables only contain times, not replayable inputs. Import the macro itself (GDR / GDR2) to measure.";
    }
    bool framesKnown = std::all_of(a.inputs.begin(), a.inputs.end(), [](auto const& r) { return r.frame.has_value(); });
    if (!framesKnown || a.inputs.empty()) return "This data has no frame numbers, so it cannot be replayed.";
    if (std::fabs(a.tps - 240.0) > 1e-6) {
        return "This macro was recorded at " + num(a.tps) + " ticks per second (physics bypass). In-game measurement "
               "only supports the standard 240 TPS, so this macro cannot be replayed faithfully.";
    }
    if (pl->m_isPracticeMode) {
        return "Turn off practice mode first. Practice mode restores checkpoints instead of restarting, and "
               "every measurement trial must start from the beginning of the level.";
    }
    if (pl->m_startPosObject) {
        return "Remove the start position first. Macros are replayed from the start of the level.";
    }
    return std::nullopt;
}

std::optional<std::string> MeasurementEngine::start(PlayLayer* pl, Config cfg, std::function<void(std::string const&)> onFinished) {
    if (m_active) return "A measurement is already running.";
    if (auto err = checkCanMeasure(pl)) return err;
    if (cfg.targets.empty()) return "No inputs selected for measurement.";

    auto const& a = Session::get().analysis();
    m_layer = pl;
    m_cfg = std::move(cfg);
    m_onFinished = std::move(onFinished);
    m_events.clear();
    m_events.reserve(a.inputs.size());
    for (auto const& r : a.inputs) {
        m_events.push_back({*r.frame, r.button, r.player2, r.down, r.index});
    }
    std::stable_sort(m_events.begin(), m_events.end(), [](auto const& x, auto const& y) { return x.frame < y.frame; });
    std::vector<size_t> eventOfRecord(a.inputs.size(), 0);
    for (size_t i = 0; i < m_events.size(); i++) eventOfRecord[m_events[i].recordIndex] = i;

    m_targets.clear();
    uint64_t baselineEnd = 0;
    for (size_t rec : m_cfg.targets) {
        if (rec >= a.inputs.size()) continue;
        TargetState t;
        t.record = rec;
        t.eventIndex = eventOfRecord[rec];
        auto const& ev = m_events[t.eventIndex];
        t.frame = ev.frame;

        // Neighbouring events on the same button + player bound the search, so a
        // shifted input never swaps order with its own press/release partner.
        std::optional<uint64_t> prevFrame, nextFrame;
        for (size_t j = t.eventIndex; j-- > 0;) {
            if (m_events[j].player2 == ev.player2 && m_events[j].button == ev.button) {
                prevFrame = m_events[j].frame;
                break;
            }
        }
        for (size_t j = t.eventIndex + 1; j < m_events.size(); j++) {
            if (m_events[j].player2 == ev.player2 && m_events[j].button == ev.button) {
                nextFrame = m_events[j].frame;
                break;
            }
        }
        int64_t room = prevFrame ? static_cast<int64_t>(ev.frame) - static_cast<int64_t>(*prevFrame) - 1 : static_cast<int64_t>(ev.frame);
        room = std::max<int64_t>(room, 0);
        if (room < m_cfg.maxShift) {
            t.maxEarly = static_cast<int>(room);
            t.earlyBound = prevFrame ? "reached the previous event on this button" : "reached the start of the level";
        } else {
            t.maxEarly = m_cfg.maxShift;
            t.earlyBound = "reached the maximum shift setting";
        }
        if (nextFrame) {
            int64_t lateRoom = std::max<int64_t>(static_cast<int64_t>(*nextFrame) - static_cast<int64_t>(ev.frame) - 1, 0);
            if (lateRoom < m_cfg.maxShift) {
                t.maxLate = static_cast<int>(lateRoom);
                t.lateBound = "reached the next event on this button";
            } else {
                t.maxLate = m_cfg.maxShift;
                t.lateBound = "reached the maximum shift setting";
            }
        } else {
            t.maxLate = m_cfg.maxShift;
            t.lateBound = "reached the maximum shift setting";
        }
        baselineEnd = std::max(baselineEnd, t.frame + static_cast<uint64_t>(t.maxLate) + m_cfg.horizonTicks);
        m_targets.push_back(std::move(t));
    }
    if (m_targets.empty()) return "No valid inputs selected for measurement.";
    m_targetFrames.clear();
    for (auto const& t : m_targets) m_targetFrames.push_back(t.frame);
    std::sort(m_targetFrames.begin(), m_targetFrames.end());

    m_capFrame = UINT64_MAX;
    m_current = 0;
    m_ticksSimulated = 0;
    m_secondsSimulating = 0.0;
    m_trialsRun = 0;
    m_startedAt = std::chrono::steady_clock::now();
    m_active = true;
    m_phase = Phase::Baseline;
    m_status = "Replaying the unmodified macro (baseline)...";

    auto& meas = Session::get().analysisMut().measurement;
    meas.determinismChecked = false;
    meas.determinismPassed = false;

    // Click Between Frames splits physics steps around real-time inputs; like GDR bots, switch it
    // off (its own "soft-toggle" setting) while replaying, and restore it afterwards.
    m_cbfTouched = false;
    if (auto cbf = Loader::get()->getLoadedMod("syzzi.click_between_frames")) {
        if (cbf->hasSetting("soft-toggle")) {
            m_cbfPrevious = cbf->getSettingValue<bool>("soft-toggle");
            cbf->setSettingValue<bool>("soft-toggle", true);
            m_cbfTouched = true;
        }
    }

    beginRun(m_events, baselineEnd, true);
    CCDirector::get()->getScheduler()->scheduleSelector(
        schedule_selector(MeasurementEngine::tick), this, 0.f, false);
    updateHud();
    log::info("Frame Window Analyzer: measuring {} input(s), baseline until frame {}", m_targets.size(), baselineEnd);
    return std::nullopt;
}

void MeasurementEngine::cancel(std::string const& reason, bool restartLevel) {
    if (!m_active) return;
    if (!restartLevel) m_skipFinalReset = true;
    finalize(true, reason);
    m_skipFinalReset = false;
}

void MeasurementEngine::beginRun(std::vector<EngineEvent> events, uint64_t endFrame, bool capture) {
    m_run = Run{};
    m_run.events = std::move(events);
    m_run.endFrame = std::min(endFrame, m_capFrame);
    m_run.capture = capture;
    m_resetting = true;
    m_layer->resetLevel();
    m_resetting = false;
    if (auto fmod = FMODAudioEngine::get()) fmod->pauseAllMusic(true);
    // resetLevel() should put the tick counter back to 0. If it ever still shows the
    // previous run's value, wait until it drops before injecting inputs or judging the run.
    m_run.startProgress = m_layer->m_gameState.m_currentProgress;
    m_run.started = m_run.startProgress <= 1;
    m_run.lastProgress = m_run.startProgress;
    m_runActive = true;
}

std::string MeasurementEngine::modeName(PlayerObject* p) const {
    if (!p) return "";
    std::string m = "Cube";
    if (p->m_isShip) m = "Ship";
    else if (p->m_isBird) m = "UFO";
    else if (p->m_isBall) m = "Ball";
    else if (p->m_isDart) m = "Wave";
    else if (p->m_isRobot) m = "Robot";
    else if (p->m_isSpider) m = "Spider";
    else if (p->m_isSwing) m = "Swing";
    if (p->m_vehicleSize < 1.f) m += " (mini)";
    return m;
}

void MeasurementEngine::onProcessCommands(GJBaseGameLayer* layer) {
    if (!m_runActive || layer != m_layer) return;
    uint64_t frame = layer->m_gameState.m_currentProgress;
    if (!m_run.started) {
        if (frame >= m_run.startProgress) return;
        m_run.started = true;
    }
    m_injecting = true;
    while (m_run.next < m_run.events.size() && m_run.events[m_run.next].frame <= frame) {
        auto const& ev = m_run.events[m_run.next];
        layer->handleButton(ev.down, ev.button, !ev.player2);
        m_run.next++;
    }
    m_injecting = false;
    if (m_run.capture) {
        PlayerSnapshot s;
        bool wanted = std::binary_search(m_targetFrames.begin(), m_targetFrames.end(), frame);
        if (wanted && !m_run.snapshots.contains(frame)) {
            auto p1 = layer->m_player1;
            auto p2 = layer->m_player2;
            if (p1) {
                s.x1 = p1->m_position.x;
                s.y1 = p1->m_position.y;
                s.vy1 = p1->m_yVelocity;
            }
            if (p2) {
                s.x2 = p2->m_position.x;
                s.y2 = p2->m_position.y;
                s.vy2 = p2->m_yVelocity;
            }
            m_run.snapshots[frame] = s;
            std::string dual = layer->m_gameState.m_isDualMode ? ", dual" : "";
            m_run.modes[frame] = {modeName(p1) + dual, modeName(p2) + dual};
        }
    }
}

void MeasurementEngine::onPlayerDestroyed(GJBaseGameLayer* layer) {
    if (!m_runActive || layer != m_layer || m_run.died || !m_run.started) return;
    m_run.died = true;
    m_run.deathFrame = layer->m_gameState.m_currentProgress;
}

void MeasurementEngine::onLevelEnd(GJBaseGameLayer* layer) {
    if (!m_runActive || layer != m_layer || m_run.levelEnded || !m_run.started) return;
    m_run.levelEnded = true;
    m_run.levelEndFrame = layer->m_gameState.m_currentProgress;
}

void MeasurementEngine::tick(float) {
    if (!m_active) return;
    if (PlayLayer::get() != m_layer) {
        m_layer = nullptr; // the level is gone; do not touch it again
        finalize(true, "The level was closed during measurement.");
        return;
    }
    auto t0 = std::chrono::steady_clock::now();
    auto budget = std::chrono::milliseconds(std::max(1, m_cfg.budgetMs));
    while (m_active && std::chrono::steady_clock::now() - t0 < budget) {
        if (!m_runActive) {
            if (m_phase != Phase::Search) break; // phase transitions start their own runs
            if (!planNextTrial()) {
                finalize(false, "");
                return;
            }
            continue;
        }
        m_driving = true;
        m_layer->update(kStep);
        m_driving = false;

        uint64_t progress = m_layer->m_gameState.m_currentProgress;
        if (!m_run.started && progress < m_run.startProgress) m_run.started = true;
        if (progress != m_run.lastProgress) {
            if (progress > m_run.lastProgress) m_ticksSimulated += progress - m_run.lastProgress;
            m_run.lastProgress = progress;
            m_run.stallCalls = 0;
        } else if (++m_run.stallCalls > kStallLimit) {
            finalize(true, "The game stopped advancing during a replay (is the level paused or frozen?).");
            return;
        }
        if (!m_run.started) continue;

        if (m_run.died) onRunFinished(false);
        else if (m_run.levelEnded) onRunFinished(true);
        else if (progress >= m_run.endFrame) onRunFinished(true);
    }
    m_secondsSimulating += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (m_active) updateHud();
}

void MeasurementEngine::onRunFinished(bool success) {
    m_runActive = false;
    switch (m_phase) {
        case Phase::Baseline: {
            m_baseline = std::move(m_run);
            m_run = Run{};
            auto& analysis = Session::get().analysisMut();
            // Game modes seen at each input.
            for (auto& t : m_targets) {
                auto it = m_baseline.modes.find(t.frame);
                if (it != m_baseline.modes.end()) {
                    auto& rec = analysis.inputs[t.record];
                    rec.gameMode = rec.player2 ? it->second.second : it->second.first;
                }
            }
            if (!success && m_baseline.died) {
                uint64_t death = m_baseline.deathFrame;
                m_capFrame = death > 0 ? death - 1 : 0;
                size_t skipped = 0;
                for (auto& t : m_targets) {
                    if (t.frame + m_cfg.horizonTicks > death) {
                        t.skip = true;
                        t.skipNote = "The unmodified macro dies at frame " + std::to_string(death) + " (" +
                            num(death / 240.0, 2) + " s), before this input's survival horizon ends, so its window cannot be measured.";
                        skipped++;
                    } else if (t.frame + static_cast<uint64_t>(t.maxLate) + m_cfg.horizonTicks > m_capFrame) {
                        int64_t room = static_cast<int64_t>(m_capFrame) - static_cast<int64_t>(m_cfg.horizonTicks) - static_cast<int64_t>(t.frame);
                        t.maxLate = static_cast<int>(std::max<int64_t>(0, room));
                        t.lateBound = "limited by the baseline death later in the macro";
                    }
                }
                log::warn("Frame Window Analyzer: baseline died at frame {}, {} input(s) cannot be measured", death, skipped);
                if (skipped == m_targets.size()) {
                    finalize(true, "The unmodified macro dies at frame " + std::to_string(death) + " (" + num(death / 240.0, 2) +
                        " s) before any selected input could be checked. The macro does not replay correctly here: check that "
                        "this is the right level, that no other bot/physics mods are active, and that the macro was recorded at 240 TPS.");
                    return;
                }
            }
            if (m_baseline.levelEnded) {
                for (auto& t : m_targets) {
                    if (!t.skip && t.frame >= m_baseline.levelEndFrame) {
                        t.skip = true;
                        t.skipNote = "This input happens after the level is already completed, so it has no effect.";
                    }
                }
            }
            if (m_cfg.determinism) {
                m_phase = Phase::Determinism;
                m_status = "Replaying the macro a second time (determinism check)...";
                beginRun(m_events, m_baseline.endFrame, true);
            } else {
                m_phase = Phase::Search;
                m_status = "Measuring...";
            }
            return;
        }
        case Phase::Determinism: {
            auto& meas = Session::get().analysisMut().measurement;
            meas.determinismChecked = true;
            // The repeat run is capped just before the baseline's death (if any), so it should
            // reach that cap alive; otherwise both runs must end the same way.
            bool same = m_baseline.died
                ? (!m_run.died && m_run.lastProgress >= m_capFrame) || (m_run.died && m_run.deathFrame == m_baseline.deathFrame)
                : (!m_run.died && m_run.levelEnded == m_baseline.levelEnded);
            uint64_t firstDiff = 0;
            for (auto const& [frame, snap] : m_baseline.snapshots) {
                auto it = m_run.snapshots.find(frame);
                if (it == m_run.snapshots.end() || !(it->second == snap)) {
                    same = false;
                    firstDiff = frame;
                    break;
                }
            }
            meas.determinismPassed = same;
            if (!same) {
                finalize(true, "Determinism check failed: two identical replays of the macro diverged" +
                    (firstDiff ? " at frame " + std::to_string(firstDiff) : std::string()) +
                    ". The level (random triggers?) or another mod makes replays non-repeatable, so frame windows "
                    "cannot be measured reliably here.");
                return;
            }
            m_phase = Phase::Search;
            m_status = "Measuring...";
            return;
        }
        case Phase::Search: {
            auto& t = m_targets[m_current];
            t.trials++;
            if (m_currentIsRecheck) {
                if (success != m_expectedRecheck) t.inconsistent = true;
            } else if (t.side == Side::Early) {
                if (success) {
                    t.early = t.d;
                    t.d++;
                } else {
                    t.earlyFailed = true;
                }
            } else {
                if (success) {
                    t.late = t.d;
                    t.d++;
                } else {
                    t.lateFailed = true;
                }
            }
            return;
        }
        case Phase::Idle: return;
    }
}

bool MeasurementEngine::planNextTrial() {
    while (m_current < m_targets.size()) {
        auto& t = m_targets[m_current];
        if (t.skip) {
            m_current++;
            continue;
        }
        bool early = t.side == Side::Early;
        bool failed = early ? t.earlyFailed : t.lateFailed;
        bool& queued = early ? t.earlyRecheckQueued : t.lateRecheckQueued;
        int maxD = early ? t.maxEarly : t.maxLate;
        int sign = early ? -1 : 1;
        if (!queued && !failed && t.d <= maxD) {
            m_currentIsRecheck = false;
            startTrial(sign * t.d);
            return true;
        }
        // This side is finished. Optionally re-run its edge timings once.
        if (!queued) {
            queued = true;
            if (m_cfg.recheck) {
                int found = early ? t.early : t.late;
                if (found > 0) t.rechecks.push_back({sign * found, true});
                if (failed) t.rechecks.push_back({sign * (found + 1), false});
            }
        }
        if (!t.rechecks.empty()) {
            auto [shift, expected] = t.rechecks.back();
            t.rechecks.pop_back();
            m_currentIsRecheck = true;
            m_expectedRecheck = expected;
            startTrial(shift);
            return true;
        }
        if (early) {
            t.side = Side::Late;
            t.d = 1;
            continue;
        }
        m_current++;
    }
    return false;
}

void MeasurementEngine::startTrial(int shift) {
    auto const& t = m_targets[m_current];
    auto events = m_events;
    int64_t moved = static_cast<int64_t>(t.frame) + shift;
    events[t.eventIndex].frame = static_cast<uint64_t>(std::max<int64_t>(0, moved));
    std::stable_sort(events.begin(), events.end(), [](auto const& x, auto const& y) { return x.frame < y.frame; });
    uint64_t end = std::max<uint64_t>(t.frame, static_cast<uint64_t>(std::max<int64_t>(0, moved))) + m_cfg.horizonTicks;
    m_currentShift = shift;
    m_trialsRun++;
    beginRun(std::move(events), end, false);
}

void MeasurementEngine::finalize(bool cancelled, std::string const& reason) {
    auto& session = Session::get();
    auto& analysis = session.analysisMut();
    size_t verified = 0, lower = 0, inconsistent = 0, baselineFailed = 0, incomplete = 0;
    for (size_t i = 0; i < m_targets.size(); i++) {
        auto const& t = m_targets[i];
        if (t.record >= analysis.inputs.size()) continue;
        auto& rec = analysis.inputs[t.record];
        bool complete = m_phase == Phase::Search && i < m_current;
        if (t.skip) {
            rec.window = {};
            rec.window.status = WindowStatus::BaselineFailed;
            rec.window.note = t.skipNote;
            baselineFailed++;
            continue;
        }
        if (!complete) {
            if (cancelled && rec.window.status == WindowStatus::Unknown) {
                rec.window.note = "Not measured (measurement stopped before this input).";
            }
            incomplete++;
            continue;
        }
        WindowResult w;
        w.earlyTicks = t.early;
        w.lateTicks = t.late;
        w.earlyLimited = !t.earlyFailed;
        w.lateLimited = !t.lateFailed;
        if (w.earlyLimited) w.earlyLimitReason = t.earlyBound;
        if (w.lateLimited) w.lateLimitReason = t.lateBound;
        w.trials = t.trials;
        if (t.inconsistent) {
            w.status = WindowStatus::Inconsistent;
            w.note = "Re-running the edge timings gave a different result; the replay is not fully repeatable here.";
            inconsistent++;
        } else if (w.earlyLimited || w.lateLimited) {
            w.status = WindowStatus::LowerBound;
            std::string why;
            if (w.earlyLimited) why += "early side " + t.earlyBound;
            if (w.lateLimited) why += std::string(why.empty() ? "" : "; ") + "late side " + t.lateBound;
            w.note = "Every tested timing survived until the search stopped (" + why + "), so the real window is at least this large.";
            lower++;
        } else {
            w.status = WindowStatus::Verified;
            w.note = "Measured by replaying the level with this input moved; survival horizon " +
                num(m_cfg.horizonTicks / 240.0, 2) + " s.";
            verified++;
        }
        rec.window = w;
    }

    auto& meas = analysis.measurement;
    meas.performed = verified + lower + inconsistent > 0 || meas.performed;
    meas.finishedAt = nowTimestamp(false);
    meas.horizonSeconds = m_cfg.horizonTicks / 240.0;
    meas.maxShiftTicks = m_cfg.maxShift;
    meas.recheckBorders = m_cfg.recheck;
    if (m_layer && m_layer->m_level) {
        meas.levelName = m_layer->m_level->m_levelName;
        meas.levelId = m_layer->m_level->m_levelID.value();
    }
    meas.notes = cancelled ? ("Stopped: " + reason) : "Completed";

    m_active = false;
    m_runActive = false;
    m_phase = Phase::Idle;
    CCDirector::get()->getScheduler()->unscheduleSelector(schedule_selector(MeasurementEngine::tick), this);
    cleanupHud();
    if (m_cbfTouched) {
        if (auto cbf = Loader::get()->getLoadedMod("syzzi.click_between_frames")) {
            cbf->setSettingValue<bool>("soft-toggle", m_cbfPrevious);
        }
        m_cbfTouched = false;
    }
    if (m_layer && !m_skipFinalReset) m_layer->resetLevel(); // engine is inactive now, so this is a normal restart
    m_layer = nullptr;

    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startedAt).count();
    std::string summary = (cancelled ? "Measurement stopped. " + reason + "\n\n" : "Measurement finished in " + formatDuration(elapsed) + ".\n\n") +
        std::to_string(verified) + " verified, " + std::to_string(lower) + " lower bound, " +
        std::to_string(inconsistent) + " inconsistent, " + std::to_string(baselineFailed) + " not measurable" +
        (incomplete ? ", " + std::to_string(incomplete) + " not reached" : "") + " (" + std::to_string(m_trialsRun) + " trials).";
    session.setMessage(summary);
    session.touch();
    if (Settings::load().autoSave && (verified + lower + inconsistent) > 0) session.autoSave("measurement");
    log::info("Frame Window Analyzer: {}", summary);

    auto cb = std::move(m_onFinished);
    m_onFinished = nullptr;
    if (cb) cb(summary);
}

void MeasurementEngine::updateHud() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    auto winSize = CCDirector::get()->getWinSize();
    if (!m_hud) {
        m_hudBg = CCLayerColor::create({0, 0, 0, 170}, winSize.width, 34.f);
        m_hudBg->setPosition({0.f, winSize.height - 34.f});
        m_hudBg->setZOrder(10000);
        m_hudBg->setID("measure-hud"_spr);
        scene->addChild(m_hudBg);
        m_hud = CCLabelBMFont::create("", "bigFont.fnt");
        m_hud->setScale(.32f);
        m_hud->setPosition({winSize.width / 2, 17.f});
        m_hudBg->addChild(m_hud);
    }
    size_t total = m_targets.size();
    size_t done = std::min(m_current, total);
    double rate = m_secondsSimulating > 0 ? m_ticksSimulated / m_secondsSimulating : 0.0;
    std::string text;
    if (m_phase == Phase::Search && total > 0) {
        double avgTrials = done > 0 ? static_cast<double>(m_trialsRun) / static_cast<double>(done) : 10.0;
        double avgTicks = m_trialsRun > 0 ? static_cast<double>(m_ticksSimulated) / static_cast<double>(m_trialsRun + 1) : 0.0;
        double eta = rate > 0 ? (static_cast<double>(total - done) * avgTrials * avgTicks) / rate : 0.0;
        auto const& t = m_targets[std::min(m_current, total - 1)];
        text = "Measuring input " + std::to_string(done + 1 > total ? total : done + 1) + "/" + std::to_string(total) +
            " (#" + std::to_string(t.record + 1) + ", shift " + (m_currentShift > 0 ? "+" : "") + std::to_string(m_currentShift) + ")  -  " +
            std::to_string(static_cast<int>(100.0 * done / total)) + "%  -  ETA ~" + formatDuration(eta) +
            "  -  " + num(rate / 240.0, 1) + "x speed  -  Esc to stop";
    } else {
        text = m_status + "  -  " + num(rate / 240.0, 1) + "x speed  -  Esc to stop";
    }
    m_hud->setString(text.c_str());
    m_hud->limitLabelWidth(winSize.width - 20.f, .32f, .15f);
}

void MeasurementEngine::cleanupHud() {
    if (m_hudBg) m_hudBg->removeFromParent();
    m_hudBg = nullptr;
    m_hud = nullptr;
}

} // namespace fwa
