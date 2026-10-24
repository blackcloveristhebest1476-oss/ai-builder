#include "Nandl.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fwa::nandl {

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kLn10 = 2.302585092994045684;
constexpr double kSqrt2 = 1.414213562373095049;
} // namespace

double passProbability(double k) {
    if (!(k > 0.0)) return 0.0;
    return std::clamp(std::erf(k / kSqrt2), 0.0, 1.0);
}

double failProbability(double k) {
    if (!(k > 0.0)) return 1.0;
    return std::clamp(std::erfc(k / kSqrt2), 0.0, 1.0);
}

char const* statusName(SolveStatus s) {
    switch (s) {
        case SolveStatus::Converged: return "Converged";
        case SolveStatus::IterationLimit: return "Iteration limit reached";
        case SolveStatus::NoBracket: return "No solution range";
        case SolveStatus::InvalidInput: return "Invalid input";
        case SolveStatus::AllIgnored: return "All windows ignored";
        case SolveStatus::Cancelled: return "Cancelled";
    }
    return "?";
}

std::string validate(std::vector<Row> const& rows, RunOptions const& opt, Constants const& c) {
    if (rows.empty()) return "No inputs to calculate. Add frame-window data first.";
    if (!std::isfinite(opt.windowFps) || opt.windowFps <= 0) return "Window FPS must be greater than 0.";
    if (!std::isfinite(opt.respawnSeconds) || opt.respawnSeconds < 0) return "Respawn time must be 0 seconds or greater.";
    if (!std::isfinite(c.kt) || !std::isfinite(c.ku) || !std::isfinite(c.kc)) return "Modifier constants must be finite numbers.";
    for (size_t i = 0; i < rows.size(); i++) {
        auto const& r = rows[i];
        if (!std::isfinite(r.time)) return "Input row " + std::to_string(i + 1) + " has an invalid time position.";
        if (r.window && (!std::isfinite(*r.window) || *r.window < 0)) {
            return "Input row " + std::to_string(i + 1) + " has an invalid frame window (must be >= 0 or ignored).";
        }
    }
    if (c.kc != 0.0) {
        // The calculator produces NaN (and refuses) when the local interval is negative.
        double prevTime = 0.0;
        int64_t prevInput = 0;
        for (size_t i = 0; i < rows.size(); i++) {
            double time = opt.respawnSeconds + rows[i].time;
            int64_t input = rows[i].input;
            if (input - prevInput != 0) {
                double dt = (time - prevTime) / static_cast<double>(input - prevInput);
                if (std::isfinite(dt) && dt < 0) {
                    return "The CPS modifier needs input numbers and time positions that increase together "
                           "(row " + std::to_string(i + 1) + " goes backwards).";
                }
            }
            prevInput = input;
            prevTime = time;
        }
    }
    return "";
}

Stats computeStats(std::vector<Row> const& rows, RunOptions const& opt, double L, Constants const& c,
                   std::vector<InputDetail>* details) {
    Stats st;
    if (details) {
        details->clear();
        details->reserve(rows.size());
    }
    if (rows.empty()) {
        st.completionProbability = 1.0;
        return st;
    }

    double maxFrames = 0.0;
    for (auto const& r : rows) {
        if (r.window) maxFrames = std::max(maxFrames, *r.window == 0.0 ? 1.0 : *r.window);
    }

    double const f = opt.windowFps;
    double prevTime = 0.0;
    int64_t prevInput = 0;

    double expected = 0.0;  // E[T_A] accumulator
    double logReach = 0.0;  // ln of P(reaching the current input)

    for (auto const& r : rows) {
        double const time = opt.respawnSeconds + r.time;
        double const nerve = std::exp(-c.kt * time);
        double const fatigue = std::exp(-c.ku * static_cast<double>(r.input));

        double deltaTime = 1.0;
        if (r.input - prevInput != 0) {
            double dt = (time - prevTime) / static_cast<double>(r.input - prevInput);
            deltaTime = (dt == 0.0 || std::isnan(dt)) ? 1.0 : dt; // JS: `... || 1`
        }
        double cps = 1.0;
        if (c.kc != 0.0) {
            cps = 1.0 / (std::pow(0.25, c.kc) * std::max(1.0, std::pow(2.0 / deltaTime, c.kc)));
        }
        double const multiplier = nerve * fatigue * cps;

        double pass, fail, k = 0.0;
        bool alwaysPass = false;
        if (!r.window) {
            if (c.kc <= 0.0) {
                alwaysPass = true;
            } else {
                k = std::max(((maxFrames + 1.0) / 2.0) * (L / f), 0.0) * multiplier;
            }
        } else {
            double const normalized = *r.window == 0.0 ? 1.0 : *r.window;
            k = std::max((normalized / 2.0) * (L / f), 0.0) * multiplier;
        }
        if (alwaysPass) {
            pass = 1.0;
            fail = 0.0;
        } else {
            pass = passProbability(k);
            fail = failProbability(k);
        }

        if (details) details->push_back({k, multiplier, pass, fail, logReach});

        // E[T_A] += t_i * r_i * q_i
        if (fail > 0.0 && logReach > -745.0) {
            expected += time * std::exp(logReach) * fail;
        }
        // r_{i+1} = r_i * p_i  (log space; log1p keeps precision when p is close to 1)
        if (pass <= 0.0) {
            logReach = -kInf;
        } else if (logReach != -kInf) {
            logReach += fail < 0.5 ? std::log1p(-fail) : std::log(pass);
        }

        prevInput = r.input;
        prevTime = time;
    }

    double const lastTime = opt.respawnSeconds + rows.back().time;
    if (logReach > -745.0) expected += lastTime * std::exp(logReach);

    st.expectedAttemptTime = expected;
    st.logCompletion = logReach;
    st.completable = logReach != -kInf;
    st.completionProbability = st.completable ? std::exp(logReach) : 0.0;
    if (!st.completable) {
        st.log10ExpectedAttempts = kInf;
        st.expectedTotalTime = kInf;
        st.log10ExpectedTotalTime = kInf;
    } else {
        st.log10ExpectedAttempts = -logReach / kLn10;
        if (expected <= 0.0) {
            st.expectedTotalTime = 0.0;
            st.log10ExpectedTotalTime = -kInf;
        } else {
            double lnTotal = std::log(expected) - logReach;
            st.log10ExpectedTotalTime = lnTotal / kLn10;
            st.expectedTotalTime = lnTotal > 709.0 ? kInf : std::exp(lnTotal);
        }
    }
    return st;
}

namespace {

/// True when E[T_C] > target or is not finite (same decision rule as the calculator).
bool aboveTarget(Stats const& s, double target) {
    if (!s.completable || !std::isfinite(s.expectedTotalTime)) return true;
    return s.expectedTotalTime > target;
}

double distanceTo(Stats const& s, double target) {
    return std::isfinite(s.expectedTotalTime) ? std::fabs(s.expectedTotalTime - target) : kInf;
}

} // namespace

SolveResult solvePrecision(std::vector<Row> const& rows, RunOptions const& opt, Constants const& c,
                           SolveOptions const& so, std::atomic<bool> const* cancel,
                           std::function<void(double)> const& progress) {
    SolveResult res;
    auto err = validate(rows, opt, c);
    if (!err.empty()) {
        res.status = SolveStatus::InvalidInput;
        res.message = err;
        return res;
    }
    if (!std::isfinite(so.targetSeconds) || so.targetSeconds <= 0) {
        res.status = SolveStatus::InvalidInput;
        res.message = "Target time must be greater than 0 seconds.";
        return res;
    }
    if (!std::isfinite(so.tolerance) || so.tolerance <= 0) {
        res.status = SolveStatus::InvalidInput;
        res.message = "Tolerance must be greater than 0 seconds.";
        return res;
    }
    if (so.maxIterations < 1 || so.maxIterations > 100000) {
        res.status = SolveStatus::InvalidInput;
        res.message = "Maximum iterations must be between 1 and 100000.";
        return res;
    }
    double const minimumCompletion = opt.respawnSeconds + rows.back().time;
    if (so.targetSeconds <= minimumCompletion) {
        res.status = SolveStatus::InvalidInput;
        res.message = "Target time must be greater than the final input time (" + num(minimumCompletion, 3) + " seconds).";
        return res;
    }

    double const target = so.targetSeconds;
    bool const allIgnored = std::all_of(rows.begin(), rows.end(), [](Row const& r) { return !r.window; });

    double low = 0.0;
    Stats lowStats = computeStats(rows, opt, low, c);
    if (allIgnored && std::isfinite(lowStats.expectedTotalTime) && lowStats.expectedTotalTime < target) {
        res.status = SolveStatus::AllIgnored;
        res.message = "This target time cannot be solved because every frame window is ignored. Add at least one numeric frame window.";
        res.stats = lowStats;
        return res;
    }

    double high = 100.0;
    Stats highStats = computeStats(rows, opt, high, c);
    int expansions = 0;
    while (aboveTarget(highStats, target) && expansions < 128) {
        if (cancel && cancel->load()) {
            res.status = SolveStatus::Cancelled;
            res.message = "Calculation cancelled.";
            return res;
        }
        high *= 2.0;
        highStats = computeStats(rows, opt, high, c);
        expansions++;
    }
    res.bracketExpansions = expansions;
    if (aboveTarget(highStats, target)) {
        res.status = SolveStatus::NoBracket;
        res.message = "The solver could not find a precision range for this target time.";
        return res;
    }

    double best = high;
    Stats bestStats = highStats;
    double bestDistance = distanceTo(highStats, target);

    int it = 0;
    for (; it < so.maxIterations && bestDistance > so.tolerance; it++) {
        if (cancel && cancel->load()) {
            res.status = SolveStatus::Cancelled;
            res.message = "Calculation cancelled.";
            return res;
        }
        double mid = (low + high) / 2.0;
        Stats midStats = computeStats(rows, opt, mid, c);
        double d = distanceTo(midStats, target);
        if (d < bestDistance) {
            best = mid;
            bestStats = midStats;
            bestDistance = d;
        }
        if (aboveTarget(midStats, target)) low = mid;
        else high = mid;
        if (progress) progress(static_cast<double>(it + 1) / so.maxIterations);
    }

    res.precision = best;
    res.stats = bestStats;
    res.absoluteError = bestDistance;
    res.iterations = it;
    if (bestDistance <= so.tolerance) {
        res.status = SolveStatus::Converged;
        res.message = "Converged after " + std::to_string(it) + " bisection step(s).";
    } else {
        res.status = SolveStatus::IterationLimit;
        res.message = "Stopped after " + std::to_string(it) + " iteration(s); the closest result is " +
            num(bestDistance, 3) + " s away from the target. Raise the iteration limit or the tolerance.";
    }
    if (progress) progress(1.0);
    return res;
}

} // namespace fwa::nandl
