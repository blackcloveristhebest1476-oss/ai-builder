#include "Analysis.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>
#include <locale>
#include <numeric>
#include <sstream>

namespace fwa {

char const* statusName(WindowStatus s) {
    switch (s) {
        case WindowStatus::Unknown: return "Unknown";
        case WindowStatus::Verified: return "Verified";
        case WindowStatus::LowerBound: return "Lower bound";
        case WindowStatus::Inconsistent: return "Inconsistent";
        case WindowStatus::BaselineFailed: return "Baseline failed";
        case WindowStatus::UserSupplied: return "User supplied";
        case WindowStatus::Estimated: return "Estimated";
        case WindowStatus::Excluded: return "Excluded";
    }
    return "?";
}

char const* statusShortName(WindowStatus s) {
    switch (s) {
        case WindowStatus::Unknown: return "unknown";
        case WindowStatus::Verified: return "verified";
        case WindowStatus::LowerBound: return "lower-bound";
        case WindowStatus::Inconsistent: return "inconsistent";
        case WindowStatus::BaselineFailed: return "baseline-failed";
        case WindowStatus::UserSupplied: return "user-supplied";
        case WindowStatus::Estimated: return "estimated";
        case WindowStatus::Excluded: return "excluded";
    }
    return "unknown";
}

std::optional<WindowStatus> statusFromName(std::string_view s) {
    for (auto st : {WindowStatus::Unknown, WindowStatus::Verified, WindowStatus::LowerBound, WindowStatus::Inconsistent,
                    WindowStatus::BaselineFailed, WindowStatus::UserSupplied, WindowStatus::Estimated, WindowStatus::Excluded}) {
        if (s == statusShortName(st)) return st;
    }
    return std::nullopt;
}

bool isMeasured(WindowStatus s) {
    return s == WindowStatus::Verified || s == WindowStatus::LowerBound || s == WindowStatus::Inconsistent;
}

Analysis analysisFromMacro(Macro const& macro) {
    Analysis a;
    a.source.kind = SourceKind::Macro;
    a.source.formatId = macro.formatId;
    a.source.formatName = macro.formatName;
    a.source.fileName = macro.sourceName;
    a.source.macroMeta = macro.meta;
    a.source.diagnostics = macro.diagnostics;
    a.tps = macro.meta.framerate;
    a.inputs.reserve(macro.inputs.size());
    for (size_t i = 0; i < macro.inputs.size(); i++) {
        auto const& in = macro.inputs[i];
        InputRecord r;
        r.index = i;
        r.frame = in.frame;
        r.time = static_cast<double>(in.frame) / a.tps;
        r.button = in.button;
        r.player2 = in.player2;
        r.down = in.down;
        r.duplicate = in.duplicate;
        r.redundant = in.redundant;
        if (in.duplicate || in.redundant) {
            r.window.status = WindowStatus::Excluded;
            r.window.note = in.duplicate ? "Duplicate event" : "Redundant event (no state change)";
        }
        a.inputs.push_back(std::move(r));
    }
    return a;
}

Analysis analysisFromWindowData(WindowDataset const& ds, std::string formatId, std::string formatName,
                                std::string fileName, double windowFpsFallback, std::vector<Diagnostic> diags) {
    Analysis a;
    a.source.kind = SourceKind::WindowData;
    a.source.formatId = std::move(formatId);
    a.source.formatName = std::move(formatName);
    a.source.fileName = std::move(fileName);
    a.tps = ds.gameFps.value_or(240.0);
    double rate = ds.windowFps.value_or(windowFpsFallback);
    if (!ds.windowFps) {
        diags.push_back({Severity::Info, "The file does not say which timing-window rate its windows use; assuming " +
            num(rate) + " Hz (your Timing-window FPS setting)."});
    }
    if (ds.respawnSeconds) {
        diags.push_back({Severity::Info, "The file specifies a respawn time of " + num(*ds.respawnSeconds) +
            " s. Set Respawn Time in the settings if you want to use it."});
    }
    a.source.diagnostics = std::move(diags);
    size_t ignored = 0;
    for (size_t i = 0; i < ds.rows.size(); i++) {
        auto const& row = ds.rows[i];
        InputRecord r;
        r.index = i;
        r.time = row.timeSeconds;
        r.sourceInputNumber = row.input;
        if (row.windowFrames) {
            r.window.status = WindowStatus::UserSupplied;
            r.window.windowSeconds = *row.windowFrames / rate;
            r.window.note = "From " + a.source.formatName;
        } else {
            r.window.status = WindowStatus::Unknown;
            r.window.note = "Marked as ignored (-) in the source file";
            ignored++;
        }
        a.inputs.push_back(std::move(r));
    }
    // Keep chronological order (NaNDL rows are usually already sorted).
    bool sorted = std::is_sorted(a.inputs.begin(), a.inputs.end(), [](auto const& x, auto const& y) { return x.time < y.time; });
    if (!sorted) {
        a.source.diagnostics.push_back({Severity::Warning, "Rows are not in time order. They are shown as stored; the CPS modifier may reject them."});
    }
    if (ignored > 0) {
        a.source.diagnostics.push_back({Severity::Info, std::to_string(ignored) + " row(s) have an ignored window (-)."});
    }
    return a;
}

bool isCounted(InputRecord const& r, CountOptions const& opt) {
    if (r.duplicate || r.redundant) return false;
    if (!r.down && !opt.includeReleases) return false;
    if (r.player2 ? !opt.player2 : !opt.player1) return false;
    return true;
}

bool isMeasurable(InputRecord const& r, CountOptions const& opt) {
    return isCounted(r, opt) && r.frame.has_value();
}

std::optional<double> windowFrames(InputRecord const& r, double windowFps, double tps, bool includeLowerBounds) {
    auto const& w = r.window;
    switch (w.status) {
        case WindowStatus::Verified:
            return static_cast<double>(w.totalTicks()) * windowFps / tps;
        case WindowStatus::LowerBound:
            if (!includeLowerBounds) return std::nullopt;
            return static_cast<double>(w.totalTicks()) * windowFps / tps;
        case WindowStatus::UserSupplied:
        case WindowStatus::Estimated:
            if (w.windowSeconds) return *w.windowSeconds * windowFps;
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

// ---------------------------------------------------------------------------

std::vector<double> defaultEdges() {
    return {0, 1, 2, 3, 4, 5, 6, 8, 10};
}

std::vector<double> parseEdges(std::string_view text, std::string* error) {
    std::vector<double> out;
    std::string cur;
    auto flush = [&]() -> bool {
        auto t = trim(cur);
        cur.clear();
        if (t.empty()) return true;
        std::istringstream ss(t);
        ss.imbue(std::locale::classic());
        double d;
        ss >> d;
        if (ss.fail() || !std::isfinite(d) || d < 0) return false;
        out.push_back(d);
        return true;
    };
    for (char c : text) {
        if (c == ',' || c == ';' || c == ' ') {
            if (!flush()) {
                if (error) *error = "Edges must be non-negative numbers separated by commas.";
                return defaultEdges();
            }
        } else {
            cur += c;
        }
    }
    if (!flush()) {
        if (error) *error = "Edges must be non-negative numbers separated by commas.";
        return defaultEdges();
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.empty()) {
        if (error) *error = "No edges given.";
        return defaultEdges();
    }
    if (out.front() != 0.0) out.insert(out.begin(), 0.0);
    if (out.size() > 24) {
        if (error) *error = "At most 24 edges are supported.";
        return defaultEdges();
    }
    return out;
}

Histogram makeHistogram(std::vector<double> const& values, std::vector<double> const& edges) {
    Histogram h;
    h.edges = edges.empty() ? defaultEdges() : edges;
    h.counts.assign(h.edges.size(), 0);
    for (size_t i = 0; i < h.edges.size(); i++) {
        if (i + 1 < h.edges.size()) h.labels.push_back(num(h.edges[i]) + "-" + num(h.edges[i + 1]));
        else h.labels.push_back(num(h.edges[i]) + "+");
    }
    for (double v : values) {
        if (!std::isfinite(v)) continue;
        // bucket j holds edges[j] <= v < edges[j+1]
        auto it = std::upper_bound(h.edges.begin(), h.edges.end(), v);
        size_t j = it == h.edges.begin() ? 0 : static_cast<size_t>(it - h.edges.begin()) - 1;
        h.counts[j]++;
    }
    return h;
}

Asymmetry asymmetryOf(WindowResult const& w) {
    if (!isMeasured(w.status)) return Asymmetry::NotMeasured;
    int d = w.earlyTicks - w.lateTicks;
    if (std::abs(d) <= 1) return Asymmetry::Centered;
    return d > 0 ? Asymmetry::EarlyBiased : Asymmetry::LateBiased;
}

char const* asymmetryName(Asymmetry a) {
    switch (a) {
        case Asymmetry::NotMeasured: return "-";
        case Asymmetry::Centered: return "Centered";
        case Asymmetry::EarlyBiased: return "More room early";
        case Asymmetry::LateBiased: return "More room late";
    }
    return "-";
}

WindowStats computeWindowStats(Analysis const& a, StatsOptions const& opt) {
    WindowStats s;
    s.totalInputs = a.inputs.size();
    std::vector<double> values;
    for (auto const& r : a.inputs) {
        if (!isCounted(r, opt.count)) continue;
        s.countedInputs++;
        switch (r.window.status) {
            case WindowStatus::Verified: s.verified++; break;
            case WindowStatus::LowerBound: s.lowerBound++; break;
            case WindowStatus::Inconsistent: s.inconsistent++; break;
            case WindowStatus::BaselineFailed: s.baselineFailed++; break;
            case WindowStatus::UserSupplied: s.userSupplied++; break;
            case WindowStatus::Estimated: s.estimated++; break;
            default: s.unknown++; break;
        }
        auto w = windowFrames(r, opt.windowFps, a.tps, opt.includeLowerBounds);
        if (!w) continue;
        values.push_back(*w);
        if (*w <= opt.framePerfectThreshold + 1e-9) s.framePerfect++;
        if (*w <= opt.tightThreshold + 1e-9) s.tight++;
        if (r.window.status == WindowStatus::Verified) {
            if (!s.narrowestVerifiedIndex || *w < s.narrowestVerified) {
                s.narrowestVerifiedIndex = r.index;
                s.narrowestVerified = *w;
            }
            if (!s.widestVerifiedIndex || *w > s.widestVerified) {
                s.widestVerifiedIndex = r.index;
                s.widestVerified = *w;
            }
        }
        switch (asymmetryOf(r.window)) {
            case Asymmetry::Centered: s.centered++; break;
            case Asymmetry::EarlyBiased: s.earlyBiased++; break;
            case Asymmetry::LateBiased: s.lateBiased++; break;
            default: break;
        }
    }
    s.withWindow = values.size();
    if (!values.empty()) {
        s.average = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end());
        size_t n = sorted.size();
        s.median = n % 2 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
        s.minimum = sorted.front();
        s.maximum = sorted.back();
    }
    s.histogram = makeHistogram(values, opt.edges);
    return s;
}

// ---------------------------------------------------------------------------

NandlDataset buildNandlDataset(Analysis const& a, CountOptions const& count, double windowFps, bool includeLowerBounds) {
    NandlDataset ds;
    bool keepNumbers = a.source.kind == SourceKind::WindowData;
    int64_t n = 0;
    for (auto const& r : a.inputs) {
        if (!isCounted(r, count)) continue;
        n++;
        nandl::Row row;
        row.input = keepNumbers ? r.sourceInputNumber : n;
        row.time = r.time;
        row.window = windowFrames(r, windowFps, a.tps, includeLowerBounds);
        if (!row.window) ds.ignoredForMissingWindow++;
        else if (r.window.status == WindowStatus::LowerBound) ds.lowerBoundsUsed++;
        else if (r.window.status == WindowStatus::Estimated) ds.estimatedUsed++;
        ds.rows.push_back(row);
        ds.recordIndex.push_back(r.index);
    }
    return ds;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

namespace {

char const* sourceKindName(SourceKind k) {
    switch (k) {
        case SourceKind::None: return "none";
        case SourceKind::Macro: return "macro";
        case SourceKind::WindowData: return "window-data";
        case SourceKind::Sample: return "sample";
        case SourceKind::Saved: return "saved";
    }
    return "none";
}

SourceKind sourceKindFrom(std::string const& s) {
    if (s == "macro") return SourceKind::Macro;
    if (s == "window-data") return SourceKind::WindowData;
    if (s == "sample") return SourceKind::Sample;
    if (s == "saved") return SourceKind::Saved;
    return SourceKind::None;
}

char const* severityName(Severity s) {
    switch (s) {
        case Severity::Info: return "info";
        case Severity::Warning: return "warning";
        case Severity::Error: return "error";
    }
    return "info";
}

json::Value numOrNull(double v) {
    return std::isfinite(v) ? json::Value(v) : json::Value(nullptr);
}

json::Value statsJson(nandl::Stats const& st) {
    json::Value o = json::Value::object();
    o.set("expectedAttemptTimeSeconds", numOrNull(st.expectedAttemptTime));
    o.set("completionProbability", numOrNull(st.completionProbability));
    o.set("lnCompletionProbability", numOrNull(st.logCompletion));
    o.set("log10ExpectedAttempts", numOrNull(st.log10ExpectedAttempts));
    o.set("expectedTotalTimeSeconds", numOrNull(st.expectedTotalTime));
    o.set("log10ExpectedTotalTimeSeconds", numOrNull(st.log10ExpectedTotalTime));
    o.set("completable", st.completable);
    return o;
}

} // namespace

json::Value toJson(Analysis const& a, ExportSettings const& s, std::optional<PrecisionSnapshot> const& precision,
                   std::string const& generator) {
    json::Value root = json::Value::object();
    root.set("format", "frame-window-analyzer");
    root.set("version", 1);
    root.set("generator", generator);

    json::Value src = json::Value::object();
    src.set("kind", sourceKindName(a.source.kind));
    src.set("formatId", a.source.formatId);
    src.set("formatName", a.source.formatName);
    src.set("fileName", a.source.fileName);
    if (a.source.macroMeta) {
        auto const& m = *a.source.macroMeta;
        json::Value meta = json::Value::object();
        meta.set("author", m.author);
        meta.set("description", m.description);
        meta.set("botName", m.botName);
        meta.set("botVersion", m.botVersion);
        meta.set("levelId", static_cast<int64_t>(m.levelId));
        meta.set("levelName", m.levelName);
        meta.set("gameVersion", m.gameVersionText);
        meta.set("framerate", m.framerate);
        meta.set("framerateFromFile", m.framerateFromFile);
        meta.set("seed", m.seed);
        meta.set("coins", m.coins);
        meta.set("ldm", m.ldm);
        meta.set("platformer", m.platformer);
        if (m.storedDurationSeconds) meta.set("durationSeconds", *m.storedDurationSeconds);
        meta.set("formatVersion", m.formatVersion);
        json::Value deaths = json::Value::array();
        for (auto d : m.deathFrames) deaths.push(json::Value(d));
        meta.set("deathFrames", deaths);
        src.set("macro", meta);
    }
    json::Value diags = json::Value::array();
    for (auto const& d : a.source.diagnostics) {
        json::Value o = json::Value::object();
        o.set("severity", severityName(d.severity));
        o.set("message", d.message);
        diags.push(o);
    }
    src.set("diagnostics", diags);
    root.set("source", src);
    root.set("tps", a.tps);

    json::Value set = json::Value::object();
    set.set("gameFps", s.gameFps);
    set.set("windowFps", s.windowFps);
    set.set("respawnSeconds", s.respawnSeconds);
    set.set("targetSeconds", s.targetSeconds);
    set.set("toleranceSeconds", s.tolerance);
    set.set("maxIterations", s.maxIterations);
    set.set("nerveEnabled", s.nerveEnabled);
    set.set("nerveConstant", s.constants.kt);
    set.set("fatigueEnabled", s.fatigueEnabled);
    set.set("fatigueConstant", s.constants.ku);
    set.set("cpsEnabled", s.cpsEnabled);
    set.set("cpsConstant", s.constants.kc);
    set.set("countReleases", s.stats.count.includeReleases);
    set.set("countPlayer1", s.stats.count.player1);
    set.set("countPlayer2", s.stats.count.player2);
    set.set("includeLowerBounds", s.stats.includeLowerBounds);
    set.set("framePerfectThreshold", s.stats.framePerfectThreshold);
    set.set("tightThreshold", s.stats.tightThreshold);
    json::Value edges = json::Value::array();
    for (double e : s.stats.edges) edges.push(json::Value(e));
    set.set("histogramEdges", edges);
    root.set("settings", set);

    json::Value meas = json::Value::object();
    meas.set("performed", a.measurement.performed);
    meas.set("finishedAt", a.measurement.finishedAt);
    meas.set("levelName", a.measurement.levelName);
    meas.set("levelId", a.measurement.levelId);
    meas.set("horizonSeconds", a.measurement.horizonSeconds);
    meas.set("maxShiftTicks", a.measurement.maxShiftTicks);
    meas.set("recheckBorders", a.measurement.recheckBorders);
    meas.set("determinismChecked", a.measurement.determinismChecked);
    meas.set("determinismPassed", a.measurement.determinismPassed);
    meas.set("notes", a.measurement.notes);
    root.set("measurement", meas);

    auto stats = computeWindowStats(a, s.stats);
    json::Value summary = json::Value::object();
    summary.set("totalInputs", stats.totalInputs);
    summary.set("countedInputs", stats.countedInputs);
    summary.set("withWindow", stats.withWindow);
    summary.set("verified", stats.verified);
    summary.set("lowerBound", stats.lowerBound);
    summary.set("inconsistent", stats.inconsistent);
    summary.set("baselineFailed", stats.baselineFailed);
    summary.set("userSupplied", stats.userSupplied);
    summary.set("estimated", stats.estimated);
    summary.set("unknown", stats.unknown);
    summary.set("averageWindowFrames", stats.withWindow ? json::Value(stats.average) : json::Value(nullptr));
    summary.set("medianWindowFrames", stats.withWindow ? json::Value(stats.median) : json::Value(nullptr));
    summary.set("narrowestVerifiedFrames", stats.narrowestVerifiedIndex ? json::Value(stats.narrowestVerified) : json::Value(nullptr));
    summary.set("widestVerifiedFrames", stats.widestVerifiedIndex ? json::Value(stats.widestVerified) : json::Value(nullptr));
    summary.set("framePerfect", stats.framePerfect);
    summary.set("tight", stats.tight);
    json::Value hist = json::Value::array();
    for (size_t i = 0; i < stats.histogram.counts.size(); i++) {
        json::Value b = json::Value::object();
        b.set("range", stats.histogram.labels[i]);
        b.set("count", stats.histogram.counts[i]);
        hist.push(b);
    }
    summary.set("histogram", hist);
    root.set("summary", summary);

    if (precision) {
        json::Value p = json::Value::object();
        p.set("mode", precision->mode);
        p.set("status", precision->status);
        p.set("precisionSigmaPerSecond", numOrNull(precision->precision));
        p.set("absoluteErrorSeconds", numOrNull(precision->absoluteError));
        p.set("iterations", precision->iterations);
        p.set("stats", statsJson(precision->stats));
        root.set("precision", p);
    }

    json::Value inputs = json::Value::array();
    for (auto const& r : a.inputs) {
        json::Value o = json::Value::object();
        o.set("index", r.index + 1);
        o.set("frame", r.frame ? json::Value(*r.frame) : json::Value(nullptr));
        o.set("time", r.time);
        o.set("button", static_cast<int>(r.button));
        o.set("player2", r.player2);
        o.set("down", r.down);
        if (r.duplicate) o.set("duplicate", true);
        if (r.redundant) o.set("redundant", true);
        if (a.source.kind == SourceKind::WindowData) o.set("sourceInput", r.sourceInputNumber);
        if (!r.gameMode.empty()) o.set("gameMode", r.gameMode);
        o.set("counted", isCounted(r, s.stats.count));
        o.set("status", statusShortName(r.window.status));
        if (isMeasured(r.window.status)) {
            o.set("earlyTicks", r.window.earlyTicks);
            o.set("lateTicks", r.window.lateTicks);
            o.set("totalTicks", r.window.totalTicks());
            o.set("earlyLimited", r.window.earlyLimited);
            o.set("lateLimited", r.window.lateLimited);
            if (!r.window.earlyLimitReason.empty()) o.set("earlyLimitReason", r.window.earlyLimitReason);
            if (!r.window.lateLimitReason.empty()) o.set("lateLimitReason", r.window.lateLimitReason);
        }
        if (r.window.windowSeconds) o.set("windowSeconds", *r.window.windowSeconds);
        auto wf = windowFrames(r, s.windowFps, a.tps, true);
        o.set("windowFrames", wf ? json::Value(*wf) : json::Value(nullptr));
        if (r.window.trials) o.set("trials", r.window.trials);
        if (!r.window.note.empty()) o.set("note", r.window.note);
        inputs.push(o);
    }
    root.set("inputs", inputs);
    return root;
}

Expected<Analysis> analysisFromJson(json::Value const& doc, std::vector<Diagnostic>& diags,
                                    std::optional<ExportSettings>* settingsOut) {
    using R = Expected<Analysis>;
    if (doc["format"].asString() != "frame-window-analyzer") return R::fail("Not a Frame Window Analyzer save file.");
    auto version = doc["version"].asInt(0);
    if (version != 1) return R::fail("Unsupported save version " + std::to_string(version) + ".");

    Analysis a;
    auto const& src = doc["source"];
    a.source.kind = sourceKindFrom(src["kind"].asString());
    a.source.formatId = src["formatId"].asString();
    a.source.formatName = src["formatName"].asString();
    a.source.fileName = src["fileName"].asString();
    if (src["macro"].isObject()) {
        auto const& m = src["macro"];
        MacroMetadata meta;
        meta.author = m["author"].asString();
        meta.description = m["description"].asString();
        meta.botName = m["botName"].asString();
        meta.botVersion = static_cast<int>(m["botVersion"].asInt());
        meta.levelId = static_cast<uint32_t>(m["levelId"].asInt());
        meta.levelName = m["levelName"].asString();
        meta.gameVersionText = m["gameVersion"].asString();
        meta.framerate = m["framerate"].asDouble(240.0);
        meta.framerateFromFile = m["framerateFromFile"].asBool();
        meta.seed = m["seed"].asInt();
        meta.coins = static_cast<int>(m["coins"].asInt());
        meta.ldm = m["ldm"].asBool();
        meta.platformer = m["platformer"].asBool();
        if (m["durationSeconds"].isNumber()) meta.storedDurationSeconds = m["durationSeconds"].asDouble();
        meta.formatVersion = static_cast<int>(m["formatVersion"].asInt());
        for (auto const& d : m["deathFrames"].items()) meta.deathFrames.push_back(static_cast<uint64_t>(d.asInt()));
        a.source.macroMeta = meta;
    }
    for (auto const& d : src["diagnostics"].items()) {
        Diagnostic dg;
        auto sv = d["severity"].asString();
        dg.severity = sv == "error" ? Severity::Error : sv == "warning" ? Severity::Warning : Severity::Info;
        dg.message = d["message"].asString();
        a.source.diagnostics.push_back(dg);
    }
    a.tps = doc["tps"].asDouble(240.0);
    if (!std::isfinite(a.tps) || a.tps <= 0) return R::fail("The save file has an invalid tick rate.");

    auto const& meas = doc["measurement"];
    a.measurement.performed = meas["performed"].asBool();
    a.measurement.finishedAt = meas["finishedAt"].asString();
    a.measurement.levelName = meas["levelName"].asString();
    a.measurement.levelId = meas["levelId"].asInt();
    a.measurement.horizonSeconds = meas["horizonSeconds"].asDouble();
    a.measurement.maxShiftTicks = static_cast<int>(meas["maxShiftTicks"].asInt());
    a.measurement.recheckBorders = meas["recheckBorders"].asBool();
    a.measurement.determinismChecked = meas["determinismChecked"].asBool();
    a.measurement.determinismPassed = meas["determinismPassed"].asBool();
    a.measurement.notes = meas["notes"].asString();

    if (!doc["inputs"].isArray()) return R::fail("The save file has no inputs list.");
    size_t i = 0;
    for (auto const& o : doc["inputs"].items()) {
        InputRecord r;
        r.index = i++;
        if (o["frame"].isNumber()) r.frame = static_cast<uint64_t>(std::max<int64_t>(0, o["frame"].asInt()));
        r.time = o["time"].asDouble();
        if (!std::isfinite(r.time)) return R::fail("Input " + std::to_string(i) + " has an invalid time.");
        r.button = static_cast<uint8_t>(o["button"].asInt(1));
        r.player2 = o["player2"].asBool();
        r.down = o["down"].asBool(true);
        r.duplicate = o["duplicate"].asBool();
        r.redundant = o["redundant"].asBool();
        r.sourceInputNumber = o["sourceInput"].asInt(static_cast<int64_t>(i));
        r.gameMode = o["gameMode"].asString();
        auto st = statusFromName(o["status"].asString());
        if (!st) {
            diags.push_back({Severity::Warning, "Input " + std::to_string(i) + " has an unknown status; set to Unknown."});
            st = WindowStatus::Unknown;
        }
        r.window.status = *st;
        r.window.earlyTicks = static_cast<int>(o["earlyTicks"].asInt());
        r.window.lateTicks = static_cast<int>(o["lateTicks"].asInt());
        r.window.earlyLimited = o["earlyLimited"].asBool();
        r.window.lateLimited = o["lateLimited"].asBool();
        r.window.earlyLimitReason = o["earlyLimitReason"].asString();
        r.window.lateLimitReason = o["lateLimitReason"].asString();
        if (o["windowSeconds"].isNumber()) r.window.windowSeconds = o["windowSeconds"].asDouble();
        r.window.trials = static_cast<int>(o["trials"].asInt());
        r.window.note = o["note"].asString();
        if ((r.window.status == WindowStatus::UserSupplied || r.window.status == WindowStatus::Estimated) && !r.window.windowSeconds) {
            diags.push_back({Severity::Warning, "Input " + std::to_string(i) + " is marked as having a window but has no value; set to Unknown."});
            r.window.status = WindowStatus::Unknown;
        }
        a.inputs.push_back(std::move(r));
    }

    if (settingsOut) {
        auto const& s = doc["settings"];
        if (s.isObject()) {
            ExportSettings es;
            es.gameFps = s["gameFps"].asDouble(240.0);
            es.windowFps = s["windowFps"].asDouble(240.0);
            es.respawnSeconds = s["respawnSeconds"].asDouble(0.0);
            es.targetSeconds = s["targetSeconds"].asDouble(86400.0);
            es.tolerance = s["toleranceSeconds"].asDouble(1.0);
            es.maxIterations = static_cast<int>(s["maxIterations"].asInt(240));
            es.nerveEnabled = s["nerveEnabled"].asBool();
            es.constants.kt = s["nerveConstant"].asDouble(nandl::kDefaultNerve);
            es.fatigueEnabled = s["fatigueEnabled"].asBool();
            es.constants.ku = s["fatigueConstant"].asDouble(nandl::kDefaultFatigue);
            es.cpsEnabled = s["cpsEnabled"].asBool();
            es.constants.kc = s["cpsConstant"].asDouble(nandl::kDefaultCps);
            es.stats.count.includeReleases = s["countReleases"].asBool();
            es.stats.count.player1 = s["countPlayer1"].asBool(true);
            es.stats.count.player2 = s["countPlayer2"].asBool(true);
            es.stats.includeLowerBounds = s["includeLowerBounds"].asBool(true);
            es.stats.framePerfectThreshold = s["framePerfectThreshold"].asDouble(1.0);
            es.stats.tightThreshold = s["tightThreshold"].asDouble(2.0);
            std::vector<double> edges;
            for (auto const& e : s["histogramEdges"].items()) edges.push_back(e.asDouble());
            if (!edges.empty()) es.stats.edges = edges;
            es.stats.windowFps = es.windowFps;
            *settingsOut = es;
        }
    }
    return a;
}

// ---------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------

namespace {

std::string csvEscape(std::string const& s) {
    if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    return out + "\"";
}

std::string csvNum(double v, int decimals = 6) {
    if (!std::isfinite(v)) return "";
    return num(v, decimals);
}

} // namespace

std::string toCsv(Analysis const& a, ExportSettings const& s, std::vector<double> const& passProbabilities) {
    std::string out =
        "input,counted_input,frame,time_s,player,button,event,game_mode,status,early_ticks,late_ticks,"
        "window_ticks,window_frames,early_limited,late_limited,asymmetry,pass_probability,note\n";
    int64_t counted = 0;
    bool keepNumbers = a.source.kind == SourceKind::WindowData;
    for (auto const& r : a.inputs) {
        bool isC = isCounted(r, s.stats.count);
        if (isC) counted++;
        out += std::to_string(r.index + 1) + ",";
        out += isC ? std::to_string(keepNumbers ? r.sourceInputNumber : counted) : "";
        out += ",";
        out += r.frame ? std::to_string(*r.frame) : "";
        out += "," + csvNum(r.time, 6);
        out += std::string(",") + (r.player2 ? "2" : "1");
        out += std::string(",") + buttonName(r.button);
        out += std::string(",") + (r.down ? "press" : "release");
        out += "," + csvEscape(r.gameMode);
        out += std::string(",") + statusShortName(r.window.status);
        bool measured = isMeasured(r.window.status);
        out += "," + (measured ? std::to_string(r.window.earlyTicks) : "");
        out += "," + (measured ? std::to_string(r.window.lateTicks) : "");
        out += "," + (measured ? std::to_string(r.window.totalTicks()) : "");
        auto wf = windowFrames(r, s.windowFps, a.tps, true);
        out += "," + (wf ? csvNum(*wf, 6) : std::string());
        out += std::string(",") + (measured ? (r.window.earlyLimited ? "yes" : "no") : "");
        out += std::string(",") + (measured ? (r.window.lateLimited ? "yes" : "no") : "");
        out += std::string(",") + asymmetryName(asymmetryOf(r.window));
        double p = r.index < passProbabilities.size() ? passProbabilities[r.index] : std::nan("");
        out += "," + (std::isfinite(p) ? csvNum(p, 12) : std::string());
        out += "," + csvEscape(r.window.note);
        out += "\n";
    }
    return out;
}

json::Value toNandlCalculatorJson(Analysis const& a, ExportSettings const& s) {
    auto ds = buildNandlDataset(a, s.stats.count, s.windowFps, s.stats.includeLowerBounds);
    json::Value root = json::Value::object();
    root.set("format", "nandl-calculator");
    root.set("version", 1);
    json::Value set = json::Value::object();
    set.set("gameFps", s.gameFps);
    set.set("windowFps", s.windowFps);
    set.set("respawnSeconds", s.respawnSeconds);
    set.set("timeUnit", "seconds");
    root.set("settings", set);
    json::Value rows = json::Value::array();
    for (auto const& row : ds.rows) {
        json::Value o = json::Value::object();
        o.set("input", row.input);
        o.set("timePosition", row.time);
        if (row.window) o.set("frameWindow", *row.window);
        else o.set("frameWindow", "-");
        rows.push(o);
    }
    root.set("frameWindows", rows);
    return root;
}

// ---------------------------------------------------------------------------
// Sample data
// ---------------------------------------------------------------------------

Analysis makeSampleAnalysis() {
    Analysis a;
    a.source.kind = SourceKind::Sample;
    a.source.formatId = "sample";
    a.source.formatName = "Sample data (synthetic)";
    a.source.fileName = "sample";
    a.source.diagnostics.push_back({Severity::Warning,
        "SAMPLE DATA: every value here is synthetic and was generated for demonstration. Nothing was measured."});
    a.tps = 240.0;

    uint32_t state = 0x2545F491u; // fixed seed -> identical sample every time
    auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };
    uint64_t frame = 120;
    size_t idx = 0;
    for (int click = 0; click < 90; click++) {
        frame += 40 + next() % 160;
        uint32_t roll = next() % 100;
        int early, late;
        if (roll < 6) { early = 0; late = 0; }                 // frame perfect
        else if (roll < 18) { early = 0; late = 1; }
        else if (roll < 40) { early = 1 + next() % 2; late = 1 + next() % 2; }
        else { early = 1 + next() % 7; late = 1 + next() % 7; }

        InputRecord press;
        press.index = idx++;
        press.frame = frame;
        press.time = static_cast<double>(frame) / a.tps;
        press.down = true;
        press.gameMode = click < 30 ? "Cube" : click < 55 ? "Ship" : click < 75 ? "Wave" : "Ball";
        press.window.status = WindowStatus::Estimated;
        press.window.earlyTicks = early;
        press.window.lateTicks = late;
        press.window.windowSeconds = static_cast<double>(early + late + 1) / a.tps;
        press.window.note = "Synthetic sample value (not measured)";
        a.inputs.push_back(press);

        frame += 10 + next() % 50;
        InputRecord release;
        release.index = idx++;
        release.frame = frame;
        release.time = static_cast<double>(frame) / a.tps;
        release.down = false;
        release.gameMode = press.gameMode;
        release.window.status = WindowStatus::Unknown;
        release.window.note = "Synthetic sample release";
        a.inputs.push_back(release);
    }
    return a;
}

} // namespace fwa
