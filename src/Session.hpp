#pragma once

// Global, main-thread-only state shared by the analyzer UI and the
// measurement engine: the loaded analysis, precision results and persistence.

#include "Settings.hpp"
#include "core/Analysis.hpp"
#include "core/Formats.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace fwa {

/// A precision calculation that may run on a worker thread. The worker only
/// touches this object (which it shares), never the Session.
struct PrecisionJob {
    enum class State { Running, Done };
    std::atomic<State> state{State::Running};
    std::atomic<bool> cancel{false};
    std::atomic<double> progress{0.0};

    // Inputs (copied when the job starts)
    std::vector<nandl::Row> rows;
    std::vector<size_t> recordIndex;
    nandl::RunOptions run;
    nandl::Constants constants;
    nandl::SolveOptions solve;
    bool fixedMode = false;
    double fixedPrecision = 0.0;
    uint64_t revision = 0;       // analysis revision the job was started for

    // Outputs (written by the worker before state becomes Done)
    nandl::SolveResult result;
    std::vector<nandl::InputDetail> details;

    void execute();
};

class Session {
public:
    static Session& get();

    bool hasData() const { return !m_analysis.empty(); }
    Analysis const& analysis() const { return m_analysis; }
    Analysis& analysisMut() { touch(); return m_analysis; }
    /// Bumped on every change; the UI uses it to know when to rebuild.
    uint64_t revision() const { return m_revision; }
    void touch() { m_revision++; }

    /// Replace the loaded data (clears precision results).
    void setAnalysis(Analysis a);
    void clear();

    /// Reads and imports a file. Returns an error message on failure.
    std::optional<std::string> importFile(std::filesystem::path const& path);
    void loadSample();

    // ---- precision ----
    /// Starts a precision calculation (background or inline depending on settings).
    /// Returns an error message if it could not start.
    std::optional<std::string> startPrecision(bool fixedMode);
    void cancelPrecision();
    std::shared_ptr<PrecisionJob> const& job() const { return m_job; }
    /// Call every frame from the UI; moves finished job results into the session.
    /// Returns true if a job finished during this call.
    bool pollPrecision();
    std::optional<PrecisionSnapshot> const& precision() const { return m_precision; }
    /// p_i per record index (NaN where not part of the calculation).
    std::vector<double> const& passProbabilities() const { return m_pass; }
    std::vector<double> const& sigmas() const { return m_sigma; }
    std::string const& precisionMessage() const { return m_precisionMessage; }
    size_t precisionIgnoredInputs() const { return m_precisionIgnored; }
    size_t precisionLowerBounds() const { return m_precisionLowerBounds; }
    size_t precisionEstimated() const { return m_precisionEstimated; }

    // ---- persistence ----
    std::filesystem::path analysesDir() const;
    /// Saves to the analyses folder; returns the path or an error.
    std::variant<std::filesystem::path, std::string> saveToFolder(std::string const& reason);
    void autoSave(std::string const& reason);
    std::string exportJson() const;
    std::string exportCsv() const;
    std::string exportNandl() const;
    std::string suggestedBaseName() const;
    std::string generatorString() const;

    std::string const& lastMessage() const { return m_lastMessage; }
    void setMessage(std::string m) { m_lastMessage = std::move(m); }

private:
    Session() = default;
    Analysis m_analysis;
    uint64_t m_revision = 1;
    std::shared_ptr<PrecisionJob> m_job;
    std::optional<PrecisionSnapshot> m_precision;
    std::vector<double> m_pass;
    std::vector<double> m_sigma;
    std::string m_precisionMessage;
    size_t m_precisionIgnored = 0;
    size_t m_precisionLowerBounds = 0;
    size_t m_precisionEstimated = 0;
    std::string m_lastMessage;
};

std::string nowTimestamp(bool forFileName);
std::string formatDuration(double seconds);
std::string formatProbability(double p, double lnP);
std::string formatLog10Count(double log10Value);

} // namespace fwa
