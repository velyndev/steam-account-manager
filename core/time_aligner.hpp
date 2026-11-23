#pragma once

#include <cstdint>

namespace sam::time_aligner {

void start();
void stop();

std::int64_t aligned_now();

// Positive means the Steam clock is ahead of ours.
std::int64_t offset_seconds();

bool synced();
std::int64_t seconds_since_last_success();

bool sync_now();
void sync_now_async();

#ifdef SAM_UI_CAPTURE
// Dev-only (the --ui-capture harness): reports synced with a zero offset without asking
// Steam, and makes aligned_now() return `fixed_unix`, so generated codes and their
// countdown are the same in every capture. Never call start() alongside it.
void use_fixed_clock(std::int64_t fixed_unix);
#endif

}  // namespace sam::time_aligner
