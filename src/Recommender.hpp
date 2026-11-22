#pragma once

// Pure recommendation logic. No Geode / GD includes on purpose, so this file
// can be unit tested on any platform (see tests/test_recommender.cpp).

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace picker {
    struct Level {
        int id = 0;            // in-game level ID
        std::string name;
        int position = 0;      // 1 = hardest. 0 = unknown
        int points = 0;
        bool legacy = false;
        bool twoPlayer = false;
        std::vector<std::string> tags;
        std::optional<double> enjoyment; // EDEL enjoyment, 0-100
    };

    enum class Mode {
        Both,   // new hardests and easier levels
        Harder, // only levels harder than your hardest
        Easier, // only levels easier than your hardest
    };

    struct Options {
        Mode mode = Mode::Both;
        int maxJump = 100;        // how many places above your hardest a pick may be
        bool includeLegacy = false;
        bool includeTwoPlayer = false;
        bool useEnjoyment = true;
        int count = 15;           // how many picks to return
    };

    struct TagStat {
        std::string tag;
        int count = 0;         // how many of your completions have it
        double userRate = 0;   // share of your completions with this tag (0-1)
        double baseRate = 0;   // share of the whole list with this tag (0-1)
    };

    struct Pick {
        const Level* level = nullptr;
        double score = 0;      // final ranking score
        double match = 0;      // tag similarity to your profile (0-1)
        std::vector<std::string> sharedTags; // tags it shares with your profile, strongest first
    };

    struct Report {
        int completedCount = 0;
        const Level* hardest = nullptr;  // hardest completed level, if any
        std::vector<TagStat> profile;    // sorted by userRate, highest first
        std::vector<Pick> picks;         // best first
    };

    // `levels` must outlive the returned Report (it holds pointers into it).
    Report recommend(
        const std::vector<Level>& levels,
        const std::unordered_set<int>& completedIds,
        const Options& options
    );
}
