#include "Builder.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJSpriteColor.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/ColorAction.hpp>
#include <Geode/binding/UndoObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>

#include <cmath>
#include <map>

using namespace geode::prelude;

namespace ai_builder {

namespace {

// Max number of selected objects described to the AI (keeps prompts small).
constexpr size_t MAX_CONTEXT_OBJECTS = 400;

std::string fmtNum(double v) {
    double r = std::round(v * 100.0) / 100.0;
    if (std::abs(r - std::round(r)) < 1e-6) return fmt::format("{}", static_cast<long long>(std::llround(r)));
    return fmt::format("{:.2f}", r);
}

} // namespace

SelectionInfo captureSelection(EditorUI* ui) {
    SelectionInfo info;
    auto layer = ui->m_editorLayer;

    if (auto arr = ui->getSelectedObjects()) {
        for (auto obj : CCArrayExt<GameObject*>(arr)) {
            if (obj) info.objects.emplace_back(obj);
        }
    }

    if (info.objects.empty()) {
        // Build near the middle of the screen.
        auto winSize = CCDirector::get()->getWinSize();
        auto center = layer->m_objectLayer->convertToNodeSpace(winSize / 2);
        float ox = std::floor(center.x / CELL) * CELL - CELL * 5;
        if (ox < 0) ox = 0;
        float oy;
        if (center.y < GROUND_Y + CELL * 8) {
            oy = GROUND_Y;
            info.originOnGround = true;
        } else {
            oy = std::floor(center.y / CELL) * CELL - CELL * 3;
        }
        info.origin = ccp(ox, oy);
        return info;
    }

    // Bounding box of the selection's object centers.
    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    for (auto& obj : info.objects) {
        auto p = obj->getPosition();
        minX = std::min(minX, p.x); minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x); maxY = std::max(maxY, p.y);
    }
    info.origin = ccp(std::floor(minX / CELL) * CELL, std::floor(minY / CELL) * CELL);
    info.originOnGround = std::abs(info.origin.y - GROUND_Y) < 0.5f;
    info.widthCells = static_cast<int>(std::ceil((maxX - info.origin.x) / CELL));
    info.heightCells = static_cast<int>(std::ceil((maxY - info.origin.y) / CELL));

    // Describe each object compactly, using the same fields the AI outputs.
    std::string lines;
    size_t count = 0;
    std::map<int, int> idCounts;
    for (auto& obj : info.objects) {
        info.ids.insert(obj->m_objectID);
        idCounts[obj->m_objectID]++;
        if (count >= MAX_CONTEXT_OBJECTS) continue;
        count++;

        auto p = obj->getPosition();
        double gx = (p.x - info.origin.x - CELL / 2) / CELL;
        double gy = (p.y - info.origin.y - CELL / 2) / CELL;
        std::string line = fmt::format("{{\"id\":{},\"x\":{},\"y\":{}", obj->m_objectID, fmtNum(gx), fmtNum(gy));
        float rot = obj->getRotation();
        if (std::abs(rot) > 0.01f) line += fmt::format(",\"r\":{}", fmtNum(rot));
        if (std::abs(obj->m_scaleX - 1.f) > 0.01f) line += fmt::format(",\"sx\":{}", fmtNum(obj->m_scaleX));
        if (std::abs(obj->m_scaleY - 1.f) > 0.01f) line += fmt::format(",\"sy\":{}", fmtNum(obj->m_scaleY));
        if (obj->m_isFlipX) line += ",\"fx\":1";
        if (obj->m_isFlipY) line += ",\"fy\":1";
        if (obj->m_baseColor && obj->m_baseColor->m_colorID > 0 &&
            obj->m_baseColor->m_colorID != obj->m_baseColor->m_defaultColorID) {
            line += fmt::format(",\"c\":{}", obj->m_baseColor->m_colorID);
        }
        if (obj->m_detailColor && obj->m_detailColor->m_colorID > 0 &&
            obj->m_detailColor->m_colorID != obj->m_detailColor->m_defaultColorID) {
            line += fmt::format(",\"c2\":{}", obj->m_detailColor->m_colorID);
        }
        line += "}\n";
        lines += line;
    }
    if (info.objects.size() > MAX_CONTEXT_OBJECTS) {
        lines += fmt::format("...and {} more objects (not listed). Object ID counts in the full selection:", info.objects.size() - MAX_CONTEXT_OBJECTS);
        for (auto [id, n] : idCounts) lines += fmt::format(" {}x{}", id, n);
        lines += "\n";
    }
    info.summary = lines;
    return info;
}

int applyDesign(EditorUI* ui, SelectionInfo const& sel, Design const& design, BuildMode mode) {
    auto layer = ui->m_editorLayer;
    if (!layer) return 0;

    // Build a GD object string: key,value pairs separated by commas, objects by semicolons.
    std::string str;
    for (auto const& o : design.objects) {
        float px = sel.origin.x + static_cast<float>(o.x) * CELL + CELL / 2;
        float py = sel.origin.y + static_cast<float>(o.y) * CELL + CELL / 2;
        str += fmt::format("1,{},2,{:.2f},3,{:.2f}", o.id, px, py);
        if (std::abs(o.rot) > 0.001) str += fmt::format(",6,{:.2f}", o.rot);
        if (o.flipX) str += ",4,1";
        if (o.flipY) str += ",5,1";
        if (std::abs(o.sx - 1) > 0.001) str += fmt::format(",128,{:.3f}", o.sx);
        if (std::abs(o.sy - 1) > 0.001) str += fmt::format(",129,{:.3f}", o.sy);
        if (o.color > 0) str += fmt::format(",21,{}", o.color);
        if (o.color2 > 0) str += fmt::format(",22,{}", o.color2);
        if (o.zOrder != 0) str += fmt::format(",25,{}", o.zOrder);
        if (layer->m_currentLayer > 0) str += fmt::format(",20,{}", layer->m_currentLayer);
        str += ";";
    }

    if (mode == BuildMode::Replace && !sel.objects.empty()) {
        // Re-select exactly the snapshot, then delete it through GD so it gets its own undo step.
        ui->deselectAll();
        auto arr = CCArray::create();
        for (auto const& obj : sel.objects) {
            if (obj && obj->getParent()) arr->addObject(obj);
        }
        if (arr->count() > 0) {
            ui->selectObjects(arr, true);
            ui->onDeleteSelected(nullptr);
        }
    }

    ui->deselectAll();
    auto created = layer->createObjectsFromString(str, true, true);
    if (!created || created->count() == 0) return 0;

    // Register the whole build as a single paste-style undo step.
    if (layer->m_undoObjects) {
        if (auto undo = UndoObject::createWithArray(created, UndoCommand::Paste)) {
            layer->m_undoObjects->addObject(undo);
            if (layer->m_redoObjects) layer->m_redoObjects->removeAllObjects();
        }
    }

    // Apply color channels.
    if (Mod::get()->getSettingValue<bool>("apply-colors") && !design.colors.empty() && layer->m_effectManager) {
        for (auto const& c : design.colors) {
            auto action = layer->m_effectManager->getColorAction(c.channel);
            if (!action) continue;
            action->m_color = c.color;
            action->m_fromColor = c.color;
            action->m_toColor = c.color;
            layer->m_effectManager->colorActionChanged(action);
        }
    }

    if (layer->m_level) layer->m_level->levelWasAltered();

    ui->selectObjects(created, true);
    ui->updateButtons();
    return static_cast<int>(created->count());
}

} // namespace ai_builder
