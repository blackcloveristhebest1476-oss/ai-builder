#pragma once

#include <Geode/Geode.hpp>
#include <set>
#include <string>
#include <vector>

namespace ai_builder {

// Size of one editor grid cell in GD units.
inline constexpr float CELL = 30.f;
// Y coordinate of the top of the ground in the editor.
inline constexpr float GROUND_Y = 90.f;

// One object the AI asked us to place. Coordinates are in grid cells
// relative to the build origin (x right, y up). (0,0) is the cell whose
// bottom-left corner sits on the origin.
struct GenObject {
    int id = 1;
    double x = 0;
    double y = 0;
    double rot = 0;     // degrees, clockwise
    double sx = 1;      // scale x
    double sy = 1;      // scale y
    bool flipX = false;
    bool flipY = false;
    int color = 0;      // main color channel (0 = object default)
    int color2 = 0;     // detail color channel (0 = object default)
    int zOrder = 0;     // 0 = default
};

struct GenColor {
    int channel = 1;
    cocos2d::ccColor3B color = {255, 255, 255};
};

struct Design {
    std::vector<GenObject> objects;
    std::vector<GenColor> colors;
    std::string notes;
    int droppedObjects = 0; // objects rejected (bad / disallowed IDs, over cap)
};

// Snapshot of the user's selection when the popup was opened.
struct SelectionInfo {
    std::vector<geode::Ref<GameObject>> objects;
    cocos2d::CCPoint origin;  // GD units, bottom-left corner of the build area
    bool originOnGround = false;
    int widthCells = 0;
    int heightCells = 0;
    std::set<int> ids;        // object IDs present in the selection
    std::string summary;      // compact text description sent to the AI
};

} // namespace ai_builder
