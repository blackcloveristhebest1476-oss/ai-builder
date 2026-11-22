#include "RangeBar.hpp"
#include "Assist.hpp"

#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>
#include <cstdint>

using namespace geode::prelude;

namespace cc {

namespace {

constexpr char const* kBarId = "click-corrector-range-bar";
constexpr float kWidth = 230.f;
constexpr float kHeight = 10.f;
constexpr float kNowX = 34.f;         // where "now" sits on the bar
constexpr float kReleaseHeight = 3.f; // the strip for releases, along the bottom

} // namespace

void RangeBar::addTo(PlayLayer* layer) {
    if (!layer || layer->getChildByID(kBarId)) return;
    if (auto bar = RangeBar::create(layer)) layer->addChild(bar, 1000);
}

RangeBar* RangeBar::create(PlayLayer* layer) {
    auto ret = new RangeBar();
    if (ret->init(layer)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool RangeBar::init(PlayLayer* layer) {
    if (!CCNode::init()) return false;
    m_layer = layer;
    this->setID(kBarId);
    auto winSize = CCDirector::get()->getWinSize();
    this->setPosition({winSize.width / 2 - kWidth / 2, 5.f});

    m_draw = CCDrawNode::create();
    this->addChild(m_draw);

    m_cue = CCLabelBMFont::create("CLICK", "bigFont.fnt");
    m_cue->setScale(.3f);
    m_cue->setColor({90, 255, 130});
    m_cue->setAnchorPoint({.5f, 0.f});
    m_cue->setPosition({kNowX, kHeight + 2.f});
    m_cue->setVisible(false);
    this->addChild(m_cue);

    this->scheduleUpdate();
    return true;
}

void RangeBar::rect(float x0, float y0, float x1, float y1, ccColor4F const& color) {
    x0 = std::clamp(x0, 0.f, kWidth);
    x1 = std::clamp(x1, 0.f, kWidth);
    if (x1 - x0 < .01f) return;
    CCPoint verts[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    m_draw->drawPolygon(verts, 4, color, 0.f, color);
}

void RangeBar::update(float) {
    auto& assist = Assist::get();
    m_draw->clear();
    bool show = assist.layer() == m_layer && this->getParent() == m_layer && assist.showBar() && assist.active();
    if (!show) {
        m_cue->setVisible(false);
        return;
    }

    auto const& ref = assist.reference();
    int64_t now = m_layer->m_gameState.m_currentProgress;
    int64_t window = assist.window();
    int64_t ahead = assist.barFrames();
    float perTick = (kWidth - kNowX) / static_cast<float>(ahead);
    auto xOf = [&](int64_t frame) { return kNowX + static_cast<float>(frame - now) * perTick; };

    rect(0.f, 0.f, kWidth, kHeight, {0.f, 0.f, 0.f, .45f});

    // Upcoming events of the same kind (press/release, button, player) share the bar, so a
    // range starts only after the one before it: that earlier click is matched first.
    int64_t lastSame[2][2][4];
    for (auto& a : lastSame) for (auto& b : a) for (auto& c : b) c = INT64_MIN;

    bool clickNow = false;
    auto it = std::lower_bound(ref.begin(), ref.end(), static_cast<uint64_t>(std::max<int64_t>(0, now)),
        [](fwa::MacroInput const& in, uint64_t f) { return in.frame < f; });
    for (size_t i = static_cast<size_t>(it - ref.begin()); i < ref.size(); i++) {
        auto const& in = ref[i];
        int64_t frame = static_cast<int64_t>(in.frame);
        if (frame - window > now + ahead) break;
        if (assist.used(i)) continue; // already caught by one of your clicks
        if (!in.down && !assist.correctReleases()) continue;
        auto& last = lastSame[in.down ? 1 : 0][in.player2 ? 1 : 0][in.button & 3];
        int64_t start = std::max(frame - window, last == INT64_MIN ? INT64_MIN : last + 1);
        last = frame;

        float x0 = xOf(start), x1 = xOf(frame);
        if (in.down) {
            rect(x0, kReleaseHeight, x1, kHeight, {.25f, .85f, .4f, .8f});
            rect(x1 - 1.2f, kReleaseHeight, x1 + .6f, kHeight, {1.f, 1.f, 1.f, .95f}); // the right frame
            if (start <= now && now <= frame) clickNow = true;
        } else {
            rect(x0, 0.f, x1, kReleaseHeight, {.3f, .6f, 1.f, .85f});
            rect(x1 - 1.f, 0.f, x1 + .5f, kReleaseHeight, {1.f, 1.f, 1.f, .9f});
        }
    }

    // The "now" line: turns green while a click would be corrected.
    ccColor4F line = clickNow ? ccColor4F{.35f, 1.f, .5f, 1.f} : ccColor4F{1.f, 1.f, 1.f, .9f};
    rect(kNowX - (clickNow ? 1.5f : .8f), -2.f, kNowX + (clickNow ? 1.5f : .8f), kHeight + 2.f, line);
    bool alive = m_layer->m_player1 && !m_layer->m_player1->m_isDead;
    m_cue->setVisible(clickNow && alive);
}

} // namespace cc
