#pragma once

// The page scaffold: every screen draws inside one page, so all screens share one padding, one
// header shape and one rhythm.
//
//   void draw_trade_offers(app::AppState& state) {
//       kit::PageScope page("trade-offers", {.scroll = false});
//       {
//           kit::PageHeaderScope header("Trade offers", {.context = "3 waiting for a decision"});
//           if (facet::Button("Refresh offers", {.icon = facet::Icon("refresh")})) refresh(state);
//       }
//       draw_offer_list(state);  // fills the rest of the page
//   }

namespace sam::ui::kit {

struct PageOptions {
    // true: the page body scrolls as a whole (forms, settings, detail pages); its height
    // follows the content. false: the body is exactly the free height of the content region
    // and does not scroll, for screens whose own panes scroll (list and detail, tables).
    bool scroll = true;
};

// Opens the page of a screen in the shell's content region: a region of the full free width
// and height with the page padding (spacing.xxl at the sides and the bottom, spacing.xl at the
// top; spacing.lg on every side in a Compact width) and no background of its own. Pushes `id`
// on the id stack. Always pair with end_page (or use PageScope); the content is always
// submitted. Reads facet::T() and the available width; writes nothing.
void begin_page(const char* id, const PageOptions& options = {});
void end_page();

class [[nodiscard]] PageScope {
public:
    explicit PageScope(const char* id, const PageOptions& options = {}) { begin_page(id, options); }
    ~PageScope() { end_page(); }
    PageScope(const PageScope&) = delete;
    PageScope& operator=(const PageScope&) = delete;
};

struct PageHeaderOptions {
    // One line under the title in Footnote and labelSecondary: a count, the selected account,
    // what the page is filtered by. Null for none. Read during the call.
    const char* context = nullptr;
};

// The page header: the title in the Title1 style (one per screen, its focal heading), the
// optional context line under it, and a trailing actions area. Items submitted between
// begin_page_header and end_page_header are the actions: they sit in a row at the trailing
// end, vertically centered on the title block, spacing.sm apart. They are items of a Facet
// horizontal stack: submit them one after the other, without ImGui::SameLine. Buttons,
// icon buttons, a segmented control or a search field all fit. In a Compact width the actions move to their
// own row under the title. end_page_header adds the gap to the page content (spacing.lg).
// Always pair them (or use PageHeaderScope). Writes nothing; keeps the stack layout of the last
// frame under the id of the page.
void begin_page_header(const char* title, const PageHeaderOptions& options = {});
void end_page_header();

class [[nodiscard]] PageHeaderScope {
public:
    explicit PageHeaderScope(const char* title, const PageHeaderOptions& options = {}) {
        begin_page_header(title, options);
    }
    ~PageHeaderScope() { end_page_header(); }
    PageHeaderScope(const PageHeaderScope&) = delete;
    PageHeaderScope& operator=(const PageHeaderScope&) = delete;
};

}  // namespace sam::ui::kit
