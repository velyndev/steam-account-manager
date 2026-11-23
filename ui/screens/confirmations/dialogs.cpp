#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

#include <facet/core/layout.h>
#include <facet/core/scale.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/inputs.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/selection.h>

#include "ui/kit/feedback.hpp"
#include "ui/kit/format.hpp"
#include "ui/kit/navigation.hpp"
#include "ui/screens/confirmations/view.hpp"

namespace sam::ui::screens::confirmations_view {

using namespace confirmations_detail;

namespace {

struct TypeWords {
    sda::ConfirmationType type;
    const char* one;
    const char* many;
};

constexpr TypeWords kTypeWords[] = {
    {sda::ConfirmationType::Trade, "trade", "trades"},
    {sda::ConfirmationType::MarketListing, "market listing", "market listings"},
    {sda::ConfirmationType::PhoneNumberChange, "phone number change", "phone number changes"},
    {sda::ConfirmationType::AccountRecovery, "account recovery", "account recoveries"},
    {sda::ConfirmationType::FeatureOptOut, "feature opt-out", "feature opt-outs"},
    {sda::ConfirmationType::Unknown, "other confirmation", "other confirmations"},
};

const TypeWords& words_of(sda::ConfirmationType type) {
    for (const TypeWords& w : kTypeWords) {
        if (w.type == type) return w;
    }
    return kTypeWords[5];
}

// "2 trades, 1 market listing and 1 phone number change".
std::string breakdown(const std::vector<Pick>& picks) {
    std::vector<std::string> parts;
    for (const TypeWords& w : kTypeWords) {
        const auto n = std::count_if(picks.begin(), picks.end(), [&](const Pick& p) { return p.conf.type == w.type; });
        if (n == 0) continue;
        parts.push_back(std::to_string(n) + " " + (n == 1 ? w.one : w.many));
    }
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += i + 1 == parts.size() ? " and " : ", ";
        out += parts[i];
    }
    return out;
}

float dialog_width(float wanted) {
    const float room = ImGui::GetMainViewport()->Size.x / facet::Scale() - facet::T().spacing.xxl * 2.0F;
    return std::max(320.0F, std::min(wanted, room));
}

float dialog_table_height(float wanted) {
    const float room = ImGui::GetMainViewport()->Size.y / facet::Scale() - 300.0F;
    return std::max(160.0F, std::min(wanted, room));
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const char* source_text(sda::AuditSource source) {
    switch (source) {
        case sda::AuditSource::UserBulk: return "You, in bulk";
        case sda::AuditSource::Auto: return "Auto-approve";
        case sda::AuditSource::UserSingle:
        default: return "You";
    }
}

struct OutcomeLook {
    const char* title;
    facet::Tone tone;
    const char* icon;
    const char* explanation;
};

OutcomeLook look_of(ScanOutcome outcome) {
    switch (outcome) {
        case ScanOutcome::Linked:
            return {"Linked", facet::Tone::Success, "check", "Its identity secret and device id were added to the account."};
        case ScanOutcome::Encrypted:
            return {"Encrypted", facet::Tone::Warning, "lock",
                    "The file is encrypted. Decrypt it in the Steam Desktop Authenticator first."};
        case ScanOutcome::Ambiguous:
            return {"Matches several accounts", facet::Tone::Warning, "warning",
                    "The file name fits more than one account, so it was left alone."};
        case ScanOutcome::NoMatch:
            return {"Other account", facet::Tone::Warning, "warning",
                    "The file name fits this account, but its content belongs to another one."};
        case ScanOutcome::MissingSecrets:
            return {"No secrets", facet::Tone::Warning, "warning",
                    "The content matches, but it has no identity secret or device id."};
        case ScanOutcome::Unreadable:
            return {"Unreadable", facet::Tone::Destructive, "error", "The file is not a valid maFile."};
        case ScanOutcome::NotNeeded:
        default:
            return {"Not needed", facet::Tone::Neutral, "info",
                    "No account that is missing maFile data matches this file name."};
    }
}

int outcome_rank(ScanOutcome outcome) {
    switch (outcome) {
        case ScanOutcome::Linked: return 0;
        case ScanOutcome::NotNeeded: return 2;
        default: return 1;
    }
}

}  // namespace

static std::string warning_for(sda::ConfirmationType type) {
    if (type == sda::ConfirmationType::AccountRecovery) {
        return "Approving an account recovery lets the device that asked for it take over this account. "
               "Approve it only if you asked for it yourself.";
    }
    return "Approving a phone number change moves this account's Steam Guard phone number. Approve it only if "
           "you are changing the number yourself.";
}

void ask_decision(app::AppState& state, std::vector<Pick> picks, bool allow) {
    if (picks.empty()) return;
    if (allow && picks.size() == 1 && !risky(picks.front().conf.type)) {
        respond(state, picks, true);
        return;
    }
    Decision d;
    d.allow = allow;
    if (picks.size() == 1) {
        const Pick& p = picks.front();
        const std::string what = lower(type_title(p.conf.type));
        d.title = (allow ? "Approve this " : "Deny this ") + what + "?";
        // A headline that only repeats the kind ("Account recovery") is not quoted again.
        const bool plain = lower(p.conf.headline) == what;
        d.message = (plain ? "On " + account_name(state, p.account_id)
                           : "\"" + p.conf.headline + "\" on " + account_name(state, p.account_id)) +
                    (plain && !p.conf.summary.empty() ? ": " + p.conf.summary : std::string()) + "." +
                    (allow ? std::string() : std::string(" Steam cancels what you deny."));
        if (allow) d.warning = warning_for(p.conf.type);
        d.confirm_label = (allow ? "Approve " : "Deny ") + what;
    } else {
        const int n = static_cast<int>(picks.size());
        std::vector<std::string> accounts;
        std::unordered_map<std::string, int> per_account;
        for (const Pick& p : picks) {
            if (per_account[p.account_id]++ == 0) accounts.push_back(p.account_id);
        }
        const std::string where = accounts.size() == 1 ? account_name(state, accounts.front())
                                                       : kit::format_count(static_cast<std::int64_t>(accounts.size())) +
                                                             " accounts";
        d.title = (allow ? "Approve " : "Deny ") + kit::format_count(n) + " confirmations?";
        if (allow) {
            std::vector<Pick> safe;
            std::vector<Pick> careful;
            for (const Pick& p : picks) (risky(p.conf.type) ? careful : safe).push_back(p);
            d.risky_count = static_cast<int>(careful.size());
            d.message = (safe.empty() ? std::string("Nothing routine is selected") : breakdown(safe)) + " on " + where +
                        ". Steam carries approvals out right away.";
            if (!careful.empty()) {
                d.warning = breakdown(careful) +
                            (careful.size() == 1 ? " is" : " are") +
                            " left out: approving one can hand the account to whoever asked for it.";
                d.optin_label = "Also approve " + breakdown(careful);
            }
        } else {
            d.message = breakdown(picks) + " on " + where + ". Steam cancels what you deny.";
        }
        const int cap = state.settings.confirmations.bulk_size_cap;
        const bool batched =
            cap > 0 && std::any_of(per_account.begin(), per_account.end(), [&](const auto& e) { return e.second > cap; });
        if (batched) d.message += " Accounts with more than " + kit::format_count(cap) + " are sent in batches.";
        d.confirm_label = "Deny " + kit::format_count(n);
    }
    d.picks = std::move(picks);
    d.open = true;
    view().decision = std::move(d);
}

namespace {

void finish(app::AppState& state, ViewState& v, const std::vector<Pick>& picks, bool allow) {
    for (const Pick& p : picks) v.selection.SetItemSelected(row_key(p.account_id, p.conf.id), false);
    respond(state, picks, allow);
}

}  // namespace

// Why approvals get their own dialog: a risky kind shows its warning before the decision, and a
// bulk approve says what it leaves out and lets the user include it on purpose.
void draw_decision(app::AppState& state, ViewState& v) {
    const facet::Theme& t = facet::T();
    Decision& d = v.decision;
    const std::string title = d.title + "###conf-decision";
    if (!d.allow) {
        const facet::DialogResult result =
            kit::destructive_dialog(title.c_str(), &d.open, d.message.c_str(), d.confirm_label.c_str());
        if (result == facet::DialogResult::Confirmed) finish(state, v, d.picks, false);
        if (result != facet::DialogResult::None) d = Decision{};
        return;
    }
    if (!facet::BeginModal(title.c_str(), &d.open, {.width = dialog_width(460.0F)})) {
        if (!d.picks.empty() && !d.open) d = Decision{};
        return;
    }
    std::vector<Pick> chosen;
    for (const Pick& p : d.picks) {
        if (d.include_risky || !risky(p.conf.type) || d.picks.size() == 1) chosen.push_back(p);
    }
    facet::StyledText(d.message.c_str(), {.wrap = true});
    if (!d.warning.empty()) {
        ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));
        facet::InlineAlert("##risk", nullptr, d.warning.c_str(), {.tone = facet::Tone::Warning});
    }
    if (!d.optin_label.empty()) {
        ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));
        facet::Checkbox(d.optin_label.c_str(), &d.include_risky);
    }
    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.lg)));
    const std::string confirm = d.picks.size() == 1
                                    ? d.confirm_label
                                    : "Approve " + kit::format_count(static_cast<std::int64_t>(chosen.size()));
    bool done = false;
    facet::BeginStack("##decision-actions", {.orientation = facet::Orientation::Horizontal,
                                             .width = facet::AvailableWidthUnits()});
    facet::StackSpacer();
    if (facet::Button("Cancel")) done = true;
    ImGui::BeginDisabled(chosen.empty());
    if (facet::Button((confirm + "###confirm").c_str(),
                      {.variant = facet::ButtonVariant::Primary, .icon = facet::Icon("check")})) {
        finish(state, v, chosen, true);
        done = true;
    }
    ImGui::EndDisabled();
    facet::EndStack();
    facet::EndModal();
    if (done) d = Decision{};
}

// Why a modal holding a filterable table: the log is looked up now and then, by account or by
// decision, and closes back onto the pending list it explains.
void draw_history(app::AppState& state, ViewState& v) {
    const facet::Theme& t = facet::T();
    if (!facet::BeginModal("Confirmation history", &v.history_open, {.width = dialog_width(800.0F)})) return;

    facet::SearchField("##history-search", &v.history_query, {.placeholder = "Search account or details", .width = 240.0F});
    ImGui::SameLine(0.0F, facet::Px(t.spacing.sm));
    static const char* const kActions[] = {"All decisions", "Approved", "Denied"};
    facet::SegmentedControl("##history-action", &v.history_action, kActions);
    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));

    const auto& entries = state.conf_audit.entries();
    const std::string query = lower(v.history_query);
    struct Line {
        const sda::AuditEntry* entry;
        std::string account;
    };
    std::vector<Line> lines;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (v.history_action == 1 && !it->allow) continue;
        if (v.history_action == 2 && it->allow) continue;
        std::string name = account_name(state, it->account_id, it->account_login);
        if (!query.empty()) {
            const bool hit = lower(name).find(query) != std::string::npos ||
                             lower(it->headline).find(query) != std::string::npos ||
                             lower(type_title(it->conf_type)).find(query) != std::string::npos;
            if (!hit) continue;
        }
        lines.push_back({&*it, std::move(name)});
    }

    static const facet::DataTableColumn kColumns[] = {
        {.label = "Time", .width = 104.0F},
        {.label = "Account", .width = 112.0F},
        {.label = "Type", .width = 84.0F},
        {.label = "Decision", .width = 96.0F},
        {.label = "By", .width = 84.0F},
        {.label = "Details"},
    };
    const char* empty = entries.empty() ? "No decision has been recorded yet" : "No entry matches the filters";
    if (facet::BeginDataTable("##history-table", kColumns, static_cast<int>(lines.size()), nullptr,
                              {.emptyText = empty, .height = dialog_table_height(360.0F)})) {
        facet::RowRange rows;
        while (facet::NextVisibleRows(&rows)) {
            for (int i = rows.first; i < rows.last; ++i) {
                const Line& line = lines[static_cast<std::size_t>(i)];
                const sda::AuditEntry& e = *line.entry;
                ImGui::PushID(i);
                (void)facet::DataTableRow(i);
                ImGui::TableNextColumn();
                const std::string when = date_time_text(e.unix_time);
                facet::StyledText(when.c_str(), {.secondary = true});
                ImGui::TableNextColumn();
                facet::TruncatedText(line.account.c_str());
                ImGui::TableNextColumn();
                facet::Badge(type_title(e.conf_type, true), {.icon = type_icon(e.conf_type)});
                ImGui::TableNextColumn();
                facet::StatusPill(e.allow ? "Approved" : "Denied",
                                  {.tone = e.allow ? facet::Tone::Success : facet::Tone::Neutral,
                                   .icon = facet::Icon(e.allow ? "check" : "close"),
                                   .size = facet::ControlSize::Small});
                ImGui::TableNextColumn();
                facet::StyledText(source_text(e.source), {.secondary = true});
                ImGui::TableNextColumn();
                facet::TruncatedText(e.headline.c_str());
                ImGui::PopID();
            }
        }
        facet::EndDataTable();
    }

    ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));
    char retention[128];
    std::snprintf(retention, sizeof(retention), "%zu of %zu entries shown. Entries older than %d days are removed.",
                  lines.size(), entries.size(), state.settings.confirmations.audit_retention_days);
    facet::StyledText(retention, {.style = facet::TextStyle::Footnote, .secondary = true});
    ImGui::SameLine(0.0F, facet::Px(t.spacing.sm));
    if (facet::Link("Change retention", {.size = facet::ControlSize::Small})) {
        v.history_open = false;
        kit::open_settings(state, SettingsCategory::SteamGuard);
    }
    facet::EndModal();
}

// Why a table: a scan touches many files, and the user needs to see per file what was linked
// to which account and why the rest was left alone.
void draw_scan_result(app::AppState& state, ViewState& v) {
    const facet::Theme& t = facet::T();
    if (!facet::BeginModal("maFile scan", &v.scan_open, {.width = dialog_width(640.0F)})) return;
    ScanResult r;
    {
        std::lock_guard lk(g_mtx);
        r = g_last_scan;
    }
    int still_missing = 0;
    for (const core::Account& a : state.vault.accounts) {
        if (needs_mafile_data(a)) ++still_missing;
    }

    if (r.targeted == 0) {
        (void)facet::EmptyState("No account is missing maFile data",
                                {.message = "Every account with an authenticator already has its identity secret "
                                            "and device id, so there was nothing to link.",
                                 .icon = facet::Icon("success")});
    } else {
        char line[128];
        std::snprintf(line, sizeof(line), "Linked %d of %d %s that %s missing maFile data.", r.linked, r.targeted,
                      r.targeted == 1 ? "account" : "accounts", r.targeted == 1 ? "was" : "were");
        facet::StyledText(line, {.wrap = true});
        ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.sm)));

        std::stable_sort(r.entries.begin(), r.entries.end(), [](const ScanEntry& a, const ScanEntry& b) {
            return outcome_rank(a.outcome) < outcome_rank(b.outcome);
        });
        static const facet::DataTableColumn kColumns[] = {
            {.label = "File"},
            {.label = "Account", .width = 160.0F},
            {.label = "Result", .width = 200.0F},
        };
        const float rows_h =
            t.table.headerHeight + t.list.rowHeight * static_cast<float>(std::max<std::size_t>(r.entries.size(), 3)) +
            t.spacing.xs * 2.0F + 1.0F;
        if (facet::BeginDataTable("##scan-table", kColumns, static_cast<int>(r.entries.size()), nullptr,
                                  {.emptyText = "The folder holds no .maFile files",
                                   .height = dialog_table_height(std::min(rows_h, 320.0F))})) {
            facet::RowRange rows;
            while (facet::NextVisibleRows(&rows)) {
                for (int i = rows.first; i < rows.last; ++i) {
                    const ScanEntry& e = r.entries[static_cast<std::size_t>(i)];
                    ImGui::PushID(i);
                    (void)facet::DataTableRow(i);
                    ImGui::TableNextColumn();
                    facet::TruncatedText(e.file.c_str(), {.mode = facet::TruncateMode::Middle});
                    ImGui::TableNextColumn();
                    if (e.account_id.empty()) {
                        facet::StyledText("No match", {.secondary = true});
                    } else {
                        const std::string name = account_name(state, e.account_id);
                        facet::TruncatedText(name.c_str());
                    }
                    ImGui::TableNextColumn();
                    const OutcomeLook look = look_of(e.outcome);
                    facet::StatusPill(look.title,
                                      {.tone = look.tone, .icon = facet::Icon(look.icon), .size = facet::ControlSize::Small});
                    facet::ItemTooltip("%s", look.explanation);
                    ImGui::PopID();
                }
            }
            facet::EndDataTable();
        }
    }

    if (still_missing > 0) {
        ImGui::Dummy(ImVec2(0.0F, facet::Px(t.spacing.md)));
        char missing[96];
        std::snprintf(missing, sizeof(missing), "%d %s still missing maFile data.", still_missing,
                      still_missing == 1 ? "account is" : "accounts are");
        facet::StyledText(missing, {.style = facet::TextStyle::Footnote, .secondary = true});
        ImGui::SameLine(0.0F, facet::Px(t.spacing.sm));
        if (facet::Link("Scan another folder", {.size = facet::ControlSize::Small})) {
            v.scan_open = false;
            scan_folder_for_mafiles(state);
        }
    }
    facet::EndModal();
}

}  // namespace sam::ui::screens::confirmations_view
