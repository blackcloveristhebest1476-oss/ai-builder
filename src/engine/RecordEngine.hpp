#pragma once

// Records your own inputs while you play, GDR-style (frame = the game's tick
// counter when GD handles the button). When you finish the level (or stop it
// from the analyzer) the recording is saved as a .gdr2 file and loaded into the
// analyzer automatically.

#include "../core/Macro.hpp"

#include <Geode/Geode.hpp>

#include <optional>
#include <string>
#include <vector>

namespace fwa {

class RecordEngine : public cocos2d::CCObject {
public:
    static RecordEngine* get();

    std::optional<std::string> start(PlayLayer* pl);
    /// Ends the recording. `use` = load it into the analyzer and save it.
    void finish(bool use, std::string const& reason);

    bool active() const { return m_active; }
    bool controls(GJBaseGameLayer* layer) const { return m_active && layer == m_layer; }
    size_t inputCount() const { return m_inputs.size(); }
    int attempt() const { return m_attempt; }

    // Called from hooks
    void onButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1);
    void onLevelReset(PlayLayer* layer);
    void onCheckpointLoaded(PlayLayer* layer, uint64_t frame);
    void onLevelComplete(PlayLayer* layer);

private:
    RecordEngine() = default;
    void updateHud();
    void hideHud();

    bool m_active = false;
    PlayLayer* m_layer = nullptr;
    std::vector<MacroInput> m_inputs;
    bool m_usedPractice = false;
    int m_attempt = 0;
    bool m_cbfTouched = false;
    bool m_cbfPrevious = false;
    geode::Ref<cocos2d::CCLabelBMFont> m_hud;
};

} // namespace fwa
