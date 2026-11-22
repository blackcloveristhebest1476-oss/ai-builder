#pragma once

// Click range bar: a strip at the bottom of the screen where the upcoming
// clicks of your saved run scroll towards a "now" line. While the line is
// inside a green range, a click is corrected to the right frame.

#include <Geode/Geode.hpp>

namespace cc {

class RangeBar : public cocos2d::CCNode {
public:
    /// Adds the bar to a level (once).
    static void addTo(PlayLayer* layer);

    void update(float dt) override;

protected:
    static RangeBar* create(PlayLayer* layer);
    bool init(PlayLayer* layer);
    void rect(float x0, float y0, float x1, float y1, cocos2d::ccColor4F const& color);

    PlayLayer* m_layer = nullptr;
    cocos2d::CCDrawNode* m_draw = nullptr;
    cocos2d::CCLabelBMFont* m_cue = nullptr;
};

} // namespace cc
