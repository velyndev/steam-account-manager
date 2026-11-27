#ifdef SAM_UI_CAPTURE

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include "app/app_paths.hpp"
#include "app/app_state.hpp"
#include "app/ui_capture.hpp"
#include "app/ui_capture_internal.hpp"
#include "app/vault_registry.hpp"
#include "core/account_store/store.hpp"
#include "core/crypto/base64.hpp"
#include "core/crypto/secure_string.hpp"
#include "core/sda/confirmation.hpp"
#include "core/trade/models.hpp"
#include "platform/paths.hpp"
#include "ui/screens/confirmations_detail.hpp"
#include "ui/screens/trade_offers_detail.hpp"

namespace sam::app::ui_capture {

namespace {

constexpr int kDemoAccountCount = 30;
constexpr std::uint64_t kDemoSteamIdBase = 76561190000000100ULL;

struct DemoPersona {
    const char* login;
    const char* persona;
    const wchar_t* display_name;
};

constexpr DemoPersona kPersonas[kDemoAccountCount] = {
    {"demo_falcon", "Falcon Demo", L""},
    {"demo_otter", "Otter Placeholder", L"Main (old)"},
    {"demo_badger", "Badger Sample", L""},
    {"demo_snowfox", "雪狐", L"Snow fox"},
    {"demo_heron", "Heron Testcase", L""},
    {"demo_lynx", "Lynx Fixture", L"Wingman duo"},
    {"demo_marten", "Marten Mock", L""},
    {"demo_starlight", "별빛", L"Starlight"},
    {"demo_pika", "Pika Stand-in", L""},
    {"demo_raven", "Raven Dummy", L""},
    {"demo_quokka", "Quokka Example", L"Storage 1"},
    {"demo_tapir", "Tapir Prototype", L""},
    {"demo_walrus", "Walrus Draft", L"Trader"},
    {"demo_ibis", "Ibis Specimen", L""},
    {"demo_gecko", "Gecko Sandbox", L""},
    {"demo_moose", "Moose Lorem", L"Storage 2"},
    {"demo_civet", "Civet Ipsum", L""},
    {"demo_okapi", "Okapi Testbed", L""},
    {"demo_puffin", "Puffin Mockup", L""},
    {"demo_dingo", "Dingo Template", L"Farm A"},
    {"demo_ferret", "Ferret Preview", L""},
    {"demo_kiwi", "Kiwi Placeholder", L""},
    {"demo_mole", "Mole Sample", L"Farm B"},
    {"demo_newt", "Newt Fixture", L""},
    {"demo_oriole", "Oriole Demo", L""},
    {"demo_panda", "Panda Testcase", L""},
    {"demo_rhea", "Rhea Mock", L"Farm C"},
    {"demo_skink", "Skink Example", L""},
    {"demo_vole", "Vole Draft", L""},
    {"demo_yak", "Yak Specimen", L"Spare"},
};

constexpr const char* kGroupIds[] = {"demo-grp-main", "demo-grp-smurf", "demo-grp-trade",
                                     "demo-grp-farm"};

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string base64url(std::string_view text) {
    std::string out = crypto::base64_encode(text);
    for (char& c : out) {
        if (c == '+') c = '-';
        if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') out.pop_back();
    return out;
}

crypto::SecureString fake_jwt(std::uint64_t steam_id, const char* audience, std::int64_t exp) {
    char payload[256];
    std::snprintf(payload, sizeof(payload),
                  "{\"iss\":\"capture-demo\",\"sub\":\"%llu\",\"aud\":[%s],\"exp\":%lld}",
                  static_cast<unsigned long long>(steam_id), audience, static_cast<long long>(exp));
    const std::string token = base64url("{\"typ\":\"JWT\",\"alg\":\"none\"}") + "." + base64url(payload) +
                              ".capture-demo-signature";
    return crypto::make_secure(token);
}

std::string fake_secret(const char* kind, int index) {
    char raw[24];
    std::snprintf(raw, sizeof(raw), "%s-demo-%02d-xx", kind, index);
    std::string text(raw);
    text.resize(20, 'x');
    return crypto::base64_encode(text);
}

core::Account make_account(int i, std::int64_t now) {
    const DemoPersona& p = kPersonas[i];
    core::Account a;
    a.id = demo_account_id(i);
    a.steam_id_64 = kDemoSteamIdBase + static_cast<std::uint64_t>(i);
    a.login = p.login;
    a.display_name = p.display_name;
    a.created_unix = now - (400 - i * 11) * 86400LL;
    a.last_login_unix = i % 6 == 0 ? 0 : now - (i * 7 + 1) * 3600LL;

    const bool nfa = i % 5 == 4;
    a.is_nfa = nfa;
    if (nfa) {
        a.refresh_token = fake_jwt(a.steam_id_64, "\"client\",\"web\"", now + 120 * 86400LL);
        a.refresh_token_expires = now + 120 * 86400LL;
        a.nfa_status = i % 10 == 9 ? core::NfaTokenStatus::Revoked : core::NfaTokenStatus::Valid;
    } else {
        a.password = crypto::make_secure("capture-demo-account-password");
        a.refresh_token = fake_jwt(a.steam_id_64, "\"mobile\",\"web\"", now + 150 * 86400LL);
        a.refresh_token_expires = now + 150 * 86400LL;
    }
    if (i == 0 || i == 2) {
        a.cm_refresh_token = fake_jwt(a.steam_id_64, "\"client\"", now + 180 * 86400LL);
        a.cm_refresh_token_expires = now + 180 * 86400LL;
        a.cm_status = core::NfaTokenStatus::Valid;
    }

    if (i % 3 != 2) {
        core::SteamGuardAccount g;
        g.shared_secret = fake_secret("shared", i);
        g.identity_secret = fake_secret("identity", i);
        g.secret_1 = fake_secret("secret", i);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "R%05d", 10000 + i * 137);
        g.revocation_code = buf;
        std::snprintf(buf, sizeof(buf), "%llu", 1000000000000000000ULL + static_cast<unsigned long long>(i));
        g.serial_number = buf;
        std::snprintf(buf, sizeof(buf), "android:00000000-0000-4000-8000-0000000000%02d", i);
        g.device_id = buf;
        g.account_name = p.login;
        g.uri = std::string("otpauth://totp/Steam:") + p.login + "?secret=DEMO&issuer=Steam";
        g.server_time = now;
        a.sda = std::move(g);
    }

    if (i % 7 != 6) a.group_id = kGroupIds[i % 4];
    if (i % 2 == 0) a.tag_ids.push_back("demo-tag-prime");
    if (i % 5 == 1) a.tag_ids.push_back("demo-tag-faceit");
    if (i == 10 || i == 15) a.tag_ids.push_back("demo-tag-storage");
    if (i == 1 || i == 18 || i == 23) a.tag_ids.push_back("demo-tag-review");
    a.trust = static_cast<core::TrustLabel>(i % 4);
    a.login_method = i % 3 == 0 ? core::LoginMethod::LaunchCs2 : core::LoginMethod::Normal;

    if (i == 1) a.notes = L"Old main. Keep the inventory, do not trade.";
    if (i == 3) a.notes = L"Bought the operation pass in March.";
    if (i == 10) a.notes = L"Storage units for stickers and cases.";
    if (i == 18) a.notes = L"Ban appeal sent, waiting for an answer.";
    if (i == 22) a.notes = L"Farm account: weekly drop only.";

    a.web.persona_name = p.persona;
    a.web.profile_url = "https://steamcommunity.com/profiles/" + std::to_string(a.steam_id_64);
    a.web.country_code = (i % 3 == 0) ? "DE" : ((i % 3 == 1) ? "US" : "KR");
    a.web.created_unix = now - (3000 - i * 53) * 86400LL;
    a.web.community_visibility_state = 3;
    a.web.profile_state = 1;
    a.web.steam_level = (i * 13) % 90;
    a.web.owned_games_count = 1 + (i * 29) % 240;
    a.web.total_playtime_minutes = 600LL * (i + 1) * 37;
    a.web.last_refreshed_unix = now - 1800;

    a.bans.economy_ban = "none";
    a.bans.last_refreshed_unix = now - 1800;
    switch (i) {
        case 1:
            a.bans.vac_banned = true;
            a.bans.vac_ban_count = 1;
            a.bans.days_since_last_ban = 412;
            break;
        case 5:
            a.bans.game_ban_count = 2;
            a.bans.days_since_last_ban = 37;
            break;
        case 9:
            a.bans.community_banned = true;
            break;
        case 12:
            a.bans.economy_ban = "banned";
            break;
        case 15:
            a.bans.economy_ban = "probation";
            break;
        case 18:
            a.bans.vac_banned = true;
            a.bans.vac_ban_count = 2;
            a.bans.game_ban_count = 1;
            a.bans.days_since_last_ban = 3;
            break;
        default:
            break;
    }
    a.prev_snapshot.vac_ban_count = a.bans.vac_ban_count;
    a.prev_snapshot.game_ban_count = a.bans.game_ban_count;
    a.prev_snapshot.community_banned = a.bans.community_banned;
    a.prev_snapshot.economy_ban = a.bans.economy_ban;
    a.prev_snapshot.snapshot_unix = now - 1800;

    a.cs2.prime_status = i % 2 == 0 || i % 7 == 3;
    a.cs2.prime_confirmed = i % 5 != 0;  // every fifth account shows the estimate
    a.cs2.premier_rating = (i % 6 == 5) ? -1 : 2500 + (i * 1789) % 27000;
    a.cs2.premier_wins = a.cs2.premier_rating > 0 ? 10 + (i * 7) % 180 : -1;
    a.cs2.wingman_rank = (i % 4 == 3) ? -1 : 1 + (i * 5) % 18;
    a.cs2.wingman_wins = a.cs2.wingman_rank > 0 ? 5 + (i * 3) % 90 : -1;
    a.cs2.cs2_player_level = 1 + (i * 11) % 40;
    a.cs2.cs2_player_xp = (i * 977) % 5000;
    a.cs2.vac_live = a.bans.vac_banned;
    if (i == 4) {
        a.cs2.cooldown_expires_unix = now + 3 * 3600 + 25 * 60;
        a.cs2.cooldown_reason = "Abandon";
    } else if (i == 13) {
        a.cs2.cooldown_expires_unix = now + 6 * 86400LL;
        a.cs2.cooldown_reason = "Griefing";
    }
    if (i % 4 == 2) a.cs2.weekly_drop_reset_unix = now + 2 * 86400LL;
    a.cs2.last_refreshed_unix = now - 1800;
    a.cs2.gc_last_pulled_unix = now - 3600;

    if (!nfa && i % 3 == 0) {
        a.funds.total_spend_usd_cents = 1999LL * (i + 1);
        a.funds.currency = "USD";
        a.funds.last_refreshed_unix = now - 86400;
    }
    if (!nfa && i % 4 == 0) {
        a.trade_url = "https://steamcommunity.com/tradeoffer/new/?partner=" +
                      std::to_string(static_cast<std::uint32_t>(a.steam_id_64 & 0xFFFFFFFFULL)) +
                      "&token=Demo" + std::to_string(i);
        a.trade_url_fetched_unix = now - 86400;
    }
    return a;
}

core::Vault make_demo_vault(std::int64_t now) {
    core::Vault v;
    v.groups = {
        {"demo-grp-main", "Main", 0x4F8EF7FFu},
        {"demo-grp-smurf", "Smurfs", 0xE5A13BFFu},
        {"demo-grp-trade", "Trading", 0x3CB878FFu},
        {"demo-grp-farm", "Drop farm", 0xB86BD8FFu},
    };
    v.tags = {
        {"demo-tag-prime", "Prime", 0x3CB878FFu},
        {"demo-tag-faceit", "FACEIT", 0xF26B3AFFu},
        {"demo-tag-storage", "Storage", 0x7C8DA6FFu},
        {"demo-tag-review", "Needs review", 0xE0484EFFu},
    };
    v.accounts.reserve(kDemoAccountCount);
    for (int i = 0; i < kDemoAccountCount; ++i) v.accounts.push_back(make_account(i, now));
    return v;
}

sda::Confirmation make_confirmation(int account, int n, sda::ConfirmationType type,
                                    const char* headline, const char* summary, std::int64_t created) {
    sda::Confirmation c;
    c.id = std::to_string(9000000000LL + account * 100 + n);
    c.nonce = std::to_string(77000000000LL + account * 100 + n);
    c.creator_id = std::to_string(5000000000LL + account * 100 + n);
    c.type = type;
    c.headline = headline;
    c.summary = summary;
    c.creation_unix = created;
    return c;
}

core::trade::InventoryItem make_item(std::uint64_t asset, const char* name, const char* type,
                                     const char* color, bool tradable) {
    core::trade::InventoryItem it;
    it.asset_id = asset;
    it.class_id = asset * 3;
    it.instance_id = 0;
    it.name = name;
    it.market_hash_name = name;
    it.type = type;
    it.name_color = color;
    it.tradable = tradable;
    it.marketable = tradable;
    return it;
}

core::trade::TradeOffer make_offer(const char* id, int partner, bool ours, core::trade::TradeOfferState state,
                                   const char* message, std::int64_t now) {
    core::trade::TradeOffer o;
    o.offer_id = id;
    o.partner_steam_id_64 = kDemoSteamIdBase + static_cast<std::uint64_t>(partner);
    o.partner_account_id = static_cast<std::uint32_t>(o.partner_steam_id_64 & 0xFFFFFFFFULL);
    o.is_our_offer = ours;
    o.state = state;
    o.message = message;
    o.created_unix = now - 5400;
    o.updated_unix = now - 1200;
    o.expiration_unix = now + 13 * 86400LL;
    o.needs_confirmation = state == core::trade::TradeOfferState::CreatedNeedsConfirmation;
    return o;
}

void seed_confirmations(AppState& state, std::int64_t now) {
    using namespace ui::screens::confirmations_detail;
    using T = sda::ConfirmationType;
    std::lock_guard lk(g_mtx);
    g_lists.clear();
    int total = 0;
    const auto put = [&](int account, std::vector<sda::Confirmation> items) {
        total += static_cast<int>(items.size());
        PendingList list;
        list.items = std::move(items);
        g_lists[demo_account_id(account)] = std::move(list);
    };
    put(0, {make_confirmation(0, 1, T::Trade, "Trade with Otter Placeholder",
                              "You will give 2 items and receive 1 item", now - 600),
            make_confirmation(0, 2, T::MarketListing, "Sell - Demo Sticker | Placeholder",
                              "Listed for 0.42 USD", now - 1500)});
    put(3, {make_confirmation(3, 1, T::Trade, "Trade with 별빛", "You will receive 3 items", now - 300)});
    put(6, {make_confirmation(6, 1, T::PhoneNumberChange, "Change phone number",
                              "+49 *** *** 12", now - 7200)});
    put(7, {make_confirmation(7, 1, T::MarketListing, "Sell - Demo Case Key",
                              "Listed for 2.10 USD", now - 900),
            make_confirmation(7, 2, T::MarketListing, "Sell - Demo Graffiti | Lorem",
                              "Listed for 0.03 USD", now - 960),
            make_confirmation(7, 3, T::AccountRecovery, "Account recovery", "Requested from a new device",
                              now - 4000)});
    PendingList refreshing;
    refreshing.refreshing = true;
    g_lists[demo_account_id(10)] = std::move(refreshing);
    PendingList failed;
    failed.last_error = "Steam returned HTTP 429 (rate limited). Try again in a minute.";
    g_lists[demo_account_id(13)] = std::move(failed);
    state.pending_confirmations_count.store(total, std::memory_order_relaxed);
    state.pending_confirmations_loaded.store(true, std::memory_order_relaxed);
}

void seed_trade_offers(AppState& state, std::int64_t now) {
    using namespace ui::screens::trade_offers_detail;
    using S = core::trade::TradeOfferState;
    std::lock_guard lk(g_mtx);
    g_states.clear();
    g_in_flight.clear();

    AccountTradeState a0;
    auto incoming = make_offer("6100000001", 12, false, S::Active, "Swap for the case?", now);
    incoming.items_to_receive = {make_item(31001, "Demo Case", "Base Grade Container", "D2D2D2", true)};
    incoming.items_to_give = {make_item(31002, "Demo Sticker | Placeholder", "High Grade Sticker", "4B69FF", true),
                              make_item(31003, "Demo Graffiti | Lorem", "Base Grade Graffiti", "D2D2D2", true)};
    a0.received.push_back(std::move(incoming));
    auto sent = make_offer("6100000002", 3, true, S::CreatedNeedsConfirmation, "", now);
    sent.items_to_give = {make_item(31004, "Demo Agent | Test Pilot", "Exceptional Agent", "8847FF", true)};
    a0.sent.push_back(std::move(sent));
    a0.last_refresh_unix = now - 300;
    g_states[demo_account_id(0)] = std::move(a0);

    AccountTradeState a12;
    auto escrow = make_offer("6100000003", 0, true, S::InEscrow, "As agreed", now);
    escrow.escrow_end_unix = now + 5 * 86400LL;
    escrow.items_to_give = {make_item(31005, "Demo Knife | Mockup", "Covert Knife", "EB4B4B", false)};
    escrow.items_to_receive = {make_item(31006, "Demo Gloves | Sample", "Extraordinary Gloves", "EB4B4B", false)};
    a12.sent.push_back(std::move(escrow));
    a12.last_refresh_unix = now - 900;
    g_states[demo_account_id(12)] = std::move(a12);

    AccountTradeState a8;
    a8.last_error = "Steam returned HTTP 503. Try again later.";
    g_states[demo_account_id(8)] = std::move(a8);

    AccountTradeState a16;
    a16.refreshing = true;
    g_states[demo_account_id(16)] = std::move(a16);

    state.pending_trade_offers_count.store(1, std::memory_order_relaxed);
}

core::Vault& loaded_demo_vault() {
    static core::Vault vault;
    return vault;
}

}  // namespace

std::string demo_account_id(int index) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "demo-acc-%02d", index);
    return buf;
}

bool prepare_demo_data(std::string& error) {
    try {
        ensure_data_dirs();
        const crypto::SecureString pw = crypto::make_secure(kDemoMasterPassword);
        VaultRegistry reg = load_registry();
        const struct {
            const char* id;
            const char* name;
            std::uint32_t color;
        } vaults[] = {
            {kDemoVaultId, "Demo accounts", 0x4F8EF7FFu},
            {"demo-vault-work", "Work (demo)", 0x3CB878FFu},
            {"demo-vault-archive", "Archive (demo)", 0xB86BD8FFu},
        };
        const std::int64_t now = now_unix();
        for (const auto& v : vaults) {
            platform::set_active_vault_id(v.id);
            std::error_code ec;
            std::filesystem::create_directories(platform::active_vault_dir(), ec);
            if (std::string_view(v.id) == kDemoVaultId) {
                core::store::save_vault(vault_path(), make_demo_vault(now), pw);
            } else {
                core::store::create_new_vault(vault_path(), pw);
            }
            add_vault_entry(reg, v.id, v.name, v.color);
        }
        platform::set_active_vault_id(kDemoVaultId);
        loaded_demo_vault() = core::store::load_vault(vault_path(), pw);
        platform::set_active_vault_id("");
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

void select_demo_vault(AppState& state) {
    platform::set_active_vault_id(kDemoVaultId);
    state.needs_vault_pick = false;
    state.unlocked = false;
    state.current_screen = Screen::Unlock;
}

void open_demo_vault(AppState& state) {
    select_demo_vault(state);
    state.vault = loaded_demo_vault();
    state.master_password = crypto::make_secure(kDemoMasterPassword);
    state.unlocked = true;
    state.last_interaction = std::chrono::steady_clock::now();
    state.current_screen = Screen::Accounts;
    bind_vault_session(state);
    state.warned_missing_api_key = true;

    const std::int64_t now = now_unix();
    seed_confirmations(state, now);
    seed_trade_offers(state, now);
}

}  // namespace sam::app::ui_capture

#endif  // SAM_UI_CAPTURE
