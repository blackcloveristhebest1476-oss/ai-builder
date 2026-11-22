// Game hooks. Every hook is a pass-through unless the engine is attached to that layer.

#include "Assist.hpp"

#include <Geode/binding/CheckpointObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/ui/Notification.hpp>

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;
using cc::Assist;

class $modify(CorrectorBaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        // See your real input before other mods' input hooks do.
        (void) self.setHookPriority("GJBaseGameLayer::handleButton", Priority::Early);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        // Send held-back clicks whose tick has come (the injection point GDR bots use).
        auto& assist = Assist::get();
        if (assist.controls(this)) assist.onTick(this);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto& assist = Assist::get();
        if (!assist.controls(this)) return GJBaseGameLayer::handleButton(down, button, isPlayer1);
        if (!assist.injecting() && assist.onUserButton(this, down, button, isPlayer1)) return; // held back
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        assist.onHandled(this, down, button, isPlayer1);
    }
};

class $modify(CorrectorPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        Assist::get().detach(Assist::get().layer());
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        Assist::get().attach(this);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        Assist::get().onReset(this);
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        if (checkpoint) Assist::get().onCheckpointLoaded(this, checkpoint->m_gameState.m_currentProgress);
        PlayLayer::loadFromCheckpoint(checkpoint);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        if (object != m_anticheatSpike) Assist::get().onDeath(this);
    }

    void levelComplete() {
        auto& assist = Assist::get();
        // Safe Mode: GD does not save a completion made in test mode.
        bool safe = assist.blocksProgress(this);
        bool wasTestMode = m_isTestMode;
        if (safe) m_isTestMode = true;
        PlayLayer::levelComplete();
        m_isTestMode = wasTestMode;
        if (safe) {
            Notification::create(fmt::format("Click Corrector fixed {} click(s): completion not saved (Safe Mode)",
                assist.corrected()), NotificationIcon::Info, 4.f)->show();
        }
        assist.onLevelComplete(this);
    }

    void onQuit() {
        Assist::get().detach(this);
        PlayLayer::onQuit();
    }
};

class $modify(CorrectorGameLevel, GJGameLevel) {
    void savePercentage(int percent, bool isPracticeMode, int clicks, int attempts, bool isChkValid) {
        // Safe Mode: an attempt with corrected clicks never saves a new best.
        auto& assist = Assist::get();
        if (auto pl = PlayLayer::get()) {
            if (pl->m_level == this && assist.blocksProgress(pl)) return;
        }
        GJGameLevel::savePercentage(percent, isPracticeMode, clicks, attempts, isChkValid);
    }
};
