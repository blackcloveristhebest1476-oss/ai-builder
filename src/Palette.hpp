#pragma once

// Curated list of Geometry Dash 2.2 object IDs that the AI is allowed to use.
// IDs cross-checked against the object catalog from EditorAI (MIT, entity12208).
// Each entry: id, short description shown to the AI.

#include <array>
#include <string_view>

namespace ai_builder {

struct PaletteEntry {
    int id;
    std::string_view desc;
};

inline constexpr std::array PALETTE = {
    // --- Solid blocks (1x1 cell) ---
    PaletteEntry{1,    "block: classic black gradient square, solid"},
    PaletteEntry{2,    "block: grid-pattern top edge piece"},
    PaletteEntry{3,    "block: grid-pattern outer corner piece"},
    PaletteEntry{5,    "block: grid-pattern inner (fill) piece"},
    PaletteEntry{6,    "block: grid-pattern pillar top"},
    PaletteEntry{7,    "block: grid-pattern pillar middle"},
    PaletteEntry{83,   "block: grid-pattern full square"},
    PaletteEntry{69,   "block: black square with cross pattern"},
    PaletteEntry{70,   "block: beveled top piece"},
    PaletteEntry{73,   "block: beveled inner piece"},
    PaletteEntry{76,   "block: mechanical square"},
    PaletteEntry{90,   "block: plain black square with outline"},
    PaletteEntry{94,   "block: plain black inner square (no outline)"},
    PaletteEntry{143,  "block: breakable brick"},
    PaletteEntry{175,  "block: metal block"},
    PaletteEntry{467,  "block: outline-only square"},
    PaletteEntry{1210, "block: thick outline square"},
    PaletteEntry{255,  "block: colorable beveled square"},
    PaletteEntry{269,  "block: colorable bricks square"},
    PaletteEntry{247,  "block: colorable grid-pattern square"},
    PaletteEntry{263,  "block: colorable mechanical square"},
    // --- Pixel-art / shape building (fully colorable, use these for pictures) ---
    PaletteEntry{211,  "PIXEL: solid colorable square, no outline - best for pixel art (1x1 cell)"},
    PaletteEntry{220,  "PIXEL: small solid colorable square (half-cell size)"},
    PaletteEntry{917,  "PIXEL: tiny colorable square (1/16 of a cell, i.e. 1/4 cell wide)"},
    PaletteEntry{279,  "transparent colorable square (see-through)"},
    PaletteEntry{146,  "invisible solid block (hidden in play)"},
    // --- Half blocks / slabs ---
    PaletteEntry{40,   "slab: black half-height slab (sits at bottom half of cell)"},
    PaletteEntry{215,  "slab: colorable slab"},
    PaletteEntry{219,  "slab: small colorable slab"},
    PaletteEntry{195,  "small black gradient square (half-cell)"},
    // --- Slopes ---
    PaletteEntry{289,  "slope: 45 degree grid slope, 1x1 cell, rises to the right"},
    PaletteEntry{291,  "slope: wide 22.5 degree grid slope, 2 cells wide x 1 tall, rises to the right (x = left cell + 0.5)"},
    PaletteEntry{371,  "slope: black gradient 45 degree slope 1x1"},
    PaletteEntry{372,  "slope: black gradient wide slope 2x1"},
    PaletteEntry{1338, "slope: outline 45 degree slope 1x1"},
    PaletteEntry{1339, "slope: outline wide slope 2x1"},
    PaletteEntry{349,  "slope: colorable mechanical slope 1x1"},
    // --- Hazards ---
    PaletteEntry{8,    "hazard: spike, full cell, points up"},
    PaletteEntry{39,   "hazard: half-height spike"},
    PaletteEntry{103,  "hazard: small spike"},
    PaletteEntry{392,  "hazard: tiny spike"},
    PaletteEntry{216,  "hazard: colorable spike"},
    PaletteEntry{9,    "hazard: ground spikes (pit filler)"},
    PaletteEntry{177,  "hazard: ice spike"},
    PaletteEntry{88,   "hazard: large black sawblade (about 3 cells wide, rotates)"},
    PaletteEntry{89,   "hazard: medium black sawblade (about 2 cells wide)"},
    PaletteEntry{98,   "hazard: small black sawblade (about 1 cell)"},
    PaletteEntry{191,  "fake spike (decoration, harmless)"},
    // --- Pads & orbs ---
    PaletteEntry{35,   "yellow jump pad (sits on ground)"},
    PaletteEntry{140,  "pink jump pad (small jump)"},
    PaletteEntry{1332, "red jump pad (big jump)"},
    PaletteEntry{67,   "blue gravity pad"},
    PaletteEntry{36,   "yellow jump orb"},
    PaletteEntry{141,  "pink jump orb"},
    PaletteEntry{1333, "red jump orb"},
    PaletteEntry{84,   "blue gravity orb"},
    PaletteEntry{1022, "green gravity orb"},
    PaletteEntry{1330, "black drop orb"},
    PaletteEntry{1704, "green dash orb"},
    PaletteEntry{1751, "pink gravity dash orb"},
    // --- Portals (portals are 1 cell wide x 3 cells tall, x/y is their center) ---
    PaletteEntry{12,   "portal: cube mode"},
    PaletteEntry{13,   "portal: ship mode"},
    PaletteEntry{47,   "portal: ball mode"},
    PaletteEntry{111,  "portal: UFO mode"},
    PaletteEntry{660,  "portal: wave mode"},
    PaletteEntry{745,  "portal: robot mode"},
    PaletteEntry{1331, "portal: spider mode"},
    PaletteEntry{1933, "portal: swing mode"},
    PaletteEntry{10,   "portal: normal gravity"},
    PaletteEntry{11,   "portal: flipped gravity"},
    PaletteEntry{99,   "portal: mini size"},
    PaletteEntry{101,  "portal: normal size"},
    PaletteEntry{286,  "portal: dual mode on"},
    PaletteEntry{287,  "portal: dual mode off"},
    PaletteEntry{45,   "portal: mirror on"},
    PaletteEntry{46,   "portal: mirror off"},
    PaletteEntry{200,  "speed portal: 0.5x"},
    PaletteEntry{201,  "speed portal: 1x"},
    PaletteEntry{202,  "speed portal: 2x"},
    PaletteEntry{203,  "speed portal: 3x"},
    PaletteEntry{1334, "speed portal: 4x"},
    // --- Collectibles ---
    PaletteEntry{1329, "user coin"},
    PaletteEntry{2063, "checkpoint"},
    // --- Decoration ---
    PaletteEntry{15,   "deco: tall rod"},
    PaletteEntry{16,   "deco: medium rod"},
    PaletteEntry{17,   "deco: short rod"},
    PaletteEntry{498,  "deco: chain link"},
    PaletteEntry{499,  "deco: chain base"},
    PaletteEntry{129,  "deco: medium cloud"},
    PaletteEntry{130,  "deco: large cloud"},
    PaletteEntry{131,  "deco: small cloud"},
    PaletteEntry{936,  "deco: medium cartoon cloud"},
    PaletteEntry{907,  "deco: large bush"},
    PaletteEntry{908,  "deco: medium bush"},
    PaletteEntry{909,  "deco: small bush"},
    PaletteEntry{406,  "deco: patch of grass"},
    PaletteEntry{939,  "deco: flower"},
    PaletteEntry{1294, "deco: textured grass block side"},
    PaletteEntry{180,  "deco: large cartwheel"},
    PaletteEntry{137,  "deco: large wheel"},
    PaletteEntry{183,  "deco: large blade"},
    PaletteEntry{97,   "deco: tiny gear"},
    PaletteEntry{50,   "deco: pulsing filled circle"},
    PaletteEntry{51,   "deco: pulsing ring"},
    PaletteEntry{52,   "deco: pulsing heart"},
    PaletteEntry{54,   "deco: pulsing star"},
    PaletteEntry{132,  "deco: big pulsing arrow"},
    PaletteEntry{133,  "deco: pulsing exclamation mark"},
    PaletteEntry{136,  "deco: pulsing question mark"},
    PaletteEntry{1764, "deco: tiny circle (dot)"},
    PaletteEntry{1765, "deco: tiny cross"},
    PaletteEntry{1766, "deco: tiny triangle"},
    PaletteEntry{1767, "deco: tiny rectangle"},
    PaletteEntry{1768, "deco: tiny arrow"},
    PaletteEntry{1753, "deco: thin line (1 cell long, rotate it for diagonals)"},
    PaletteEntry{1757, "deco: thin half line"},
    PaletteEntry{1754, "deco: thin glowing line"},
    PaletteEntry{503,  "deco: straight medium glow edge"},
    PaletteEntry{1011, "deco: straight large glow edge"},
    PaletteEntry{1886, "deco: medium glow orb"},
    PaletteEntry{1888, "deco: large glow orb"},
    PaletteEntry{1603, "deco: cartoon arrow"},
};

inline bool isPaletteId(int id) {
    for (auto const& e : PALETTE) {
        if (e.id == id) return true;
    }
    return false;
}

} // namespace ai_builder
