#include "Assist.hpp"
#include "core/Formats.hpp"

#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cmath>
#include <span>

using namespace geode::prelude;
using fwa::MacroInput;

namespace cc {

namespace {

constexpr char const* kHudId = "click-corrector-hud";

uint64_t tick(GJBaseGameLayer* layer) {
    return layer->m_gameState.m_currentProgress;
}

std::string levelKey(GJGameLevel* level) {
    if (!level) return "unknown";
    int id = level->m_levelID.value();
    if (id > 0) return "id_" + std::to_string(id);
    std::string name = level->m_levelName;
    for (auto& c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) c = '_';
    }
    if (name.empty()) name = "unnamed";
    return "local_" + name;
}

void sortInputs(std::vector<MacroInput>& inputs) {
    std::stable_sort(inputs.begin(), inputs.end(), [](MacroInput const& a, MacroInput const& b) {
        return a.frame < b.frame;
    });
}

} // namespace

Assist& Assist::get() {
    static Assist instance;
    return instance;
}

void Assist::reloadSettings() {
    auto mod = Mod::get();
    m_enabled = mod->getSettingValue<bool>("enabled");
    m_window = static_cast<int>(std::clamp<int64_t>(mod->getSettingValue<int64_t>("frames-early"), 1, 120));
    m_correctReleases = mod->getSettingValue<bool>("correct-releases");
    m_learn = mod->getSettingValue<bool>("learn");
    m_safeMode = mod->getSettingValue<bool>("safe-mode");
    m_showHud = mod->getSettingValue<bool>("show-hud");
    m_showBar = mod->getSettingValue<bool>("show-bar");
    m_barFrames = static_cast<int>(std::clamp<int64_t>(mod->getSettingValue<int64_t>("bar-frames"), 30, 480));
    updateHud();
}

void Assist::attach(PlayLayer* layer) {
    m_layer = layer;
    m_levelKey = levelKey(layer ? layer->m_level : nullptr);
    m_pending.clear();
    m_attempt.clear();
    m_attemptFromStart = true;
    m_corrected = 0;
    m_lastEarly = 0;
    m_injecting = false;
    m_ref.clear();
    m_used.clear();
    m_refSource.clear();
    m_unsupported.clear();
    if (!layer) return;

    if (layer->m_levelSettings && layer->m_levelSettings->m_platformerMode) {
        m_unsupported = "Platformer levels are not supported.";
    } else if (layer->m_startPosObject) {
        m_unsupported = "Turned off while a start position is used: the reference run counts frames from the "
                        "beginning of the level.";
    }
    reloadSettings();
    loadReference();
}

void Assist::detach(PlayLayer* layer) {
    if (layer != m_layer) return;
    m_layer = nullptr;
    m_pending.clear();
    m_attempt.clear();
    m_injecting = false;
}

bool Assist::secondPlayer(GJBaseGameLayer* layer, bool isPlayer1) const {
    // Same convention as GDR macros: "player 2" only for the second player's own
    // controls in a two-player level while dual mode is on.
    bool twoPlayer = layer->m_levelSettings && layer->m_levelSettings->m_twoPlayerMode;
    bool realPlayer1 = !twoPlayer || isPlayer1 || !layer->m_gameState.m_isDualMode;
    return !realPlayer1;
}

size_t Assist::referenceClicks() const {
    size_t n = 0;
    for (auto const& in : m_ref) n += in.down ? 1 : 0;
    return n;
}

// ---------------------------------------------------------------------------
// Correcting
// ---------------------------------------------------------------------------

bool Assist::onUserButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1) {
    if (!controls(layer) || !usable() || !m_enabled || m_ref.empty()) return false;
    if (!layer->m_player1 || layer->m_player1->m_isDead) return false;
    if (layer->m_levelEndAnimationStarted) return false;

    uint64_t now = tick(layer);
    uint8_t b = static_cast<uint8_t>(std::clamp(button, 1, 3));
    bool p2 = secondPlayer(layer, isPlayer1);

    // An event may never overtake an earlier one on the same button that is still held back.
    // Without a match of its own it moves by the same amount, so the hold keeps its length.
    bool queued = false;
    uint64_t notBefore = now;
    for (auto const& p : m_pending) {
        if (p.button == b && p.player2 == p2) {
            queued = true;
            notBefore = std::max({notBefore, p.frame + 1, now + (p.frame - p.origin)});
        }
    }

    uint64_t target = now;
    int early = 0;
    if (down || m_correctReleases) {
        uint64_t window = static_cast<uint64_t>(m_window);
        auto same = [&](MacroInput const& in) { return in.down == down && in.button == b && in.player2 == p2; };
        auto first = std::lower_bound(m_ref.begin(), m_ref.end(), now, [](MacroInput const& in, uint64_t f) {
            return in.frame < f;
        });
        size_t start = static_cast<size_t>(first - m_ref.begin());

        // The nearest unmatched reference event at or after this tick, inside the window.
        std::optional<size_t> ahead;
        for (size_t i = start; i < m_ref.size() && m_ref[i].frame <= now + window; i++) {
            if (!m_used[i] && same(m_ref[i])) {
                ahead = i;
                break;
            }
        }
        // The nearest unmatched one behind it: if that is closer, this click is a late
        // click for that event, not an early click for the next one. Leave it alone.
        std::optional<size_t> behind;
        for (size_t i = start; i-- > 0 && m_ref[i].frame + window >= now;) {
            if (!m_used[i] && same(m_ref[i])) {
                behind = i;
                break;
            }
        }

        if (ahead && behind && now - m_ref[*behind].frame <= m_ref[*ahead].frame - now) {
            m_used[*behind] = 1;
        } else if (ahead) {
            m_used[*ahead] = 1;
            target = m_ref[*ahead].frame;
            early = static_cast<int>(target - now);
        } else if (behind) {
            m_used[*behind] = 1;
        }
    }

    target = std::max(target, notBefore);
    if (target <= now && !queued) return false; // on time, late, or nothing to match: untouched

    Pending p{target, now, down, static_cast<int>(b), isPlayer1, p2};
    auto pos = std::upper_bound(m_pending.begin(), m_pending.end(), target, [](uint64_t f, Pending const& q) {
        return f < q.frame;
    });
    m_pending.insert(pos, p);
    if (early > 0) {
        m_corrected++;
        m_lastEarly = early;
        updateHud();
    }
    return true;
}

void Assist::onTick(GJBaseGameLayer* layer) {
    if (!controls(layer) || m_pending.empty()) return;
    if (!layer->m_player1 || layer->m_player1->m_isDead) {
        m_pending.clear();
        return;
    }
    uint64_t now = tick(layer);
    m_injecting = true;
    while (!m_pending.empty() && m_pending.front().frame <= now) {
        Pending p = m_pending.front();
        m_pending.pop_front();
        layer->handleButton(p.down, p.button, p.isPlayer1);
    }
    m_injecting = false;
}

// ---------------------------------------------------------------------------
// Learning the reference from your own runs
// ---------------------------------------------------------------------------

void Assist::onHandled(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1) {
    if (!controls(layer) || !usable()) return;
    if (layer->m_player1 && layer->m_player1->m_isDead) return;
    MacroInput in;
    in.frame = tick(layer);
    in.button = static_cast<uint8_t>(std::clamp(button, 1, 3));
    in.player2 = secondPlayer(layer, isPlayer1);
    in.down = down;
    in.fileOrder = m_attempt.size();
    m_attempt.push_back(in);
}

void Assist::onReset(PlayLayer* layer) {
    if (!controls(layer)) return;
    m_pending.clear();
    reloadSettings();
    // In practice mode a reset returns to a checkpoint (handled in onCheckpointLoaded).
    if (layer->m_checkpointArray && layer->m_checkpointArray->count() > 0) return;
    m_attempt.clear();
    m_attemptFromStart = true;
    std::fill(m_used.begin(), m_used.end(), 0);
    m_corrected = 0;
    m_lastEarly = 0;
    updateHud();
}

void Assist::onCheckpointLoaded(PlayLayer* layer, uint64_t frame) {
    if (!controls(layer)) return;
    m_pending.clear();
    std::erase_if(m_attempt, [frame](MacroInput const& in) { return in.frame > frame; });
    for (size_t i = 0; i < m_ref.size(); i++) {
        if (m_ref[i].frame > frame) m_used[i] = 0;
    }
}

void Assist::onDeath(PlayLayer* layer) {
    if (!controls(layer)) return;
    m_pending.clear();
}

void Assist::onLevelComplete(PlayLayer* layer) {
    if (!controls(layer) || !usable()) return;
    m_pending.clear();
    if (!m_learn || !m_attemptFromStart || m_attempt.empty()) return;
    auto inputs = m_attempt;
    sortInputs(inputs);
    std::string source = layer->m_isPracticeMode ? "your practice-mode completion" : "your completion";
    if (saveReference(inputs, source)) {
        setReference(std::move(inputs), source);
        // The run that just ended used the old reference; nothing left to match in it.
        std::fill(m_used.begin(), m_used.end(), 1);
    }
}

bool Assist::blocksProgress(GJBaseGameLayer* layer) const {
    if (!controls(layer) || !m_safeMode || m_corrected <= 0) return false;
    return !m_layer->m_isPracticeMode;
}

// ---------------------------------------------------------------------------
// Reference storage (standard .gdr2 files, one per level)
// ---------------------------------------------------------------------------

std::filesystem::path Assist::referencePath() const {
    return Mod::get()->getSaveDir() / "references" / (m_levelKey + ".gdr2");
}

void Assist::setReference(std::vector<MacroInput> inputs, std::string source) {
    sortInputs(inputs);
    m_ref = std::move(inputs);
    m_used.assign(m_ref.size(), 0);
    m_refSource = std::move(source);
}

void Assist::loadReference() {
    auto path = referencePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;
    auto data = file::readBinary(path);
    if (!data) {
        log::warn("Could not read {}: {}", utils::string::pathToString(path), data.unwrapErr());
        return;
    }
    auto bytes = std::move(data).unwrap();
    auto parsed = fwa::parseGdr2(std::span<uint8_t const>(bytes.data(), bytes.size()));
    if (!parsed) {
        log::warn("Reference {} is not readable: {}", utils::string::pathToString(path), parsed.error());
        return;
    }
    auto macro = std::move(parsed).value();
    // Only your own runs are used; a reference imported by an older version is ignored.
    if (macro.meta.description.starts_with("imported macro")) return;
    setReference(std::move(macro.inputs), macro.meta.description.empty() ? "a saved run" : macro.meta.description);
    log::info("Loaded reference for {}: {} inputs", m_levelKey, m_ref.size());
}

bool Assist::saveReference(std::vector<MacroInput> inputs, std::string const& source) {
    fwa::Macro m;
    m.meta.framerate = 240.0;
    m.meta.framerateFromFile = true;
    m.meta.gameVersionText = "2.2081";
    m.meta.botName = "Click Corrector";
    m.meta.botVersion = 1;
    m.meta.description = source;
    if (m_layer && m_layer->m_level) {
        m.meta.levelId = static_cast<uint32_t>(std::max(0, static_cast<int>(m_layer->m_level->m_levelID.value())));
        m.meta.levelName = m_layer->m_level->m_levelName;
    }
    sortInputs(inputs);
    m.inputs = std::move(inputs);
    if (!m.inputs.empty()) m.meta.storedDurationSeconds = static_cast<double>(m.inputs.back().frame) / 240.0;

    auto path = referencePath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto bytes = fwa::writeGdr2(m, GEODE_COMP_GD_VERSION);
    auto written = file::writeBinary(path, bytes);
    if (!written) {
        log::warn("Could not save {}: {}", utils::string::pathToString(path), written.unwrapErr());
        Notification::create("Click Corrector could not save the reference run", NotificationIcon::Error)->show();
        return false;
    }
    return true;
}

void Assist::deleteReference() {
    std::error_code ec;
    std::filesystem::remove(referencePath(), ec);
    m_ref.clear();
    m_used.clear();
    m_refSource.clear();
    m_pending.clear();
    updateHud();
}

// ---------------------------------------------------------------------------
// Counter
// ---------------------------------------------------------------------------

void Assist::updateHud() {
    if (!m_layer) return;
    auto label = static_cast<CCLabelBMFont*>(m_layer->getChildByID(kHudId));
    bool show = m_showHud && m_enabled && usable() && !m_ref.empty();
    if (!show) {
        if (label) label->setVisible(false);
        return;
    }
    if (!label) {
        label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setID(kHudId);
        label->setScale(.3f);
        label->setOpacity(170);
        label->setAnchorPoint({0.f, 1.f});
        label->setPosition({5.f, CCDirector::get()->getWinSize().height - 4.f});
        m_layer->addChild(label, 1000);
    }
    label->setVisible(true);
    auto text = m_corrected > 0
        ? fmt::format("Corrected {}  (last: {}f early)", m_corrected, m_lastEarly)
        : fmt::format("Click Corrector: {}f", m_window);
    label->setString(text.c_str());
}

} // namespace cc
