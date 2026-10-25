#include "PlaybackEngine.hpp"
#include "MeasurementEngine.hpp"
#include "../Session.hpp"
#include "../core/Util.hpp"
#include "../ui/Theme.hpp"
#include "../Sounds.hpp"

#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace fwa {

namespace {

std::optional<double> markedWindow(InputRecord const& r, Settings const& s, double tps, MarkerMode mode) {
    if (!r.down || !isCounted(r, s.count())) return std::nullopt;
    if (!isMeasured(r.window.status) && r.window.status != WindowStatus::UserSupplied &&
        r.window.status != WindowStatus::Estimated) {
        return std::nullopt;
    }
    if (r.window.status == WindowStatus::Inconsistent) return std::nullopt;
    auto w = windowFrames(r, s.windowFps, tps, true);
    if (!w) return std::nullopt;
    switch (mode) {
        case MarkerMode::FramePerfect: return *w <= s.framePerfectThreshold + 1e-9 ? w : std::nullopt;
        case MarkerMode::Tight: return *w <= s.tightThreshold + 1e-9 ? w : std::nullopt;
        case MarkerMode::AllMeasured: return w;
    }
    return std::nullopt;
}

} // namespace

PlaybackEngine* PlaybackEngine::get() {
    static PlaybackEngine* instance = [] {
        auto e = new PlaybackEngine();
        e->retain();
        return e;
    }();
    return instance;
}

size_t PlaybackEngine::countMarked(MarkerMode mode) {
    auto const& a = Session::get().analysis();
    auto s = Settings::load();
    size_t n = 0;
    for (auto const& r : a.inputs) {
        if (markedWindow(r, s, a.tps, mode)) n++;
    }
    return n;
}

std::optional<std::string> PlaybackEngine::start(PlayLayer* pl, MarkerMode mode, std::optional<ShiftOverride> shifted) {
    if (m_active) return "Playback is already running.";
    if (MeasurementEngine::get()->active()) return "A measurement is running.";
    if (auto err = MeasurementEngine::checkCanMeasure(pl)) return err;

    auto const& a = Session::get().analysis();
    auto s = Settings::load();
    auto stats = s.stats();
    m_edges = stats.edges;
    m_rangeLabels = bucketLabels(m_edges, stats.integerLabels);
    m_counts.assign(m_edges.size(), 0);
    std::vector<std::string> rangeSounds;
    for (double e : m_edges) rangeSounds.push_back(sounds::enabled() ? sounds::soundForEdge(e) : std::string());
    m_events.clear();
    m_marked = 0;
    for (auto const& r : a.inputs) {
        Event ev;
        ev.frame = *r.frame;
        ev.button = r.button;
        ev.player2 = r.player2;
        ev.down = r.down;
        ev.markFrames = markedWindow(r, s, a.tps, mode);
        ev.lowerBound = r.window.status == WindowStatus::LowerBound;
        if (ev.markFrames && isMeasured(r.window.status)) {
            ev.split = "-" + std::to_string(r.window.earlyTicks) + (r.window.earlyLimited ? "+" : "") + " / +" +
                std::to_string(r.window.lateTicks) + (r.window.lateLimited ? "+" : "");
        }
        if (ev.markFrames) {
            ev.color = ui::windowColor(*ev.markFrames, s.stats().edges);
            m_marked++;
        }
        // Counter + sounds: every counted press that has a window, whatever the marker mode.
        if (r.down && isCounted(r, s.count()) && r.window.status != WindowStatus::Inconsistent) {
            if (auto w = windowFrames(r, s.windowFps, a.tps, true)) {
                ev.bucket = bucketIndex(*w, m_edges);
                ev.sound = rangeSounds[*ev.bucket];
            }
        }
        m_events.push_back(ev);
    }

    if (shifted && shifted->record < m_events.size() && a.inputs[shifted->record].down) {
        // Move this one click exactly like a measurement trial would (Shift Mode setting).
        auto& press = m_events[shifted->record];
        int64_t pressFrame = std::max<int64_t>(0, static_cast<int64_t>(press.frame) + shifted->shift);
        std::optional<size_t> release;
        for (size_t j = shifted->record + 1; j < m_events.size(); j++) {
            if (m_events[j].player2 == press.player2 && m_events[j].button == press.button) {
                if (!m_events[j].down) release = j;
                break;
            }
        }
        if (s.tap) {
            uint64_t rel = static_cast<uint64_t>(pressFrame) + static_cast<uint64_t>(std::max(1, s.tapTicks));
            if (release) {
                m_events[*release].frame = rel;
            } else {
                Event r = press;
                r.down = false;
                r.markFrames.reset();
                r.frame = rel;
                m_events.push_back(r);
            }
        } else if (s.wholeClick && release) {
            m_events[*release].frame = static_cast<uint64_t>(
                std::max<int64_t>(0, static_cast<int64_t>(m_events[*release].frame) + shifted->shift));
        }
        auto& p = m_events[shifted->record];
        p.frame = static_cast<uint64_t>(pressFrame);
        // Always mark the moved click so it is easy to spot.
        auto w = windowFrames(a.inputs[shifted->record], s.windowFps, a.tps, true);
        p.markFrames = w.value_or(0.0);
        p.color = w ? ui::windowColor(*w, s.stats().edges) : ccColor3B{255, 255, 255};
        p.split = "shifted " + std::string(shifted->shift > 0 ? "+" : "") + std::to_string(shifted->shift) + " tick(s)" +
            (s.tap ? " (tap)" : "");
        if (!w) p.markFrames.reset();
        m_shiftNote = "Input #" + std::to_string(shifted->record + 1) + " moved " + (shifted->shift > 0 ? "+" : "") +
            std::to_string(shifted->shift) + " tick(s)";
        if (!p.markFrames) {
            p.markFrames = 0.0;
            p.split = "shifted " + std::to_string(shifted->shift);
        }
    } else {
        m_shiftNote.clear();
    }
    std::stable_sort(m_events.begin(), m_events.end(), [](auto const& x, auto const& y) { return x.frame < y.frame; });

    m_markerSeconds = static_cast<float>(Mod::get()->getSettingValue<double>("playback-marker-seconds"));
    m_layer = pl;
    m_active = true;
    m_finishing = false;
    m_next = 0;
    clearMarkers();
    m_resetting = true;
    pl->resetLevel();
    m_resetting = false;
    m_next = 0;
    showHud();
    log::info("Frame Window Analyzer: playback started, {} marked input(s)", m_marked);
    return std::nullopt;
}

void PlaybackEngine::stop(std::string const& reason, bool restartLevel) {
    if (!m_active) return;
    m_active = false;
    m_injecting = false;
    hideHud();
    if (m_counter) {
        // Leave the totals readable for a few seconds, then clear them away.
        m_counter->runAction(CCSequence::create(CCDelayTime::create(6.f), CCFadeOut::create(.5f), CCRemoveSelf::create(), nullptr));
    }
    auto layer = m_layer;
    m_layer = nullptr;
    if (!reason.empty()) {
        Notification::create(reason, NotificationIcon::Info, 4.f)->show();
    }
    if (restartLevel && layer) {
        // Never restart from inside GD's own death/collision code: wait a frame.
        Loader::get()->queueInMainThread([layer] {
            if (PlayLayer::get() == layer && !layer->m_isPaused) layer->resetLevel();
        });
    }
    // Markers stay until the next reset so the last ones can still be seen.
}

void PlaybackEngine::clearMarkers() {
    for (auto& m : m_markers) {
        if (m) m->removeFromParent();
    }
    m_markers.clear();
}

void PlaybackEngine::onLevelReset(GJBaseGameLayer* layer) {
    if (layer != m_layer) return;
    m_next = 0;
    clearMarkers();
    std::fill(m_counts.begin(), m_counts.end(), 0);
    updateCounter();
}

void PlaybackEngine::onProcessCommands(GJBaseGameLayer* layer) {
    if (!controls(layer)) return;
    uint64_t frame = layer->m_gameState.m_currentProgress;
    m_injecting = true;
    while (m_next < m_events.size() && m_events[m_next].frame <= frame) {
        auto const& ev = m_events[m_next];
        layer->handleButton(ev.down, ev.button, !ev.player2);
        if (ev.bucket && *ev.bucket < m_counts.size()) {
            m_counts[*ev.bucket]++;
            updateCounter();
            sounds::play(ev.sound);
        }
        if (ev.markFrames) {
            bool twoPlayer = layer->m_levelSettings && layer->m_levelSettings->m_twoPlayerMode;
            if (ev.player2 && twoPlayer) {
                spawnMarker(layer, layer->m_player2, ev);
            } else {
                spawnMarker(layer, layer->m_player1, ev);
                // In dual mode without two-player controls the click moves both players.
                if (layer->m_gameState.m_isDualMode && !twoPlayer) spawnMarker(layer, layer->m_player2, ev);
            }
        }
        m_next++;
    }
    m_injecting = false;
}

void PlaybackEngine::spawnMarker(GJBaseGameLayer* layer, PlayerObject* player, Event const& ev) {
    if (!player || !layer->m_objectLayer) return;
    CCPoint world = player->getParent() ? player->getParent()->convertToWorldSpace(player->getPosition())
                                        : player->getPosition();
    CCPoint local = layer->m_objectLayer->convertToNodeSpace(world);

    auto node = CCNode::create();
    node->setPosition(local);
    node->setID("frame-marker"_spr);

    auto ring = CCDrawNode::create();
    // dark outline, then the colored ring (color = frame-window range color)
    ring->drawCircle({0.f, 0.f}, 18.f, {0.f, 0.f, 0.f, 0.f}, 4.5f, {0.f, 0.f, 0.f, .85f}, 48);
    ring->drawCircle({0.f, 0.f}, 18.f, {0.f, 0.f, 0.f, 0.f}, 2.6f, ui::toColor4F(ev.color, 1.f), 48);
    node->addChild(ring);

    auto text = (ev.lowerBound ? ">=" : "") + num(*ev.markFrames, 2);
    auto shadow = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    shadow->setScale(.62f);
    shadow->setColor({0, 0, 0});
    shadow->setOpacity(200);
    shadow->setAnchorPoint({1.f, .5f});
    shadow->setPosition({-19.f, -9.f});
    node->addChild(shadow);
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(.6f);
    label->setColor(ev.color);
    label->setAnchorPoint({1.f, .5f});
    label->setPosition({-20.f, -8.f});
    node->addChild(label);

    if (!ev.split.empty()) {
        // where the window comes from: ticks of room early / late
        auto split = CCLabelBMFont::create(ev.split.c_str(), "chatFont.fnt");
        split->setScale(.55f);
        split->setColor(ev.color);
        split->setPosition({0.f, 27.f});
        node->addChild(split);
    }

    layer->m_objectLayer->addChild(node, 9999);

    node->setScale(.4f);
    node->runAction(CCEaseBackOut::create(CCScaleTo::create(.18f, 1.f)));
    if (m_markerSeconds > 0.f) {
        node->runAction(CCSequence::create(CCDelayTime::create(m_markerSeconds), CCRemoveSelf::create(), nullptr));
    }
    m_markers.push_back(node);
    if (m_markers.size() > 400) {
        if (m_markers.front()) m_markers.front()->removeFromParent();
        m_markers.erase(m_markers.begin());
    }
}

void PlaybackEngine::onPlayerDestroyed(GJBaseGameLayer* layer) {
    if (!controls(layer) || m_finishing) return;
    m_finishing = true;
    double t = layer->m_gameState.m_currentProgress / 240.0;
    stop("Playback: the player died at " + num(t, 2) + " s. The macro does not replay correctly here.");
}

void PlaybackEngine::onLevelEnd(GJBaseGameLayer* layer) {
    if (!controls(layer) || m_finishing) return;
    m_finishing = true;
    stop("Playback finished (" + std::to_string(m_marked) + " marked input(s)). Not counted as a completion.");
}

void PlaybackEngine::showHud() {
    hideHud();
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    auto winSize = CCDirector::get()->getWinSize();
    auto label = CCLabelBMFont::create(
        (m_shiftNote.empty()
            ? fmt::format("Playback - {} marked input(s) - your inputs are ignored - Esc to stop", m_marked)
            : fmt::format("Watching: {} - your inputs are ignored - Esc to stop", m_shiftNote)).c_str(), "bigFont.fnt");
    label->setScale(.3f);
    label->setOpacity(190);
    label->setPosition({winSize.width / 2, winSize.height - 10.f});
    label->setID("playback-hud"_spr);
    scene->addChild(label, 10000);
    m_hud = label;
    buildCounter();
}

void PlaybackEngine::buildCounter() {
    if (m_counter) m_counter->removeFromParent();
    m_counter = nullptr;
    m_counterLabels.clear();
    auto where = Mod::get()->getSettingValue<std::string>("playback-counter");
    if (where == "Hidden" || m_edges.empty()) return;
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    auto winSize = CCDirector::get()->getWinSize();
    bool right = where == "Top right";
    float scale = static_cast<float>(std::clamp(Mod::get()->getSettingValue<double>("playback-counter-scale"), .2, 2.0));
    float rowH = 34.f * scale;
    auto node = CCNodeRGBA::create();
    node->setCascadeOpacityEnabled(true); // so the fade-out after playback reaches the labels
    node->setID("playback-counter"_spr);
    size_t n = m_edges.size();
    m_counterLabels.assign(n, nullptr);
    for (size_t k = 0; k < n; k++) {
        size_t bucket = n - 1 - k; // widest range on top
        auto color = ui::bucketColor(bucket, n);
        float y = -rowH * static_cast<float>(k);
        auto name = CCLabelBMFont::create((m_rangeLabels[bucket] + ":").c_str(), "bigFont.fnt");
        name->setScale(scale);
        name->setColor(color);
        name->setAnchorPoint({0.f, .5f});
        name->setPosition({0.f, y});
        node->addChild(name);
        auto count = CCLabelBMFont::create("0", "bigFont.fnt");
        count->setScale(scale);
        count->setColor(color);
        count->setAnchorPoint({0.f, .5f});
        count->setPosition({130.f * scale, y});
        node->addChild(count);
        m_counterLabels[bucket] = count;
    }
    float width = 175.f * scale;
    node->setPosition({right ? winSize.width - width - 6.f : 8.f, winSize.height - 22.f - rowH / 2});
    scene->addChild(node, 10000);
    m_counter = node;
    updateCounter();
}

void PlaybackEngine::updateCounter() {
    for (size_t i = 0; i < m_counterLabels.size() && i < m_counts.size(); i++) {
        if (m_counterLabels[i]) m_counterLabels[i]->setString(std::to_string(m_counts[i]).c_str());
    }
}

void PlaybackEngine::hideHud() {
    if (m_hud) m_hud->removeFromParent();
    m_hud = nullptr;
    // The counter stays on screen after playback ends so the totals can be read;
    // it is removed when the next playback starts or the level is left.
}

} // namespace fwa
