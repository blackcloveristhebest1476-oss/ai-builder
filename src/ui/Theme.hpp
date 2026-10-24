#pragma once

#include "../core/Analysis.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace fwa::ui {

// ---- colors -------------------------------------------------------------

/// Color for histogram bucket `index` (0 = narrowest range) out of `count`.
cocos2d::ccColor3B bucketColor(size_t index, size_t count);
/// Color for a frame-window value using the current display ranges.
cocos2d::ccColor3B windowColor(double frames, std::vector<double> const& edges);
cocos2d::ccColor3B statusColor(WindowStatus s);
cocos2d::ccColor4F toColor4F(cocos2d::ccColor3B c, float alpha = 1.f);

inline constexpr cocos2d::ccColor3B kText{235, 235, 245};
inline constexpr cocos2d::ccColor3B kMuted{150, 155, 175};
inline constexpr cocos2d::ccColor3B kAccent{120, 200, 255};
inline constexpr cocos2d::ccColor3B kGood{120, 230, 140};
inline constexpr cocos2d::ccColor3B kWarn{255, 200, 90};
inline constexpr cocos2d::ccColor3B kBad{255, 110, 110};

/// Re-reads the theme setting (call when settings change).
void reloadTheme();

// ---- node helpers --------------------------------------------------------

/// Dark rounded panel.
geode::NineSlice* panel(cocos2d::CCSize size, GLubyte opacity = 110);

/// Left-anchored label. `font` "big" = bigFont, "chat" = chatFont, "gold" = goldFont.
cocos2d::CCLabelBMFont* label(std::string const& text, char const* font, float scale,
                              cocos2d::ccColor3B color = kText, cocos2d::CCPoint anchor = {0.f, .5f});

/// Text button. `style`: 0 = green (primary), 1 = blue/grey, 2 = dark, 3 = red.
CCMenuItemSpriteExtra* button(std::string const& text, float scale, int style,
                              std::function<void(CCMenuItemSpriteExtra*)> callback, float minWidth = 0.f);

/// Checkbox + caption pair. Callback receives the NEW state.
CCMenuItemToggler* toggle(cocos2d::CCMenu* menu, cocos2d::CCNode* labelParent, cocos2d::CCPoint pos,
                          std::string const& caption, bool value, std::function<void(bool)> callback,
                          float scale = .5f);

/// "Label: value" row where the value is colored.
cocos2d::CCNode* statRow(std::string const& name, std::string const& value, float width,
                         cocos2d::ccColor3B valueColor = kText, float scale = .3f);

void showInfo(std::string const& title, std::string const& text);

/// Runs `fn` on the next frame while keeping `owner` alive. Use it whenever a
/// button callback rebuilds the menu that contains that button: removing a
/// CCMenuItem from inside its own callback would free it while it is running.
void defer(cocos2d::CCNode* owner, std::function<void()> fn);

} // namespace fwa::ui
