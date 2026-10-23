#include "AIClient.hpp"
#include "Palette.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace ai_builder {

namespace {

bool isAnthropic() {
    return Mod::get()->getSettingValue<std::string>("provider") != "OpenAI-compatible";
}

std::string trimTrailingSlash(std::string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == ' ')) s.pop_back();
    return s;
}

double num(matjson::Value const& obj, std::string_view key, double def) {
    if (!obj.contains(key)) return def;
    auto const& v = obj[key];
    if (v.isNumber()) return v.asDouble().unwrapOr(def);
    if (v.isBool()) return v.asBool().unwrapOr(false) ? 1.0 : 0.0;
    if (v.isString()) {
        auto s = v.asString().unwrapOr("");
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (end != s.c_str()) return d;
    }
    return def;
}

bool flag(matjson::Value const& obj, std::string_view key) {
    return num(obj, key, 0) != 0;
}

bool parseHex(std::string hex, ccColor3B& out) {
    hex.erase(std::remove_if(hex.begin(), hex.end(), [](char c) { return c == '#' || c == ' '; }), hex.end());
    if (hex.size() != 6) return false;
    for (char c : hex) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    auto v = std::stoul(hex, nullptr, 16);
    out = { GLubyte((v >> 16) & 0xFF), GLubyte((v >> 8) & 0xFF), GLubyte(v & 0xFF) };
    return true;
}

double clampFinite(double v, double lo, double hi, double def) {
    if (!std::isfinite(v)) return def;
    return std::clamp(v, lo, hi);
}

} // namespace

std::string checkSettings() {
    auto key = Mod::get()->getSettingValue<std::string>("api-key");
    auto model = Mod::get()->getSettingValue<std::string>("model");
    if (model.empty()) return "No model is set. Open the mod settings and enter a model name.";
    if (isAnthropic() && key.empty()) {
        return "No API key set. Open the <cy>AI Builder</c> settings and paste your Anthropic API key "
               "(from console.anthropic.com), or switch the provider to OpenAI-compatible.";
    }
    if (!isAnthropic() && Mod::get()->getSettingValue<std::string>("base-url").empty()) {
        return "No Base URL set for the OpenAI-compatible provider.";
    }
    return "";
}

std::string buildSystemPrompt() {
    std::string palette;
    for (auto const& e : PALETTE) {
        palette += fmt::format("{}: {}\n", e.id, e.desc);
    }

    return fmt::format(R"PROMPT(You are a level-design assistant built into the Geometry Dash 2.2 level editor.
The user describes a design; you output the exact objects to place.

COORDINATES
- Work on the editor grid. 1 unit = 1 grid cell (one standard block, 30 GD units).
- x grows to the right, y grows upward. (0,0) is the bottom-left cell of the build area.
- An object at (x,y) is centered in that cell. Use integers for whole-block positions; use .5 or .25 steps only for small/offset objects.
- If the message says the origin is on the ground, y=0 is the row resting on the floor. Never place objects below y=0 in that case.
- The player runs left to right; designs that are meant to be played should flow in that direction.

OBJECT FIELDS (omit any field that equals its default)
- "id": object ID (required)
- "x", "y": cell position (required)
- "r": rotation in degrees, clockwise (default 0). Slopes and spikes can be rotated in 90-degree steps.
- "s": uniform scale (default 1). Or "sx"/"sy" for separate x/y scale (0.1 to 4).
- "fx", "fy": 1 to mirror horizontally / vertically (default 0)
- "c": main color channel 1-999 (default: object's own color). "c2": detail color channel.
- "z": z-order inside its layer (default 0, higher draws on top)

COLORS
- To color something, assign it a channel with "c", and define that channel in "colors" with a hex color.
- Use channels starting from {first_channel} upward so you don't collide with the user's own colors, unless the user's structure already uses channels you should reuse.
- For pixel art and pictures, use object 211 (solid colorable square) one per pixel, one channel per distinct color.

OBJECT PALETTE (id: description). Prefer these IDs. Also allowed: any ID that appears in the user's selected structure.
{palette}
DESIGN TIPS
- Real GD levels build platforms out of solid blocks, use edge/corner pieces on borders, and add decoration (rods, chains, glow, clouds, bushes) for polish.
- Spikes sit on top of blocks: a spike on a block at (x,y) goes at (x,y+1).
- Keep jumps fair: a cube can jump about 3 cells high and 4-5 cells far at normal speed.
- When given a selected structure, match its style (same block IDs, colors, scale) unless told otherwise.

OUTPUT FORMAT
Reply with ONLY one JSON object, no markdown fences, no commentary:
{{"notes":"one short sentence about what you built","colors":[{{"channel":{first_channel},"hex":"FF8800"}}],"objects":[{{"id":1,"x":0,"y":0}},{{"id":8,"x":3,"y":0,"c":{first_channel}}}]}}
"colors" may be an empty array. Stay under {max_objects} objects.)PROMPT",
        fmt::arg("palette", palette),
        fmt::arg("first_channel", 50),
        fmt::arg("max_objects", Mod::get()->getSettingValue<int64_t>("max-objects"))
    );
}

std::string buildUserMessage(std::string const& description, SelectionInfo const& sel, BuildMode mode) {
    std::string msg;
    if (sel.objects.empty()) {
        msg += "The user has nothing selected. Build the design from scratch.\n";
        msg += sel.originOnGround
            ? "The origin is on the ground (y=0 rests on the floor).\n"
            : "The origin is in mid-air near the camera view (not on the ground).\n";
    } else {
        msg += fmt::format(
            "The user selected a structure of {} objects spanning about {}x{} cells. "
            "Here are its objects in the same coordinate system you must use:\n{}\n",
            sel.objects.size(), sel.widthCells, sel.heightCells, sel.summary
        );
        if (sel.originOnGround) msg += "The structure's bottom row is on the ground (y=0 rests on the floor).\n";
        if (mode == BuildMode::Replace) {
            msg += "MODE: REPLACE. The selected structure will be deleted and replaced by your output. "
                   "Output the complete new version (redesigned / modified as described), occupying roughly the same area unless told otherwise.\n";
        } else {
            msg += "MODE: ADD. The selected structure stays where it is. Output ONLY the new objects to add "
                   "(do not repeat existing objects). Position them relative to the structure as the description implies.\n";
        }
    }
    msg += "\nDESIGN REQUEST: " + description;
    return msg;
}

web::WebFuture sendDesignRequest(std::string const& system, std::string const& user) {
    auto mod = Mod::get();
    auto key = mod->getSettingValue<std::string>("api-key");
    auto model = mod->getSettingValue<std::string>("model");
    auto maxTokens = static_cast<int>(mod->getSettingValue<int64_t>("max-tokens"));

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(300));
    req.header("Content-Type", "application/json");

    if (isAnthropic()) {
        req.header("x-api-key", key);
        req.header("anthropic-version", "2023-06-01");
        req.bodyJSON(matjson::makeObject({
            {"model", model},
            {"max_tokens", maxTokens},
            {"system", system},
            {"messages", std::vector<matjson::Value>{
                matjson::makeObject({{"role", "user"}, {"content", user}})
            }},
        }));
        return req.post("https://api.anthropic.com/v1/messages");
    }

    if (!key.empty()) req.header("Authorization", "Bearer " + key);
    req.bodyJSON(matjson::makeObject({
        {"model", model},
        {"max_tokens", maxTokens},
        {"messages", std::vector<matjson::Value>{
            matjson::makeObject({{"role", "system"}, {"content", system}}),
            matjson::makeObject({{"role", "user"}, {"content", user}})
        }},
    }));
    auto base = trimTrailingSlash(mod->getSettingValue<std::string>("base-url"));
    return req.post(base + "/chat/completions");
}

Result<std::string> extractResponseText(web::WebResponse const& res) {
    if (res.cancelled()) return Err("Request was cancelled.");

    auto json = res.json();
    if (!res.ok()) {
        std::string detail;
        if (json) {
            auto const& j = json.unwrap();
            if (j.contains("error")) {
                auto const& e = j["error"];
                if (e.isString()) detail = e.asString().unwrapOr("");
                else if (e.contains("message")) detail = e["message"].asString().unwrapOr("");
            }
        }
        if (detail.empty()) detail = res.string().unwrapOr("no response body");
        if (detail.size() > 300) detail = detail.substr(0, 300) + "...";
        if (res.code() <= 0) {
            return Err("Couldn't reach the AI server (network error). Check your internet / Base URL.");
        }
        return Err("AI server returned HTTP {}: {}", res.code(), detail);
    }
    if (!json) return Err("AI server sent a response that isn't JSON.");
    auto const& j = json.unwrap();

    std::string text;
    if (isAnthropic()) {
        if (j.contains("content") && j["content"].isArray()) {
            for (auto const& block : j["content"].asArray().unwrap()) {
                if (block.contains("text")) text += block["text"].asString().unwrapOr("");
            }
        }
        if (j.contains("stop_reason") && j["stop_reason"].asString().unwrapOr("") == "max_tokens") {
            log::warn("AI response hit max_tokens; output may be truncated");
        }
    } else {
        if (j.contains("choices") && j["choices"].isArray() && j["choices"].size() > 0) {
            auto const& choice = j["choices"][0];
            if (choice.contains("message") && choice["message"].contains("content")) {
                text = choice["message"]["content"].asString().unwrapOr("");
            }
        }
    }
    if (text.empty()) return Err("The AI returned an empty answer.");
    return Ok(text);
}

Result<Design> parseDesign(std::string const& text, std::set<int> const& extraAllowedIds) {
    auto first = text.find('{');
    auto last = text.rfind('}');
    if (first == std::string::npos || last == std::string::npos || last <= first) {
        return Err("The AI didn't return a design (no JSON found). Try rephrasing your description.");
    }
    auto parsed = matjson::parse(std::string_view(text).substr(first, last - first + 1));
    if (!parsed) {
        return Err("The AI's design was cut off or malformed. Try a smaller design or raise Max Output Tokens.");
    }
    auto const& root = parsed.unwrap();

    bool strict = Mod::get()->getSettingValue<bool>("strict-ids");
    auto maxObjects = static_cast<size_t>(Mod::get()->getSettingValue<int64_t>("max-objects"));

    Design design;
    if (root.contains("notes")) design.notes = root["notes"].asString().unwrapOr("");
    if (design.notes.size() > 200) design.notes = design.notes.substr(0, 200) + "...";

    if (root.contains("colors") && root["colors"].isArray()) {
        for (auto const& c : root["colors"].asArray().unwrap()) {
            if (!c.isObject()) continue;
            int ch = static_cast<int>(num(c, "channel", 0));
            if (ch < 1 || ch > 999) continue;
            GenColor gc;
            gc.channel = ch;
            std::string hex = c.contains("hex") ? c["hex"].asString().unwrapOr("") : "";
            if (hex.empty() && c.contains("color")) hex = c["color"].asString().unwrapOr("");
            if (!parseHex(hex, gc.color)) continue;
            design.colors.push_back(gc);
        }
    }

    if (!root.contains("objects") || !root["objects"].isArray()) {
        return Err("The AI's answer had no objects in it.");
    }
    for (auto const& o : root["objects"].asArray().unwrap()) {
        if (!o.isObject() || !o.contains("id")) { design.droppedObjects++; continue; }
        GenObject g;
        g.id = static_cast<int>(num(o, "id", 0));
        bool allowed = g.id > 0 && g.id < 5000 &&
            (!strict || isPaletteId(g.id) || extraAllowedIds.contains(g.id));
        if (!allowed || design.objects.size() >= maxObjects) { design.droppedObjects++; continue; }

        g.x = clampFinite(num(o, "x", 0), -2000, 2000, 0);
        g.y = clampFinite(num(o, "y", 0), -200, 200, 0);
        g.rot = clampFinite(num(o, "r", num(o, "rot", 0)), -3600, 3600, 0);
        double s = clampFinite(num(o, "s", 1), 0.05, 8, 1);
        g.sx = clampFinite(num(o, "sx", s), 0.05, 8, 1);
        g.sy = clampFinite(num(o, "sy", s), 0.05, 8, 1);
        g.flipX = flag(o, "fx");
        g.flipY = flag(o, "fy");
        int c = static_cast<int>(num(o, "c", 0));
        int c2 = static_cast<int>(num(o, "c2", 0));
        g.color = (c >= 1 && c <= 1012) ? c : 0;
        g.color2 = (c2 >= 1 && c2 <= 1012) ? c2 : 0;
        g.zOrder = static_cast<int>(clampFinite(num(o, "z", 0), -100, 100, 0));
        design.objects.push_back(g);
    }

    if (design.objects.empty()) {
        return Err("The AI's design had no usable objects. Try describing it differently.");
    }
    return Ok(std::move(design));
}

} // namespace ai_builder
