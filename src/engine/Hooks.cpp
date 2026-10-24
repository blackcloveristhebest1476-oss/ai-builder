// Game hooks used by the measurement engine and the playback engine. Every
// hook is a pass-through unless one of them is running on that exact layer.

#include "MeasurementEngine.hpp"
#include "PlaybackEngine.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;
using fwa::MeasurementEngine;
using fwa::PlaybackEngine;

class $modify(FWABaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        // Run before other mods' input hooks so stray user/autoclicker input is dropped first.
        (void) self.setHookPriority("GJBaseGameLayer::handleButton", Priority::Early);
    }

    void update(float dt) {
        auto engine = MeasurementEngine::get();
        // While measuring, the engine steps the game itself; GD's own frame update is frozen.
        if (engine->controls(this) && !engine->driving()) return;
        GJBaseGameLayer::update(dt);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        auto engine = MeasurementEngine::get();
        if (engine->controls(this)) engine->onProcessCommands(this);
        auto playback = PlaybackEngine::get();
        if (playback->controls(this)) playback->onProcessCommands(this);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        // Only the replayed inputs reach the game during a measurement or playback.
        auto engine = MeasurementEngine::get();
        if (engine->controls(this) && !engine->injecting()) return;
        auto playback = PlaybackEngine::get();
        if (playback->controls(this) && !playback->injecting()) return;
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(FWAPlayLayer, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // The anticheat spike is GD's own noclip check at level start; never intercept it.
        if (object != m_anticheatSpike) {
            auto engine = MeasurementEngine::get();
            if (engine->controls(this)) {
                engine->onPlayerDestroyed(this);
                return; // the engine ends the trial; skip death effects, sounds and progress saving
            }
            auto playback = PlaybackEngine::get();
            if (playback->controls(this)) {
                playback->onPlayerDestroyed(this);
                return; // a bot replay must never save a new best percentage
            }
        }
        PlayLayer::destroyPlayer(player, object);
    }

    bool interceptLevelEnd() {
        auto engine = MeasurementEngine::get();
        if (engine->controls(this)) {
            engine->onLevelEnd(this);
            return true;
        }
        auto playback = PlaybackEngine::get();
        if (playback->controls(this)) {
            playback->onLevelEnd(this);
            return true;
        }
        return false;
    }

    void levelComplete() {
        // A replay never counts as a real completion.
        if (interceptLevelEnd()) return;
        PlayLayer::levelComplete();
    }

    void playEndAnimationToPos(CCPoint pos) {
        if (interceptLevelEnd()) return;
        PlayLayer::playEndAnimationToPos(pos);
    }

    void playPlatformerEndAnimationToPos(CCPoint pos, bool instant) {
        if (interceptLevelEnd()) return;
        PlayLayer::playPlatformerEndAnimationToPos(pos, instant);
    }

    void resetLevel() {
        auto engine = MeasurementEngine::get();
        // Ignore restarts that don't come from the engine (e.g. the R key) while measuring.
        if (engine->controls(this) && !engine->resetting()) return;
        PlayLayer::resetLevel();
        // During playback a restart (yours or ours) starts the replay from the beginning.
        auto playback = PlaybackEngine::get();
        if (playback->controls(this)) playback->onLevelReset(this);
    }

    void pauseGame(bool unfocused) {
        auto engine = MeasurementEngine::get();
        if (engine->controls(this)) {
            // Losing window focus should not throw away a long measurement.
            if (unfocused) return;
            engine->cancel("You paused the game.");
        }
        auto playback = PlaybackEngine::get();
        if (playback->controls(this)) playback->stop("Playback stopped.", false);
        PlayLayer::pauseGame(unfocused);
    }

    void onQuit() {
        auto engine = MeasurementEngine::get();
        // Quitting: stop without restarting the level we are leaving.
        if (engine->controls(this)) engine->cancel("The level was closed.", false);
        auto playback = PlaybackEngine::get();
        if (playback->controls(this)) playback->stop("", false);
        PlayLayer::onQuit();
    }
};
