#include "Formats.hpp"
#include "MsgPack.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <locale>
#include <sstream>

namespace fwa {

std::string extensionOf(std::string_view fileName) {
    auto slash = fileName.find_last_of("/\\");
    if (slash != std::string_view::npos) fileName = fileName.substr(slash + 1);
    auto dot = fileName.find_last_of('.');
    if (dot == std::string_view::npos) return "";
    return toLower(fileName.substr(dot + 1));
}

namespace {

std::string versionText(uint64_t v) {
    if (v == 0) return "";
    if (v >= 10000 && v < 100000) {
        std::string rest = std::to_string(v % 10000);
        while (rest.size() < 4) rest = "0" + rest;
        return std::to_string(v / 10000) + "." + rest;
    }
    return std::to_string(v);
}

// ---------------------------------------------------------------------------
// GDR2 binary
// Layout verified against maxnut/GDReplayFormat (include/gdr/gdr.hpp, commit d8d2f0b):
//   "GDR" | varint version | str inputTag | str author | str description |
//   f32-BE duration | varint gameVersion | f64-BE framerate | varint seed |
//   varint coins | varint ldm | varint platformer | str botName |
//   varint botVersion | varint levelId | str levelName |
//   varint extSize + ext bytes | varint deathCount + delta varints |
//   varint inputCount | varint p1Count | inputs (P1 block, then P2 block,
//   each delta-encoded from 0), each: varint packed [+ varint extSize + ext]
// Strings are varint-length-prefixed (not NUL terminated).
// ---------------------------------------------------------------------------

class ByteReader {
public:
    explicit ByteReader(std::span<uint8_t const> d) : m_data(d) {}

    size_t remaining() const { return m_data.size() - m_pos; }
    size_t pos() const { return m_pos; }

    bool varint(uint64_t& out) {
        out = 0;
        for (int i = 0; i < 10; i++) {
            if (m_pos >= m_data.size()) return false;
            uint8_t b = m_data[m_pos++];
            out |= static_cast<uint64_t>(b & 0x7F) << (7 * i);
            if ((b & 0x80) == 0) return true;
        }
        return false; // more than 10 bytes: corrupt
    }

    bool str(std::string& out) {
        uint64_t len;
        if (!varint(len)) return false;
        if (len > remaining()) return false;
        out.assign(reinterpret_cast<char const*>(m_data.data() + m_pos), static_cast<size_t>(len));
        m_pos += static_cast<size_t>(len);
        return true;
    }

    bool f32be(float& out) {
        if (remaining() < 4) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v = (v << 8) | m_data[m_pos++];
        std::memcpy(&out, &v, 4);
        return true;
    }

    bool f64be(double& out) {
        if (remaining() < 8) return false;
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | m_data[m_pos++];
        std::memcpy(&out, &v, 8);
        return true;
    }

    bool skip(uint64_t n) {
        if (n > remaining()) return false;
        m_pos += static_cast<size_t>(n);
        return true;
    }

    bool bytes(size_t n, std::span<uint8_t const>& out) {
        if (n > remaining()) return false;
        out = m_data.subspan(m_pos, n);
        m_pos += n;
        return true;
    }

private:
    std::span<uint8_t const> m_data;
    size_t m_pos = 0;
};

} // namespace

Expected<Macro> parseGdr2(std::span<uint8_t const> data) {
    using R = Expected<Macro>;
    if (data.size() < 3 || std::memcmp(data.data(), "GDR", 3) != 0) {
        return R::fail("Not a GDR2 file (missing \"GDR\" header).");
    }
    ByteReader r(data.subspan(3));
    Macro m;
    m.formatId = "gdr2";
    auto& meta = m.meta;

    auto truncated = [&](char const* what) {
        return R::fail(std::string("The GDR2 file is truncated or corrupted (could not read ") + what + ").");
    };

    uint64_t version;
    if (!r.varint(version)) return truncated("format version");
    if (version != 2) {
        return R::fail("Unsupported GDR binary version " + std::to_string(version) +
            ". Only version 2 (the current GDReplayFormat) is supported.");
    }
    meta.formatVersion = 2;
    m.formatName = "GDR2 (binary)";

    if (!r.str(meta.inputExtensionTag)) return truncated("input tag");
    if (!r.str(meta.author)) return truncated("author");
    if (!r.str(meta.description)) return truncated("description");
    float duration;
    if (!r.f32be(duration)) return truncated("duration");
    if (std::isfinite(duration) && duration >= 0) meta.storedDurationSeconds = duration;
    uint64_t gameVersion, seed, coins, ldm, platformer, botVersion, levelId;
    if (!r.varint(gameVersion)) return truncated("game version");
    meta.gameVersionText = versionText(gameVersion);
    double framerate;
    if (!r.f64be(framerate)) return truncated("framerate");
    meta.framerate = framerate;
    meta.framerateFromFile = true;
    if (!r.varint(seed)) return truncated("seed");
    meta.seed = static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(seed)));
    if (!r.varint(coins)) return truncated("coins");
    meta.coins = static_cast<int>(coins);
    if (!r.varint(ldm)) return truncated("LDM flag");
    meta.ldm = ldm != 0;
    if (!r.varint(platformer)) return truncated("platformer flag");
    meta.platformer = platformer != 0;
    if (!r.str(meta.botName)) return truncated("bot name");
    if (!r.varint(botVersion)) return truncated("bot version");
    meta.botVersion = static_cast<int>(botVersion);
    if (!r.varint(levelId)) return truncated("level id");
    meta.levelId = static_cast<uint32_t>(levelId);
    if (!r.str(meta.levelName)) return truncated("level name");

    uint64_t extSize;
    if (!r.varint(extSize)) return truncated("extension size");
    if (!r.skip(extSize)) return R::fail("The GDR2 file is corrupted (replay extension size exceeds the file).");
    if (extSize > 0) {
        m.addDiagnostic(Severity::Info, "The replay contains " + std::to_string(extSize) +
            " byte(s) of bot-specific extension data; it is not needed for analysis and was skipped.");
    }

    uint64_t deathCount;
    if (!r.varint(deathCount)) return truncated("death count");
    if (deathCount > r.remaining()) return R::fail("The GDR2 file is corrupted (death count exceeds the file size).");
    uint64_t acc = 0;
    for (uint64_t i = 0; i < deathCount; i++) {
        uint64_t delta;
        if (!r.varint(delta)) return truncated("death frames");
        acc += delta;
        meta.deathFrames.push_back(acc);
    }
    if (deathCount > 0) {
        m.addDiagnostic(Severity::Warning, "The replay stores " + std::to_string(deathCount) +
            " death frame(s). Inputs after a recorded death may not be reproducible from a single run.");
    }

    uint64_t inputCount, p1Count;
    if (!r.varint(inputCount)) return truncated("input count");
    if (!r.varint(p1Count)) return truncated("player 1 input count");
    if (inputCount > r.remaining()) return R::fail("The GDR2 file is corrupted (input count exceeds the file size).");
    if (p1Count > inputCount) return R::fail("The GDR2 file is corrupted (player 1 input count is larger than the total).");

    bool hasInputExt = !meta.inputExtensionTag.empty();
    if (hasInputExt) {
        m.addDiagnostic(Severity::Info, "Inputs carry \"" + meta.inputExtensionTag +
            "\" extension data (e.g. physics fixes). It is skipped; only frame, button, player and state are used.");
    }

    m.inputs.reserve(static_cast<size_t>(inputCount));
    uint64_t prev = 0;
    for (uint64_t i = 0; i < inputCount; i++) {
        if (i == p1Count) prev = 0; // P2 block starts, deltas restart from 0
        uint64_t packed;
        if (!r.varint(packed)) {
            return R::fail("The GDR2 file is truncated: expected " + std::to_string(inputCount) +
                " inputs but the data ends after " + std::to_string(i) + ".");
        }
        MacroInput in;
        if (meta.platformer) {
            in.frame = prev + (packed >> 3);
            in.button = static_cast<uint8_t>((packed >> 1) & 3);
            in.down = (packed & 1) != 0;
        } else {
            in.frame = prev + (packed >> 1);
            in.button = 1;
            in.down = (packed & 1) != 0;
        }
        in.player2 = i >= p1Count;
        in.fileOrder = static_cast<size_t>(i);
        if (hasInputExt) {
            uint64_t inExt;
            if (!r.varint(inExt) || !r.skip(inExt)) {
                return R::fail("The GDR2 file is corrupted (input extension of input " + std::to_string(i + 1) + " is invalid).");
            }
        }
        prev = in.frame;
        m.inputs.push_back(in);
    }
    if (r.remaining() > 0) {
        m.addDiagnostic(Severity::Warning, std::to_string(r.remaining()) +
            " unexpected byte(s) after the last input were ignored.");
    }

    // GDR2 stores P1 then P2; the reference implementation stable-sorts by frame.
    std::stable_sort(m.inputs.begin(), m.inputs.end(), [](auto const& a, auto const& b) { return a.frame < b.frame; });
    return m;
}

// ---------------------------------------------------------------------------
// GDR 1 (JSON or MessagePack document)
// Field names verified against maxnut/GDR-converter (include/gdr_convert.hpp):
// gameVersion (number), description, duration, bot{name,version}, level{id,name},
// author, seed, coins, ldm, framerate (optional), inputs[{frame,btn,2p,down}]
// ---------------------------------------------------------------------------

namespace {

bool readBoolLike(json::Value const& v, bool& out) {
    if (v.isBool()) { out = v.asBool(); return true; }
    if (v.isNumber()) { out = v.asDouble() != 0.0; return true; }
    return false;
}

} // namespace

Expected<Macro> parseGdr1Document(json::Value const& doc, std::string formatId, std::string formatName) {
    using R = Expected<Macro>;
    if (!doc.isObject()) return R::fail("A GDR 1 replay must be an object at the top level.");
    auto const& inputs = doc["inputs"];
    if (!inputs.isArray()) return R::fail("This file has no \"inputs\" list, so it is not a GDR replay.");

    Macro m;
    m.formatId = std::move(formatId);
    m.formatName = std::move(formatName);
    m.meta.formatVersion = 1;
    auto& meta = m.meta;
    std::vector<std::string> missing;

    if (doc["gameVersion"].isNumber()) {
        meta.gameVersionText = num(doc["gameVersion"].asDouble(), 4);
    } else if (doc["gameVersion"].isString()) {
        meta.gameVersionText = doc["gameVersion"].asString();
    } else {
        missing.push_back("gameVersion");
    }
    if (doc["description"].isString()) meta.description = doc["description"].asString();
    if (doc["author"].isString()) meta.author = doc["author"].asString();
    else missing.push_back("author");
    if (doc["duration"].isNumber() && std::isfinite(doc["duration"].asDouble())) {
        meta.storedDurationSeconds = doc["duration"].asDouble();
    }
    auto const& bot = doc["bot"];
    if (bot.isObject()) {
        meta.botName = bot["name"].asString();
        if (bot["version"].isNumber()) meta.botVersion = static_cast<int>(bot["version"].asInt());
        else if (bot["version"].isString()) meta.botVersion = std::atoi(bot["version"].asString().c_str());
    } else {
        missing.push_back("bot");
    }
    auto const& level = doc["level"];
    if (level.isObject()) {
        meta.levelId = static_cast<uint32_t>(std::max<int64_t>(0, level["id"].asInt()));
        meta.levelName = level["name"].asString();
    } else {
        missing.push_back("level");
    }
    meta.seed = doc["seed"].asInt();
    meta.coins = static_cast<int>(doc["coins"].asInt());
    readBoolLike(doc["ldm"], meta.ldm);

    if (doc.contains("framerate")) {
        if (!doc["framerate"].isNumber()) return R::fail("The \"framerate\" field is not a number.");
        meta.framerate = doc["framerate"].asDouble();
        meta.framerateFromFile = true;
    } else {
        meta.framerate = 240.0;
        meta.framerateFromFile = false;
        m.addDiagnostic(Severity::Info, "The file does not store a framerate; using the GDR default of 240 ticks per second.");
    }
    if (!missing.empty()) {
        std::string list;
        for (auto const& k : missing) list += (list.empty() ? "" : ", ") + k;
        m.addDiagnostic(Severity::Info, "Metadata field(s) missing: " + list + ". Inputs can still be analyzed.");
    }

    size_t index = 0;
    std::vector<std::string> problems;
    for (auto const& item : inputs.items()) {
        auto where = "input #" + std::to_string(index + 1);
        index++;
        if (!item.isObject()) {
            problems.push_back(where + " is not an object");
            continue;
        }
        auto const& frame = item["frame"];
        if (!frame.isNumber() || frame.asDouble() < 0 || frame.asDouble() != std::floor(frame.asDouble())) {
            problems.push_back(where + " has a missing or invalid \"frame\" (must be a whole number >= 0)");
            continue;
        }
        MacroInput in;
        in.frame = static_cast<uint64_t>(frame.asDouble());
        in.fileOrder = index - 1;
        if (!readBoolLike(item["down"], in.down)) {
            problems.push_back(where + " has a missing or invalid \"down\" value");
            continue;
        }
        if (item.contains("btn")) {
            auto b = item["btn"].asInt(-1);
            if (b < 1 || b > 3) {
                problems.push_back(where + " has an invalid \"btn\" (must be 1, 2 or 3)");
                continue;
            }
            in.button = static_cast<uint8_t>(b);
        } else {
            problems.push_back(where + " has no \"btn\" value");
            continue;
        }
        if (item.contains("2p")) {
            if (!readBoolLike(item["2p"], in.player2)) {
                problems.push_back(where + " has an invalid \"2p\" value");
                continue;
            }
        } else {
            problems.push_back(where + " has no \"2p\" value");
            continue;
        }
        if (in.button != 1) meta.platformer = true;
        m.inputs.push_back(in);
    }
    if (!problems.empty()) {
        std::string msg = std::to_string(problems.size()) + " input(s) are invalid, so the replay was not loaded. First problem(s): ";
        for (size_t i = 0; i < problems.size() && i < 3; i++) msg += (i ? "; " : "") + problems[i];
        return R::fail(msg + ".");
    }
    return m;
}

// ---------------------------------------------------------------------------
// NaNDL calculator export (format "nandl-calculator", version 1), mirroring
// normalizeCalculatorImport() in nandl.pages.dev/calculator.js.
// ---------------------------------------------------------------------------

namespace {

std::optional<double> jsNumber(json::Value const& v) {
    // Like JavaScript's Number(x) for the shapes that appear in these files.
    if (v.isNumber()) return v.asDouble();
    if (v.isString()) {
        auto s = trim(v.asString());
        if (s.empty()) return 0.0;
        std::istringstream ss(s);
        ss.imbue(std::locale::classic());
        double d;
        ss >> d;
        if (ss.fail()) return std::nullopt;
        ss >> std::ws;
        if (!ss.eof()) return std::nullopt;
        return d;
    }
    if (v.isBool()) return v.asBool() ? 1.0 : 0.0;
    if (v.isNull()) return 0.0;
    return std::nullopt;
}

} // namespace

Expected<WindowDataset> parseNandlCalculatorJson(json::Value const& doc, std::vector<Diagnostic>& diags) {
    using R = Expected<WindowDataset>;
    json::Value const* rowsVal = nullptr;
    if (doc.isArray()) rowsVal = &doc;
    else if (doc.isObject()) {
        if (doc.contains("frameWindows")) rowsVal = &doc["frameWindows"];
        else if (doc.contains("inputs")) rowsVal = &doc["inputs"];
    }
    if (!rowsVal || !rowsVal->isArray() || rowsVal->items().empty()) {
        return R::fail("The JSON file must contain at least one frame-window row.");
    }

    WindowDataset ds;
    std::string timeUnit = "seconds";
    if (doc.isObject() && doc.contains("settings")) {
        auto const& s = doc["settings"];
        if (!s.isObject()) return R::fail("The JSON settings value must be an object.");
        if (s.contains("gameFps")) {
            auto v = jsNumber(s["gameFps"]);
            if (!v || !std::isfinite(*v) || *v <= 0) return R::fail("Game FPS must be a number greater than 0.");
            ds.gameFps = *v;
        }
        if (s.contains("windowFps")) {
            auto v = jsNumber(s["windowFps"]);
            if (!v || !std::isfinite(*v) || *v <= 0) return R::fail("Window FPS must be a number greater than 0.");
            ds.windowFps = *v;
        }
        if (s.contains("respawnSeconds")) {
            auto v = jsNumber(s["respawnSeconds"]);
            if (!v || !std::isfinite(*v) || *v < 0) return R::fail("Respawn time must be a number greater than or equal to 0.");
            ds.respawnSeconds = *v;
        }
        if (s.contains("timeUnit")) {
            auto const& tu = s["timeUnit"].asString();
            if (tu != "seconds" && tu != "frames") return R::fail("Time unit must be either \"seconds\" or \"frames\".");
            timeUnit = tu;
        } else if (s.contains("useFrameNumbers")) {
            if (!s["useFrameNumbers"].isBool()) return R::fail("Use Frame Numbers must be true or false.");
            timeUnit = s["useFrameNumbers"].asBool() ? "frames" : "seconds";
        }
    }

    double gameFps = ds.gameFps.value_or(240.0);
    if (timeUnit == "frames" && !ds.gameFps) {
        diags.push_back({Severity::Warning, "Time positions are frame numbers but no Game FPS is stored; assuming 240 (the NaNDL default)."});
    }

    size_t i = 0;
    for (auto const& row : rowsVal->items()) {
        i++;
        if (!row.isObject()) return R::fail("Frame-window row " + std::to_string(i) + " must be an object.");
        auto input = jsNumber(row["input"]);
        if (!input || !std::isfinite(*input) || *input != std::floor(*input)) {
            return R::fail("Input number in imported row " + std::to_string(i) + " must be an integer.");
        }
        auto time = jsNumber(row["timePosition"]);
        if (!row.contains("timePosition") || !time || !std::isfinite(*time)) {
            return R::fail("Time position in imported row " + std::to_string(i) + " must be a number.");
        }
        WindowRow wr;
        wr.input = static_cast<int64_t>(*input);
        wr.timeSeconds = timeUnit == "frames" ? *time / gameFps : *time;
        auto const& fw = row["frameWindow"];
        if (fw.isString() && fw.asString() == "-") {
            wr.windowFrames = std::nullopt;
        } else {
            auto w = jsNumber(fw);
            if (!row.contains("frameWindow") || !w || !std::isfinite(*w) || *w < 0) {
                return R::fail("Frame window in imported row " + std::to_string(i) + " must be a non-negative number or -.");
            }
            wr.windowFrames = *w;
        }
        ds.rows.push_back(wr);
    }
    return ds;
}

// ---------------------------------------------------------------------------
// CSV / whitespace frame-window tables
// ---------------------------------------------------------------------------

namespace {

std::vector<std::string> splitCsvLine(std::string const& line, char sep) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur += '"';
                    i++;
                } else {
                    quoted = false;
                }
            } else {
                cur += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == sep) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(trim(cur));
    return out;
}

std::vector<std::string> splitWs(std::string const& line) {
    std::vector<std::string> out;
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok) out.push_back(tok);
    return out;
}

std::optional<double> parseNum(std::string const& s) {
    if (s.empty()) return std::nullopt;
    std::istringstream ss(s);
    ss.imbue(std::locale::classic());
    double d;
    ss >> d;
    if (ss.fail()) return std::nullopt;
    ss >> std::ws;
    if (!ss.eof()) return std::nullopt;
    return d;
}

int findColumn(std::vector<std::string> const& header, std::initializer_list<char const*> names) {
    for (size_t i = 0; i < header.size(); i++) {
        auto h = toLower(header[i]);
        for (auto n : names) {
            if (h == n) return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

Expected<WindowDataset> parseWindowCsv(std::string_view text, std::vector<Diagnostic>& diags) {
    using R = Expected<WindowDataset>;
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : text) {
            if (c == '\n') {
                lines.push_back(cur);
                cur.clear();
            } else if (c != '\r') {
                cur += c;
            }
        }
        if (!cur.empty()) lines.push_back(cur);
    }
    // Drop blank and '#' comment lines (the NaNDL text format allows '#').
    std::vector<std::pair<size_t, std::string>> content;
    for (size_t i = 0; i < lines.size(); i++) {
        auto t = trim(lines[i]);
        if (t.empty() || t[0] == '#') continue;
        content.emplace_back(i + 1, t);
    }
    if (content.empty()) return R::fail("The table file is empty.");

    char sep = ',';
    bool whitespace = false;
    auto const& first = content[0].second;
    if (first.find(',') == std::string::npos) {
        if (first.find(';') != std::string::npos) sep = ';';
        else if (first.find('\t') != std::string::npos) sep = '\t';
        else whitespace = true;
    }
    auto split = [&](std::string const& l) { return whitespace ? splitWs(l) : splitCsvLine(l, sep); };

    auto header = split(first);
    int colInput = -1, colTime = -1, colWindow = -1;
    size_t startRow = 0;
    bool firstIsNumeric = header.size() >= 3 && parseNum(header[0]) && parseNum(header[1]);
    if (firstIsNumeric) {
        colInput = 0;
        colTime = 1;
        colWindow = 2;
        startRow = 0;
        diags.push_back({Severity::Info, "No header row found; reading columns as: input number, time (seconds), frame window."});
    } else {
        colInput = findColumn(header, {"input", "input_number", "counted_input", "inputnumber"});
        colTime = findColumn(header, {"time_s", "timeposition", "time", "time_seconds", "seconds"});
        colWindow = findColumn(header, {"window_frames", "framewindow", "frame_window", "window"});
        if (colTime < 0 || colWindow < 0) {
            return R::fail("The table needs a time column (time_s / timePosition) and a frame-window column (window_frames / frameWindow).");
        }
        startRow = 1;
    }

    WindowDataset ds;
    int64_t autoInput = 0;
    for (size_t r = startRow; r < content.size(); r++) {
        auto const& [lineNo, line] = content[r];
        auto cells = split(line);
        auto cell = [&](int col) -> std::string { return col >= 0 && static_cast<size_t>(col) < cells.size() ? cells[col] : ""; };
        WindowRow row;
        autoInput++;
        if (colInput >= 0) {
            auto in = parseNum(cell(colInput));
            if (!in || *in != std::floor(*in)) return R::fail("Line " + std::to_string(lineNo) + ": the input number must be an integer.");
            row.input = static_cast<int64_t>(*in);
        } else {
            row.input = autoInput;
        }
        auto t = parseNum(cell(colTime));
        if (!t || !std::isfinite(*t)) return R::fail("Line " + std::to_string(lineNo) + ": the time position is missing or not a number.");
        row.timeSeconds = *t;
        auto wtext = cell(colWindow);
        if (wtext == "-" || wtext.empty()) {
            row.windowFrames = std::nullopt;
        } else {
            auto w = parseNum(wtext);
            if (!w || !std::isfinite(*w) || *w < 0) return R::fail("Line " + std::to_string(lineNo) + ": the frame window must be a non-negative number or -.");
            row.windowFrames = *w;
        }
        ds.rows.push_back(row);
    }
    if (ds.rows.empty()) return R::fail("The table has a header but no data rows.");
    if (colInput < 0) {
        diags.push_back({Severity::Info, "No input-number column; rows were numbered 1, 2, 3, ..."});
    }
    return ds;
}

// ---------------------------------------------------------------------------
// Handlers + registry
// ---------------------------------------------------------------------------

namespace {

std::string_view asText(std::span<uint8_t const> data) {
    return std::string_view(reinterpret_cast<char const*>(data.data()), data.size());
}

char firstNonWs(std::span<uint8_t const> data) {
    size_t i = 0;
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) i = 3;
    for (; i < data.size() && i < 4096; i++) {
        char c = static_cast<char>(data[i]);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return c;
    }
    return 0;
}

bool looksLikeText(std::span<uint8_t const> data) {
    size_t n = std::min<size_t>(data.size(), 4096);
    for (size_t i = 0; i < n; i++) {
        uint8_t b = data[i];
        if (b == 0) return false;
        if (b < 0x09) return false;
    }
    return n > 0;
}

ImportResult macroResult(Macro m) {
    ImportResult r;
    r.kind = ImportKind::Macro;
    r.formatId = m.formatId;
    r.formatName = m.formatName;
    r.diagnostics = m.diagnostics;
    r.macro = std::move(m);
    return r;
}

class Gdr2Handler final : public FormatHandler {
public:
    std::string_view id() const override { return "gdr2"; }
    std::string_view displayName() const override { return "GDR2 replay (binary)"; }
    std::vector<std::string> extensions() const override { return {"gdr2", "gdr"}; }
    int sniff(std::span<uint8_t const> data, std::string_view) const override {
        return data.size() >= 3 && std::memcmp(data.data(), "GDR", 3) == 0 ? 100 : 0;
    }
    Expected<ImportResult> parse(std::span<uint8_t const> data) const override {
        auto m = parseGdr2(data);
        if (!m) return Expected<ImportResult>::fail(m.error());
        auto macro = std::move(m).value();
        if (!validateMacro(macro)) {
            return Expected<ImportResult>::fail(macro.diagnostics.back().message);
        }
        return macroResult(std::move(macro));
    }
};

class GdrMsgPackHandler final : public FormatHandler {
public:
    std::string_view id() const override { return "gdr1-msgpack"; }
    std::string_view displayName() const override { return "GDR replay (MessagePack)"; }
    std::vector<std::string> extensions() const override { return {"gdr"}; }
    int sniff(std::span<uint8_t const> data, std::string_view ext) const override {
        if (!msgpack::looksLikeMap(data)) return 0;
        return ext == "gdr" ? 60 : 30;
    }
    Expected<ImportResult> parse(std::span<uint8_t const> data) const override {
        auto doc = msgpack::decode(data);
        if (!doc) return Expected<ImportResult>::fail("This looks like a MessagePack GDR replay but it could not be decoded: " + doc.error());
        auto m = parseGdr1Document(doc.value(), "gdr1-msgpack", "GDR (MessagePack)");
        if (!m) return Expected<ImportResult>::fail(m.error());
        auto macro = std::move(m).value();
        if (!validateMacro(macro)) return Expected<ImportResult>::fail(macro.diagnostics.back().message);
        return macroResult(std::move(macro));
    }
};

class JsonDocumentHandler final : public FormatHandler {
public:
    std::string_view id() const override { return "json"; }
    std::string_view displayName() const override { return "JSON (GDR, NaNDL calculator, saved analysis)"; }
    std::vector<std::string> extensions() const override { return {"json", "gdr"}; }
    int sniff(std::span<uint8_t const> data, std::string_view ext) const override {
        char c = firstNonWs(data);
        if (c != '{' && c != '[') return 0;
        return ext == "json" || ext == "gdr" ? 70 : 50;
    }
    Expected<ImportResult> parse(std::span<uint8_t const> data) const override {
        using R = Expected<ImportResult>;
        auto parsed = json::parse(asText(data));
        if (!parsed) return R::fail("The file is not valid JSON. " + parsed.error());
        auto const& doc = parsed.value();

        if (doc.isObject() && doc["format"].asString() == "frame-window-analyzer") {
            ImportResult r;
            r.kind = ImportKind::SavedAnalysis;
            r.formatId = "fwa-analysis";
            r.formatName = "Frame Window Analyzer save";
            r.analysisJson = doc;
            return r;
        }

        auto rowsLookLikeNandl = [](json::Value const& rows) {
            return rows.isArray() && !rows.items().empty() && rows.items()[0].isObject() &&
                rows.items()[0].contains("timePosition");
        };
        bool isNandl = (doc.isObject() && doc["format"].asString() == "nandl-calculator") ||
            (doc.isObject() && doc.contains("frameWindows")) ||
            rowsLookLikeNandl(doc) ||
            (doc.isObject() && rowsLookLikeNandl(doc["inputs"]));
        if (isNandl) {
            ImportResult r;
            r.kind = ImportKind::WindowData;
            r.formatId = "nandl-json";
            r.formatName = "NaNDL calculator JSON";
            auto ds = parseNandlCalculatorJson(doc, r.diagnostics);
            if (!ds) return R::fail(ds.error());
            if (doc.isObject() && doc.contains("format") && doc["format"].asString() != "nandl-calculator") {
                r.diagnostics.push_back({Severity::Warning, "Unexpected \"format\" value \"" + doc["format"].asString() + "\"; read as NaNDL calculator rows."});
            }
            r.windows = std::move(ds).value();
            return r;
        }

        if (doc.isObject() && doc["inputs"].isArray()) {
            auto const& items = doc["inputs"].items();
            bool framesPresent = items.empty() || (items[0].isObject() && items[0].contains("frame"));
            if (framesPresent) {
                auto m = parseGdr1Document(doc, "gdr1-json", "GDR (JSON)");
                if (!m) return R::fail(m.error());
                auto macro = std::move(m).value();
                if (!validateMacro(macro)) return R::fail(macro.diagnostics.back().message);
                return macroResult(std::move(macro));
            }
        }

        return R::fail(
            "This JSON file is not a supported format. Supported JSON files are GDR replays "
            "(with an \"inputs\" list of {frame, btn, 2p, down}), NaNDL calculator exports, "
            "and Frame Window Analyzer saves."
        );
    }
};

class CsvHandler final : public FormatHandler {
public:
    std::string_view id() const override { return "window-table"; }
    std::string_view displayName() const override { return "Frame-window table (CSV / text)"; }
    std::vector<std::string> extensions() const override { return {"csv", "txt", "tsv"}; }
    int sniff(std::span<uint8_t const> data, std::string_view ext) const override {
        if (!looksLikeText(data)) return 0;
        char c = firstNonWs(data);
        if (c == '{' || c == '[') return 0;
        if (data.size() >= 3 && std::memcmp(data.data(), "GDR", 3) == 0) return 0;
        if (ext == "csv" || ext == "tsv" || ext == "txt") return 40;
        return 10;
    }
    Expected<ImportResult> parse(std::span<uint8_t const> data) const override {
        ImportResult r;
        r.kind = ImportKind::WindowData;
        r.formatId = "window-table";
        r.formatName = "Frame-window table";
        auto ds = parseWindowCsv(asText(data), r.diagnostics);
        if (!ds) return Expected<ImportResult>::fail(ds.error());
        r.windows = std::move(ds).value();
        return r;
    }
};

} // namespace

FormatRegistry::FormatRegistry() {
    add(std::make_unique<Gdr2Handler>());
    add(std::make_unique<JsonDocumentHandler>());
    add(std::make_unique<GdrMsgPackHandler>());
    add(std::make_unique<CsvHandler>());
}

FormatRegistry& FormatRegistry::get() {
    static FormatRegistry instance;
    return instance;
}

void FormatRegistry::add(std::unique_ptr<FormatHandler> handler) {
    m_handlers.push_back(std::move(handler));
}

std::vector<FormatHandler const*> FormatRegistry::handlers() const {
    std::vector<FormatHandler const*> out;
    for (auto const& h : m_handlers) out.push_back(h.get());
    return out;
}

std::vector<std::string> FormatRegistry::allExtensions() const {
    std::vector<std::string> out;
    for (auto const& h : m_handlers) {
        for (auto const& e : h->extensions()) {
            if (std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
        }
    }
    return out;
}

Expected<ImportResult> FormatRegistry::importData(std::span<uint8_t const> data, std::string_view fileName) const {
    using R = Expected<ImportResult>;
    if (data.empty()) return R::fail("The file is empty.");
    constexpr size_t kMaxFileSize = 256ull * 1024 * 1024;
    if (data.size() > kMaxFileSize) return R::fail("The file is larger than 256 MB, which is far bigger than any macro.");

    auto ext = extensionOf(fileName);
    FormatHandler const* best = nullptr;
    int bestScore = 0;
    for (auto const& h : m_handlers) {
        int score = h->sniff(data, ext);
        if (score > bestScore) {
            bestScore = score;
            best = h.get();
        }
    }
    if (!best) {
        std::string names;
        for (auto const& h : m_handlers) names += std::string(names.empty() ? "" : ", ") + std::string(h->displayName());
        return R::fail("Unrecognized file format. Supported formats: " + names + ".");
    }
    auto result = best->parse(data);
    if (!result) return result;
    if (result.value().macro) result.value().macro->sourceName = std::string(fileName);
    return result;
}

} // namespace fwa
