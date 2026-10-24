// Automated tests for the Frame Window Analyzer core library.
// Build: see tests/CMakeLists.txt (no Geode required).

#include "TestFramework.hpp"

#include "../src/core/Analysis.hpp"
#include "../src/core/Formats.hpp"
#include "../src/core/Json.hpp"
#include "../src/core/MsgPack.hpp"
#include "../src/core/Nandl.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

using namespace fwa;

#ifndef FWA_FIXTURE_DIR
#define FWA_FIXTURE_DIR "fixtures"
#endif

namespace {

std::vector<uint8_t> readFixture(std::string const& name) {
    std::ifstream f(std::string(FWA_FIXTURE_DIR) + "/" + name, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> bytes(std::string const& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

Expected<ImportResult> importBytes(std::vector<uint8_t> const& data, std::string const& name) {
    return FormatRegistry::get().importData(std::span<uint8_t const>(data.data(), data.size()), name);
}

// --- tiny MessagePack encoder used to build GDR 1 fixtures ------------------
struct MP {
    std::vector<uint8_t> out;
    void map(size_t n) { if (n < 16) out.push_back(static_cast<uint8_t>(0x80 | n)); else { out.push_back(0xde); out.push_back(static_cast<uint8_t>(n >> 8)); out.push_back(static_cast<uint8_t>(n)); } }
    void arr(size_t n) { if (n < 16) out.push_back(static_cast<uint8_t>(0x90 | n)); else { out.push_back(0xdc); out.push_back(static_cast<uint8_t>(n >> 8)); out.push_back(static_cast<uint8_t>(n)); } }
    void str(std::string const& s) { out.push_back(static_cast<uint8_t>(0xa0 | s.size())); out.insert(out.end(), s.begin(), s.end()); }
    void u(uint64_t v) {
        if (v < 128) out.push_back(static_cast<uint8_t>(v));
        else if (v < 65536) { out.push_back(0xcd); out.push_back(static_cast<uint8_t>(v >> 8)); out.push_back(static_cast<uint8_t>(v)); }
        else { out.push_back(0xce); for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    }
    void b(bool v) { out.push_back(v ? 0xc3 : 0xc2); }
    void f32(float f) { uint32_t x; std::memcpy(&x, &f, 4); out.push_back(0xca); for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>(x >> (8 * i))); }
};

std::vector<uint8_t> makeGdr1MsgPack(bool includeFramerate) {
    MP m;
    m.map(includeFramerate ? 11 : 10);
    m.str("author"); m.str("Prevter");
    m.str("bot"); m.map(2); m.str("name"); m.str("Bot"); m.str("version"); m.str("1.0");
    m.str("coins"); m.u(0);
    m.str("description"); m.str("");
    m.str("duration"); m.f32(5.82f);
    if (includeFramerate) { m.str("framerate"); m.f32(240.f); }
    m.str("gameVersion"); m.f32(2.2081f);
    m.str("inputs"); m.arr(4);
    auto in = [&](uint64_t frame, bool down, bool p2) {
        m.map(4); m.str("2p"); m.b(p2); m.str("btn"); m.u(1); m.str("down"); m.b(down); m.str("frame"); m.u(frame);
    };
    in(352, true, false); in(371, false, false); in(400, true, true); in(300, true, false);
    m.str("ldm"); m.b(false);
    m.str("level"); m.map(2); m.str("id"); m.u(0); m.str("name"); m.str("Bot touch trigger test");
    m.str("seed"); m.u(0);
    return m.out;
}

std::vector<nandl::Row> rowsFrom(std::initializer_list<std::tuple<int64_t, double, double>> list) {
    std::vector<nandl::Row> rows;
    for (auto const& [i, t, w] : list) {
        nandl::Row r;
        r.input = i;
        r.time = t;
        if (w >= 0) r.window = w;
        rows.push_back(r);
    }
    return rows;
}

nandl::Constants const kNone{0, 0, 0};
nandl::Constants const kNerveFatigue{nandl::kDefaultNerve, nandl::kDefaultFatigue, 0};
nandl::Constants const kAll{nandl::kDefaultNerve, nandl::kDefaultFatigue, nandl::kDefaultCps};
nandl::Constants const kCpsOnly{0, 0, nandl::kDefaultCps};

std::vector<nandl::Row> dataset1() {
    return rowsFrom({{1, 1.0, 4}, {2, 2.5, 2}, {3, 3.0, 1}, {4, 7.25, 6}, {5, 10.0, 3}});
}
std::vector<nandl::Row> dataset2() { // -1 = ignored ("-")
    return rowsFrom({{1, 0.5, -1}, {2, 1.0, 0}, {3, 1.5, 2}, {4, 2.0, -1}, {5, 4.0, 5}});
}
std::vector<nandl::Row> dataset3() {
    std::vector<nandl::Row> rows;
    for (int i = 1; i <= 2000; i++) {
        nandl::Row r;
        r.input = i;
        r.time = i * 0.37 + (i % 7) * 0.01;
        r.window = static_cast<double>((static_cast<int64_t>(i) * 7919) % 13);
        rows.push_back(r);
    }
    return rows;
}

} // namespace

// ===========================================================================
// JSON
// ===========================================================================

TEST("json: parse and dump round trip") {
    auto r = json::parse(R"({"a": 1, "b": [true, false, null], "c": "x\"yé\n", "d": -2.5e3, "e": {}})");
    REQUIRE(r.ok());
    auto const& v = r.value();
    CHECK_EQ(v["a"].asInt(), 1);
    CHECK(v["a"].isInteger());
    CHECK_EQ(v["b"].size(), size_t(3));
    CHECK_EQ(v["c"].asString(), std::string("x\"y\xC3\xA9\n"));
    CHECK_NEAR(v["d"].asDouble(), -2500.0, 1e-15);
    auto again = json::parse(json::dump(v));
    REQUIRE(again.ok());
    CHECK_EQ(json::dump(again.value(), -1), json::dump(v, -1));
}

TEST("json: errors are reported, not crashed on") {
    CHECK(!json::parse("").ok());
    CHECK(!json::parse("{").ok());
    CHECK(!json::parse("[1,]").ok());
    CHECK(!json::parse("{\"a\":1} x").ok());
    CHECK(!json::parse("\"unterminated").ok());
    CHECK(!json::parse("01").ok()); // leading zero: "0" then trailing data
    auto deep = std::string(1000, '[') + std::string(1000, ']');
    auto r = json::parse(deep);
    CHECK(!r.ok());
    CHECK_CONTAINS(r.error(), "nested too deeply");
}

TEST("json: number formatting is locale independent and round-trips") {
    CHECK_EQ(json::formatNumber(0.1), std::string("0.1"));
    CHECK_EQ(json::formatNumber(240.0), std::string("240"));
    CHECK_EQ(json::formatNumber(1.0 / 3.0), std::string("0.3333333333333333"));
    CHECK_EQ(json::formatNumber(std::nan("")), std::string("null"));
    double v = 0.1 + 0.2;
    auto r = json::parse(json::formatNumber(v));
    REQUIRE(r.ok());
    CHECK_EQ(r.value().asDouble(), v);
}

// ===========================================================================
// MessagePack
// ===========================================================================

TEST("msgpack: decodes GDR 1 style documents") {
    auto data = makeGdr1MsgPack(true);
    auto r = msgpack::decode(data);
    REQUIRE(r.ok());
    CHECK_EQ(r.value()["author"].asString(), std::string("Prevter"));
    CHECK_EQ(r.value()["inputs"].size(), size_t(4));
    CHECK_NEAR(r.value()["framerate"].asDouble(), 240.0, 1e-12);
}

TEST("msgpack: truncated and garbage data fail cleanly") {
    auto data = makeGdr1MsgPack(true);
    data.resize(data.size() / 2);
    CHECK(!msgpack::decode(data).ok());
    std::vector<uint8_t> hugeArray = {0xdd, 0xff, 0xff, 0xff, 0xff};
    CHECK(!msgpack::decode(hugeArray).ok());
    std::vector<uint8_t> ext = {0xd4, 0x01, 0x02};
    CHECK(!msgpack::decode(ext).ok());
}

// ===========================================================================
// Macro parsing: GDR2 (fixtures generated with the reference implementation)
// ===========================================================================

TEST("gdr2: basic two-player replay matches the reference writer") {
    auto data = readFixture("basic.gdr2");
    REQUIRE(!data.empty());
    auto r = importBytes(data, "basic.gdr2");
    REQUIRE(r.ok());
    auto const& res = r.value();
    CHECK(res.kind == ImportKind::Macro);
    CHECK_EQ(res.formatId, std::string("gdr2"));
    REQUIRE(res.macro.has_value());
    auto const& m = *res.macro;
    CHECK_EQ(m.meta.author, std::string("tester"));
    CHECK_EQ(m.meta.description, std::string("fixture"));
    CHECK_EQ(m.meta.botName, std::string("FixtureBot"));
    CHECK_EQ(m.meta.botVersion, 3);
    CHECK_EQ(m.meta.levelId, 128u);
    CHECK_EQ(m.meta.levelName, std::string("Fixture Level"));
    CHECK_EQ(m.meta.gameVersionText, std::string("2.2081"));
    CHECK_NEAR(m.meta.framerate, 240.0, 0);
    CHECK_EQ(m.meta.seed, int64_t(1234));
    CHECK_EQ(m.meta.coins, 1);
    CHECK(!m.meta.platformer);
    REQUIRE(m.meta.storedDurationSeconds.has_value());
    CHECK_NEAR(*m.meta.storedDurationSeconds, 12.5, 0);
    REQUIRE(m.meta.deathFrames.size() == 2);
    CHECK_EQ(m.meta.deathFrames[0], uint64_t(500));
    CHECK_EQ(m.meta.deathFrames[1], uint64_t(900));
    REQUIRE(m.inputs.size() == 10);
    // Chronological (stable by frame, P1 before P2 on the same frame).
    uint64_t frames[] = {100, 130, 200, 250, 400, 460, 1000, 1000, 1001, 1100};
    bool p2[] = {false, false, true, true, false, false, false, true, false, true};
    bool down[] = {true, false, true, false, true, false, true, true, false, false};
    for (size_t i = 0; i < 10; i++) {
        CHECK_EQ(m.inputs[i].frame, frames[i]);
        CHECK_EQ(m.inputs[i].player2, p2[i]);
        CHECK_EQ(m.inputs[i].down, down[i]);
        CHECK_EQ(int(m.inputs[i].button), 1);
    }
    CHECK_EQ(m.pressCount(), size_t(5));
    CHECK_EQ(m.player2Count(), size_t(4));
}

TEST("gdr2: platformer packing, large deltas, negative seed, max level id") {
    auto r = importBytes(readFixture("platformer.gdr2"), "platformer.gdr2");
    REQUIRE(r.ok());
    auto const& m = *r.value().macro;
    CHECK(m.meta.platformer);
    CHECK_NEAR(m.meta.framerate, 480.0, 0);
    CHECK_EQ(m.meta.seed, int64_t(-5));
    CHECK_EQ(m.meta.levelId, 4294967295u);
    REQUIRE(m.inputs.size() == 6);
    CHECK_EQ(int(m.inputs[0].button), 3);
    CHECK_EQ(int(m.inputs[2].button), 2);
    CHECK_EQ(int(m.inputs[4].button), 1);
    CHECK_EQ(m.inputs[5].frame, uint64_t(100000));
    CHECK_NEAR(m.durationSeconds(), 100000.0 / 480.0, 1e-12);
}

TEST("gdr2: input and replay extensions are skipped correctly") {
    auto r = importBytes(readFixture("physics-ext.gdr2"), "physics-ext.gdr2");
    REQUIRE(r.ok());
    auto const& m = *r.value().macro;
    CHECK_EQ(m.meta.inputExtensionTag, std::string("Phys"));
    REQUIRE(m.inputs.size() == 3);
    CHECK_EQ(m.inputs[0].frame, uint64_t(10));
    CHECK_EQ(m.inputs[1].frame, uint64_t(20));
    CHECK(m.inputs[1].player2);
    CHECK_EQ(m.inputs[2].frame, uint64_t(30));
}

TEST("gdr2: truncated, wrong-version and corrupted files are rejected with messages") {
    auto data = readFixture("basic.gdr2");
    REQUIRE(!data.empty());
    for (size_t cut : {size_t(4), size_t(20), size_t(60), data.size() - 1}) {
        std::vector<uint8_t> t(data.begin(), data.begin() + static_cast<long>(cut));
        auto r = importBytes(t, "x.gdr2");
        CHECK(!r.ok());
    }
    auto v3 = data;
    v3[3] = 0x03;
    auto r = importBytes(v3, "x.gdr2");
    CHECK(!r.ok());
    CHECK_CONTAINS(r.error(), "Unsupported GDR binary version 3");

    // Corrupt the input count to something enormous.
    auto bad = data;
    bad[69] = 0xff; bad[70] = 0xff;
    CHECK(!importBytes(bad, "x.gdr2").ok());
}

// ===========================================================================
// Macro parsing: GDR 1 (JSON + MessagePack)
// ===========================================================================

TEST("gdr1: MessagePack replay is detected by content and sorted") {
    auto r = importBytes(makeGdr1MsgPack(true), "renamed.bin");
    REQUIRE(r.ok());
    CHECK_EQ(r.value().formatId, std::string("gdr1-msgpack"));
    auto const& m = *r.value().macro;
    REQUIRE(m.inputs.size() == 4);
    CHECK_EQ(m.inputs[0].frame, uint64_t(300)); // was stored out of order
    CHECK_EQ(m.inputs[1].frame, uint64_t(352));
    CHECK(m.meta.framerateFromFile);
    bool warned = false;
    for (auto const& d : m.diagnostics) warned |= d.message.find("out of chronological order") != std::string::npos;
    CHECK(warned);
    // P1 presses at 300 and 352 with no release in between -> second one is redundant.
    CHECK(m.inputs[1].redundant);
}

TEST("gdr1: JSON replay, missing framerate falls back to 240 with a note") {
    std::string doc = R"({"gameVersion": 2.2081, "description": "", "duration": 1.5, "bot": {"name": "x", "version": "2"},
        "level": {"id": 42, "name": "L"}, "author": "a", "seed": 0, "coins": 0, "ldm": false,
        "inputs": [{"frame": 10, "btn": 1, "2p": false, "down": true}, {"frame": 25, "btn": 1, "2p": false, "down": false}]})";
    auto r = importBytes(bytes(doc), "x.gdr");
    REQUIRE(r.ok());
    CHECK_EQ(r.value().formatId, std::string("gdr1-json"));
    auto const& m = *r.value().macro;
    CHECK(!m.meta.framerateFromFile);
    CHECK_NEAR(m.meta.framerate, 240.0, 0);
    CHECK_EQ(m.meta.levelId, 42u);
    CHECK_EQ(m.meta.botVersion, 2);
    CHECK_EQ(m.inputs.size(), size_t(2));
}

TEST("gdr1: invalid inputs reject the whole file") {
    std::string doc = R"({"framerate": 240, "inputs": [{"frame": -5, "btn": 1, "2p": false, "down": true}]})";
    auto r = importBytes(bytes(doc), "x.json");
    CHECK(!r.ok());
    CHECK_CONTAINS(r.error(), "frame");
    std::string doc2 = R"({"framerate": 240, "inputs": [{"frame": 5, "btn": 9, "2p": false, "down": true}]})";
    CHECK(!importBytes(bytes(doc2), "x.json").ok());
    std::string doc3 = R"({"framerate": 0, "inputs": [{"frame": 5, "btn": 1, "2p": false, "down": true}]})";
    auto r3 = importBytes(bytes(doc3), "x.json");
    CHECK(!r3.ok());
    CHECK_CONTAINS(r3.error(), "framerate");
    std::string doc4 = R"({"framerate": 240, "inputs": [{"frame": 1.5, "btn": 1, "2p": false, "down": true}]})";
    CHECK(!importBytes(bytes(doc4), "x.json").ok());
}

TEST("validation: duplicates and redundant events are flagged, empty macro is a warning") {
    std::string doc = R"({"framerate": 240, "inputs": [
        {"frame": 10, "btn": 1, "2p": false, "down": true},
        {"frame": 10, "btn": 1, "2p": false, "down": true},
        {"frame": 20, "btn": 1, "2p": false, "down": false},
        {"frame": 30, "btn": 1, "2p": false, "down": false},
        {"frame": 30, "btn": 1, "2p": true, "down": true}]})";
    auto r = importBytes(bytes(doc), "x.json");
    REQUIRE(r.ok());
    auto const& m = *r.value().macro;
    CHECK(!m.inputs[0].duplicate);
    CHECK(m.inputs[1].duplicate);
    CHECK(!m.inputs[2].redundant);
    CHECK(m.inputs[3].redundant);
    CHECK(!m.inputs[4].redundant); // different player
    auto a = analysisFromMacro(m);
    CountOptions co;
    CHECK(isCounted(a.inputs[0], co));
    CHECK(!isCounted(a.inputs[1], co));
    CHECK(!isCounted(a.inputs[2], co)); // releases not counted by default
    co.includeReleases = true;
    CHECK(isCounted(a.inputs[2], co));
    CHECK(!isCounted(a.inputs[3], co));

    auto e = importBytes(bytes(R"({"framerate": 240, "inputs": []})"), "empty.json");
    REQUIRE(e.ok());
    CHECK(e.value().macro->inputs.empty());
    bool warned = false;
    for (auto const& d : e.value().diagnostics) warned |= d.severity == Severity::Warning;
    CHECK(warned);
}

TEST("validation: implausible frame numbers are treated as corruption") {
    std::string doc = R"({"framerate": 240, "inputs": [{"frame": 99999999999, "btn": 1, "2p": false, "down": true}]})";
    auto r = importBytes(bytes(doc), "x.json");
    CHECK(!r.ok());
    CHECK_CONTAINS(r.error(), "implausibly large");
}

// ===========================================================================
// Format detection
// ===========================================================================

TEST("formats: unsupported files are rejected, never reinterpreted") {
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0x0d};
    auto r = importBytes(png, "level.gdr");
    CHECK(!r.ok());
    CHECK_CONTAINS(r.error(), "Unrecognized file format");
    CHECK(!importBytes({}, "x.gdr").ok());
    auto j = importBytes(bytes(R"({"hello": "world"})"), "x.json");
    CHECK(!j.ok());
    CHECK_CONTAINS(j.error(), "not a supported format");
    auto txt = importBytes(bytes("just some words\nnot a table\n"), "notes.txt");
    CHECK(!txt.ok());
}

TEST("formats: NaNDL calculator export (numbers, numeric strings, '-', frames unit)") {
    std::string doc = R"({"format": "nandl-calculator", "version": 1,
        "settings": {"gameFps": 240, "windowFps": 60, "respawnSeconds": 1, "timeUnit": "frames"},
        "frameWindows": [{"input": 1, "timePosition": 240, "frameWindow": 2},
                         {"input": "2", "timePosition": "480", "frameWindow": "-"},
                         {"input": 3, "timePosition": 720, "frameWindow": "0.5"}]})";
    auto r = importBytes(bytes(doc), "nandl-calculator.json");
    REQUIRE(r.ok());
    CHECK(r.value().kind == ImportKind::WindowData);
    auto const& ds = *r.value().windows;
    REQUIRE(ds.rows.size() == 3);
    CHECK_NEAR(ds.rows[0].timeSeconds, 1.0, 1e-15);
    CHECK_EQ(ds.rows[1].input, int64_t(2));
    CHECK(!ds.rows[1].windowFrames.has_value());
    CHECK_NEAR(*ds.rows[2].windowFrames, 0.5, 0);
    CHECK_NEAR(*ds.windowFps, 60.0, 0);
    auto a = analysisFromWindowData(ds, "nandl-json", "NaNDL", "f.json", 240.0, {});
    // 2 frames at 60 Hz = 1/30 s -> 8 frames at 240 Hz.
    CHECK_NEAR(*windowFrames(a.inputs[0], 240.0, a.tps, true), 8.0, 1e-12);

    auto bad = importBytes(bytes(R"({"frameWindows": [{"input": 1.5, "timePosition": 1, "frameWindow": 2}]})"), "x.json");
    CHECK(!bad.ok());
    CHECK_CONTAINS(bad.error(), "must be an integer");
    auto neg = importBytes(bytes(R"({"frameWindows": [{"input": 1, "timePosition": 1, "frameWindow": -2}]})"), "x.json");
    CHECK(!neg.ok());
}

TEST("formats: CSV tables with header, without header, and whitespace") {
    auto r = importBytes(bytes("input,time_s,window_frames\n1,0.5,3\n2,1.25,-\n3,2,1.5\n"), "w.csv");
    REQUIRE(r.ok());
    auto const& ds = *r.value().windows;
    REQUIRE(ds.rows.size() == 3);
    CHECK(!ds.rows[1].windowFrames);
    CHECK_NEAR(ds.rows[2].timeSeconds, 2.0, 0);

    auto r2 = importBytes(bytes("# comment\n1 0.5 3\n2 1.0 4\n"), "w.txt");
    REQUIRE(r2.ok());
    CHECK_EQ(r2.value().windows->rows.size(), size_t(2));

    auto r3 = importBytes(bytes("time_s,window_frames\n0.5,abc\n"), "w.csv");
    CHECK(!r3.ok());
}

// ===========================================================================
// NaNDL model: probabilities and hand-calculated examples
// ===========================================================================

TEST("nandl: two-sided normal probabilities") {
    CHECK_NEAR(nandl::passProbability(1.0), 0.6826894921370859, 1e-14);
    CHECK_NEAR(nandl::passProbability(2.0), 0.9544997361036416, 1e-14);
    CHECK_NEAR(nandl::passProbability(3.0), 0.9973002039367398, 1e-14);
    CHECK_NEAR(nandl::failProbability(3.0), 0.0026997960632601913, 1e-12);
    CHECK_EQ(nandl::passProbability(0.0), 0.0);
    CHECK_EQ(nandl::passProbability(-1.0), 0.0);
    // Far tail: erfc keeps the fail chance non-zero where 1 - erf would be exactly 0.
    CHECK(nandl::failProbability(12.0) > 0.0);
    CHECK(nandl::failProbability(12.0) < 1e-30);
}

TEST("nandl: single and two-input examples calculated by hand") {
    nandl::RunOptions opt{240.0, 0.0};
    // N=2 at 240 Hz -> w = 1/120 s; L = 120 -> s = 0.5 * w * L = 0.5
    auto one = nandl::computeStats(rowsFrom({{1, 1.0, 2}}), opt, 120.0, kNone);
    CHECK_NEAR(one.completionProbability, 0.3829249225480262, 1e-13);
    CHECK_NEAR(one.expectedAttemptTime, 1.0, 1e-13); // single input: t1 * q1 + t1 * P(C) = t1
    CHECK_NEAR(one.expectedTotalTime, 1.0 / 0.3829249225480262, 1e-12);

    std::vector<nandl::InputDetail> det;
    auto two = nandl::computeStats(rowsFrom({{1, 1.0, 2}, {2, 3.0, 4}}), opt, 120.0, kNone, &det);
    CHECK_NEAR(det[0].sigma, 0.5, 1e-15);
    CHECK_NEAR(det[1].sigma, 1.0, 1e-15);
    CHECK_NEAR(two.expectedAttemptTime, 1.7658498450960523, 1e-13);
    CHECK_NEAR(two.completionProbability, 0.2614188209009449, 1e-13);
    CHECK_NEAR(two.expectedTotalTime, 6.7548688308297296, 1e-12);
    CHECK_NEAR(det[1].logReach, std::log(0.3829249225480262), 1e-13);
}

TEST("nandl: modifiers match the published equations") {
    nandl::RunOptions opt{240.0, 0.0};
    std::vector<nandl::InputDetail> det;
    // Nerve: lambda = exp(-kt * t)
    nandl::computeStats(rowsFrom({{1, 1.0, 2}}), opt, 120.0, {0.01, 0, 0}, &det);
    CHECK_NEAR(det[0].multiplier, std::exp(-0.01), 1e-15);
    CHECK_NEAR(det[0].pass, 0.3794174678145954, 1e-13);
    // Fatigue: lambda = exp(-ku * i), using the input number
    nandl::computeStats(rowsFrom({{2, 1.0, 4}}), opt, 120.0, {0, 0.1, 0}, &det);
    CHECK_NEAR(det[0].multiplier, std::exp(-0.2), 1e-15);
    CHECK_NEAR(det[0].pass, 0.5870599533149182, 1e-13);
    // CPS: c = (i - i') / (t_i - t_i'), lambda = (4 / max(1, 2c))^kc
    nandl::computeStats(rowsFrom({{1, 0.5, 2}, {2, 0.75, 2}}), opt, 120.0, {0, 0, 1.0}, &det);
    CHECK_NEAR(det[0].multiplier, 1.0, 1e-15); // c = 2 -> 4 / 4
    CHECK_NEAR(det[1].multiplier, 0.5, 1e-15); // c = 4 -> 4 / 8
    // Slow inputs never get a bonus above (4/1)^kc... and c < 0.5 clamps to max(1, 2c) = 1.
    nandl::computeStats(rowsFrom({{1, 10.0, 2}}), opt, 120.0, {0, 0, 1.0}, &det);
    CHECK_NEAR(det[0].multiplier, 4.0, 1e-15);
}

TEST("nandl: respawn time, zero windows and ignored windows follow the calculator") {
    nandl::RunOptions opt{240.0, 2.0};
    auto withRespawn = nandl::computeStats(rowsFrom({{1, 1.0, 2}}), opt, 120.0, kNone);
    CHECK_NEAR(withRespawn.expectedAttemptTime, 3.0, 1e-13);
    // window 0 behaves like window 1
    nandl::RunOptions o2{240.0, 0.0};
    auto z = nandl::computeStats(rowsFrom({{1, 1.0, 0}}), o2, 100.0, kNone);
    auto o = nandl::computeStats(rowsFrom({{1, 1.0, 1}}), o2, 100.0, kNone);
    CHECK_EQ(z.completionProbability, o.completionProbability);
    // ignored window: always passes without CPS
    auto ig = nandl::computeStats(rowsFrom({{1, 1.0, -1}, {2, 2.0, 3}}), o2, 100.0, kNone);
    auto only = nandl::computeStats(rowsFrom({{2, 2.0, 3}}), o2, 100.0, kNone);
    CHECK_NEAR(ig.completionProbability, only.completionProbability, 1e-15);
    // with CPS it uses (largest window + 1)
    std::vector<nandl::InputDetail> det;
    nandl::computeStats(rowsFrom({{1, 1.0, -1}, {2, 2.0, 3}}), o2, 100.0, {0, 0, 1e-12}, &det);
    CHECK_NEAR(det[0].sigma, (4.0 / 2.0) * (100.0 / 240.0), 1e-9);
}

// ===========================================================================
// NaNDL model: cross-check against the reference calculator
// (values produced by window.PrecisionCalculator on nandl.pages.dev, calculator.js?v=9)
// ===========================================================================

TEST("nandl reference: fixed-precision stats") {
    nandl::RunOptions o240{240.0, 0.0};
    auto a = nandl::computeStats(dataset1(), o240, 100.0, kNone);
    CHECK_NEAR(a.expectedAttemptTime, 2.1929385466628029, 1e-12);
    CHECK_NEAR(a.completionProbability, 0.011717247231272382, 1e-12);
    CHECK_NEAR(a.expectedTotalTime, 187.15475600873452, 1e-12);

    auto b = nandl::computeStats(dataset1(), o240, 100.0, kAll);
    CHECK_NEAR(b.expectedAttemptTime, 2.5043836185658002, 1e-12);
    CHECK_NEAR(b.completionProbability, 0.028350545280863334, 1e-12);
    CHECK_NEAR(b.expectedTotalTime, 88.336347458412490, 1e-12);

    nandl::RunOptions o60{60.0, 1.5};
    auto c = nandl::computeStats(dataset2(), o60, 50.0, kNone);
    CHECK_NEAR(c.expectedAttemptTime, 3.1423942814150463, 1e-12);
    CHECK_NEAR(c.completionProbability, 0.18518302159742947, 1e-12);
    CHECK_NEAR(c.expectedTotalTime, 16.969127376300818, 1e-12);

    auto d = nandl::computeStats(dataset2(), o60, 50.0, kCpsOnly);
    CHECK_NEAR(d.expectedAttemptTime, 3.1373490485751114, 1e-12);
    CHECK_NEAR(d.completionProbability, 0.18949500390089841, 1e-12);
    CHECK_NEAR(d.expectedTotalTime, 16.556368157420518, 1e-12);

    auto e = nandl::computeStats(dataset3(), o240, 500.0, kAll);
    CHECK_NEAR(e.expectedAttemptTime, 6.2956458515573690, 1e-10);
    CHECK_NEAR(e.completionProbability, 1.2637972330786911e-260, 1e-9);
    CHECK_NEAR(e.expectedTotalTime, 4.9815315991955227e+260, 1e-9);

    auto zero = nandl::computeStats(dataset1(), o240, 0.0, kNone);
    CHECK_NEAR(zero.expectedAttemptTime, 1.0, 1e-15);
    CHECK_EQ(zero.completionProbability, 0.0);
    CHECK(!zero.completable);
    CHECK(std::isinf(zero.expectedTotalTime));
}

TEST("nandl reference: required precision L* (bisection identical to the calculator)") {
    nandl::RunOptions o240{240.0, 0.0};
    nandl::SolveOptions so; // 86400 s, tolerance 1 s, 240 iterations
    struct Ref { std::vector<nandl::Row> rows; nandl::RunOptions opt; nandl::Constants c; nandl::SolveOptions so; double L, ea, pc, et, err; };
    nandl::SolveOptions tight{3600.0, 1e-6, 240};
    nandl::RunOptions o60{60.0, 1.5};
    nandl::RunOptions o4{240.0, 2.0};
    std::vector<Ref> refs = {
        {dataset1(), o240, kNone, so, 24.095535278320313, 1.2475736817905114, 0.000014439480501176256, 86400.177741081658, 0.17774108165758662},
        {dataset1(), o240, kNerveFatigue, so, 24.311733245849609, 1.2493357341523106, 0.000014459891698710052, 86400.075476620768, 0.075476620768313296},
        {dataset1(), o240, kAll, so, 18.827056884765625, 1.2342811823258306, 0.000014285542382501375, 86400.722442133134, 0.72244213313388173},
        {dataset1(), o240, kNone, tight, 48.057617369340733, 1.5131259972847517, 0.00042031277693477487, 3600.0000007602962, 7.6029618867323734e-7},
        {dataset2(), o60, kCpsOnly, so, 4.8064231872558594, 2.1430657785480842, 0.000024803998698725962, 86400.011731098872, 0.011731098871678114},
        {dataset2(), o60, kNone, so, 2.1466016769409180, 2.5081543351853863, 0.000029029282412030498, 86400.838284102443, 0.83828410244314000},
        {dataset3(), o240, kNone, so, 1104.3029785156250, 112.28720660814670, 0.0012996246530619475, 86399.720368181152, 0.27963181884842925},
        {dataset3(), o240, kNerveFatigue, so, 4363.0615234375000, 590.29673443212050, 0.0068321524182887701, 86399.819309061975, 0.18069093802478164},
        {rowsFrom({{1, 1.0, 3}, {2, 1.0, 3}, {3, 2.0, 3}}), o4, kCpsOnly, so, 5.0598144531250000, 3.0011352841277614, 0.000034735391914708144, 86399.925801815378, 0.074198184622218832},
    };
    for (size_t i = 0; i < refs.size(); i++) {
        auto const& r = refs[i];
        auto res = nandl::solvePrecision(r.rows, r.opt, r.c, r.so);
        CHECK(res.status == nandl::SolveStatus::Converged);
        CHECK_NEAR(res.precision, r.L, 1e-12);
        CHECK_NEAR(res.stats.expectedAttemptTime, r.ea, 1e-10);
        CHECK_NEAR(res.stats.completionProbability, r.pc, 1e-9);
        CHECK_NEAR(res.stats.expectedTotalTime, r.et, 1e-11);
        CHECK_ABS(res.absoluteError, r.err, 1e-6);
    }
}

TEST("nandl solver: error cases and status reporting") {
    nandl::RunOptions o{240.0, 0.0};
    auto tooSmall = nandl::solvePrecision(dataset1(), o, kNone, {5.0, 1.0, 240});
    CHECK(tooSmall.status == nandl::SolveStatus::InvalidInput);
    CHECK_CONTAINS(tooSmall.message, "final input time");

    auto ignored = nandl::solvePrecision(rowsFrom({{1, 1.0, -1}, {2, 2.0, -1}}), o, kNone, {});
    CHECK(ignored.status == nandl::SolveStatus::AllIgnored);

    auto empty = nandl::solvePrecision({}, o, kNone, {});
    CHECK(empty.status == nandl::SolveStatus::InvalidInput);

    auto badFps = nandl::solvePrecision(dataset1(), {0.0, 0.0}, kNone, {});
    CHECK(badFps.status == nandl::SolveStatus::InvalidInput);

    auto nanConst = nandl::solvePrecision(dataset1(), o, {std::nan(""), 0, 0}, {});
    CHECK(nanConst.status == nandl::SolveStatus::InvalidInput);

    auto limited = nandl::solvePrecision(dataset1(), o, kNone, {86400.0, 1e-9, 3});
    CHECK(limited.status == nandl::SolveStatus::IterationLimit);
    CHECK_EQ(limited.iterations, 3);

    auto backwards = nandl::solvePrecision(rowsFrom({{1, 2.0, 3}, {2, 1.0, 3}}), o, kCpsOnly, {});
    CHECK(backwards.status == nandl::SolveStatus::InvalidInput);

    std::atomic<bool> cancel{true};
    auto cancelled = nandl::solvePrecision(dataset1(), o, kNone, {86400.0, 1e-12, 240}, &cancel);
    CHECK(cancelled.status == nandl::SolveStatus::Cancelled);
}

TEST("nandl: numerically extreme macros stay finite in log space") {
    // 200k frame-perfect inputs: P(C) underflows double precision by far.
    std::vector<nandl::Row> rows;
    for (int i = 1; i <= 200000; i++) rows.push_back({i, i * 0.05, 1.0});
    auto st = nandl::computeStats(rows, {240.0, 0.0}, 200.0, kNone);
    CHECK(st.completable);
    CHECK_EQ(st.completionProbability, 0.0);       // underflows as a double...
    CHECK(std::isfinite(st.logCompletion));        // ...but the log is exact
    CHECK(std::isfinite(st.log10ExpectedAttempts));
    CHECK(st.log10ExpectedAttempts > 1000.0);
    CHECK(std::isinf(st.expectedTotalTime));
    CHECK(std::isfinite(st.log10ExpectedTotalTime));
    // Huge precision: every input essentially certain, no NaN.
    auto easy = nandl::computeStats(dataset1(), {240.0, 0.0}, 1e9, kNone);
    CHECK_NEAR(easy.completionProbability, 1.0, 1e-15);
    CHECK_NEAR(easy.expectedTotalTime, 10.0, 1e-12);
}

TEST("nandl: large macro (1,000,000 inputs) solves in reasonable time") {
    std::vector<nandl::Row> rows;
    rows.reserve(1000000);
    for (int i = 1; i <= 1000000; i++) rows.push_back({i, i * 0.01, static_cast<double>(4 + i % 9)});
    auto t0 = std::chrono::steady_clock::now();
    auto res = nandl::solvePrecision(rows, {240.0, 0.0}, kNone, {});
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    (1M-input solve took %lld ms, %d iterations)\n", static_cast<long long>(ms), res.iterations);
    CHECK(res.status == nandl::SolveStatus::Converged);
    CHECK(res.precision > 0);
}

// ===========================================================================
// Analysis model, statistics, exports
// ===========================================================================

namespace {
Analysis measuredAnalysis() {
    Analysis a;
    a.source.kind = SourceKind::Macro;
    a.tps = 240.0;
    auto add = [&](uint64_t frame, bool down, WindowStatus st, int e, int l) {
        InputRecord r;
        r.index = a.inputs.size();
        r.frame = frame;
        r.time = frame / 240.0;
        r.down = down;
        r.window.status = st;
        r.window.earlyTicks = e;
        r.window.lateTicks = l;
        a.inputs.push_back(r);
    };
    add(240, true, WindowStatus::Verified, 0, 0);      // 1 frame (frame perfect)
    add(260, false, WindowStatus::Unknown, 0, 0);
    add(480, true, WindowStatus::Verified, 1, 0);      // 2
    add(500, false, WindowStatus::Unknown, 0, 0);
    add(720, true, WindowStatus::Verified, 4, 0);      // 5, early biased
    add(960, true, WindowStatus::LowerBound, 5, 6);    // 12 (lower bound)
    add(1200, true, WindowStatus::BaselineFailed, 0, 0);
    return a;
}
} // namespace

TEST("analysis: frame-window boundaries, stats and histogram buckets") {
    auto a = measuredAnalysis();
    CHECK_EQ(a.inputs[2].window.totalTicks(), 2);
    StatsOptions so;
    auto s = computeWindowStats(a, so);
    CHECK_EQ(s.totalInputs, size_t(7));
    CHECK_EQ(s.countedInputs, size_t(5));
    CHECK_EQ(s.verified, size_t(3));
    CHECK_EQ(s.lowerBound, size_t(1));
    CHECK_EQ(s.baselineFailed, size_t(1));
    CHECK_EQ(s.withWindow, size_t(4));
    CHECK_NEAR(s.average, (1 + 2 + 5 + 12) / 4.0, 1e-15);
    CHECK_NEAR(s.median, 3.5, 1e-15);
    CHECK_NEAR(s.narrowestVerified, 1.0, 0);
    CHECK_NEAR(s.widestVerified, 5.0, 0); // lower bounds are not "verified"
    CHECK_EQ(s.framePerfect, size_t(1));
    CHECK_EQ(s.tight, size_t(2));
    CHECK_EQ(s.earlyBiased, size_t(1));
    // buckets [0,1) [1,2) [2,3) [3,4) [4,5) [5,6) [6,8) [8,10) [10,inf)
    std::vector<size_t> expected = {0, 1, 1, 0, 0, 1, 0, 0, 1};
    CHECK(s.histogram.counts == expected);
    CHECK_EQ(s.histogram.labels.front(), std::string("0-1"));
    CHECK_EQ(s.histogram.labels.back(), std::string("10+"));

    so.includeLowerBounds = false;
    auto s2 = computeWindowStats(a, so);
    CHECK_EQ(s2.withWindow, size_t(3));

    // Window conversion to another timing rate: 2 ticks at 240 TPS = 0.5 frames at 60 Hz.
    CHECK_NEAR(*windowFrames(a.inputs[2], 60.0, 240.0, true), 0.5, 1e-15);
}

TEST("analysis: histogram edge parsing") {
    std::string err;
    auto e = parseEdges("10, 2, 1, 2,5", &err);
    CHECK(err.empty());
    std::vector<double> expected = {0, 1, 2, 5, 10};
    CHECK(e == expected);
    parseEdges("1,x", &err);
    CHECK(!err.empty());
}

TEST("analysis: NaNDL dataset uses counted inputs and marks missing windows as ignored") {
    auto a = measuredAnalysis();
    auto ds = buildNandlDataset(a, CountOptions{}, 240.0, true);
    REQUIRE(ds.rows.size() == 5);
    CHECK_EQ(ds.rows[0].input, int64_t(1));
    CHECK_EQ(ds.rows[4].input, int64_t(5));
    CHECK_NEAR(*ds.rows[0].window, 1.0, 0);
    CHECK(!ds.rows[4].window.has_value()); // baseline failed -> "-"
    CHECK_EQ(ds.ignoredForMissingWindow, size_t(1));
    CHECK_EQ(ds.lowerBoundsUsed, size_t(1));
}

TEST("analysis: JSON export re-imports losslessly; CSV and NaNDL exports are consistent") {
    auto a = measuredAnalysis();
    a.inputs[0].gameMode = "Ship";
    a.inputs[0].window.note = "note, with \"quotes\"";
    ExportSettings s;
    auto doc = toJson(a, s, std::nullopt, "test");
    auto text = json::dump(doc);
    auto parsed = json::parse(text);
    REQUIRE(parsed.ok());
    // Through the import registry, as a user would load it.
    auto imp = importBytes(bytes(text), "analysis.json");
    REQUIRE(imp.ok());
    CHECK(imp.value().kind == ImportKind::SavedAnalysis);
    std::vector<Diagnostic> diags;
    std::optional<ExportSettings> settings;
    auto back = analysisFromJson(parsed.value(), diags, &settings);
    REQUIRE(back.ok());
    auto const& b = back.value();
    REQUIRE(b.inputs.size() == a.inputs.size());
    for (size_t i = 0; i < a.inputs.size(); i++) {
        CHECK(b.inputs[i].window.status == a.inputs[i].window.status);
        CHECK_EQ(b.inputs[i].window.earlyTicks, a.inputs[i].window.earlyTicks);
        CHECK_EQ(b.inputs[i].window.lateTicks, a.inputs[i].window.lateTicks);
        CHECK_EQ(*b.inputs[i].frame, *a.inputs[i].frame);
        CHECK_EQ(b.inputs[i].down, a.inputs[i].down);
    }
    CHECK_EQ(b.inputs[0].gameMode, std::string("Ship"));
    CHECK_EQ(b.inputs[0].window.note, a.inputs[0].window.note);
    REQUIRE(settings.has_value());
    CHECK_NEAR(settings->windowFps, 240.0, 0);

    auto csv = toCsv(a, s, {});
    CHECK_CONTAINS(csv, "input,counted_input,frame,time_s");
    CHECK_CONTAINS(csv, "\"note, with \"\"quotes\"\"\"");
    size_t lines = static_cast<size_t>(std::count(csv.begin(), csv.end(), '\n'));
    CHECK_EQ(lines, a.inputs.size() + 1);

    auto nj = toNandlCalculatorJson(a, s);
    std::vector<Diagnostic> d2;
    auto ds = parseNandlCalculatorJson(nj, d2);
    REQUIRE(ds.ok());
    auto expectedRows = buildNandlDataset(a, s.stats.count, s.windowFps, true);
    REQUIRE(ds.value().rows.size() == expectedRows.rows.size());
    for (size_t i = 0; i < expectedRows.rows.size(); i++) {
        CHECK_EQ(ds.value().rows[i].input, expectedRows.rows[i].input);
        CHECK_EQ(ds.value().rows[i].timeSeconds, expectedRows.rows[i].time);
        CHECK(ds.value().rows[i].windowFrames.has_value() == expectedRows.rows[i].window.has_value());
    }
}

TEST("analysis: sample data is deterministic and labelled as synthetic") {
    auto a = makeSampleAnalysis();
    auto b = makeSampleAnalysis();
    REQUIRE(a.inputs.size() == b.inputs.size());
    CHECK_EQ(a.inputs.size(), size_t(180));
    for (size_t i = 0; i < a.inputs.size(); i++) {
        CHECK_EQ(*a.inputs[i].frame, *b.inputs[i].frame);
        CHECK(a.inputs[i].window.status != WindowStatus::Verified);
    }
    CHECK(a.source.kind == SourceKind::Sample);
}

TEST("large macro: GDR JSON with 200,000 inputs parses and validates") {
    std::string doc = "{\"framerate\": 240, \"inputs\": [";
    for (int i = 0; i < 200000; i++) {
        if (i) doc += ",";
        doc += "{\"frame\":" + std::to_string(i * 3) + ",\"btn\":1,\"2p\":false,\"down\":" + (i % 2 ? "false" : "true") + "}";
    }
    doc += "]}";
    auto t0 = std::chrono::steady_clock::now();
    auto r = importBytes(bytes(doc), "big.json");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    (200k-input JSON import took %lld ms)\n", static_cast<long long>(ms));
    REQUIRE(r.ok());
    CHECK_EQ(r.value().macro->inputs.size(), size_t(200000));
    auto a = analysisFromMacro(*r.value().macro);
    CHECK_EQ(computeWindowStats(a, StatsOptions{}).countedInputs, size_t(100000));
}

TEST("robustness: thousands of randomly corrupted files never crash the importer") {
    std::vector<std::vector<uint8_t>> seeds = {
        readFixture("basic.gdr2"), readFixture("platformer.gdr2"), readFixture("physics-ext.gdr2"),
        makeGdr1MsgPack(true),
        bytes(R"({"framerate": 240, "inputs": [{"frame": 10, "btn": 1, "2p": false, "down": true}]})"),
        bytes("input,time_s,window_frames\n1,0.5,3\n"),
    };
    uint32_t state = 12345;
    auto rnd = [&]() { state = state * 1664525u + 1013904223u; return state >> 8; };
    size_t okCount = 0, errCount = 0;
    for (int iter = 0; iter < 6000; iter++) {
        auto data = seeds[iter % seeds.size()];
        int edits = 1 + static_cast<int>(rnd() % 4);
        for (int e = 0; e < edits && !data.empty(); e++) {
            switch (rnd() % 3) {
                case 0: data[rnd() % data.size()] = static_cast<uint8_t>(rnd()); break;
                case 1: data.resize(rnd() % data.size()); break;
                case 2: data.insert(data.begin() + static_cast<long>(rnd() % data.size()), static_cast<uint8_t>(rnd())); break;
            }
        }
        auto r = importBytes(data, iter % 2 ? "fuzz.gdr" : "fuzz.json");
        (r.ok() ? okCount : errCount)++;
    }
    std::printf("    (fuzz: %zu parsed, %zu rejected with an error)\n", okCount, errCount);
    CHECK(okCount + errCount == 6000);
}

int main() {
    int failedCases = 0;
    for (auto const& c : tf::registry()) {
        int before = tf::failures();
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        if (tf::failures() != before) {
            failedCases++;
            std::printf("[ FAIL ] %s\n", c.name);
        } else {
            std::printf("[  OK  ] %s\n", c.name);
        }
    }
    std::printf("\n%zu test cases, %d checks, %d failed check(s), %d failed case(s)\n",
        tf::registry().size(), tf::checks(), tf::failures(), failedCases);
    return failedCases == 0 ? 0 : 1;
}
