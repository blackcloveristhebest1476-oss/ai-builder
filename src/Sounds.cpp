#include "Sounds.hpp"
#include "core/Util.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>

using namespace geode::prelude;

namespace fwa::sounds {

std::vector<Preset> const& presets() {
    static std::vector<Preset> const list = {
        {"Death Bong", kDefaultSound},
        {"Click", "counter003.ogg"},
        {"Coin", "gold02.ogg"},
        {"Reward", "reward01.ogg"},
        {"Achievement", "achievement_01.ogg"},
        {"New Best", "highscoreGet02.ogg"},
        {"Play", "playSound_01.ogg"},
        {"Quit", "quitSound_01.ogg"},
        {"Magic", "magicExplosion.ogg"},
        {"Secret Key", "secretKey.ogg"},
        {"Unlock", "unlockPath.ogg"},
        {"Door", "door001.ogg"},
        {"Explosion", "explode_11.ogg"},
    };
    return list;
}

namespace {

std::string key(double edge) {
    return "sound-edge-" + num(edge, 4);
}

/// Absolute path of a stored sound value, or "" when the file cannot be found.
std::string resolve(std::string const& file) {
    std::error_code ec;
    if (file == kDefaultSound) {
        auto p = Mod::get()->getResourcesDir() / "death-bong.ogg";
        if (std::filesystem::exists(p, ec)) return utils::string::pathToString(p);
        std::string full = CCFileUtils::sharedFileUtils()->fullPathForFilename("death-bong.ogg"_spr, false);
        return std::filesystem::exists(std::filesystem::path(full), ec) ? full : "";
    }
    auto path = std::filesystem::path(file);
    if (path.is_absolute()) return std::filesystem::exists(path, ec) ? file : "";
    std::string full = CCFileUtils::sharedFileUtils()->fullPathForFilename(file.c_str(), false);
    return std::filesystem::exists(std::filesystem::path(full), ec) ? full : "";
}

} // namespace

std::string soundForEdge(double lowerEdge) {
    auto k = key(lowerEdge);
    if (!Mod::get()->hasSavedValue(k)) return kDefaultSound;
    return Mod::get()->getSavedValue<std::string>(k);
}

void setSoundForEdge(double lowerEdge, std::string const& file) {
    Mod::get()->setSavedValue<std::string>(key(lowerEdge), file);
}

float volume() {
    if (!Mod::get()->hasSavedValue("sound-volume")) return .8f;
    return std::clamp(static_cast<float>(Mod::get()->getSavedValue<double>("sound-volume")), 0.f, 1.f);
}

void setVolume(float v) {
    Mod::get()->setSavedValue<double>("sound-volume", std::clamp(v, 0.f, 1.f));
}

bool enabled() {
    if (!Mod::get()->hasSavedValue("sounds-enabled")) return true;
    return Mod::get()->getSavedValue<bool>("sounds-enabled");
}

void setEnabled(bool on) {
    Mod::get()->setSavedValue<bool>("sounds-enabled", on);
}

float pitchStep() {
    if (!Mod::get()->hasSavedValue("sound-pitch-step")) return 1.5f;
    return std::clamp(static_cast<float>(Mod::get()->getSavedValue<double>("sound-pitch-step")), 0.f, 4.f);
}

void setPitchStep(float semitones) {
    Mod::get()->setSavedValue<double>("sound-pitch-step", std::clamp(semitones, 0.f, 4.f));
}

float pitchForBucket(size_t bucket) {
    return std::pow(2.f, static_cast<float>(bucket) * pitchStep() / 12.f);
}

void play(std::string const& file, float pitch) {
    if (file.empty()) return;
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;

    // Sounds are loaded once and kept: they are short and played often.
    static std::unordered_map<std::string, FMOD::Sound*> cache;
    FMOD::Sound* sound = nullptr;
    if (auto it = cache.find(file); it != cache.end()) {
        sound = it->second;
    } else {
        auto path = resolve(file);
        if (path.empty()) {
            log::warn("Frame Window Analyzer: sound not found: {}", file);
            return;
        }
        if (engine->m_system->createSound(path.c_str(), FMOD_DEFAULT, nullptr, &sound) != FMOD_OK || !sound) {
            log::warn("Frame Window Analyzer: could not load sound {}", path);
            return;
        }
        cache[file] = sound;
    }

    FMOD::Channel* channel = nullptr;
    if (engine->m_system->playSound(sound, nullptr, true, &channel) != FMOD_OK || !channel) return;
    channel->setVolume(volume());
    channel->setPitch(std::clamp(pitch, .25f, 4.f));
    channel->setPaused(false);
}

std::string displayName(std::string const& file) {
    if (file.empty()) return "None";
    for (auto const& p : presets()) {
        if (file == p.file) return p.name;
    }
    auto name = utils::string::pathToString(std::filesystem::path(file).filename());
    return name.empty() ? file : name;
}

} // namespace fwa::sounds
