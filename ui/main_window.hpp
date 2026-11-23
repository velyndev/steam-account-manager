#pragma once

#include "app/app_state.hpp"

namespace sam::ui {

// Draws the whole window for one frame: Facet's app frame with the title bar, and while the
// vault is unlocked the sidebar and the current screen; the command palette,
// the shortcut sheet, the update dialog and the toasts on top. Call between facet::NewFrame and
// facet::EndFrame, outside any window. Returns false if the app should quit (never today).
//
// Screens are hosted in a child window covering the area right of the sidebar down to the
// window's bottom edge, with no padding, no background, no scrolling and no implicit indent or item
// width; each screen lays itself out with kit::PageScope (ui/kit/page.hpp).
bool draw(app::AppState& state);

// Points Dear ImGui's ini file (io.IniFilename) at data_dir()/ui_state.ini, where Facet keeps
// its widget state (split sizes, palette recents, the window placement). Call after
// ImGui::CreateContext and before facet::Init, which reads Facet's sections from that file.
// The path follows platform::data_dir(), so the capture harness writes into its throwaway
// folder.
void configure_ini_file();

// True while a popup or a modal is open: the host then answers the title bar's caption with
// HTCLIENT, so a click there closes the popup instead of starting a window drag.
bool caption_blocked();

// Smallest window in design units, passed to the app frame (facet::AppFrameMinSize turns it
// into pixels at the current DPI and UI scale for WM_GETMINMAXINFO). 720 wide keeps the title
// bar's search, privacy and lock buttons beside a usable drag area and leaves the content next
// to the icon rail in Facet's Regular width class; 500 tall fits the unlock card under the
// title bar and a list of five rows below the page header.
inline constexpr float kMinWindowWidth = 720.0F;
inline constexpr float kMinWindowHeight = 500.0F;

// Forgets the shell's UI state (open palette, dialogs, rail override). The capture harness
// calls it before each scene, which gets a fresh AppState too.
void reset_shell_state();

#ifdef SAM_UI_CAPTURE
// Capture scenes only: open the command palette and type `query` into it once it shows.
void capture_open_palette(const char* query);
// Capture scenes only: show the sidebar as the icon rail whatever the window width.
void capture_force_rail();
// Capture scenes only: draw `content` in the content region instead of the current screen.
void capture_override_content(void (*content)(app::AppState& state));
#endif

}  // namespace sam::ui
