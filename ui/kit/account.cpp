#include "ui/kit/account.hpp"

#include <algorithm>
#include <vector>

#include <imgui.h>

#include <facet/core/image.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>
#include <facet/widgets/buttons.h>
#include <facet/widgets/display.h>
#include <facet/widgets/layout.h>
#include <facet/widgets/overlays.h>
#include <facet/widgets/lists.h>
#include <facet/widgets/media.h>

#include "ui/kit/row_line.hpp"
#include "ui/widgets/avatar_cache.hpp"

namespace sam::ui::kit {

bool login_hidden(const app::AppState& state, const core::Account& account) {
    return state.settings.privacy_mode && state.revealed_logins.find(account.id) == state.revealed_logins.end();
}

std::string login_text(const app::AppState& state, const core::Account& account) {
    return login_hidden(state, account) ? std::string(kHiddenLogin) : account.login;
}

std::string display_name(const app::AppState& state, const core::Account& account) {
    if (!account.web.persona_name.empty()) return account.web.persona_name;
    return login_text(state, account);
}

bool login(app::AppState& state, const core::Account& account, const LoginOptions& options) {
    ImGui::PushID(account.id.c_str());
    bool changed = false;
    const facet::ControlSize size =
        options.style == facet::TextStyle::Body ? facet::ControlSize::Regular : facet::ControlSize::Small;
    if (!state.settings.privacy_mode) {
        facet::StyledText(account.login.c_str(), {.style = options.style, .secondary = options.secondary});
    } else if (login_hidden(state, account)) {
        if (facet::Button(kHiddenLogin, {.variant = facet::ButtonVariant::Plain,
                                         .icon = facet::Icon("eye"),
                                         .size = size})) {
            state.revealed_logins.insert(account.id);
            changed = true;
        }
        facet::ItemTooltip("Show this login until the vault locks");
    } else {
        ImGui::BeginGroup();
        facet::StyledText(account.login.c_str(), {.style = options.style, .secondary = options.secondary});
        ImGui::SameLine(0.0F, facet::Px(facet::T().spacing.xs));
        if (facet::IconButton("Hide this login again", facet::Icon("eye-off"), {.size = size})) {
            state.revealed_logins.erase(account.id);
            changed = true;
        }
        ImGui::EndGroup();
    }
    ImGui::PopID();
    return changed;
}

// Why an Image with a half-size radius next to facet::Avatar: Avatar takes Facet image
// handles only and the avatar cache holds app textures, so a ready picture is drawn as a round
// image of the same diameter and the initials come from Avatar itself.
void avatar(const app::AppState& state, const core::Account& account, const AvatarOptions& options) {
    const float diameter_units =
        options.diameter > 0.0F ? options.diameter : facet::T().metrics.controlHeight;
    ID3D11ShaderResourceView* picture = nullptr;
    if (state.settings.show_avatars && !account.web.avatar_url_full.empty()) {
        picture = widgets::avatar_for(account.web.avatar_url_full);
    }
    if (picture != nullptr) {
        const float d = facet::Px(diameter_units);
        facet::Image(picture, ImVec2(d, d),
                     {.fit = facet::ImageFit::Cover, .radius = d * 0.5F, .showStates = false});
        return;
    }
    const std::string name = display_name(state, account);
    ImGui::PushID(account.id.c_str());
    facet::Avatar((name + "##avatar").c_str(), facet::ImageHandle{}, {.diameter = diameter_units});
    ImGui::PopID();
}

// Why a group with SameLine: the row has to sit inside list rows and table cells whose height
// the caller owns, so it is laid out as stock items without a container of its own.
void account_row(const app::AppState& state, const core::Account& account, const AccountRowOptions& options) {
    const facet::Theme& t = facet::T();
    const bool two_lines = options.layout == AccountRowOptions::Layout::TwoLines;
    const std::string name = display_name(state, account);
    const bool has_persona = !account.web.persona_name.empty();
    std::string second;
    // A hidden login is left out instead of printing "Hidden login" on every row; the reveal
    // lives where one account is shown (kit::login).
    if (options.show_login && has_persona && !login_hidden(state, account)) second = login_text(state, account);
    if (options.detail != nullptr && options.detail[0] != '\0') {
        if (!second.empty()) second += " \xC2\xB7 ";
        second += options.detail;
    }

    const float line_body = facet::MeasureText(facet::TextStyle::Body, "Ag").y;
    const float line_foot = facet::MeasureText(facet::TextStyle::Footnote, "Ag").y;
    const float text_h = two_lines && !second.empty() ? line_body + line_foot : line_body;
    const float diameter_px = two_lines ? line_body + line_foot : line_body;

    ImGui::PushID(account.id.c_str());
    {
        detail::RowLine line("##account-row", std::max(text_h, diameter_px), options.row_height, facet::Gap::Item);
        if (options.show_avatar) {
            avatar(state, account, {.diameter = diameter_px / facet::Scale()});
            line.next();
        }
        if (two_lines) {
            facet::BeginStack("##names", {.gap = facet::Gap::None});
            facet::TruncatedText(name.c_str(), {.style = facet::TextStyle::Body});
            if (!second.empty()) {
                facet::TruncatedText(second.c_str(), {.style = facet::TextStyle::Footnote, .secondary = true});
            }
            facet::EndStack();
        } else {
            facet::StyledText(name.c_str(), {.style = facet::TextStyle::Body});
            if (!second.empty()) {
                line.next();
                facet::TruncatedText(second.c_str(), {.style = facet::TextStyle::Body, .secondary = true});
            }
        }
    }
    ImGui::PopID();
    (void)t;
}

bool has_authenticator(const core::Account& account) {
    return account.sda.has_value();
}

bool has_steam_id(const core::Account& account) {
    return account.steam_id_64 != 0;
}

namespace {

struct PickerData {
    std::vector<const core::Account*> accounts;
    std::vector<std::string> labels;
    std::vector<std::string> details;
};

bool picker_item(void* user, int index, facet::ChoiceItem* out) {
    const auto* data = static_cast<const PickerData*>(user);
    if (index < 0 || index >= static_cast<int>(data->accounts.size())) return false;
    out->label = data->labels[static_cast<std::size_t>(index)].c_str();
    const std::string& detail = data->details[static_cast<std::size_t>(index)];
    out->detail = detail.empty() ? nullptr : detail.c_str();
    return true;
}

}  // namespace

bool account_picker(const char* label, app::AppState& state, std::string* account_id,
                    const AccountPickerOptions& options) {
    if (account_id == nullptr) return false;
    PickerData data;
    int current = -1;
    for (const core::Account& a : state.vault.accounts) {
        if (options.filter != nullptr && !options.filter(a)) continue;
        if (a.id == *account_id) current = static_cast<int>(data.accounts.size());
        data.accounts.push_back(&a);
        data.labels.push_back(display_name(state, a));
        data.details.push_back(a.web.persona_name.empty() || login_hidden(state, a) ? std::string() : a.login);
    }
    const bool written = facet::Dropdown(
        label, &current, static_cast<int>(data.accounts.size()), &picker_item, &data,
        {.placeholder = options.placeholder,
         .searchable = true,
         .emptyText = options.empty_text != nullptr ? options.empty_text : "No accounts",
         .width = options.width});
    if (written && current >= 0 && current < static_cast<int>(data.accounts.size())) {
        *account_id = data.accounts[static_cast<std::size_t>(current)]->id;
        return true;
    }
    return false;
}

}  // namespace sam::ui::kit
