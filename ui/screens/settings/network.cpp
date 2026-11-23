// Settings > Network: the proxy mode, the single proxy, and the per-account proxy table.

#include <map>
#include <string>
#include <string_view>

#include <imgui.h>

#include <facet/core/text.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/containers.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/selection.h>

#include "app/job_pump.hpp"
#include "core/crypto/secure_string.hpp"
#include "core/http/client.hpp"
#include "ui/kit/account.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/kit/row.hpp"
#include "ui/screens/settings/capture_hooks.hpp"
#include "ui/screens/settings/categories.hpp"

namespace sam::ui::screens::settings {

namespace {

struct TestResult {
    bool testing = false;
    bool ok = false;
    std::string text;
};

struct NetworkState {
    std::map<std::string, TestResult> tests;  // "single" or an account id
    std::string filter;                       // the per-account table's account filter
    std::string editing_id;                   // the account whose proxy the editor shows
    std::string loaded_for;                   // the account the editor buffer was loaded from
    std::string buffer;                       // the editor's proxy text
};

NetworkState& network_state() {
    static NetworkState s;
    return s;
}

}  // namespace

void reset_network_transient() {
    network_state().editing_id.clear();
    network_state().loaded_for.clear();
    network_state().buffer.clear();
}

namespace {

// Asks api.ipify.org through `proxy` on the job pump and stores the exit IP or the error
// under `key`. The UI thread gets the result through post_ui_callback.
void run_proxy_test(app::AppState& state, const std::string& key, const std::string& proxy) {
    TestResult& r = network_state().tests[key];
    r.testing = true;
    r.ok = false;
    r.text = "Testing";
    app::AppState* st = &state;
    app::job_pump::submit([st, key, proxy] {
        http::ScopedProxyOverride ov(proxy);
        http::Request req;
        req.url = "https://api.ipify.org";
        req.timeout_seconds = 20;
        req.max_retries = 1;
        const auto resp = http::request(req);
        TestResult result;
        if (resp.status == 200 && !resp.body.empty()) {
            result.ok = true;
            result.text = "Works, exit IP " + resp.body;
        } else if (resp.transport_error) {
            result.text = "Failed: " + resp.error_message;
        } else {
            result.text = "Failed with HTTP " + std::to_string(resp.status);
        }
        st->post_ui_callback([key, result] { network_state().tests[key] = result; });
    });
}

// The result of the last test under `key`: in full, or as "Works" / "Failed" with the full text
// as tooltip where a table cell has no room.
bool test_running(const std::string& key) {
    const auto it = network_state().tests.find(key);
    return it != network_state().tests.end() && it->second.testing;
}

void test_result_line(const std::string& key, bool compact = false) {
    const auto it = network_state().tests.find(key);
    if (it == network_state().tests.end()) return;
    const TestResult& r = it->second;
    if (r.testing) {
        facet::StatusPill(compact ? "Testing" : "Testing the proxy", {.liveDot = true, .size = facet::ControlSize::Small});
    } else {
        facet::StatusPill(compact ? (r.ok ? "Works" : "Failed") : r.text.c_str(), {.tone = r.ok ? facet::Tone::Success : facet::Tone::Destructive,
                                           .icon = facet::Icon(r.ok ? "success" : "error"),
                                           .size = facet::ControlSize::Small});
        if (compact) facet::ItemTooltip("%s", r.text.c_str());
    }
}

// "socks5://host:1080" from "socks5://user:pass@host:1080": the table never shows credentials.
std::string proxy_summary(std::string_view url) {
    if (url.empty()) return "Direct connection";
    std::string_view scheme;
    if (const auto pos = url.find("://"); pos != std::string_view::npos) {
        scheme = url.substr(0, pos + 3);
        url.remove_prefix(pos + 3);
    }
    bool creds = false;
    if (const auto at = url.rfind('@'); at != std::string_view::npos) {
        url.remove_prefix(at + 1);
        creds = true;
    }
    std::string out = std::string(scheme) + std::string(url);
    if (creds) out += " (with sign-in)";
    return out;
}

constexpr const char* kProxyFormat = "socks5://user:pass@host:1080, or http:// and https://";

void draw_mode(Page& p) {
    app::AppState& state = p.state;
    section(p, "Proxy", "Covers the app's own web traffic, not the Steam client you launch.");
    if (begin_row(p, "Proxy mode",
                  {.description = "One proxy for every account, or one per account. Accounts without their own "
                                  "proxy connect directly.",
                   .keywords = "proxy socks http mode network vpn ip",
                   .modified = p.s.proxy_mode != p.d.proxy_mode,
                   .immediate = true})) {
        static const char* const kModes[] = {"Direct", "One proxy", "Per account"};
        int index = static_cast<int>(p.s.proxy_mode);
        if (facet::SegmentedControl("##value", &index, kModes)) {
            p.s.proxy_mode = static_cast<app::ProxyMode>(index);
            state.sync_proxy_policy();
            commit_now(state);
        }
        end_row();
    }
    if (p.s.proxy_mode != app::ProxyMode::Single) return;
    if (begin_dependent(p, "##single", true)) {
        if (begin_row(p, "Proxy address",
                      {.description = kProxyFormat, .keywords = "proxy url address host port socks5", .immediate = true})) {
            facet::SecureField("##value", &p.s.single_proxy, {.placeholder = "socks5://host:1080", .width = cap(360.0F)});
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                state.sync_proxy_policy();
                commit_now(state);
            }
            const bool testing = test_running("single");
            ImGui::BeginDisabled(testing || p.s.single_proxy.empty());
            if (fit_button("Test the proxy", {.loading = testing})) {
                run_proxy_test(state, "single", p.s.single_proxy);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            test_result_line("single");
            end_row();
        }
        end_dependent(p);
    }
}

// The proxy editor of one account, in a popover anchored to that account's row: the field, the
// test with its result, and Cancel, Test and Save proxy (the one primary action).
void draw_editor_body(Page& p, NetworkState& n, core::Account& acc, bool* open) {
    if (n.loaded_for != n.editing_id) {
        n.buffer.assign(acc.proxy.begin(), acc.proxy.end());
        n.loaded_for = n.editing_id;
    }
    const std::string name = kit::display_name(p.state, acc);
    facet::Text(facet::TextStyle::Headline, "Proxy of %s", name.c_str());
    facet::SecureField("##account-proxy", &n.buffer,
                       {.placeholder = "Empty for a direct connection", .width = 360.0F, .helpText = kProxyFormat});
    test_result_line(n.editing_id);
    ImGui::Spacing();
    const bool testing = test_running(n.editing_id);
    const bool changed = std::string_view(n.buffer) != crypto::view(acc.proxy);
    align_trailing(fit_width("Cancel") + fit_width("Test the proxy") + fit_width("Save proxy") +
                   2.0F * facet::T().spacing.sm);
    if (fit_button("Cancel")) *open = false;
    ImGui::SameLine();
    ImGui::BeginDisabled(testing || n.buffer.empty());
    if (fit_button("Test the proxy", {.loading = testing})) run_proxy_test(p.state, n.editing_id, n.buffer);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!changed);
    if (fit_button("Save proxy", {.variant = facet::ButtonVariant::Primary})) {
        acc.proxy = crypto::make_secure(n.buffer);
        p.state.vault_dirty = true;
        p.state.save_vault_if_dirty();
        kit::show_toast(p.state, "Proxy saved for " + name, {.tone = widgets::ToastTone::Success});
        *open = false;
    }
    ImGui::EndDisabled();
}

// Why a table with an anchored editor: the table shows every account's proxy at once (never the
// credentials), and Edit opens the editor on the row it belongs to, which stays highlighted.
void draw_per_account(Page& p) {
    if (p.s.proxy_mode != app::ProxyMode::PerAccount) return;
    if (!block(p, "Per-account proxies", "Each account's own proxy, with a test.", "per account proxy table list")) {
        return;
    }
    NetworkState& n = network_state();
    facet::SearchField("##proxy-filter", &n.filter, {.placeholder = "Filter accounts", .width = cap(280.0F)});

    std::vector<int> rows;
    for (int i = 0; i < static_cast<int>(p.state.vault.accounts.size()); ++i) {
        const core::Account& a = p.state.vault.accounts[static_cast<std::size_t>(i)];
        const std::string hay = kit::display_name(p.state, a) + " " + kit::login_text(p.state, a);
        if (facet::PassesFilter(n.filter, {}, hay)) rows.push_back(i);
    }
    static ImGuiSelectionBasicStorage selection;
    selection.Clear();
    int editing_row = -1;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (p.state.vault.accounts[static_cast<std::size_t>(rows[r])].id == n.editing_id) editing_row = static_cast<int>(r);
    }
    if (editing_row >= 0) selection.SetItemSelected(selection.AdapterIndexToStorageId(&selection, editing_row), true);

    static const facet::DataTableColumn kColumns[] = {
        {.label = "Account"}, {.label = "Proxy"}, {.label = "##edit", .width = 72.0F}};
    const char* empty = p.state.vault.accounts.empty() ? "This vault has no accounts yet." : "No account matches the filter.";
    const float height = rows.empty() ? 120.0F : 40.0F + 36.0F * static_cast<float>(std::min<std::size_t>(rows.size(), 8));
    std::string open_for;
    if (facet::BeginDataTable("##proxies", kColumns, static_cast<int>(rows.size()), &selection,
                              {.singleSelect = true, .emptyText = empty, .height = height})) {
        facet::RowRange range;
        while (facet::NextVisibleRows(&range)) {
            for (int r = range.first; r < range.last; ++r) {
                core::Account& a = p.state.vault.accounts[static_cast<std::size_t>(rows[static_cast<std::size_t>(r)])];
                ImGui::PushID(a.id.c_str());
                if (facet::DataTableRow(r)) open_for = a.id;
                ImGui::TableNextColumn();
                {
                    kit::RowCell cell;
                    const std::string who = kit::display_name(p.state, a);
                    facet::TruncatedText(who.c_str());
                }
                ImGui::TableNextColumn();
                {
                    kit::RowCell cell;
                    const std::string summary = proxy_summary(crypto::view(a.proxy));
                    facet::TruncatedText(summary.c_str());
                }
                ImGui::TableNextColumn();
                {
                    kit::RowCell cell;
                    if (fit_button("Edit", {.size = facet::ControlSize::Small})) open_for = a.id;
                }
                if (a.id == n.editing_id) {
                    bool open = true;
                    if (facet::BeginPopover("##proxy-editor", &open, {.placement = facet::PopoverPlacement::Below})) {
                        draw_editor_body(p, n, a, &open);
                        facet::EndPopover();
                    }
                    if (!open) n.editing_id.clear();
                }
                ImGui::PopID();
            }
        }
        facet::EndDataTable();
    }
    // A click on another row selects it: open the editor there.
    for (std::size_t r = 0; r < rows.size() && open_for.empty(); ++r) {
        const ImGuiID key = selection.AdapterIndexToStorageId(&selection, static_cast<int>(r));
        if (selection.Contains(key) && static_cast<int>(r) != editing_row) {
            open_for = p.state.vault.accounts[static_cast<std::size_t>(rows[r])].id;
        }
    }
    if (!open_for.empty() && open_for != n.editing_id) {
        n.editing_id = open_for;
        n.loaded_for.clear();
    }
}

}  // namespace

#ifdef SAM_UI_CAPTURE
void capture::edit_proxy_of(const std::string& account_id) {
    network_state().editing_id = account_id;
}
#endif

void draw_network(Page& p) {
    draw_mode(p);
    draw_per_account(p);
}

}  // namespace sam::ui::screens::settings
