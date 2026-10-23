#pragma once

#include "AIClient.hpp"
#include "Design.hpp"

class EditorUI;

namespace ai_builder {

// Takes a snapshot of the current editor selection (or the camera position
// when nothing is selected) and turns it into AI context.
SelectionInfo captureSelection(EditorUI* ui);

// Places the design in the level. Returns the number of objects created.
// The whole build is a single undo step.
int applyDesign(EditorUI* ui, SelectionInfo const& sel, Design const& design, BuildMode mode);

} // namespace ai_builder
