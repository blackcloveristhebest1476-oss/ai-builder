#pragma once

// Per frame-window-range sounds for playback. Each range is identified by its
// lower edge, so switching between "1-2" and "1" label styles keeps the choice.

#include <string>
#include <vector>

namespace fwa::sounds {

struct Preset {
    char const* name;
    char const* file; // GD resource file name
};

/// Built-in Geometry Dash sound effects to choose from.
std::vector<Preset> const& presets();

/// "" = no sound. Either a preset file name or an absolute path to a custom file.
std::string soundForEdge(double lowerEdge);
void setSoundForEdge(double lowerEdge, std::string const& file);

float volume();          // 0..1
void setVolume(float v);
bool enabled();
void setEnabled(bool on);

/// Plays a sound (ignores "" and failures).
void play(std::string const& file);
/// Short display name for a stored value.
std::string displayName(std::string const& file);

} // namespace fwa::sounds
