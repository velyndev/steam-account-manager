#pragma once

// Actions the screens repeat: the activatable action card, the empty or error state with one
// Primary action, and the quiet destructive button (brief rule 3).

#include <facet/core/icons.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/common.h>

namespace sam::ui::kit {

// --- Action card -------------------------------------------------------------------------------

struct ActionCardOptions {
    facet::Icon icon;                   // drawn at 24 units left of the text; none for none
    const char* description = nullptr;  // one or two lines under the title, labelSecondary; null for none
    float width = 0.0F;                 // units; 0 fills the available width
    // Cards that pass the same group id share one height: the tallest of the group in the last
    // frame, so a row of cards lines up whatever their descriptions say. Null: own height.
    const char* equal_height_group = nullptr;
};

// A card that does something when activated: the icon, the visible label part as the title
// (Headline) and the description, on the card surface (bgSecondary, card radius, controlBorder
// hairline). Hover and press tint it with Facet's fill tokens, keyboard focus draws the focus
// ring (focusRing at metrics.focusRingWidth, inside the edge), and inside ImGui::BeginDisabled it
// takes the disabled fill and labelTertiary text and does nothing. It looks like Facet's
// OptionCard without the selection mark, so it reads as "do this", not "choose this, then
// confirm". Returns true on the frame it is activated: a click released inside, Enter or Space
// while focused. One Tab stop; the label is its id (PushID in loops). Keeps the hover and press
// animation under its id and the group heights in the kit.
bool action_card(const char* label, const ActionCardOptions& options = {});

// --- Empty and error states ---------------------------------------------------------------------

struct EmptyStateOptions {
    const char* message = nullptr;       // wrapped text under the title; null for none
    facet::Icon icon;                    // above the title; none for none
    const char* action_label = nullptr;  // the one action, drawn as the Primary button; null for none
    facet::Icon action_icon;             // leading icon of the action; none for none
    facet::Tone tone = facet::Tone::Neutral;  // Destructive for an error state
    float width = 0.0F;                  // units; 0 fills the available width
    float height = 0.0F;                 // units; 0 fits the content; the content is centered in it
};

// Facet's EmptyState (icon, title, message, centered) with its single action as the Primary
// button under it, as rule 3 asks for the only action of an empty or error state ("Add
// account", "Try again", "Connect"). Returns true on the frame the action is activated. The
// action is the only Tab stop. Keeps no state beyond Facet's.
bool empty_state(const char* label, const EmptyStateOptions& options = {});

// --- Quiet destructive button -------------------------------------------------------------------

// A destructive action on a page (rule 3): a Secondary button that fits its label, with a
// fitting icon (trash by default), so it does not compete with the view's one Primary button.
// The solid Destructive button belongs only to the confirm button of a destructive dialog
// (kit::destructive_dialog). Facet cannot tint only a Secondary button's icon, so the icon keeps
// the label color: the label must say what is destroyed ("Remove account", "Delete vault").
// Returns true on activation, like facet::Button.
bool quiet_destructive_button(const char* label, facet::Icon icon = facet::Icon("trash"),
                              facet::ControlSize size = facet::ControlSize::Inherit);

}  // namespace sam::ui::kit
