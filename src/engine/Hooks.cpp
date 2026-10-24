// Game hooks used by the measurement engine. Every hook is a pass-through
// unless a measurement is running on that exact layer.

#include "MeasurementEngine.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;
using fwa::MeasurementEngine;

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
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto engine = MeasurementEngine::get();
        // Only the engine's replayed inputs reach the game during a measurement.
        if (engine->controls(this) && !engine->injecting()) return;
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(FWAPlayLayer, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto engine = MeasurementEngine::get();
        // The anticheat spike is GD's own noclip check at level start; never intercept it.
        if (engine->controls(this) && object != m_anticheatSpike) {
            engine->onPlayerDestroyed(this);
            return; // the engine ends the trial; skip death effects, sounds and progress saving
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        auto engine = MeasurementEngine::get();
        if (engine->controls(this)) {
            engine->onLevelEnd(this);
            return; // a measurement replay never counts as a real completion
        }
        PlayLayer::levelComplete();
    }

    void playEndAnimationToPos(CCPoint pos) {
        auto engine = MeasurementEngine::get();
        if (engine->controls(this)) {
            engine->onLevelEnd(this);
            return;
        }
        PlayLayer::playEndAnimationToPos(pos);
    }

    void playPlatformerEndAnimationToPos(CCPoint pos, bool instant) {
        auto engine = MeasurementEngine::get();
        if (engine->controls(this)) {
            engine->onLevelEnd(this);
            return;
        }
        PlayLayer::playPlatformerEndAnimationToPos(pos, instant);
    }

    void resetLevel() {
        auto engine = MeasurementEngine::get();
        // Ignore restarts that don't come from the engine (e.g. the R key) while measuring.
        if (engine->controls(this) && !engine->resetting()) return;
        PlayLayer::resetLevel();
    }

    void pauseGame(bool unfocused) {
        auto engine = MeasurementEngine::get();
        if (engine->controls(this)) {
            // Losing window focus should not throw away a long measurement.
            if (unfocused) return;
            engine->cancel("You paused the game.");
        }
        PlayLayer::pauseGame(unfocused);
    }

    void onQuit() {
        auto engine = MeasurementEngine::get();
        if (engine->controls(this)) engine->cancel("The level was closed.");
        PlayLayer::onQuit();
    }
};
