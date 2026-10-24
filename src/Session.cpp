#include "Session.hpp"
#include "core/Util.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>

#include <cmath>
#include <ctime>
#include <limits>
#include <thread>

using namespace geode::prelude;

namespace fwa {

// ---------------------------------------------------------------------------
// formatting helpers
// ---------------------------------------------------------------------------

std::string nowTimestamp(bool forFileName) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), forFileName ? "%Y%m%d-%H%M%S" : "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::string formatDuration(double seconds) {
    if (std::isnan(seconds)) return "-";
    if (std::isinf(seconds)) return "infinite";
    if (seconds < 60) return num(seconds, 2) + " s";
    if (seconds < 3600) return num(seconds / 60.0, 2) + " min";
    if (seconds < 86400 * 2) return num(seconds / 3600.0, 2) + " h";
    if (seconds < 86400.0 * 365 * 2) return num(seconds / 86400.0, 2) + " days";
    double years = seconds / (86400.0 * 365.25);
    if (years < 1e6) return num(years, 1) + " years";
    return "10^" + num(std::log10(years), 1) + " years";
}

std::string formatLog10Count(double log10Value) {
    if (std::isnan(log10Value)) return "-";
    if (std::isinf(log10Value)) return log10Value > 0 ? "infinite" : "0";
    if (log10Value < 9) {
        double v = std::pow(10.0, log10Value);
        return num(std::round(v), 0);
    }
    double exponent = std::floor(log10Value);
    double mantissa = std::pow(10.0, log10Value - exponent);
    return num(mantissa, 2) + "e" + num(exponent, 0);
}

std::string formatProbability(double p, double lnP) {
    if (std::isfinite(p) && p > 0) {
        if (p >= 0.001) return num(p * 100.0, 3) + "%";
        return sig(p * 100.0, 4) + "%";
    }
    if (std::isfinite(lnP)) {
        double log10p = lnP / std::log(10.0) + 2.0; // percent
        double exponent = std::floor(log10p);
        double mantissa = std::pow(10.0, log10p - exponent);
        return num(mantissa, 2) + "e" + num(exponent, 0) + "%";
    }
    return "0%";
}

// ---------------------------------------------------------------------------
// PrecisionJob
// ---------------------------------------------------------------------------

void PrecisionJob::execute() {
    if (fixedMode) {
        result = {};
        result.precision = fixedPrecision;
        auto err = nandl::validate(rows, run, constants);
        if (!err.empty()) {
            result.status = nandl::SolveStatus::InvalidInput;
            result.message = err;
        } else {
            result.stats = nandl::computeStats(rows, run, fixedPrecision, constants, &details);
            result.status = nandl::SolveStatus::Converged;
            result.message = "Stats for the fixed precision L = " + num(fixedPrecision, 4) + ".";
        }
    } else {
        result = nandl::solvePrecision(rows, run, constants, solve, &cancel,
            [this](double p) { progress.store(p, std::memory_order_relaxed); });
        if (result.status == nandl::SolveStatus::Converged || result.status == nandl::SolveStatus::IterationLimit) {
            nandl::computeStats(rows, run, result.precision, constants, &details);
        }
    }
    progress.store(1.0);
    state.store(State::Done, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

Session& Session::get() {
    static Session instance;
    return instance;
}

void Session::setAnalysis(Analysis a) {
    cancelPrecision();
    m_analysis = std::move(a);
    m_precision.reset();
    m_pass.clear();
    m_sigma.clear();
    m_precisionMessage.clear();
    touch();
}

void Session::clear() {
    setAnalysis(Analysis{});
}

std::optional<std::string> Session::importFile(std::filesystem::path const& path) {
    auto settings = Settings::load();
    auto data = file::readBinary(path);
    if (!data) return "Could not read the file: " + std::string(data.unwrapErr());
    auto bytes = std::move(data).unwrap();
    auto fileName = utils::string::pathToString(path.filename());

    auto res = FormatRegistry::get().importData(std::span<uint8_t const>(bytes.data(), bytes.size()), fileName);
    if (!res) return res.error();
    auto imported = std::move(res).value();

    switch (imported.kind) {
        case ImportKind::Macro: {
            auto macro = std::move(*imported.macro);
            if (!macro.meta.framerateFromFile && settings.gameFps != macro.meta.framerate) {
                macro.meta.framerate = settings.gameFps;
                macro.addDiagnostic(Severity::Info, "Using your Game FPS setting (" + num(settings.gameFps) +
                    ") as the physics rate because the file does not store one.");
            }
            auto analysis = analysisFromMacro(macro);
            setAnalysis(std::move(analysis));
            setMessage("Loaded " + fileName + " (" + macro.formatName + ", " + std::to_string(macro.inputs.size()) + " inputs).");
            break;
        }
        case ImportKind::WindowData: {
            auto ds = std::move(*imported.windows);
            if (!ds.gameFps) ds.gameFps = settings.gameFps;
            auto analysis = analysisFromWindowData(ds, imported.formatId, imported.formatName, fileName,
                settings.windowFps, imported.diagnostics);
            setAnalysis(std::move(analysis));
            setMessage("Loaded " + std::to_string(ds.rows.size()) + " frame-window rows from " + fileName + ".");
            break;
        }
        case ImportKind::SavedAnalysis: {
            std::vector<Diagnostic> diags;
            std::optional<ExportSettings> saved;
            auto a = analysisFromJson(*imported.analysisJson, diags, &saved);
            if (!a) return a.error();
            auto analysis = std::move(a).value();
            for (auto& d : diags) analysis.source.diagnostics.push_back(d);
            if (saved && (std::fabs(saved->windowFps - settings.windowFps) > 1e-9 ||
                          std::fabs(saved->respawnSeconds - settings.respawnSeconds) > 1e-9)) {
                analysis.source.diagnostics.push_back({Severity::Info,
                    "This save was made with Timing-Window FPS " + num(saved->windowFps) + " and respawn " +
                    num(saved->respawnSeconds) + " s. Your current settings are used for new calculations."});
            }
            setAnalysis(std::move(analysis));
            setMessage("Loaded saved analysis " + fileName + ".");
            break;
        }
    }
    return std::nullopt;
}

void Session::loadSample() {
    setAnalysis(makeSampleAnalysis());
    setMessage("Loaded synthetic sample data. Nothing in it was measured.");
}

std::optional<std::string> Session::startPrecision(bool fixedMode) {
    if (m_job) return "A calculation is already running.";
    if (!hasData()) return "Import a macro or frame-window data first.";
    auto s = Settings::load();
    auto ds = buildNandlDataset(m_analysis, s.count(), s.windowFps, s.includeLowerBounds);
    if (ds.rows.empty()) return "There are no counted inputs. (Releases are only counted if 'Count Releases' is on.)";
    if (ds.ignoredForMissingWindow == ds.rows.size() && !s.cps) {
        return "No frame windows are available yet. Measure them in-game (Measure button) or import "
               "frame-window data. Precision is never calculated from invented windows.";
    }
    auto job = std::make_shared<PrecisionJob>();
    job->rows = std::move(ds.rows);
    job->recordIndex = std::move(ds.recordIndex);
    job->run = s.runOptions();
    job->constants = s.constants();
    job->solve = s.solveOptions();
    job->fixedMode = fixedMode;
    job->fixedPrecision = s.fixedPrecision;
    job->revision = m_revision;
    auto err = nandl::validate(job->rows, job->run, job->constants);
    if (!err.empty()) return err;

    m_precisionIgnored = ds.ignoredForMissingWindow;
    m_precisionLowerBounds = ds.lowerBoundsUsed;
    m_precisionEstimated = ds.estimatedUsed;
    m_job = job;
    if (s.background) {
        std::thread([job] { job->execute(); }).detach();
    } else {
        job->execute();
    }
    return std::nullopt;
}

void Session::cancelPrecision() {
    if (m_job) {
        m_job->cancel.store(true);
        m_job.reset(); // the worker keeps its own reference and finishes on its own
    }
}

bool Session::pollPrecision() {
    if (!m_job || m_job->state.load(std::memory_order_acquire) != PrecisionJob::State::Done) return false;
    auto job = std::move(m_job);
    m_job.reset();
    if (job->revision != m_revision) {
        m_precisionMessage = "The result was discarded because the data changed during the calculation.";
        return true;
    }
    auto const& r = job->result;
    bool usable = r.status == nandl::SolveStatus::Converged || r.status == nandl::SolveStatus::IterationLimit;
    m_precisionMessage = r.message;
    if (!usable) {
        m_precision.reset();
        m_pass.clear();
        m_sigma.clear();
        return true;
    }
    PrecisionSnapshot snap;
    snap.solved = !job->fixedMode;
    snap.mode = job->fixedMode ? "fixed" : "solve";
    snap.precision = r.precision;
    snap.stats = r.stats;
    snap.status = nandl::statusName(r.status);
    snap.absoluteError = r.absoluteError;
    snap.iterations = r.iterations;
    m_precision = snap;
    m_pass.assign(m_analysis.inputs.size(), std::numeric_limits<double>::quiet_NaN());
    m_sigma.assign(m_analysis.inputs.size(), std::numeric_limits<double>::quiet_NaN());
    for (size_t i = 0; i < job->details.size() && i < job->recordIndex.size(); i++) {
        size_t idx = job->recordIndex[i];
        if (idx < m_pass.size()) {
            m_pass[idx] = job->details[i].pass;
            m_sigma[idx] = job->details[i].sigma;
        }
    }
    if (Settings::load().autoSave) autoSave("precision");
    return true;
}

std::filesystem::path Session::analysesDir() const {
    auto dir = Mod::get()->getSaveDir() / "analyses";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string Session::suggestedBaseName() const {
    std::string base;
    if (m_analysis.source.macroMeta && !m_analysis.source.macroMeta->levelName.empty()) {
        base = m_analysis.source.macroMeta->levelName;
    } else if (!m_analysis.source.fileName.empty()) {
        base = m_analysis.source.fileName;
        auto dot = base.find_last_of('.');
        if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
    } else {
        base = "analysis";
    }
    std::string clean;
    for (char c : base) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        clean += ok ? c : (c == ' ' ? '_' : '-');
    }
    if (clean.empty()) clean = "analysis";
    if (clean.size() > 48) clean.resize(48);
    return clean;
}

std::string Session::generatorString() const {
    return "Frame Window Analyzer " + Mod::get()->getVersion().toVString() + " (" + nowTimestamp(false) + ")";
}

std::string Session::exportJson() const {
    auto s = Settings::load();
    return json::dump(toJson(m_analysis, s.exportSettings(), m_precision, generatorString()));
}

std::string Session::exportCsv() const {
    return toCsv(m_analysis, Settings::load().exportSettings(), m_pass);
}

std::string Session::exportNandl() const {
    return json::dump(toNandlCalculatorJson(m_analysis, Settings::load().exportSettings()));
}

std::variant<std::filesystem::path, std::string> Session::saveToFolder(std::string const& reason) {
    if (!hasData()) return std::string("Nothing to save.");
    auto path = analysesDir() / (suggestedBaseName() + "_" + nowTimestamp(true) + "_" + reason + ".json");
    auto res = file::writeStringSafe(path, exportJson());
    if (!res) return std::string("Could not save: ") + std::string(res.unwrapErr());
    return path;
}

void Session::autoSave(std::string const& reason) {
    if (m_analysis.source.kind == SourceKind::Sample) return;
    auto r = saveToFolder(reason);
    if (auto err = std::get_if<std::string>(&r)) {
        log::warn("Frame Window Analyzer autosave failed: {}", *err);
    } else {
        log::info("Frame Window Analyzer saved {}", utils::string::pathToString(std::get<std::filesystem::path>(r)));
    }
}

} // namespace fwa
