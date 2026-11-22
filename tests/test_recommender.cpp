// Standalone test for the recommendation logic and the AREDL parser.
// Build (needs geode-sdk/json and geode-sdk/result checked out):
//   c++ -std=c++23 -I../src -I<json>/include -I<result>/include \
//       test_recommender.cpp ../src/Recommender.cpp ../src/Aredl.cpp <json>/src/*.cpp -o test
#include "Aredl.hpp"
#include "Recommender.hpp"

#include <cassert>
#include <cstdio>

using namespace picker;

static Level L(int id, const char* name, int pos, std::vector<std::string> tags, double enjoy = 50, bool legacy = false, bool twoP = false) {
    Level l; l.id = id; l.name = name; l.position = pos; l.tags = std::move(tags);
    l.enjoyment = enjoy; l.legacy = legacy; l.twoPlayer = twoP; return l;
}

int main() {
    // --- parser: real objects copied from the live API ---
    const char* body = R"([
{"id":"f3dd5ead","name":"GRIEF","position":1,"publisher_id":"x","points":5000,"status":"MainList","requires_raw_footage":false,"level_id":149992717,"two_player":false,"tags":["2.2","XL","Fast-Paced","Timings","Chokepoints","Learny","High CPS","Nerve Control","Wave"],"description":"d","song":482872,"edel_enjoyment":85.0,"is_edel_pending":true,"gddl_tier":null,"nlw_tier":null},
{"id":"034634f7","name":"Artemis 13","position":2,"publisher_id":"x","points":4937,"status":"MainList","requires_raw_footage":false,"level_id":148598469,"two_player":false,"tags":["2.2","Long","NONG","Timings","Overall",null],"description":"d","song":null,"edel_enjoyment":null,"is_edel_pending":true,"gddl_tier":null,"nlw_tier":null},
{"id":"aaaa","name":"Old","position":900,"points":10,"status":"Legacy","level_id":5,"two_player":true,"tags":[]},
{"id":"bbbb","name":"Gone","position":null,"points":0,"status":"Removed","level_id":6,"two_player":false,"tags":[]}
])";
    auto parsed = parseLevels(body);
    assert(parsed.size() == 3);
    assert(parsed[0].id == 149992717 && parsed[0].name == "GRIEF" && parsed[0].position == 1);
    assert(parsed[0].tags.size() == 9 && parsed[0].enjoyment && *parsed[0].enjoyment == 85.0);
    assert(parsed[1].tags.size() == 5 && !parsed[1].enjoyment);
    assert(parsed[2].legacy && parsed[2].twoPlayer);
    assert(parseLevels("not json").empty());
    assert(parseLevels("{\"message\":\"error\"}").empty());

    // --- recommender ---
    std::vector<Level> list = {
        L(1,  "TopWave",     10,  {"Wave", "Timings", "2.1"}),
        L(2,  "WaveStep",    480, {"Wave", "Timings", "Fast-Paced"}),
        L(3,  "ShipStep",    470, {"Ship", "Memory", "Slow-Paced"}),
        L(4,  "Beaten A",    500, {"Wave", "Timings", "Fast-Paced"}),
        L(5,  "Beaten B",    700, {"Wave", "Flow", "Fast-Paced"}),
        L(6,  "Beaten C",    900, {"Wave", "Timings", "NONG"}),
        L(7,  "EasyWave",    800, {"Wave", "Fast-Paced"}),
        L(8,  "EasyMemory",  810, {"Memory", "Slow-Paced"}),
        L(9,  "LegacyWave",  1001,{"Wave", "Timings"}, 50, true),
        L(10, "TwoP",        600, {"Wave", "Timings", "2P"}, 50, false, true),
        L(11, "FarWave",     300, {"Wave", "Timings", "Fast-Paced"}),
        L(12, "Filler1",     950, {"Ship", "Cube"}),
        L(13, "Filler2",     960, {"Ship", "Ball"}),
        L(14, "Filler3",     970, {"Cube", "Memory"}),
    };
    std::unordered_set<int> done = {4, 5, 6, 999999};

    Options opt;
    auto r = recommend(list, done, opt);
    assert(r.completedCount == 3);
    assert(r.hardest && r.hardest->id == 4);
    assert(r.profile[0].tag == "Wave" && r.profile[0].count == 3);
    for (auto& s : r.profile) assert(s.tag != "NONG");
    assert(!r.picks.empty());
    // best pick: same style, small jump
    assert(r.picks[0].level->id == 2);
    for (auto& p : r.picks) {
        assert(!done.count(p.level->id));           // never something already beaten
        assert(!p.level->legacy && !p.level->twoPlayer);
        assert(p.level->id != 1 && p.level->id != 11); // jump too large (max 100)
        assert(p.match >= 0 && p.match <= 1.0000001);
    }
    // wave levels outrank memory/ship levels
    auto rankOf = [&](int id) { for (size_t i = 0; i < r.picks.size(); i++) if (r.picks[i].level->id == id) return (int)i; return -1; };
    assert(rankOf(2) < rankOf(3));
    assert(rankOf(7) < rankOf(8));

    opt.mode = Mode::Harder;
    r = recommend(list, done, opt);
    for (auto& p : r.picks) assert(p.level->position < 500);
    assert(r.picks.size() == 2);

    opt.mode = Mode::Easier;
    r = recommend(list, done, opt);
    for (auto& p : r.picks) assert(p.level->position > 500);

    opt = Options{}; opt.maxJump = 1000; opt.includeLegacy = true; opt.includeTwoPlayer = true; opt.count = 3;
    r = recommend(list, done, opt);
    assert(r.picks.size() == 3);

    // enjoyment breaks ties between otherwise identical levels
    std::vector<Level> twins = {
        L(1, "Done", 100, {"Wave"}), L(2, "Fun", 150, {"Wave"}, 95), L(3, "Meh", 150, {"Wave"}, 5), L(4, "x", 160, {"Ship"}),
    };
    r = recommend(twins, {1}, Options{});
    assert(r.picks[0].level->id == 2);

    // no completions -> easiest levels first, no crash
    r = recommend(list, {}, Options{});
    assert(r.completedCount == 0 && !r.hardest && r.profile.empty());
    assert(!r.picks.empty() && r.picks[0].level->position >= 950);

    assert(recommend({}, done, Options{}).picks.empty());

    std::puts("all tests passed");
}
