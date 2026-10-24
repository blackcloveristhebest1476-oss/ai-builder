#pragma once

// NaNDL statistical model (https://nandl.pages.dev/).
//
// This is a faithful re-implementation of the published equations AND of the
// reference calculator's exact behaviour (calculator.js, v9), including:
//   * time positions get the respawn time added before anything else,
//   * a frame window of 0 is treated as 1,
//   * an ignored window ("-") passes with probability 1, unless the CPS
//     modifier is enabled, in which case it uses (largest window + 1),
//   * fatigue uses the row's input *number*, CPS uses the previous row
//     (starting from input 0 at time 0), with a zero/invalid interval treated
//     as 1 second per input,
//   * L* is found by the same expanding bracket + bisection, stopping when
//     |E[T_C] - target| <= tolerance.
// Differences, all for numerical robustness: P(C) is accumulated in log space
// and the fail probability q_i uses erfc directly, so huge macros do not
// underflow to 0 / return infinity where a finite answer exists.

#include "Expected.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fwa::nandl {

struct Row {
    int64_t input = 0;                  // input number i (fatigue + CPS)
    double time = 0.0;                  // seconds from level start, WITHOUT respawn
    std::optional<double> window;       // N_i in frames at windowFps; nullopt = ignored ("-")
};

struct RunOptions {
    double windowFps = 240.0;           // f
    double respawnSeconds = 0.0;
};

/// Modifier constants. 0 disables a modifier (exactly how the calculator does it).
struct Constants {
    double kt = 0.0; // nerve
    double ku = 0.0; // fatigue
    double kc = 0.0; // CPS (experimental)
};

/// NaNDL's default constants (from calculator.js DEFAULT_CONSTANTS).
inline constexpr double kDefaultNerve = 0.0016520833717346;
inline constexpr double kDefaultFatigue = 0.0002727763242154;
inline constexpr double kDefaultCps = 0.2784421686721826;

struct Stats {
    double expectedAttemptTime = 0.0;      // E[T_A] (s)
    double logCompletion = 0.0;            // ln P(C)  (-inf if impossible)
    double completionProbability = 0.0;    // P(C) (may underflow to 0; use logCompletion)
    double log10ExpectedAttempts = 0.0;    // log10(1 / P(C))
    double expectedTotalTime = 0.0;        // E[T_C] (s), +inf if it overflows / P(C) = 0
    double log10ExpectedTotalTime = 0.0;   // log10 E[T_C], finite whenever P(C) > 0 in log space
    bool completable = true;               // P(C) > 0
};

struct InputDetail {
    double sigma = 0.0;    // s_i with modifiers applied (the k passed to the normal CDF)
    double multiplier = 1.0;
    double pass = 1.0;     // p_i
    double fail = 0.0;     // q_i
    double logReach = 0.0; // ln r_i
};

/// Validates rows + options. Returns an error message or "".
std::string validate(std::vector<Row> const& rows, RunOptions const& opt, Constants const& c);

/// Computes stats for a fixed precision L (sigma/s). `details` is filled when non-null.
Stats computeStats(std::vector<Row> const& rows, RunOptions const& opt, double L, Constants const& c,
                   std::vector<InputDetail>* details = nullptr);

/// P(|X| <= k) for X ~ N(0, 1) (two-sided), and its complement, computed accurately.
double passProbability(double k);
double failProbability(double k);

struct SolveOptions {
    double targetSeconds = 86400.0;
    double tolerance = 1.0;
    int maxIterations = 240;
};

enum class SolveStatus { Converged, IterationLimit, NoBracket, InvalidInput, AllIgnored, Cancelled };
char const* statusName(SolveStatus s);

struct SolveResult {
    SolveStatus status = SolveStatus::InvalidInput;
    double precision = 0.0;   // L*
    Stats stats;
    double absoluteError = 0.0; // |E[T_C](L*) - target|
    int iterations = 0;
    int bracketExpansions = 0;
    std::string message;
};

/// progress(fraction in [0,1]) may be null. cancel may be null.
SolveResult solvePrecision(std::vector<Row> const& rows, RunOptions const& opt, Constants const& c,
                           SolveOptions const& so, std::atomic<bool> const* cancel = nullptr,
                           std::function<void(double)> const& progress = {});

} // namespace fwa::nandl
