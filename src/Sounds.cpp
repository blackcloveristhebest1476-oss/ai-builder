#include "Sounds.hpp"
#include "core/Util.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <algorithm>
#include <filesystem>

using namespace geode::prelude;

namespace fwa::sounds {

std::vector<Preset> const& presets() {
    static std::vector<Preset> const list = {
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
} // namespace

std::string soundForEdge(double lowerEdge) {
    auto k = key(lowerEdge);
    if (!Mod::get()->hasSavedValue(k)) return "";
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

void play(std::string const& file) {
    if (file.empty()) return;
    if (auto engine = FMODAudioEngine::sharedEngine()) {
        engine->playEffect(file, 1.f, 0.f, volume());
    }
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
