#include "Recommender.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace picker {
    namespace {
        // Tags that say nothing about what the gameplay asks of you.
        bool isIgnoredTag(const std::string& tag) {
            return tag == "NONG" || tag == "2P";
        }

        // How much weaker the difficulty term makes the worst-fitting level.
        constexpr double DIFFICULTY_FALLOFF = 0.35;
    }

    Report recommend(
        const std::vector<Level>& levels,
        const std::unordered_set<int>& completedIds,
        const Options& options
    ) {
        Report report;
        if (levels.empty()) return report;

        // ---- 1. Which list levels has the player beaten? -------------------
        // 2P levels appear twice with the same ID (2P + Solo); count them once,
        // preferring the solo entry.
        std::unordered_map<int, const Level*> completed;
        int maxPosition = 1;
        for (auto& level : levels) {
            maxPosition = std::max(maxPosition, level.position);
            if (!completedIds.count(level.id)) continue;
            auto [it, inserted] = completed.try_emplace(level.id, &level);
            if (!inserted && it->second->twoPlayer && !level.twoPlayer) {
                it->second = &level;
            }
        }
        report.completedCount = static_cast<int>(completed.size());

        for (auto& [id, level] : completed) {
            if (level->position <= 0) continue;
            if (!report.hardest || level->position < report.hardest->position) {
                report.hardest = level;
            }
        }

        // ---- 2. Tag statistics ---------------------------------------------
        // baseCount: how common a tag is on the whole list
        // userCount: how common it is among the player's completions
        std::unordered_map<std::string, int> baseCount, userCount;
        for (auto& level : levels) {
            for (auto& tag : level.tags) {
                if (!isIgnoredTag(tag)) baseCount[tag]++;
            }
        }
        for (auto& [id, level] : completed) {
            for (auto& tag : level->tags) {
                if (!isIgnoredTag(tag)) userCount[tag]++;
            }
        }

        const double total = static_cast<double>(levels.size());
        const double beaten = static_cast<double>(completed.size());

        // Rare tags are more telling than tags nearly every level has, so each
        // tag gets an inverse-frequency weight.
        std::unordered_map<std::string, double> idf;
        for (auto& [tag, count] : baseCount) {
            idf[tag] = std::log(1.0 + total / count);
        }

        // The player's "average level": for every tag, the share of their
        // completions that have it, scaled by the tag's weight.
        std::unordered_map<std::string, double> userVec;
        double userNorm = 0;
        for (auto& [tag, count] : userCount) {
            double rate = count / beaten;
            double value = rate * idf[tag];
            userVec[tag] = value;
            userNorm += value * value;
            report.profile.push_back({ tag, count, rate, baseCount[tag] / total });
        }
        userNorm = std::sqrt(userNorm);
        std::sort(report.profile.begin(), report.profile.end(), [](auto& a, auto& b) {
            if (a.userRate != b.userRate) return a.userRate > b.userRate;
            return a.tag < b.tag;
        });

        const bool hasProfile = userNorm > 0;

        // ---- 3. Score every level the player hasn't beaten -----------------
        for (auto& level : levels) {
            if (completedIds.count(level.id)) continue;
            if (level.position <= 0) continue;
            if (level.legacy && !options.includeLegacy) continue;
            if (level.twoPlayer && !options.includeTwoPlayer) continue;

            Pick pick;
            pick.level = &level;

            // Enjoyment nudges the score a little; unknown counts as average.
            double enjoyment = 1.0;
            if (options.useEnjoyment) {
                double e = std::clamp(level.enjoyment.value_or(50.0), 0.0, 100.0);
                enjoyment = 0.85 + 0.15 * (e / 100.0);
            }

            if (!hasProfile || !report.hardest) {
                // Nothing to learn from yet: suggest the easiest, most enjoyed
                // levels as a first extreme.
                double ease = static_cast<double>(level.position) / maxPosition;
                pick.score = ease * enjoyment;
                report.picks.push_back(std::move(pick));
                continue;
            }

            // Difficulty fit relative to the player's hardest.
            double fit;
            int hardestPos = report.hardest->position;
            if (level.position < hardestPos) {
                if (options.mode == Mode::Easier) continue;
                int jump = hardestPos - level.position;
                if (jump > options.maxJump) continue;
                // smaller jumps are more realistic
                fit = 1.0 - DIFFICULTY_FALLOFF * (jump - 1) / std::max(1, options.maxJump);
            } else {
                if (options.mode == Mode::Harder) continue;
                // closer to your hardest = more list points for your effort
                double below = level.position - hardestPos;
                double range = std::max(1, maxPosition - hardestPos);
                fit = 1.0 - DIFFICULTY_FALLOFF * (below / range);
            }

            // Cosine similarity between the level's tags and the profile.
            double dot = 0, levelNorm = 0;
            std::vector<std::pair<double, const std::string*>> shared;
            for (auto& tag : level.tags) {
                if (isIgnoredTag(tag)) continue;
                double weight = idf[tag];
                levelNorm += weight * weight;
                auto it = userVec.find(tag);
                if (it == userVec.end()) continue;
                dot += weight * it->second;
                shared.emplace_back(it->second, &tag);
            }
            if (levelNorm > 0) {
                pick.match = dot / (std::sqrt(levelNorm) * userNorm);
            }

            std::sort(shared.begin(), shared.end(), [](auto& a, auto& b) {
                return a.first > b.first;
            });
            for (auto& [value, tag] : shared) pick.sharedTags.push_back(*tag);

            pick.score = pick.match * fit * enjoyment;
            report.picks.push_back(std::move(pick));
        }

        std::sort(report.picks.begin(), report.picks.end(), [](auto& a, auto& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.level->position > b.level->position; // tie: easier first
        });
        if (options.count > 0 && report.picks.size() > static_cast<size_t>(options.count)) {
            report.picks.resize(options.count);
        }
        return report;
    }
}
