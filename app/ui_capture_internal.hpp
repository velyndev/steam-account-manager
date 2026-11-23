#pragma once

// Shared between the harness translation units (ui_capture.cpp, ui_capture_demo.cpp,
// ui_capture_scenes.cpp). Scene setups use app/ui_capture.hpp only.

#ifdef SAM_UI_CAPTURE

#include <string>
#include <vector>

#include "app/ui_capture.hpp"

namespace sam::app::ui_capture {

// Writes the demo vaults (registry, the 30-account vault and two empty ones, all under the
// throwaway data dir) through the account store, and loads the demo vault back once for
// open_demo_vault. Call after data_dir() points at the throwaway folder. False and `error`
// on failure.
bool prepare_demo_data(std::string& error);

// The scenes one per current screen, in the order the harness captures them.
std::vector<CaptureScene> builtin_scenes();

}  // namespace sam::app::ui_capture

#endif  // SAM_UI_CAPTURE
