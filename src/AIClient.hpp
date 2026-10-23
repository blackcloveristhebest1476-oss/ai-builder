#pragma once

#include "Design.hpp"
#include <Geode/utils/web.hpp>

namespace ai_builder {

enum class BuildMode {
    Add,      // keep the selection, build around / onto it
    Replace,  // delete the selection and build the design in its place
};

// Builds the system prompt (object palette + output format rules).
std::string buildSystemPrompt();

// Builds the user message from the description and the selection snapshot.
std::string buildUserMessage(std::string const& description, SelectionInfo const& sel, BuildMode mode);

// Sends the request to whichever provider is configured in the mod settings.
geode::utils::web::WebFuture sendDesignRequest(std::string const& system, std::string const& user);

// Pulls the model's text out of the provider's response (or a readable error).
geode::Result<std::string> extractResponseText(geode::utils::web::WebResponse const& res);

// Parses the model's JSON into a Design, validating IDs and numbers.
geode::Result<Design> parseDesign(std::string const& text, std::set<int> const& extraAllowedIds);

// Returns a human-readable problem with the current settings, or empty if OK.
std::string checkSettings();

} // namespace ai_builder
