// The live Game Coordinator session behind the CS2 screen: starts and retires the client,
// marshals its callbacks into Cs2ScreenState, and runs the move queue one item at a time.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/app_state.hpp"
#include "core/cs2_gc/cs2_gc_client.hpp"
#include "core/steam_login/session.hpp"
#include "ui/kit/feedback.hpp"
#include "ui/screens/cs2/cs2_internal.hpp"
#include "ui/util.hpp"

namespace sam::ui::screens::cs2 {

namespace {

constexpr const char* kConnectionLost = "Connection to Steam was lost";

bool drop_claimed_this_week(const cs2_gc::WeeklyReward& reward) {
    if (!reward.loaded || reward.available) return false;
    const std::int64_t now = now_seconds();
    const std::int64_t last_reset = next_weekly_reset(now) - 7 * 86400;
    return reward.generation_time >= static_cast<std::uint32_t>(last_reset);
}

Cs2MoveOp* running_op(Cs2ScreenState& s) {
    for (Cs2MoveOp& op : s.ops)
        if (op.status == Cs2MoveOp::Status::Running) return &op;
    return nullptr;
}

// Ends the batch after the connection dropped: the running op and the queued ones did not move.
void fail_remaining(Cs2ScreenState& s, const std::string& why) {
    for (Cs2MoveOp& op : s.ops) {
        if (op.status == Cs2MoveOp::Status::Running || op.status == Cs2MoveOp::Status::Queued) {
            op.status = Cs2MoveOp::Status::Failed;
            op.error = why;
            s.failed_items[op.item_id] = why;
        }
    }
    s.batch_open = false;
}

// Routes an on_error of the live session to the command it answers.
void route_error(Cs2ScreenState& s, std::string err) {
    if (s.phase != Cs2Phase::Connected || err == kConnectionLost) {
        // The connect failed, or the live session dropped: the client thread is ending.
        if (s.phase == Cs2Phase::Connected) fail_remaining(s, "the connection dropped");
        s.connection_error = std::move(err);
        s.phase = Cs2Phase::Failed;
        s.pending = Cs2Pending::None;
        s.unit_loading = false;
        return;
    }
    switch (s.pending) {
        case Cs2Pending::Move:
            if (Cs2MoveOp* op = running_op(s)) {
                op->status = Cs2MoveOp::Status::Failed;
                op->error = err;
                s.failed_items[op->item_id] = std::move(err);
            }
            break;
        case Cs2Pending::Claim:
            s.claim_error = std::move(err);
            break;
        case Cs2Pending::LoadUnit:
            s.unit_error = std::move(err);
            s.unit_loading = false;
            break;
        case Cs2Pending::None:
            s.action_error = std::move(err);
            s.action_alert_open = true;
            break;
    }
    s.pending = Cs2Pending::None;
}

}  // namespace

TokenState token_state(const core::Account& a, std::int64_t now) {
    if (app::cs2_client_token(a).empty()) return a.password.empty() ? TokenState::NoPassword : TokenState::NeedsSetup;
    // The CM rejects an expired token; a revoked one keeps its expiry, so its status is the
    // only sign (set by the token-injection launch check).
    if (!a.cm_refresh_token.empty()) {
        if (a.cm_status == core::NfaTokenStatus::Revoked) return TokenState::Revoked;
        if (a.cm_refresh_token_expires > 0 && a.cm_refresh_token_expires <= now) return TokenState::Expired;
    } else if (a.is_nfa) {
        if (a.nfa_status == core::NfaTokenStatus::Revoked) return TokenState::Revoked;
        if (a.refresh_token_expires > 0 && a.refresh_token_expires <= now) return TokenState::Expired;
    }
    return TokenState::Ready;
}

void retire_client(Cs2ScreenState& s) {
    if (s.client) cs2_gc::retire(std::move(s.client));
}

void connect(app::AppState& state, const core::Account& acct) {
    Cs2ScreenState& s = *state.cs2_screen;
    retire_client(s);
    s.phase = Cs2Phase::Connecting;
    s.status = "Starting";
    s.connection_error.clear();
    s.action_error.clear();
    s.claim_error.clear();
    s.snapshot = {};
    s.has_snapshot = false;
    s.pending = Cs2Pending::None;
    s.open_unit = 0;
    s.unit_loading = false;
    s.unit_error.clear();
    s.unit_contents.clear();
    s.ops.clear();
    s.failed_items.clear();
    s.connected_since = 0;
    const std::uint64_t session = ++s.session;

    cs2_gc::Cs2Credentials creds;
    creds.refresh_token = app::cs2_client_token(acct);
    creds.steam_id = acct.steam_id_64;
    creds.proxy = std::string(acct.proxy.begin(), acct.proxy.end());
    creds.claimed_generation = acct.cs2.weekly_drop_claimed_generation;
    creds.claimed_ids = acct.cs2.weekly_drop_claimed_ids;
    creds.claimed_names = acct.cs2.weekly_drop_claimed_names;

    app::AppState* st = &state;
    const std::string aid = acct.id;
    auto valid = [st, aid, session] {
        return st->cs2_screen && st->cs2_screen->account_id == aid && st->cs2_screen->session == session;
    };

    cs2_gc::Cs2Callbacks cb;
    cb.on_status = [st, valid](std::string text) {
        st->post_ui_callback([st, valid, text = std::move(text)] {
            if (!valid()) return;
            Cs2ScreenState& cs = *st->cs2_screen;
            cs.status = text;
            if (text == "Ready") {
                cs.phase = Cs2Phase::Connected;
                cs.connected_since = now_seconds();
            } else if (text == "Disconnected") {
                if (cs.phase != Cs2Phase::Failed) {
                    cs.phase = cs.connection_error.empty() ? Cs2Phase::Idle : Cs2Phase::Failed;
                }
            }
        });
    };
    cb.on_snapshot = [st, aid, valid](cs2_gc::Snapshot snap) {
        st->post_ui_callback([st, aid, valid, snap = std::move(snap)]() mutable {
            if (!valid()) return;
            Cs2ScreenState& cs = *st->cs2_screen;
            cs.snapshot = std::move(snap);
            cs.has_snapshot = true;
            st->apply_gc_snapshot_cache(aid, cs.snapshot);

            core::Account* acc = st->find_account(aid);
            if (acc == nullptr) return;
            const cs2_gc::WeeklyReward& reward = cs.snapshot.reward;
            if (st->settings.cs2_gc.auto_mark_claimed && reward.loaded && !reward.available) {
                const std::int64_t now = now_seconds();
                const bool claimed = drop_claimed_this_week(reward);
                const bool marked = acc->cs2.weekly_drop_reset_unix > now;
                if (claimed != marked) {
                    acc->cs2.weekly_drop_reset_unix = claimed ? next_weekly_reset(now) : 0;
                    st->vault_dirty = true;
                    st->save_vault_if_dirty();
                }
            }
        });
    };
    cb.on_unit_contents = [st, valid](std::uint64_t unit, std::vector<cs2_gc::DisplayItem> items) {
        st->post_ui_callback([st, valid, unit, items = std::move(items)]() mutable {
            if (!valid()) return;
            Cs2ScreenState& cs = *st->cs2_screen;
            if (cs.pending == Cs2Pending::LoadUnit) cs.pending = Cs2Pending::None;
            if (cs.open_unit == unit) {
                cs.unit_loading = false;
                cs.unit_error.clear();
                cs.unit_contents = std::move(items);
            }
            pump(*st, cs);
        });
    };
    cb.on_error = [st, valid](std::string err) {
        st->post_ui_callback([st, valid, err = std::move(err)]() mutable {
            if (!valid()) return;
            route_error(*st->cs2_screen, std::move(err));
            pump(*st, *st->cs2_screen);
        });
    };
    cb.on_action_done = [st, valid](std::string msg) {
        st->post_ui_callback([st, valid, msg = std::move(msg)] {
            if (!valid()) return;
            Cs2ScreenState& cs = *st->cs2_screen;
            if (cs.pending == Cs2Pending::Move) {
                if (Cs2MoveOp* op = running_op(cs)) {
                    op->status = Cs2MoveOp::Status::Done;
                    cs.failed_items.erase(op->item_id);
                }
            } else if (cs.pending == Cs2Pending::Claim) {
                cs.claim_error.clear();
                cs.reward_selection.Clear();
                kit::show_toast(*st, msg, {.tone = widgets::ToastTone::Success});
            }
            cs.pending = Cs2Pending::None;
            pump(*st, cs);
        });
    };
    // Not tied to the screen: a claim is recorded on the account even when the screen has
    // moved on.
    cb.on_reward_claimed = [st, aid](std::uint32_t gen, std::vector<std::uint64_t> ids,
                                     std::vector<std::string> names) {
        st->post_ui_callback([st, aid, gen, ids = std::move(ids), names = std::move(names)]() mutable {
            auto* acc = st->find_account(aid);
            if (!acc) return;
            acc->cs2.weekly_drop_claimed_generation = gen;
            acc->cs2.weekly_drop_claimed_ids = std::move(ids);
            acc->cs2.weekly_drop_claimed_names = std::move(names);
            acc->cs2.weekly_drop_reset_unix = next_weekly_reset(now_seconds());
            st->vault_dirty = true;
            st->save_vault_if_dirty();
        });
    };

    s.client = std::make_unique<cs2_gc::Cs2GcClient>(std::move(creds), std::move(cb));
}

void disconnect(Cs2ScreenState& s) {
    retire_client(s);
    ++s.session;  // late callbacks of the retired client are ignored
    s.phase = Cs2Phase::Idle;
    s.status = "Disconnected";
    s.pending = Cs2Pending::None;
    s.unit_loading = false;
    for (Cs2MoveOp& op : s.ops) {
        if (op.status == Cs2MoveOp::Status::Queued || op.status == Cs2MoveOp::Status::Running) {
            op.status = Cs2MoveOp::Status::Failed;
            op.error = "disconnected before it moved";
            s.failed_items[op.item_id] = op.error;
        }
    }
    s.batch_open = false;
}

void start_signin(app::AppState& state, const core::Account& acct) {
    Cs2ScreenState& s = *state.cs2_screen;
    s.acquiring = true;
    s.signin_error.clear();
    app::AppState* st = &state;
    const std::string aid = acct.id;
    state.acquire_cm_token(aid, [st, aid](bool ok, std::string err) {
        if (!st->cs2_screen || st->cs2_screen->account_id != aid) return;
        st->cs2_screen->acquiring = false;
        if (!ok) st->cs2_screen->signin_error = std::move(err);
    });
}

void refresh(Cs2ScreenState& s) {
    if (s.client) s.client->refresh();
}

void open_unit(Cs2ScreenState& s, std::uint64_t unit_id) {
    s.open_unit = unit_id;
    s.unit_contents.clear();
    s.unit_error.clear();
    s.unit_selection.Clear();
    s.add_selection.Clear();
    if (unit_id == 0) {
        s.unit_loading = false;
        return;
    }
    s.unit_loading = true;
    if (s.client) {
        s.pending = Cs2Pending::LoadUnit;
        s.client->load_unit(unit_id);
    }
}

void claim(Cs2ScreenState& s, std::vector<std::uint64_t> ids) {
    if (ids.empty() || s.pending != Cs2Pending::None) return;
    s.claim_error.clear();
    if (!s.client) return;
    s.pending = Cs2Pending::Claim;
    s.client->claim_reward(std::move(ids));
}

void enqueue_moves(Cs2ScreenState& s, std::uint64_t unit_id, bool into_unit,
                   std::span<const cs2_gc::DisplayItem* const> items) {
    if (batch_running(s) || unit_id == 0 || items.empty()) return;
    s.ops.clear();
    s.failed_items.clear();
    for (const cs2_gc::DisplayItem* it : items)
        s.ops.push_back({.item_id = it->id, .unit_id = unit_id, .into_unit = into_unit, .name = it->name});
    s.batch_open = true;
}

void cancel_queued(Cs2ScreenState& s) {
    std::erase_if(s.ops, [](const Cs2MoveOp& op) { return op.status == Cs2MoveOp::Status::Queued; });
}

bool batch_running(const Cs2ScreenState& s) {
    return std::any_of(s.ops.begin(), s.ops.end(), [](const Cs2MoveOp& op) {
        return op.status == Cs2MoveOp::Status::Queued || op.status == Cs2MoveOp::Status::Running;
    });
}

int ops_finished(const Cs2ScreenState& s) {
    return static_cast<int>(std::count_if(s.ops.begin(), s.ops.end(), [](const Cs2MoveOp& op) {
        return op.status == Cs2MoveOp::Status::Done || op.status == Cs2MoveOp::Status::Failed;
    }));
}

int ops_failed(const Cs2ScreenState& s) {
    return static_cast<int>(std::count_if(s.ops.begin(), s.ops.end(),
                                          [](const Cs2MoveOp& op) { return op.status == Cs2MoveOp::Status::Failed; }));
}

TileMark op_mark(const Cs2ScreenState& s, std::uint64_t id, const std::string** error) {
    if (const auto it = s.failed_items.find(id); it != s.failed_items.end()) {
        if (error != nullptr) *error = &it->second;
        return TileMark::Failed;
    }
    for (const Cs2MoveOp& op : s.ops)
        if (op.item_id == id && (op.status == Cs2MoveOp::Status::Queued || op.status == Cs2MoveOp::Status::Running))
            return TileMark::Queued;
    return TileMark::None;
}

void pump(app::AppState& state, Cs2ScreenState& s) {
    if (s.ops.empty() || s.phase != Cs2Phase::Connected || !s.client) return;
    if (s.pending != Cs2Pending::None) return;
    for (Cs2MoveOp& op : s.ops) {
        if (op.status != Cs2MoveOp::Status::Queued) continue;
        op.status = Cs2MoveOp::Status::Running;
        s.pending = Cs2Pending::Move;
        if (op.into_unit)
            s.client->move_in(op.unit_id, op.item_id);
        else
            s.client->move_out(op.unit_id, op.item_id);
        return;
    }
    // Every op has an answer: report the batch once and reload the open unit, whose contents
    // the moves changed.
    if (!s.batch_open) return;
    s.batch_open = false;
    const int total = static_cast<int>(s.ops.size());
    const int failed = ops_failed(s);
    const bool into = s.ops.front().into_unit;
    const std::uint64_t unit = s.ops.front().unit_id;
    const cs2_gc::DisplayItem* u = find_unit(s, unit);
    const std::string unit_name = u != nullptr ? u->name : std::string("the storage unit");
    std::string msg;
    if (failed == 0) {
        msg = (into ? "Moved " + item_count_text(total) + " into " : "Moved " + item_count_text(total) + " out of ") +
              unit_name;
    } else {
        msg = std::to_string(total - failed) + " of " + std::to_string(total) + " moved, " + std::to_string(failed) +
              " failed";
    }
    kit::show_toast(state, msg,
                    {.tone = failed == 0 ? widgets::ToastTone::Success : widgets::ToastTone::Warning, .id = "cs2-move"});
    // Keep the failed ops visible with their errors; the finished ones leave the queue.
    std::erase_if(s.ops, [](const Cs2MoveOp& op) { return op.status == Cs2MoveOp::Status::Done; });
    if (s.open_unit == unit) open_unit(s, unit);
}

const cs2_gc::DisplayItem* find_unit(const Cs2ScreenState& s, std::uint64_t id) {
    for (const cs2_gc::DisplayItem& u : s.snapshot.units)
        if (u.id == id) return &u;
    return nullptr;
}

}  // namespace sam::ui::screens::cs2
