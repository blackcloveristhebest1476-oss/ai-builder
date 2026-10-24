#pragma once

// Import layer. Every supported file type is a FormatHandler registered in the
// FormatRegistry; adding a new macro format means adding one handler.
//
// Detection is content based (magic bytes / document structure). The file
// extension is only a tie-breaker, so a renamed file is still read correctly,
// and nothing is ever parsed as a format it does not structurally match.

#include "Expected.hpp"
#include "Json.hpp"
#include "Macro.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fwa {

/// One row of frame-window data that did not come from a macro
/// (NaNDL calculator JSON, CSV, or manual entry).
struct WindowRow {
    int64_t input = 0;                 // NaNDL "input number" (used by the fatigue modifier)
    double timeSeconds = 0.0;          // time position in seconds
    std::optional<double> windowFrames; // frames at the dataset's window rate; nullopt = "-" (ignored)
};

struct WindowDataset {
    std::vector<WindowRow> rows;
    std::optional<double> gameFps;
    std::optional<double> windowFps;
    std::optional<double> respawnSeconds;
};

enum class ImportKind { Macro, WindowData, SavedAnalysis };

struct ImportResult {
    ImportKind kind = ImportKind::Macro;
    std::string formatId;
    std::string formatName;
    std::optional<Macro> macro;
    std::optional<WindowDataset> windows;
    std::optional<json::Value> analysisJson;
    std::vector<Diagnostic> diagnostics;
};

class FormatHandler {
public:
    virtual ~FormatHandler() = default;
    virtual std::string_view id() const = 0;
    virtual std::string_view displayName() const = 0;
    /// Lower-case extensions without the dot, used for the file picker filter.
    virtual std::vector<std::string> extensions() const = 0;
    /// 0 = definitely not this format; larger = more confident.
    virtual int sniff(std::span<uint8_t const> data, std::string_view extension) const = 0;
    virtual Expected<ImportResult> parse(std::span<uint8_t const> data) const = 0;
};

class FormatRegistry {
public:
    static FormatRegistry& get();

    void add(std::unique_ptr<FormatHandler> handler);
    std::vector<FormatHandler const*> handlers() const;
    std::vector<std::string> allExtensions() const;

    /// Detects the format and parses. `fileName` is used for the extension
    /// tie-breaker and for messages.
    Expected<ImportResult> importData(std::span<uint8_t const> data, std::string_view fileName) const;

private:
    FormatRegistry();
    std::vector<std::unique_ptr<FormatHandler>> m_handlers;
};

// Individual parsers, exposed for tests.
Expected<Macro> parseGdr2(std::span<uint8_t const> data);
/// GDR 1 document already decoded from JSON or MessagePack.
Expected<Macro> parseGdr1Document(json::Value const& doc, std::string formatId, std::string formatName);
Expected<WindowDataset> parseNandlCalculatorJson(json::Value const& doc, std::vector<Diagnostic>& diags);
Expected<WindowDataset> parseWindowCsv(std::string_view text, std::vector<Diagnostic>& diags);

std::string extensionOf(std::string_view fileName);

} // namespace fwa
