#include "RecordEngine.hpp"
#include "MeasurementEngine.hpp"
#include "PlaybackEngine.hpp"
#include "../Session.hpp"
#include "../core/Formats.hpp"
#include "../core/Util.hpp"
#include "../ui/AnalyzerPopup.hpp"

#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace fwa {

RecordEngine* RecordEngine::get() {
    static RecordEngine* instance = [] {
        auto e = new RecordEngine();
        e->retain();
        return e;
    }();
    return instance;
}

std::optional<std::string> RecordEngine::start(PlayLayer* pl) {
    if (!pl) return "Open a level first. Recording captures your inputs while you play it.";
    if (m_active) return "Already recording.";
    if (MeasurementEngine::get()->active() || PlaybackEngine::get()->active()) return "Stop the measurement or playback first.";
    if (pl->m_checkpointArray && pl->m_checkpointArray->count() > 0) {
        return "Remove your practice checkpoints first (restart practice mode), so the recording starts at the "
               "beginning of the level.";
    }
    if (pl->m_startPosObject) {
        return "Remove the start position first. A recording must start at the beginning of the level, "
               "otherwise it cannot be replayed or measured.";
    }
    m_layer = pl;
    m_active = true;
    m_inputs.clear();
    m_usedPractice = pl->m_isPracticeMode;
    m_attempt = 1;

    // Like GDR bots, switch Click Between Frames off while recording: its sub-tick inputs
    // cannot be stored in a tick-based replay. Restored when recording ends.
    m_cbfTouched = false;
    if (auto cbf = Loader::get()->getLoadedMod("syzzi.click_between_frames")) {
        if (cbf->hasSetting("soft-toggle")) {
            m_cbfPrevious = cbf->getSettingValue<bool>("soft-toggle");
            cbf->setSettingValue<bool>("soft-toggle", true);
            m_cbfTouched = true;
        }
    }

    pl->resetLevel(); // our hook clears the inputs for the new attempt
    updateHud();
    log::info("Frame Window Analyzer: recording started");
    return std::nullopt;
}

void RecordEngine::onButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1) {
    if (!controls(layer)) return;
    if (layer->m_player1 && layer->m_player1->m_isDead) return;
    bool twoPlayer = layer->m_levelSettings && layer->m_levelSettings->m_twoPlayerMode;
    // Same convention as GDR bots: player2 only for the second player's own controls.
    bool realPlayer1 = !twoPlayer || isPlayer1 || !layer->m_gameState.m_isDualMode;
    MacroInput in;
    in.frame = layer->m_gameState.m_currentProgress;
    in.button = static_cast<uint8_t>(std::clamp(button, 1, 3));
    in.player2 = !realPlayer1;
    in.down = down;
    in.fileOrder = m_inputs.size();
    m_inputs.push_back(in);
    updateHud();
}

void RecordEngine::onLevelReset(PlayLayer* layer) {
    if (!controls(layer)) return;
    // In practice mode a reset goes back to a checkpoint (handled in onCheckpointLoaded);
    // otherwise a new attempt starts from scratch.
    if (layer->m_checkpointArray && layer->m_checkpointArray->count() > 0) return;
    m_inputs.clear();
    m_attempt++;
    updateHud();
}

void RecordEngine::onCheckpointLoaded(PlayLayer* layer, uint64_t frame) {
    if (!controls(layer)) return;
    m_usedPractice = true;
    std::erase_if(m_inputs, [frame](MacroInput const& in) { return in.frame > frame; });
    updateHud();
}

void RecordEngine::onLevelComplete(PlayLayer* layer) {
    if (!controls(layer)) return;
    finish(true, "Level completed");
}

void RecordEngine::finish(bool use, std::string const& reason) {
    if (!m_active) return;
    m_active = false;
    hideHud();
    if (m_cbfTouched) {
        if (auto cbf = Loader::get()->getLoadedMod("syzzi.click_between_frames")) {
            cbf->setSettingValue<bool>("soft-toggle", m_cbfPrevious);
        }
        m_cbfTouched = false;
    }
    auto layer = m_layer;
    m_layer = nullptr;
    if (!use) {
        if (!reason.empty()) Notification::create("Recording discarded. " + reason, NotificationIcon::Info)->show();
        return;
    }
    if (m_inputs.empty()) {
        Notification::create("Recording stopped: no inputs were recorded.", NotificationIcon::Warning)->show();
        return;
    }

    Macro m;
    m.formatId = "recorded";
    m.formatName = "Recorded in-game (GDR2)";
    m.meta.framerate = 240.0;
    m.meta.framerateFromFile = true;
    m.meta.gameVersionText = "2.2081";
    m.meta.botName = "Frame Window Analyzer";
    m.meta.botVersion = 1;
    m.meta.description = m_usedPractice ? "Recorded with practice checkpoints" : "Recorded in normal mode";
    if (auto gm = GameManager::sharedState()) m.meta.author = gm->m_playerName;
    if (layer && layer->m_level) {
        m.meta.levelId = static_cast<uint32_t>(std::max(0, static_cast<int>(layer->m_level->m_levelID.value())));
        m.meta.levelName = layer->m_level->m_levelName;
    }
    if (layer && layer->m_levelSettings) m.meta.platformer = layer->m_levelSettings->m_platformerMode;
    std::stable_sort(m_inputs.begin(), m_inputs.end(), [](auto const& a, auto const& b) { return a.frame < b.frame; });
    for (auto const& in : m_inputs) m.meta.platformer = m.meta.platformer || in.button != 1;
    m.inputs = m_inputs;
    m.meta.storedDurationSeconds = static_cast<double>(m.inputs.back().frame) / 240.0;

    // Save a standard .gdr2 file (usable by other bots too).
    auto& session = Session::get();
    auto dir = Mod::get()->getSaveDir() / "recordings";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string base = m.meta.levelName.empty() ? "recording" : m.meta.levelName;
    for (auto& c : base) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) c = '_';
    }
    auto fileName = base + "_" + nowTimestamp(true) + ".gdr2";
    auto bytes = writeGdr2(m, GEODE_COMP_GD_VERSION);
    auto written = file::writeBinary(dir / fileName, bytes);
    m.sourceName = fileName;

    validateMacro(m);
    if (m_usedPractice) {
        m.addDiagnostic(Severity::Warning, "Recorded with practice-mode checkpoints. Checkpoint restores are not always "
            "exact, so this replay may not survive from the start; measurement will tell you if it doesn't.");
    }
    if (!written) {
        m.addDiagnostic(Severity::Warning, "Could not save the .gdr2 file: " + std::string(written.unwrapErr()));
    } else {
        m.addDiagnostic(Severity::Info, "Saved as " + utils::string::pathToString(dir / fileName));
    }
    session.setAnalysis(analysisFromMacro(m));
    session.setMessage("Recorded " + std::to_string(m.inputs.size()) + " inputs (" + reason + "). Saved as " + fileName + ".");
    log::info("Frame Window Analyzer: recording finished, {} inputs, saved {}", m.inputs.size(), fileName);

    Notification::create("Recording loaded into Frame Window Analyzer (" + std::to_string(m.inputs.size()) + " inputs)",
        NotificationIcon::Success, 4.f)->show();
    // Open the analyzer once the end screen has settled.
    auto self = Ref(this);
    Loader::get()->queueInMainThread([self] {
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        scene->runAction(CCSequence::create(CCDelayTime::create(1.2f), CallFuncExt::create([] {
            if (!ui::AnalyzerPopup::current()) {
                if (auto p = ui::AnalyzerPopup::create(ui::AnalyzerPopup::Dashboard)) p->show();
            }
        }), nullptr));
    });
}

void RecordEngine::updateHud() {
    if (!m_active) return;
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    auto winSize = CCDirector::get()->getWinSize();
    if (!m_hud || !m_hud->getParent()) {
        m_hud = CCLabelBMFont::create("", "bigFont.fnt");
        m_hud->setScale(.3f);
        m_hud->setColor({255, 90, 90});
        m_hud->setOpacity(210);
        m_hud->setAnchorPoint({0.f, 1.f});
        m_hud->setPosition({6.f, winSize.height - 4.f});
        m_hud->setID("record-hud"_spr);
        scene->addChild(m_hud, 10000);
    }
    m_hud->setString(fmt::format("REC  attempt {}  -  {} inputs{}", m_attempt, m_inputs.size(),
        m_usedPractice ? "  (practice)" : "").c_str());
}

void RecordEngine::hideHud() {
    if (m_hud) m_hud->removeFromParent();
    m_hud = nullptr;
}

} // namespace fwa
