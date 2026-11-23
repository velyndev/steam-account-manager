// Settings > Steam Guard: how codes are shown and copied, the global copy shortcut, how
// confirmations are fetched, and auto-approve.

#include <cstdint>
#include <optional>

#include <windows.h>

#include <imgui.h>

#include <facet/core/input.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/selection.h>

#include "ui/screens/settings/categories.hpp"

namespace sam::ui::screens::settings {

namespace {

// The global hotkey is stored as the Win32 pair RegisterHotKey takes (MOD_* flags and a
// virtual key). Facet's KeybindCapture works on ImGui chords. The keys the hotkey accepts
// (letters, digits, F1 to F24) and the four modifiers map one to one, so the conversion is
// lossless both ways; any other key is refused with a message instead of being stored.
std::optional<ImGuiKey> vk_to_key(std::uint32_t vk) {
    if (vk >= 'A' && vk <= 'Z') return static_cast<ImGuiKey>(ImGuiKey_A + static_cast<int>(vk - 'A'));
    if (vk >= '0' && vk <= '9') return static_cast<ImGuiKey>(ImGuiKey_0 + static_cast<int>(vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F24) return static_cast<ImGuiKey>(ImGuiKey_F1 + static_cast<int>(vk - VK_F1));
    return std::nullopt;
}

std::optional<std::uint32_t> key_to_vk(ImGuiKey key) {
    if (key >= ImGuiKey_A && key <= ImGuiKey_Z) return static_cast<std::uint32_t>('A' + (key - ImGuiKey_A));
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9) return static_cast<std::uint32_t>('0' + (key - ImGuiKey_0));
    if (key >= ImGuiKey_F1 && key <= ImGuiKey_F24) return static_cast<std::uint32_t>(VK_F1 + (key - ImGuiKey_F1));
    return std::nullopt;
}

constexpr std::uint32_t kModMask = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;

ImGuiKeyChord to_chord(std::uint32_t mods, std::uint32_t vk) {
    const auto key = vk_to_key(vk);
    if (!key) return ImGuiKey_None;
    ImGuiKeyChord chord = *key;
    if ((mods & MOD_CONTROL) != 0) chord |= ImGuiMod_Ctrl;
    if ((mods & MOD_SHIFT) != 0) chord |= ImGuiMod_Shift;
    if ((mods & MOD_ALT) != 0) chord |= ImGuiMod_Alt;
    if ((mods & MOD_WIN) != 0) chord |= ImGuiMod_Super;
    return chord;
}

std::uint32_t to_mods(ImGuiKeyChord chord) {
    std::uint32_t mods = 0;
    if ((chord & ImGuiMod_Ctrl) != 0) mods |= MOD_CONTROL;
    if ((chord & ImGuiMod_Shift) != 0) mods |= MOD_SHIFT;
    if ((chord & ImGuiMod_Alt) != 0) mods |= MOD_ALT;
    if ((chord & ImGuiMod_Super) != 0) mods |= MOD_WIN;
    return mods;
}

void draw_hotkey(Page& p) {
    auto& sda = p.s.sda;
    if (begin_row(p, "Global copy shortcut",
                  {.description = "Copies the selected account's code from any app, without switching to this one.",
                   .keywords = "hotkey shortcut global keyboard copy code keybind",
                   .modified = sda.global_hotkey_enabled != p.d.sda.global_hotkey_enabled,
                   .immediate = true})) {
        if (facet::Toggle("##value", &sda.global_hotkey_enabled)) {
            p.state.needs_hotkey_reregister = true;
            commit_now(p.state);
        }
        end_row();
    }

    static const char* s_error = nullptr;
    if (begin_dependent(p, "##hotkey", sda.global_hotkey_enabled)) {
        if (begin_row(p, "Shortcut",
                      {.description = "Click, then press the new keys: a letter, digit or F key, usually with Ctrl, "
                                      "Shift, Alt or Win.",
                       .keywords = "hotkey shortcut keys binding ctrl shift",
                       .immediate = true})) {
            facet::Keybind bind{.primary = to_chord(sda.global_hotkey_mods, sda.global_hotkey_vk)};
            if (bind.primary == ImGuiKey_None) s_error = "The stored shortcut uses a key this field cannot show. Press new keys.";
            if (facet::KeybindCapture("##value", &bind, {.allowMouse = false, .width = cap(240.0F), .error = s_error})) {
                const auto key = static_cast<ImGuiKey>(bind.primary & ~ImGuiMod_Mask_);
                const auto vk = key_to_vk(key);
                const std::uint32_t mods = to_mods(bind.primary);
                if (bind.primary == ImGuiKey_None) {
                    s_error = "The shortcut cannot be empty. Turn it off above instead.";
                } else if (!vk) {
                    s_error = "Use a letter, a digit or an F key.";
                } else {
                    sda.global_hotkey_mods = (sda.global_hotkey_mods & ~kModMask) | mods;
                    sda.global_hotkey_vk = *vk;
                    s_error = nullptr;
                    p.state.needs_hotkey_reregister = true;
                    commit_now(p.state);
                }
            }
            end_row();
        }
        end_dependent(p);
    }
}

void draw_authenticator(Page& p) {
    section(p, "Authenticator");
    auto& sda = p.s.sda;
    const auto& d = p.d.sda;
    toggle_row(p, "Copy the code when an account is selected", &sda.auto_copy_on_select, d.auto_copy_on_select,
               "Selecting an account on Authenticator puts its current code on the clipboard.",
               "auto copy code select clipboard");
    toggle_row(p, "Show the next code", &sda.show_next_code, d.show_next_code,
               "Shows the code of the next 30 second window under the current one.", "next code preview upcoming");
    toggle_row(p, "Hide the current code", &sda.hide_current_code, d.hide_current_code,
               "Shows dots until you click the code, which reveals it for 5 seconds.",
               "hide code reveal click dots privacy");
    draw_hotkey(p);
}

void draw_confirmations(Page& p) {
    section(p, "Confirmations", "Limits that keep Steam from rate limiting you.");
    auto& c = p.s.confirmations;
    const auto& d = p.d.confirmations;
    number_row(p, "Wait between refreshes of one account", &c.per_account_cooldown_seconds,
               d.per_account_cooldown_seconds, 0, 300, "s",
               "The shortest gap between two refreshes of the same account.", "cooldown refresh rate limit seconds");
    number_row(p, "Gap between accounts in Refresh all", &c.refresh_stagger_ms, d.refresh_stagger_ms, 0, 2000, "ms",
               "Spreads Refresh all out, so Steam does not rate limit your whole IP.",
               "stagger refresh all delay milliseconds rate limit");
    number_row(p, "Skip an account after", &c.permanent_failure_threshold, d.permanent_failure_threshold, 1, 10,
               "errors",
               "Session errors in a row before batch refreshes skip the account. A manual refresh still works "
               "and clears the mark.",
               "permanent failure errors skip threshold session");
    toggle_row(p, "Check for new confirmations in the background", &c.background_poll_enabled,
               d.background_poll_enabled, "Runs Refresh all on a timer, skipping accounts marked as failing.",
               "background poll timer check new");
    if (begin_dependent(p, "##conf-poll", c.background_poll_enabled)) {
        number_row(p, "Check every##conf", &c.background_poll_minutes, d.background_poll_minutes, 1, 120, "min",
                   "How often the background check runs.", "poll interval minutes");
        end_dependent(p);
    }
    toggle_row(p, "Account search above the list", &c.show_account_search, d.show_account_search,
               "Shows a search field over the account filter on Confirmations.", "search field account filter list");
    number_row(p, "Keep the decision history for", &c.audit_retention_days, d.audit_retention_days, 1, 730, "days",
               "Approved and denied confirmations older than this are removed from the history.",
               "audit log history retention days");
}

// Why the warning sits above the switch: the consequence has to be read before turning it on.
void draw_auto_approve(Page& p) {
    section(p, "Auto-approve");
    auto& c = p.s.confirmations;
    const auto& d = p.d.confirmations;
    if (!p.searching) {
        reveal(p);
        facet::InlineAlert("Approved without asking##auto-approve", nullptr,
                           "Confirmations of the kinds below are approved as soon as they are fetched, with no "
                           "chance to check them. Trade confirmations are never approved automatically, and "
                           "nothing is ever denied automatically.",
                           {.tone = facet::Tone::Warning});
    }
    toggle_row(p, "Auto-approve", &c.auto_approve_enabled, d.auto_approve_enabled,
               "Approves the kinds below without asking whenever confirmations are fetched.",
               "auto approve automatic accept confirm market phone");
    if (begin_dependent(p, "##auto-approve-kinds", c.auto_approve_enabled)) {
        toggle_row(p, "Market listings", &c.auto_approve_market, d.auto_approve_market,
                   "Confirms your own Community Market listings as they are created.",
                   "market listing sell auto approve");
        toggle_row(p, "Phone number changes", &c.auto_approve_phone_change, d.auto_approve_phone_change,
                   "Only turn this on while you are changing the number yourself.", "phone number change auto approve");
        end_dependent(p);
    }
}

}  // namespace

// Why this order: the code display is used every day, confirmations limits rarely, and
// auto-approve, the risky switch, sits last on its own.
void draw_steam_guard(Page& p) {
    draw_authenticator(p);
    draw_confirmations(p);
    draw_auto_approve(p);
}

}  // namespace sam::ui::screens::settings
