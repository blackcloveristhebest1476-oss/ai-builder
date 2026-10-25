#include "Settings.hpp"

#include <Geode/Geode.hpp>

#include <cmath>

using namespace geode::prelude;

namespace fwa {

namespace {

double getD(char const* key, double def) {
    double v = Mod::get()->getSettingValue<double>(key);
    return std::isfinite(v) ? v : def;
}

int getI(char const* key, int def) {
    auto v = Mod::get()->getSettingValue<int64_t>(key);
    if (v < -1000000000 || v > 1000000000) return def;
    return static_cast<int>(v);
}

bool getB(char const* key) {
    return Mod::get()->getSettingValue<bool>(key);
}

} // namespace

Settings Settings::load() {
    Settings s;
    s.gameFps = getD("game-fps", 240.0);
    s.windowFps = getD("window-fps", 240.0);
    s.targetSeconds = getD("target-time", 86400.0);
    s.respawnSeconds = getD("respawn-time", 0.0);
    s.fixedPrecision = getD("fixed-precision", 100.0);
    s.tolerance = getD("solver-tolerance", 1.0);
    s.maxIterations = getI("solver-max-iterations", 240);
    s.nerve = getB("nerve-enabled");
    s.fatigue = getB("fatigue-enabled");
    s.cps = getB("cps-enabled");
    s.kt = getD("nerve-constant", nandl::kDefaultNerve);
    s.ku = getD("fatigue-constant", nandl::kDefaultFatigue);
    s.kc = getD("cps-constant", nandl::kDefaultCps);
    s.countReleases = getB("count-releases");
    s.includeLowerBounds = getB("include-lower-bounds");
    s.framePerfectThreshold = getD("frame-perfect-threshold", 1.0);
    s.tightThreshold = getD("tight-threshold", 2.0);
    s.edges = parseEdges(Mod::get()->getSettingValue<std::string>("histogram-edges"), &s.edgesError);
    s.maxShift = getI("measure-max-shift", 24);
    s.horizonSeconds = getD("measure-horizon", 1.5);
    s.recheck = getB("measure-recheck");
    s.determinismCheck = getB("measure-determinism-check");
    auto mode = Mod::get()->getSettingValue<std::string>("measure-shift-mode");
    s.wholeClick = mode != "Press only";
    s.tap = mode.rfind("Tap", 0) == 0;
    s.tapTicks = getI("measure-tap-ticks", 1);
    s.countDistinct = Mod::get()->getSettingValue<std::string>("window-counting") != "Every surviving timing";
    setCountDistinctOutcomes(s.countDistinct);
    s.budgetMs = getI("measure-budget-ms", 14);
    s.theme = Mod::get()->getSettingValue<std::string>("color-theme");
    s.autoSave = getB("auto-save");
    s.background = getB("background-calculation");
    if (s.gameFps <= 0) s.gameFps = 240.0;
    if (s.windowFps <= 0) s.windowFps = 240.0;
    return s;
}

CountOptions Settings::count() const {
    CountOptions c;
    c.includeReleases = countReleases;
    return c;
}

StatsOptions Settings::stats() const {
    StatsOptions o;
    o.windowFps = windowFps;
    o.framePerfectThreshold = framePerfectThreshold;
    o.tightThreshold = tightThreshold;
    o.includeLowerBounds = includeLowerBounds;
    o.edges = edges.empty() ? defaultEdges() : edges;
    o.count = count();
    return o;
}

nandl::Constants Settings::constants() const {
    return {nerve ? kt : 0.0, fatigue ? ku : 0.0, cps ? kc : 0.0};
}

nandl::RunOptions Settings::runOptions() const {
    return {windowFps, respawnSeconds};
}

nandl::SolveOptions Settings::solveOptions() const {
    return {targetSeconds, tolerance, maxIterations};
}

ExportSettings Settings::exportSettings() const {
    ExportSettings e;
    e.gameFps = gameFps;
    e.windowFps = windowFps;
    e.respawnSeconds = respawnSeconds;
    e.targetSeconds = targetSeconds;
    e.tolerance = tolerance;
    e.maxIterations = maxIterations;
    e.constants = {kt, ku, kc};
    e.nerveEnabled = nerve;
    e.fatigueEnabled = fatigue;
    e.cpsEnabled = cps;
    e.stats = stats();
    return e;
}

void Settings::set(char const* key, bool value) {
    Mod::get()->setSettingValue<bool>(key, value);
}

void Settings::resetAll() {
    for (auto const& key : Mod::get()->getSettingKeys()) {
        if (auto setting = Mod::get()->getSetting(key)) {
            setting->reset();
        }
    }
}

} // namespace fwa
