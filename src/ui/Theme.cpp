#include "Theme.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/FLAlertLayer.hpp>

#include <algorithm>
#include <array>

using namespace geode::prelude;

namespace fwa::ui {

namespace {

enum class ThemeKind { Rainbow, Classic, Mono };
ThemeKind s_theme = ThemeKind::Rainbow;
bool s_themeLoaded = false;

void ensureTheme() {
    if (!s_themeLoaded) reloadTheme();
}

// Narrowest -> widest, matching the reference overlay (0-1 pink ... 10+ purple).
constexpr std::array<ccColor3B, 9> kRainbow = {{
    {255, 105, 170}, // 0-1
    {255, 95, 85},   // 1-2
    {255, 165, 75},  // 2-3
    {255, 235, 120}, // 3-4
    {120, 225, 120}, // 4-5
    {110, 235, 255}, // 5-6
    {100, 175, 255}, // 6-8
    {125, 125, 255}, // 8-10
    {185, 120, 255}, // 10+
}};

constexpr std::array<ccColor3B, 5> kClassic = {{
    {255, 75, 75}, {255, 155, 0}, {255, 225, 0}, {0, 200, 255}, {0, 255, 140},
}};

ccColor3B lerp(ccColor3B a, ccColor3B b, float t) {
    return {
        static_cast<GLubyte>(a.r + (b.r - a.r) * t),
        static_cast<GLubyte>(a.g + (b.g - a.g) * t),
        static_cast<GLubyte>(a.b + (b.b - a.b) * t),
    };
}

template <size_t N>
ccColor3B sample(std::array<ccColor3B, N> const& pal, float t) {
    t = std::clamp(t, 0.f, 1.f);
    float pos = t * (N - 1);
    size_t i = static_cast<size_t>(pos);
    if (i >= N - 1) return pal[N - 1];
    return lerp(pal[i], pal[i + 1], pos - static_cast<float>(i));
}

} // namespace

void reloadTheme() {
    auto t = Mod::get()->getSettingValue<std::string>("color-theme");
    s_theme = t == "Classic GD" ? ThemeKind::Classic : t == "Monochrome" ? ThemeKind::Mono : ThemeKind::Rainbow;
    s_themeLoaded = true;
}

ccColor3B bucketColor(size_t index, size_t count) {
    ensureTheme();
    if (count == 0) return kText;
    if (s_theme == ThemeKind::Rainbow && count == kRainbow.size()) return kRainbow[std::min(index, count - 1)];
    float t = count == 1 ? 1.f : static_cast<float>(index) / static_cast<float>(count - 1);
    switch (s_theme) {
        case ThemeKind::Rainbow: return sample(kRainbow, t);
        case ThemeKind::Classic: return sample(kClassic, t);
        case ThemeKind::Mono: {
            auto v = static_cast<GLubyte>(110 + 140 * t);
            return {v, v, v};
        }
    }
    return kText;
}

ccColor3B windowColor(double frames, std::vector<double> const& edges) {
    if (edges.empty()) return kText;
    auto it = std::upper_bound(edges.begin(), edges.end(), frames);
    size_t idx = it == edges.begin() ? 0 : static_cast<size_t>(it - edges.begin()) - 1;
    return bucketColor(idx, edges.size());
}

ccColor3B statusColor(WindowStatus s) {
    switch (s) {
        case WindowStatus::Verified: return kGood;
        case WindowStatus::LowerBound: return {170, 230, 140};
        case WindowStatus::Inconsistent: return kWarn;
        case WindowStatus::BaselineFailed: return kBad;
        case WindowStatus::UserSupplied: return kAccent;
        case WindowStatus::Estimated: return {210, 160, 255};
        case WindowStatus::Excluded: return {110, 110, 125};
        case WindowStatus::Unknown: return kMuted;
    }
    return kText;
}

ccColor4F toColor4F(ccColor3B c, float alpha) {
    return {c.r / 255.f, c.g / 255.f, c.b / 255.f, alpha};
}

NineSlice* panel(CCSize size, GLubyte opacity) {
    auto p = NineSlice::create("square02b_001.png");
    p->setContentSize(size);
    p->setColor({0, 0, 0});
    p->setOpacity(opacity);
    p->setAnchorPoint({0.f, 0.f});
    return p;
}

CCLabelBMFont* label(std::string const& text, char const* font, float scale, ccColor3B color, CCPoint anchor) {
    char const* file = "bigFont.fnt";
    if (std::string_view(font) == "chat") file = "chatFont.fnt";
    else if (std::string_view(font) == "gold") file = "goldFont.fnt";
    auto l = CCLabelBMFont::create(text.c_str(), file);
    l->setScale(scale);
    l->setColor(color);
    l->setAnchorPoint(anchor);
    return l;
}

CCMenuItemSpriteExtra* button(std::string const& text, float scale, int style,
                              std::function<void(CCMenuItemSpriteExtra*)> callback, float minWidth) {
    char const* bg = "GJ_button_01.png";
    switch (style) {
        case 1: bg = "GJ_button_02.png"; break;
        case 2: bg = "GJ_button_04.png"; break;
        case 3: bg = "GJ_button_06.png"; break;
        default: break;
    }
    auto spr = ButtonSprite::create(text.c_str(), static_cast<int>(minWidth), static_cast<int>(minWidth), 1.f,
        minWidth > 0.f, "bigFont.fnt", bg, 30.f);
    spr->setScale(scale);
    return CCMenuItemExt::createSpriteExtra(spr, [cb = std::move(callback)](CCMenuItemSpriteExtra* item) {
        if (cb) cb(item);
    });
}

CCMenuItemToggler* toggle(CCMenu* menu, CCNode* labelParent, CCPoint pos, std::string const& caption, bool value,
                          std::function<void(bool)> callback, float scale) {
    auto t = CCMenuItemExt::createTogglerWithStandardSprites(scale, [cb = std::move(callback)](CCMenuItemToggler* item) {
        // GD calls the handler before flipping the state, so the new state is the opposite.
        if (cb) cb(!item->isToggled());
    });
    t->toggle(value);
    t->setPosition(pos);
    menu->addChild(t);
    auto l = label(caption, "big", .3f);
    l->setPosition(pos + CCPoint{14.f * scale / .5f, 0.f});
    labelParent->addChild(l);
    return t;
}

CCNode* statRow(std::string const& name, std::string const& value, float width, ccColor3B valueColor, float scale) {
    auto node = CCNode::create();
    node->setContentSize({width, 12.f});
    auto n = label(name, "big", scale, kMuted);
    n->setPosition({0.f, 6.f});
    node->addChild(n);
    auto v = label(value, "big", scale, valueColor, {1.f, .5f});
    v->setPosition({width, 6.f});
    float maxW = width - n->getScaledContentSize().width - 6.f;
    if (maxW > 10.f && v->getScaledContentSize().width > maxW) v->setScale(scale * maxW / v->getScaledContentSize().width);
    node->addChild(v);
    return node;
}

void defer(CCNode* owner, std::function<void()> fn) {
    Loader::get()->queueInMainThread([ref = Ref<CCNode>(owner), fn = std::move(fn)] {
        if (fn) fn();
    });
}

void showInfo(std::string const& title, std::string const& text) {
    MDPopup::create(title.c_str(), text, "OK")->show();
}

} // namespace fwa::ui
