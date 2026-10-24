#pragma once

// The analysis document: every input of a macro (or row of imported
// frame-window data) together with its timing-window result and an honest
// label saying where that result came from.

#include "Formats.hpp"
#include "Json.hpp"
#include "Macro.hpp"
#include "Nandl.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fwa {

/// Where a frame window value came from. The UI and exports always show this.
enum class WindowStatus {
    Unknown,        // not measured yet / no data
    Verified,       // measured by replaying the real game with the input shifted
    LowerBound,     // measured, but the search stopped at a limit (window is at least this large)
    Inconsistent,   // repeated trials disagreed (non-deterministic level/setup)
    BaselineFailed, // the unmodified macro did not survive this input, so nothing can be measured
    UserSupplied,   // imported from a frame-window file or typed in by the user
    Estimated,      // a default/assumed value chosen by the user (or sample data), never a measurement
    Excluded,       // not analyzed (duplicate/redundant event, release when releases are not counted, ...)
};

char const* statusName(WindowStatus s);
char const* statusShortName(WindowStatus s);
std::optional<WindowStatus> statusFromName(std::string_view s);
/// Measured statuses are the ones that came out of the replay engine.
bool isMeasured(WindowStatus s);

struct WindowResult {
    WindowStatus status = WindowStatus::Unknown;
    // Measured windows, in physics ticks relative to the original input frame.
    int earlyTicks = 0;           // how many ticks earlier still survives
    int lateTicks = 0;            // how many ticks later still survives
    bool earlyLimited = false;    // search on the early side stopped at a limit, not at a failure
    bool lateLimited = false;
    std::string earlyLimitReason; // e.g. "previous input", "search limit"
    std::string lateLimitReason;
    // Non-measured windows (UserSupplied / Estimated), stored as a duration so
    // they convert correctly to any timing-window rate.
    std::optional<double> windowSeconds;
    int trials = 0;               // simulations run for this input
    std::string note;

    /// Total valid timing positions in ticks (early + late + 1) for measured results.
    int totalTicks() const { return earlyTicks + lateTicks + 1; }
};

struct InputRecord {
    size_t index = 0;                 // 0-based chronological position
    std::optional<uint64_t> frame;    // physics tick (macros only)
    double time = 0.0;                // seconds from level start
    uint8_t button = 1;
    bool player2 = false;
    bool down = true;
    bool duplicate = false;
    bool redundant = false;
    int64_t sourceInputNumber = 0;    // input number from imported NaNDL/CSV rows
    std::string gameMode;             // captured during measurement ("" = unknown)
    WindowResult window;
};

enum class SourceKind { None, Macro, WindowData, Sample, Saved };

struct AnalysisSource {
    SourceKind kind = SourceKind::None;
    std::string formatId;
    std::string formatName;
    std::string fileName;
    std::optional<MacroMetadata> macroMeta;
    std::vector<Diagnostic> diagnostics;
};

struct MeasurementInfo {
    bool performed = false;
    std::string finishedAt;        // ISO-like timestamp, set by the mod
    std::string levelName;
    int64_t levelId = 0;
    double horizonSeconds = 0.0;
    int maxShiftTicks = 0;
    bool recheckBorders = false;
    std::string shiftMode;         // "whole-click" or "press-only"
    bool determinismChecked = false;
    bool determinismPassed = false;
    std::string notes;
};

struct CountOptions {
    bool includeReleases = false;  // count release events as inputs
    bool player1 = true;
    bool player2 = true;
};

struct Analysis {
    AnalysisSource source;
    double tps = 240.0;            // physics ticks per second of the inputs
    std::vector<InputRecord> inputs;
    MeasurementInfo measurement;

    bool empty() const { return inputs.empty(); }
    double durationSeconds() const { return inputs.empty() ? 0.0 : inputs.back().time; }
};

Analysis analysisFromMacro(Macro const& macro);
/// windowFpsFallback is used when the dataset does not say which rate its windows use.
Analysis analysisFromWindowData(WindowDataset const& ds, std::string formatId, std::string formatName,
                                std::string fileName, double windowFpsFallback, std::vector<Diagnostic> diags);

/// Is this record part of the counted input set?
bool isCounted(InputRecord const& r, CountOptions const& opt);
/// Is this record a candidate for in-game measurement?
bool isMeasurable(InputRecord const& r, CountOptions const& opt);

/// Frame window of a record expressed in frames at `windowFps` (NaNDL's N_i), or
/// nullopt when there is no usable value.
std::optional<double> windowFrames(InputRecord const& r, double windowFps, double tps, bool includeLowerBounds);

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

struct Histogram {
    std::vector<double> edges;        // ascending lower edges; last bucket is [edges.back(), inf)
    std::vector<size_t> counts;
    std::vector<std::string> labels;  // "0-1", "1-2", ..., "10+"
};

/// Parses "0,1,2,3,4,5,6,8,10" (ascending, unique, >= 0). Falls back to the default on error.
std::vector<double> parseEdges(std::string_view text, std::string* error = nullptr);
std::vector<double> defaultEdges();
Histogram makeHistogram(std::vector<double> const& values, std::vector<double> const& edges);

struct WindowStats {
    size_t totalInputs = 0;
    size_t countedInputs = 0;
    size_t withWindow = 0;       // counted inputs with a usable window
    size_t verified = 0;
    size_t lowerBound = 0;
    size_t inconsistent = 0;
    size_t baselineFailed = 0;
    size_t userSupplied = 0;
    size_t estimated = 0;
    size_t unknown = 0;
    double average = 0.0;        // frames at windowFps
    double median = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    std::optional<size_t> narrowestVerifiedIndex;
    std::optional<size_t> widestVerifiedIndex;
    double narrowestVerified = 0.0;
    double widestVerified = 0.0;
    size_t framePerfect = 0;     // window <= framePerfectThreshold frames
    size_t tight = 0;            // window <= tightThreshold frames (includes frame perfect)
    size_t centered = 0;         // measured: |early - late| <= 1 tick
    size_t earlyBiased = 0;      // measured: more room early than late
    size_t lateBiased = 0;
    Histogram histogram;
};

struct StatsOptions {
    double windowFps = 240.0;
    double framePerfectThreshold = 1.0;
    double tightThreshold = 2.0;
    bool includeLowerBounds = true;
    std::vector<double> edges = defaultEdges();
    CountOptions count;
};

WindowStats computeWindowStats(Analysis const& a, StatsOptions const& opt);

enum class Asymmetry { NotMeasured, Centered, EarlyBiased, LateBiased };
Asymmetry asymmetryOf(WindowResult const& w);
char const* asymmetryName(Asymmetry a);

// ---------------------------------------------------------------------------
// NaNDL dataset
// ---------------------------------------------------------------------------

struct NandlDataset {
    std::vector<nandl::Row> rows;
    std::vector<size_t> recordIndex;   // rows[i] came from analysis.inputs[recordIndex[i]]
    size_t ignoredForMissingWindow = 0; // counted inputs without a usable window -> "-"
    size_t lowerBoundsUsed = 0;
    size_t estimatedUsed = 0;
};

/// Builds rows from counted inputs. Input numbers are 1..n in chronological
/// order (for imported NaNDL data, the original input numbers are kept).
NandlDataset buildNandlDataset(Analysis const& a, CountOptions const& count, double windowFps, bool includeLowerBounds);

// ---------------------------------------------------------------------------
// Export / persistence
// ---------------------------------------------------------------------------

struct ExportSettings {
    double gameFps = 240.0;
    double windowFps = 240.0;
    double respawnSeconds = 0.0;
    double targetSeconds = 86400.0;
    double tolerance = 1.0;
    int maxIterations = 240;
    nandl::Constants constants;
    bool nerveEnabled = false;
    bool fatigueEnabled = false;
    bool cpsEnabled = false;
    StatsOptions stats;
};

struct PrecisionSnapshot {
    bool solved = false;
    std::string mode;          // "solve" or "fixed"
    double precision = 0.0;
    nandl::Stats stats;
    std::string status;
    double absoluteError = 0.0;
    int iterations = 0;
};

/// Full analysis export (re-importable).
json::Value toJson(Analysis const& a, ExportSettings const& s, std::optional<PrecisionSnapshot> const& precision,
                   std::string const& generator);
Expected<Analysis> analysisFromJson(json::Value const& doc, std::vector<Diagnostic>& diags,
                                    std::optional<ExportSettings>* settingsOut = nullptr);

/// One row per input; `passProbabilities` is optional (indexed by record index, NaN = none).
std::string toCsv(Analysis const& a, ExportSettings const& s, std::vector<double> const& passProbabilities);

/// Export the counted rows in the NaNDL calculator's own JSON format, so the
/// data can be loaded into https://nandl.pages.dev/ for cross-checking.
json::Value toNandlCalculatorJson(Analysis const& a, ExportSettings const& s);

// ---------------------------------------------------------------------------
// Sample data
// ---------------------------------------------------------------------------

/// Deterministic synthetic data for trying the UI without a macro. Every
/// window is labelled Estimated with a note saying it is synthetic.
Analysis makeSampleAnalysis();

} // namespace fwa
