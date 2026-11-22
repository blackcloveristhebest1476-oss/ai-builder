#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fwa {

enum class Severity { Info, Warning, Error };

struct Diagnostic {
    Severity severity = Severity::Info;
    std::string message;
};

/// GD's PlayerButton values, which GDR stores directly.
enum class Button : uint8_t { None = 0, Jump = 1, Left = 2, Right = 3 };

char const* buttonName(uint8_t button);

struct MacroInput {
    uint64_t frame = 0;      // physics tick the input happens on
    uint8_t button = 1;      // 1 = jump, 2 = left, 3 = right
    bool player2 = false;
    bool down = true;        // press (true) or release (false)
    size_t fileOrder = 0;    // position in the file before sorting

    // Set by validation:
    bool duplicate = false;  // identical to the previous event of the same button/player
    bool redundant = false;  // press while already held / release while not held (no gameplay effect)
};

struct MacroMetadata {
    std::string author;
    std::string description;
    std::string botName;
    int botVersion = 0;
    uint32_t levelId = 0;
    std::string levelName;
    std::string gameVersionText;  // human readable game version as stored in the file ("" = not stored)
    double framerate = 240.0;     // ticks per second of the recording
    bool framerateFromFile = false;
    int64_t seed = 0;
    int coins = 0;
    bool ldm = false;
    bool platformer = false;
    std::optional<double> storedDurationSeconds; // GDR's own duration field, if any
    std::vector<uint64_t> deathFrames;           // GDR2 death markers
    std::string inputExtensionTag;               // GDR2 custom input tag ("" = none)
    int formatVersion = 0;
};

struct Macro {
    std::string formatId;    // stable id, e.g. "gdr2"
    std::string formatName;  // human readable, e.g. "GDR2 (binary)"
    std::string sourceName;  // file name it came from
    MacroMetadata meta;
    std::vector<MacroInput> inputs;
    std::vector<Diagnostic> diagnostics;

    double tps() const { return meta.framerate; }
    /// Last input frame / tps, or the stored duration when it is longer.
    double durationSeconds() const;
    size_t pressCount() const;
    size_t releaseCount() const;
    size_t player2Count() const;
    bool hasErrors() const;

    void addDiagnostic(Severity s, std::string msg) { diagnostics.push_back({s, std::move(msg)}); }
};

/// Sorts (stably) if needed and flags duplicate/redundant events, adding
/// diagnostics. Returns false if the macro is unusable (errors were added).
bool validateMacro(Macro& macro);

} // namespace fwa
