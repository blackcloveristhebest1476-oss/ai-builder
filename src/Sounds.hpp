#pragma once

// Per frame-window-range sounds for playback. Each range is identified by its
// lower edge, so switching between "1-2" and "1" label styles keeps the choice.
//
// By default every range plays the bundled "death bong" sound; the narrowest
// range plays it at normal pitch and each wider range a little higher.

#include <cstddef>
#include <string>
#include <vector>

namespace fwa::sounds {

struct Preset {
    char const* name;
    char const* file; // GD resource file name, or kDefaultSound
};

/// Stored value meaning "the bundled default sound".
inline constexpr char const* kDefaultSound = "@default";

/// The default sound first, then built-in Geometry Dash sound effects.
std::vector<Preset> const& presets();

/// "" = no sound. kDefaultSound, a GD resource name, or an absolute path to a custom file.
/// Ranges nobody has changed use kDefaultSound.
std::string soundForEdge(double lowerEdge);
void setSoundForEdge(double lowerEdge, std::string const& file);

float volume();          // 0..1
void setVolume(float v);
bool enabled();
void setEnabled(bool on);

/// Semitones the pitch rises per range (0 = every range at normal pitch).
float pitchStep();
void setPitchStep(float semitones);
/// Pitch multiplier for range `bucket` (0 = narrowest = 1.0).
float pitchForBucket(size_t bucket);

/// Plays a sound at the given pitch multiplier (ignores "" and failures).
void play(std::string const& file, float pitch = 1.f);
/// Short display name for a stored value.
std::string displayName(std::string const& file);

} // namespace fwa::sounds
