// test_tls.cpp — host tests for the TLS clock-readiness gate.
//
// The TLS wiring itself (setCACert, the WiFiClientSecure handshake) is hardware/
// network and proven by the ESP32 compile check + Phase B bench verification.
// The one *pure* decision in the TLS path is host-testable here: "is the clock
// synced enough to trust a cert's validity dates yet?" — the fail-closed boundary
// that, if it regressed, would either brick connectivity (floor too high) or let
// a 1970-clock device skip validation (floor too low / absent).
//
// Build + run:
//   g++ -std=c++17 -Wall -Wextra -Werror -o t firmware/host_tests/test_tls.cpp && ./t

#include "../water_level/logic.h"

#include <cstdio>

static int g_passed = 0, g_failed = 0;

#define CHECK(name, cond)                                          \
  do {                                                              \
    if (cond) { g_passed++; }                                       \
    else { g_failed++; std::printf("FAIL  %s\n", name); }           \
  } while (0)

// 2020-01-01T00:00:00Z — the floor below which the clock is "not synced yet".
static const long FLOOR = 1577836800L;

static void test_clock_plausibility() {
  // The ESP32 boots here — must be rejected, or TLS validates against 1970.
  CHECK("clock: epoch 0 (1970 boot) → not plausible",   !ptw_clock_is_plausible(0));
  CHECK("clock: negative epoch → not plausible",        !ptw_clock_is_plausible(-1));
  CHECK("clock: just below floor → not plausible",      !ptw_clock_is_plausible(FLOOR - 1));

  // Exactly the floor and above — synced; TLS may proceed.
  CHECK("clock: exactly 2020-01-01 → plausible",         ptw_clock_is_plausible(FLOOR));
  CHECK("clock: just above floor → plausible",           ptw_clock_is_plausible(FLOOR + 1));

  // A real present-day clock (2026-06-12 ~ 1.749e9) — plausible. And a far-future
  // clock is still "plausible" by this gate: the cert's own notAfter is what would
  // reject a wildly-future clock, not this floor (this gate only rules out the
  // pre-sync 1970 window, which is its single job).
  CHECK("clock: 2026 present-day → plausible",           ptw_clock_is_plausible(1749686400L));
  CHECK("clock: 2038 (CA1 notAfter era) → plausible",    ptw_clock_is_plausible(2147483647L));

  // The boundary is exact (no off-by-one): floor-1 false, floor true.
  CHECK("clock: boundary is exact",
        !ptw_clock_is_plausible(FLOOR - 1) && ptw_clock_is_plausible(FLOOR));
}

int main() {
  test_clock_plausibility();
  std::printf("\ntls host tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
