#pragma once

#include "core/Analysis.hpp"
#include "core/Nandl.hpp"

#include <string>
#include <vector>

namespace fwa {

/// Snapshot of the mod's settings, read once per use so a calculation never
/// sees settings change halfway through.
struct Settings {
    double gameFps = 240.0;
    double windowFps = 240.0;
    double targetSeconds = 86400.0;
    double respawnSeconds = 0.0;
    double fixedPrecision = 100.0;
    double tolerance = 1.0;
    int maxIterations = 240;
    bool nerve = false, fatigue = false, cps = false;
    double kt = nandl::kDefaultNerve, ku = nandl::kDefaultFatigue, kc = nandl::kDefaultCps;
    bool countReleases = false;
    bool includeLowerBounds = true;
    double framePerfectThreshold = 1.0;
    double tightThreshold = 2.0;
    std::vector<double> edges;
    std::string edgesError;
    int maxShift = 24;
    double horizonSeconds = 1.5;
    bool recheck = true;
    bool determinismCheck = true;
    bool wholeClick = true;
    bool tap = false;
    int tapTicks = 1;
    bool countDistinct = true;
    int budgetMs = 14;
    std::string theme = "Rainbow";
    bool autoSave = true;
    bool background = true;

    static Settings load();

    CountOptions count() const;
    StatsOptions stats() const;
    nandl::Constants constants() const; // disabled modifiers -> 0, like the calculator
    nandl::RunOptions runOptions() const;
    nandl::SolveOptions solveOptions() const;
    ExportSettings exportSettings() const;

    static void set(char const* key, bool value);
    static void resetAll();
};

} // namespace fwa
