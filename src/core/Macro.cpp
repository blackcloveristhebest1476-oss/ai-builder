#include "Macro.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace fwa {

char const* buttonName(uint8_t button) {
    switch (button) {
        case 0: return "None";
        case 1: return "Jump";
        case 2: return "Left";
        case 3: return "Right";
        default: return "?";
    }
}

double Macro::durationSeconds() const {
    double fromInputs = 0.0;
    if (!inputs.empty() && meta.framerate > 0) {
        fromInputs = static_cast<double>(inputs.back().frame) / meta.framerate;
    }
    if (meta.storedDurationSeconds && std::isfinite(*meta.storedDurationSeconds)) {
        return std::max(fromInputs, *meta.storedDurationSeconds);
    }
    return fromInputs;
}

size_t Macro::pressCount() const {
    return static_cast<size_t>(std::count_if(inputs.begin(), inputs.end(), [](auto const& i) { return i.down; }));
}

size_t Macro::releaseCount() const {
    return inputs.size() - pressCount();
}

size_t Macro::player2Count() const {
    return static_cast<size_t>(std::count_if(inputs.begin(), inputs.end(), [](auto const& i) { return i.player2; }));
}

bool Macro::hasErrors() const {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](auto const& d) { return d.severity == Severity::Error; });
}

bool validateMacro(Macro& macro) {
    auto& meta = macro.meta;

    if (!std::isfinite(meta.framerate) || meta.framerate <= 0.0) {
        macro.addDiagnostic(Severity::Error, "The macro's framerate (" + num(meta.framerate) +
            ") is not a positive number, so input times cannot be computed.");
        return false;
    }
    if (meta.framerate > 100000.0) {
        macro.addDiagnostic(Severity::Error, "The macro's framerate (" + num(meta.framerate) +
            " ticks/s) is implausibly high; the file is probably corrupted.");
        return false;
    }

    if (macro.inputs.empty()) {
        macro.addDiagnostic(Severity::Warning, "The macro contains no inputs. There is nothing to analyze.");
        return true;
    }

    // Frames beyond ~2 days at 240 TPS are not something a real replay contains.
    constexpr uint64_t kMaxPlausibleFrame = 50'000'000ull;
    size_t badButtons = 0;
    for (auto const& in : macro.inputs) {
        if (in.frame > kMaxPlausibleFrame) {
            macro.addDiagnostic(Severity::Error, "Input #" + std::to_string(in.fileOrder + 1) + " is on frame " +
                std::to_string(in.frame) + ", which is implausibly large; the file is probably corrupted.");
            return false;
        }
        if (in.button < 1 || in.button > 3) badButtons++;
    }
    if (badButtons > 0) {
        macro.addDiagnostic(Severity::Error, std::to_string(badButtons) +
            " input(s) use an unknown button id (valid ids are 1 = jump, 2 = left, 3 = right).");
        return false;
    }

    // Chronological order. GDR2 readers must sort (P1 and P2 are stored separately);
    // for other formats out-of-order data is reported.
    bool sorted = std::is_sorted(macro.inputs.begin(), macro.inputs.end(),
        [](auto const& a, auto const& b) { return a.frame < b.frame; });
    if (!sorted) {
        size_t outOfOrder = 0;
        for (size_t i = 1; i < macro.inputs.size(); i++) {
            if (macro.inputs[i].frame < macro.inputs[i - 1].frame) outOfOrder++;
        }
        std::stable_sort(macro.inputs.begin(), macro.inputs.end(),
            [](auto const& a, auto const& b) { return a.frame < b.frame; });
        if (macro.formatId != "gdr2") {
            macro.addDiagnostic(Severity::Warning, std::to_string(outOfOrder) +
                " input(s) were stored out of chronological order and have been sorted by frame (same-frame order kept).");
        }
    }

    // Duplicate / redundant events, tracked per (player, button).
    std::map<std::pair<bool, uint8_t>, bool> held;
    std::map<std::pair<bool, uint8_t>, MacroInput const*> previous;
    size_t duplicates = 0, redundant = 0;
    for (auto& in : macro.inputs) {
        auto key = std::make_pair(in.player2, in.button);
        auto prevIt = previous.find(key);
        if (prevIt != previous.end() && prevIt->second->frame == in.frame && prevIt->second->down == in.down) {
            in.duplicate = true;
            duplicates++;
        }
        bool isHeld = held[key];
        if (in.down == isHeld) {
            in.redundant = true;
            if (!in.duplicate) redundant++;
        }
        held[key] = in.down;
        previous[key] = &in;
    }
    if (duplicates > 0) {
        macro.addDiagnostic(Severity::Warning, std::to_string(duplicates) +
            " duplicate input(s) found (same frame, button, player and state as the previous event). They are kept but excluded from analysis.");
    }
    if (redundant > 0) {
        macro.addDiagnostic(Severity::Info, std::to_string(redundant) +
            " redundant event(s) (a press while already held or a release while not held). They have no effect in-game and are excluded from analysis.");
    }

    size_t p2 = macro.player2Count();
    if (p2 > 0) {
        macro.addDiagnostic(Severity::Info, std::to_string(p2) + " input(s) belong to player 2.");
    }

    if (meta.storedDurationSeconds && std::isfinite(*meta.storedDurationSeconds)) {
        double last = static_cast<double>(macro.inputs.back().frame) / meta.framerate;
        if (last > *meta.storedDurationSeconds + 1.0) {
            macro.addDiagnostic(Severity::Info, "The last input (" + num(last) +
                " s) is after the duration stored in the file (" + num(*meta.storedDurationSeconds) + " s).");
        }
    }
    return !macro.hasErrors();
}

} // namespace fwa
