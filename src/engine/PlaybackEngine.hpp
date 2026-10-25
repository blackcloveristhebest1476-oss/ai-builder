#pragma once

// Real-time playback of the loaded macro with on-screen markers.
//
// The level runs at normal speed (GD's own update), the macro's inputs are
// injected exactly like the measurement engine does, and every press whose
// measured frame window is narrow enough leaves a ring with the window size
// at the player's position. Deaths and completions during playback are
// intercepted so nothing is saved as real progress.

#include <Geode/Geode.hpp>

#include <optional>
#include <string>
#include <vector>

namespace fwa {

enum class MarkerMode { FramePerfect = 0, Tight = 1, AllMeasured = 2 };

class PlaybackEngine : public cocos2d::CCObject {
public:
    static PlaybackEngine* get();

    /// How many counted presses would get a marker in this mode.
    static size_t countMarked(MarkerMode mode);

    std::optional<std::string> start(PlayLayer* pl, MarkerMode mode);
    void stop(std::string const& reason, bool restartLevel = true);

    bool active() const { return m_active; }
    bool controls(GJBaseGameLayer* layer) const { return m_active && layer == m_layer; }
    bool injecting() const { return m_injecting; }
    bool resetting() const { return m_resetting; }

    // Called from hooks
    void onProcessCommands(GJBaseGameLayer* layer);
    void onLevelReset(GJBaseGameLayer* layer);
    void onPlayerDestroyed(GJBaseGameLayer* layer);
    void onLevelEnd(GJBaseGameLayer* layer);

private:
    struct Event {
        uint64_t frame = 0;
        uint8_t button = 1;
        bool player2 = false;
        bool down = true;
        std::optional<double> markFrames; // window to display, if this press is marked
        cocos2d::ccColor3B color{255, 255, 255};
        std::string split; // "-early +late" ticks for measured inputs
        bool lowerBound = false;
    };

    PlaybackEngine() = default;
    void spawnMarker(GJBaseGameLayer* layer, PlayerObject* player, Event const& ev);
    void clearMarkers();
    void showHud();
    void hideHud();

    bool m_active = false;
    bool m_injecting = false;
    bool m_resetting = false;
    bool m_finishing = false;
    PlayLayer* m_layer = nullptr;
    std::vector<Event> m_events;
    size_t m_next = 0;
    size_t m_marked = 0;
    float m_markerSeconds = 1.5f;
    std::vector<geode::Ref<cocos2d::CCNode>> m_markers;
    geode::Ref<cocos2d::CCNode> m_hud;
};

} // namespace fwa
