#include "ui/kit/secrets.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>

#include "core/sda/totp.hpp"
#include "core/time_aligner.hpp"
#include "platform/clipboard.hpp"

namespace sam::ui::kit {

namespace {

constexpr const char* kMasked = "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2";
constexpr int kRevealSeconds = 5;
constexpr int kWindowSeconds = 30;

std::unordered_map<std::string, std::int64_t>& reveal_until() {
    static std::unordered_map<std::string, std::int64_t> map;
    return map;
}

void copy_code(const app::AppState& state, const std::string& code) {
    platform::clipboard::set_text_with_auto_clear(code, std::chrono::seconds(state.settings.clipboard_clear_seconds));
}

int score(const std::string& s) {
    if (s.empty()) return 0;
    bool lo = false;
    bool up = false;
    bool dg = false;
    bool sy = false;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (std::islower(u)) lo = true;
        else if (std::isupper(u)) up = true;
        else if (std::isdigit(u)) dg = true;
        else sy = true;
    }
    const int classes = static_cast<int>(lo) + static_cast<int>(up) + static_cast<int>(dg) + static_cast<int>(sy);
    return std::min<int>(static_cast<int>(s.size()), 24) * 4 + classes * 10;
}

// The time each account's code was last copied (ImGui time), for the button's "Copied" moment.
std::unordered_map<std::string, double>& copied_at() {
    static std::unordered_map<std::string, double> map;
    return map;
}

// A row whose items are centered in the available width.
void begin_centered_row(const char* id) {
    facet::BeginStack(id, {.orientation = facet::Orientation::Horizontal, .width = facet::AvailableWidthUnits()});
    facet::StackSpacer();
}

void end_centered_row() {
    facet::StackSpacer();
    facet::EndStack();
}

// Why one centered block: the code is read and typed character by character, so it is large,
// spaced and alone on its line, with the ring that times it right beside it and the action
// under it.
void hero_code(const app::AppState& state, const core::Account& account, const AuthenticatorCodeOptions& options,
               const std::string& code, std::int64_t now, AuthenticatorCodeResult& result) {
    const facet::Theme& t = facet::T();
    const bool hide = state.settings.sda.hide_current_code;
    const bool revealed = !hide || reveal_until()[account.id] > now;
    const int remaining = sda::seconds_remaining(now);

    // The code's own font size: pushed for measuring and drawing, popped before any widget.
    // Unscaled size, as Facet's text styles push it: Dear ImGui applies the UI scale itself.
    ImFont* font = facet::WeightFont(facet::FontWeight::Semibold, true);
    ImGui::PushFont(font, options.hero_code_size);
    const float size = ImGui::GetFontSize();
    const float spacing = size * 0.18F;
    std::vector<std::string> glyphs;
    if (revealed) {
        for (const char c : code) glyphs.emplace_back(1, c);
    } else {
        // Hidden: five dots drawn below, each in the slot of a capital letter, so the block keeps
        // the width of a code and the dots do not depend on the font's bullet glyph.
        for (int i = 0; i < 5; ++i) glyphs.emplace_back("H");
    }
    float code_w = 0.0F;
    float code_h = 0.0F;
    std::vector<float> widths;
    for (const std::string& g : glyphs) {
        const ImVec2 s = ImGui::CalcTextSize(g.c_str(), g.c_str() + g.size());
        widths.push_back(s.x);
        code_w += s.x;
        code_h = std::max(code_h, s.y);
    }
    if (!glyphs.empty()) code_w += spacing * static_cast<float>(glyphs.size() - 1);
    ImGui::PopFont();

    const float ring_units = options.hero_code_size * 1.1F;
    const float ring_px = facet::Px(ring_units);
    const float gap = facet::Px(t.spacing.xl);
    const float total = code_w + gap + ring_px;
    const float row_h = std::max(code_h, ring_px);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float x0 = origin.x + std::max(0.0F, (avail - total) * 0.5F);

    ImGui::SetCursorScreenPos(ImVec2(x0, origin.y + (row_h - code_h) * 0.5F));
    ImGui::Dummy(ImVec2(code_w, code_h));
    const ImVec2 code_min = ImGui::GetItemRectMin();
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        facet::ItemTooltip(revealed ? "Click to copy the code" : "Click to show the code for 5 seconds");
    }
    if (ImGui::IsItemClicked()) {
        if (!revealed) {
            reveal_until()[account.id] = now + kRevealSeconds;
        } else if (!code.empty()) {
            copy_code(state, code);
            copied_at()[account.id] = ImGui::GetTime();
            result.copied = true;
        }
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(revealed ? t.colors.label : t.colors.labelSecondary);
    ImGui::PushFont(font, options.hero_code_size);
    float x = code_min.x;
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        if (revealed) {
            dl->AddText(ImVec2(std::floor(x), code_min.y), color, glyphs[i].c_str(),
                        glyphs[i].c_str() + glyphs[i].size());
        } else {
            dl->AddCircleFilled(ImVec2(x + widths[i] * 0.5F, code_min.y + code_h * 0.5F), size * 0.1F, color);
        }
        x += widths[i] + spacing;
    }
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(x0 + code_w + gap, origin.y + (row_h - ring_px) * 0.5F));
    // No number in the ring: at this size it would be tiny next to the code. The ring shows the
    // time left; the line under the code says it in words, once.
    facet::ProgressRing("##countdown", static_cast<float>(remaining) / static_cast<float>(kWindowSeconds),
                        {.tone = remaining <= 5 ? facet::Tone::Destructive : facet::Tone::Accent,
                         .diameter = ring_units});
    facet::ItemTooltip("%d s left. Clock matched to Steam's, offset %+lld s.", remaining,
                       static_cast<long long>(time_aligner::offset_seconds()));

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + row_h + facet::Px(t.spacing.sm)));
    char line[64];
    if (state.settings.sda.show_next_code) {
        const std::string next = sda::generate_code(account.sda->shared_secret, now + kWindowSeconds);
        std::snprintf(line, sizeof(line), "Next code %s in %d s", revealed ? next.c_str() : kMasked, remaining);
    } else {
        std::snprintf(line, sizeof(line), "New code in %d s", remaining);
    }
    facet::StyledText(line, {.style = facet::TextStyle::Callout,
                             .secondary = true,
                             .align = facet::Align::Center,
                             .width = facet::AvailableWidthUnits()});

    if (!options.copy_button) return;
    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));
    begin_centered_row("##hero-actions");
    const auto it = copied_at().find(account.id);
    const bool just_copied = it != copied_at().end() && ImGui::GetTime() - it->second < t.motion.feedbackHold;
    if (facet::Button(just_copied ? "Copied###copy-code" : "Copy code###copy-code",
                      {.variant = facet::ButtonVariant::Primary,
                       .icon = facet::Icon(just_copied ? "check" : "copy"),
                       .size = facet::ControlSize::Large}) &&
        !code.empty()) {
        copy_code(state, code);
        copied_at()[account.id] = ImGui::GetTime();
        result.copied = true;
    }
    if (hide) {
        if (facet::Button(revealed ? "Hide code" : "Show code",
                          {.variant = facet::ButtonVariant::Plain,
                           .icon = facet::Icon(revealed ? "eye-off" : "eye"),
                           .size = facet::ControlSize::Large})) {
            reveal_until()[account.id] = revealed ? 0 : now + kRevealSeconds;
        }
    }
    end_centered_row();
}

}  // namespace

// Why code and ring side by side: the code is the focal point and the ring belongs to it, so
// they read as one block; the buttons sit under it where the hand goes next.
AuthenticatorCodeResult authenticator_code(const app::AppState& state, const core::Account& account,
                                           const AuthenticatorCodeOptions& options) {
    AuthenticatorCodeResult result;
    if (!account.sda.has_value()) return result;
    if (sda::generate_code(account.sda->shared_secret, 0).empty()) {
        result.unreadable = true;
        return result;
    }
    ImGui::PushID(account.id.c_str());
    ImGui::PushID("authenticator-code");
    if (!time_aligner::synced()) {
        if (options.hero) {
            begin_centered_row("##hero-sync");
            facet::Spinner("Waiting for the Steam time sync");
            end_centered_row();
        } else {
            facet::Spinner("Waiting for the Steam time sync");
        }
        ImGui::PopID();
        ImGui::PopID();
        return result;
    }
    result.synced = true;
    const std::int64_t now = time_aligner::aligned_now();
    const std::string code = sda::generate_code(account.sda->shared_secret, now);
    if (options.hero) {
        hero_code(state, account, options, code, now, result);
        ImGui::PopID();
        ImGui::PopID();
        return result;
    }
    const facet::Theme& t = facet::T();
    const int remaining = sda::seconds_remaining(now);
    const bool hide = state.settings.sda.hide_current_code;
    const bool revealed = !hide || reveal_until()[account.id] > now;

    ImGui::BeginGroup();
    facet::Text(facet::TextStyle::LargeTitle, "%s", revealed ? code.c_str() : kMasked);
    if (hide && ImGui::IsItemClicked()) reveal_until()[account.id] = revealed ? 0 : now + kRevealSeconds;
    if (state.settings.sda.show_next_code) {
        const std::string next = sda::generate_code(account.sda->shared_secret, now + kWindowSeconds);
        char line[48];
        std::snprintf(line, sizeof(line), "Next %s", revealed ? next.c_str() : kMasked);
        facet::StyledText(line, {.style = facet::TextStyle::Footnote, .secondary = true});
    }
    ImGui::EndGroup();
    const float block_h = ImGui::GetItemRectSize().y;

    ImGui::SameLine(0.0F, facet::Px(t.spacing.xl));
    char seconds[8];
    std::snprintf(seconds, sizeof(seconds), "%d", remaining);
    const float ring_units = std::max(block_h / facet::Scale(), t.metrics.controlHeightLarge);
    facet::ProgressRing("##countdown", static_cast<float>(remaining) / static_cast<float>(kWindowSeconds),
                        {.tone = remaining <= 5 ? facet::Tone::Destructive : facet::Tone::Accent,
                         .centerText = seconds,
                         .diameter = ring_units});
    facet::ItemTooltip(remaining == 1 ? "1 second left" : "%d seconds left", remaining);

    if (options.copy_button) {
        if (facet::Button("Copy code", {.icon = facet::Icon("copy")}) && !code.empty()) {
            copy_code(state, code);
            result.copied = true;
        }
        if (hide) {
            ImGui::SameLine();
            if (facet::Button(revealed ? "Hide code" : "Show code", {.variant = facet::ButtonVariant::Plain,
                                                                     .icon = facet::Icon(revealed ? "eye-off" : "eye")})) {
                reveal_until()[account.id] = revealed ? 0 : now + kRevealSeconds;
            }
        }
    }
    ImGui::PopID();
    ImGui::PopID();
    return result;
}

float password_strength(const std::string& password) {
    if (password.empty()) return 0.0F;
    return std::clamp(static_cast<float>(score(password)) / 130.0F, 0.0F, 1.0F);
}

bool master_password_field(const char* label, std::string* password, const MasterPasswordOptions& options) {
    if (password == nullptr) return false;
    return facet::SecureField(label, password,
                              {.placeholder = options.placeholder,
                               .strength = options.show_strength ? password_strength(*password) : -1.0F,
                               .width = options.width,
                               .error = options.error});
}

}  // namespace sam::ui::kit
