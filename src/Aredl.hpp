#pragma once

// AREDL API response parsing. Only depends on matjson, so it can be tested
// without the game.

#include "Recommender.hpp"

#include <matjson.hpp>
#include <string_view>

namespace picker {
    // Public endpoint, no key needed. Returns main list + legacy levels.
    inline constexpr const char* AREDL_LEVELS_URL = "https://api.aredl.net/v2/api/aredl/levels";

    // Parses the body of GET /aredl/levels. Returns an empty vector if the
    // body is not what we expect.
    std::vector<Level> parseLevels(std::string_view body);
}
