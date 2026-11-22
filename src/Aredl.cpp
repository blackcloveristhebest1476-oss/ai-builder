#include "Aredl.hpp"

namespace picker {
    std::vector<Level> parseLevels(std::string_view body) {
        std::vector<Level> out;

        auto parsed = matjson::parse(body);
        if (!parsed) return out;
        auto root = std::move(parsed).unwrap();
        if (!root.isArray()) return out;

        for (auto const& item : root) {
            if (!item.isObject()) continue;

            Level level;
            level.id = static_cast<int>(item["level_id"].asInt().unwrapOr(0));
            if (level.id <= 0) continue;

            level.name = item["name"].asString().unwrapOr("Unknown");
            level.position = static_cast<int>(item["position"].asInt().unwrapOr(0));
            level.points = static_cast<int>(item["points"].asInt().unwrapOr(0));
            level.twoPlayer = item["two_player"].asBool().unwrapOr(false);

            auto status = item["status"].asString().unwrapOr("MainList");
            if (status == "Pending" || status == "Removed") continue;
            level.legacy = status == "Legacy";

            if (item["tags"].isArray()) {
                for (auto const& tag : item["tags"]) {
                    if (auto str = tag.asString()) {
                        level.tags.push_back(std::move(str).unwrap());
                    }
                }
            }

            if (item["edel_enjoyment"].isNumber()) {
                level.enjoyment = item["edel_enjoyment"].asDouble().unwrapOr(50.0);
            }

            out.push_back(std::move(level));
        }
        return out;
    }
}
