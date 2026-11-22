#pragma once

// Click Corrector engine.
//
// Every level can have a "reference": the clicks of a run that beats it (your
// own completion, or an imported macro). While you play, a press or release
// that comes 1..N ticks before the matching reference event is swallowed and
// sent again on the reference tick. Anything else passes through untouched.
//
// Ticks are GD's physics steps (m_gameState.m_currentProgress, 240 per second),
// read at the moment GD handles the button - the convention GDR macros use.

#include "core/Macro.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cc {

class Assist {
public:
    static Assist& get();

    // ---- hooks -----------------------------------------------------------
    void attach(PlayLayer* layer);
    void detach(PlayLayer* layer);
    bool controls(GJBaseGameLayer* layer) const { return layer && layer == m_layer; }
    bool injecting() const { return m_injecting; }

    /// A button GD is about to handle. Returns true when it was held back.
    bool onUserButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1);
    /// A button GD has handled (yours or a corrected one).
    void onHandled(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1);
    void onTick(GJBaseGameLayer* layer);
    void onReset(PlayLayer* layer);
    void onCheckpointLoaded(PlayLayer* layer, uint64_t frame);
    void onDeath(PlayLayer* layer);
    void onLevelComplete(PlayLayer* layer);

    /// Safe Mode: this attempt must not save a best or a completion.
    bool blocksProgress(GJBaseGameLayer* layer) const;

    // ---- used by the popup ----------------------------------------------
    PlayLayer* layer() const { return m_layer; }
    bool hasReference() const { return !m_ref.empty(); }
    size_t referenceClicks() const;
    std::string const& referenceSource() const { return m_refSource; }
    /// Empty when the mod can work in this level, otherwise the reason it cannot.
    std::string const& unsupported() const { return m_unsupported; }
    int corrected() const { return m_corrected; }
    int window() const { return m_window; }
    bool enabled() const { return m_enabled; }

    /// Reads a macro file and makes it this level's reference. Returns an error text on failure.
    std::optional<std::string> importFile(std::filesystem::path const& path, std::string* note);
    void deleteReference();
    void reloadSettings();

private:
    struct Pending {
        uint64_t frame;  // tick it is sent on
        uint64_t origin; // tick you made it on
        bool down;
        int button;
        bool isPlayer1; // exactly what GD passed
        bool player2;   // which player it really controls
    };

    Assist() = default;
    bool usable() const { return m_layer && m_unsupported.empty(); }
    bool secondPlayer(GJBaseGameLayer* layer, bool isPlayer1) const;
    std::filesystem::path referencePath() const;
    void loadReference();
    bool saveReference(std::vector<fwa::MacroInput> inputs, std::string const& source);
    void setReference(std::vector<fwa::MacroInput> inputs, std::string source);
    void updateHud();

    PlayLayer* m_layer = nullptr;
    std::string m_levelKey;
    std::string m_unsupported;

    std::vector<fwa::MacroInput> m_ref; // sorted by frame
    std::vector<char> m_used;           // reference events already matched this attempt
    std::string m_refSource;

    std::vector<fwa::MacroInput> m_attempt; // what GD handled this attempt
    bool m_attemptFromStart = false;
    std::deque<Pending> m_pending;          // held-back events, sorted by frame
    bool m_injecting = false;
    int m_corrected = 0;
    int m_lastEarly = 0;

    bool m_enabled = true;
    int m_window = 10;
    bool m_correctReleases = true;
    bool m_learn = true;
    bool m_safeMode = true;
    bool m_showHud = true;
};

} // namespace cc
