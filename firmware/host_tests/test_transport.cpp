// test_transport.cpp — host tests for the POST /reading response handling.
//
// transportReport()'s two branch decisions, pinned without a bench or live API:
//   ptw_server_reached(status)      — the retry signal
//   ptw_response_has_interval(status) — the cadence guard
// The HAL (the actual POST + JSON parse) stays in the .ino and is proven by the
// ESP32 compile check; these are the decisions a regression would silently break.
//
// Build + run:
//   g++ -std=c++17 -Wall -Wextra -Werror -o t firmware/host_tests/test_transport.cpp && ./t

#include "../water_level/logic.h"

#include <cstdio>

static int g_passed = 0, g_failed = 0;

#define CHECK(name, cond)                                          \
  do {                                                              \
    if (cond) { g_passed++; }                                       \
    else { g_failed++; std::printf("FAIL  %s\n", name); }           \
  } while (0)

// ── server-reached (drives the retry-sooner-on-failure path) ─────────────────

static void test_server_reached() {
  // Any HTTP response (any code) means the server was reached.
  CHECK("reached: 200 → true", ptw_server_reached(200));
  CHECK("reached: 429 → true", ptw_server_reached(429));
  CHECK("reached: 422 → true", ptw_server_reached(422));
  CHECK("reached: 500 → true", ptw_server_reached(500));
  // <= 0 is the HTTPClient signal for "no response arrived" — no WiFi, connect
  // failure, or beginSecure() failing closed (TLS/clock). Caller reports failure.
  CHECK("reached: 0 → false (no response)",  !ptw_server_reached(0));
  CHECK("reached: -1 → false (connect err)", !ptw_server_reached(-1));
  CHECK("reached: -11 → false (timeout)",    !ptw_server_reached(-11));
}

// ── honor next_interval_secs ────────────────────────────────────────────────

static void test_response_has_interval() {
  // The API sends the per-tier cadence on the 200 accept AND the 429
  // rate-limit reply — both must honor it.
  CHECK("interval: 200 → true", ptw_response_has_interval(200));
  CHECK("interval: 429 → true (honor backoff cadence)", ptw_response_has_interval(429));
  // No interval to honor on these — leave cadence unchanged.
  CHECK("interval: 422 → false (not assigned)", !ptw_response_has_interval(422));
  CHECK("interval: 500 → false (server error)", !ptw_response_has_interval(500));
  CHECK("interval: 401 → false (auth)",         !ptw_response_has_interval(401));
  CHECK("interval: 0 → false (no response)",    !ptw_response_has_interval(0));
  CHECK("interval: -1 → false (connect err)",   !ptw_response_has_interval(-1));
}

// ── the two compose the way transportReport relies on ─────────────────────────

static void test_compose() {
  // 429 is the subtle case: the device WAS reached *and* must honor the new
  // interval — both true. Getting either wrong is a real bug.
  CHECK("429 is both reached and interval-bearing",
        ptw_server_reached(429) && ptw_response_has_interval(429));
  // A transport failure is neither — no interval to read, and reported as failed.
  CHECK("-1 is neither reached nor interval-bearing",
        !ptw_server_reached(-1) && !ptw_response_has_interval(-1));
  // 422 reached the server but carries no interval — cadence held, not failed.
  CHECK("422 reached but no interval",
        ptw_server_reached(422) && !ptw_response_has_interval(422));
}

// ── claim portal readiness (send-otp must wait for auto-register) ─────────────

static void test_claim_gate() {
  CHECK("claim: disconnected waits for WiFi",
        ptw_claim_gate(false, false) == PTW_CLAIM_WAIT_WIFI);
  CHECK("claim: disconnected ignores stale registered flag",
        ptw_claim_gate(false, true) == PTW_CLAIM_WAIT_WIFI);
  CHECK("claim: connected + unregistered runs auto-register first",
        ptw_claim_gate(true, false) == PTW_CLAIM_REGISTER_FIRST);
  CHECK("claim: connected + locally registered may send OTP",
        ptw_claim_gate(true, true) == PTW_CLAIM_ALLOW);
}

// ── cycle success = reached AND registered ───────────────────────────────────

static void test_report_cycle_ok() {
  // An UNregistered device's /reading is answered 401, so
  // it WAS reached — but the cycle must NOT count as success, or it sleeps the
  // full (up-to-24h) interval and starves autoRegister()'s retry. ANDing with
  // `registered` makes it fail → short retry cap → re-registers within minutes.
  CHECK("cycle: reached + registered → ok (normal success)",
        ptw_report_cycle_ok(true, true));
  CHECK("cycle: reached(401) + UNregistered → NOT ok (fast retry)",
        !ptw_report_cycle_ok(true, false));
  CHECK("cycle: not-reached + registered → NOT ok (no WiFi/TLS; fast retry)",
        !ptw_report_cycle_ok(false, true));
  CHECK("cycle: not-reached + UNregistered → NOT ok",
        !ptw_report_cycle_ok(false, false));
  // Wiring proof: ptw_server_reached(401)=true is exactly the trap input — a
  // reached-but-unregistered cycle must still come back NOT ok.
  CHECK("cycle: 401-reached while unregistered composes to NOT ok",
        !ptw_report_cycle_ok(ptw_server_reached(401), /*registered=*/false));
}

// ── ptw_should_attempt_reregister ────────────────────────────────────────────

static void test_should_attempt_reregister() {
  const int N = 3;  // recommended threshold

  // Below threshold — wait for more evidence before disrupting a registered device.
  CHECK("reregister: 0 consecutive → no",   !ptw_should_attempt_reregister(0, N));
  CHECK("reregister: 1 consecutive → no",   !ptw_should_attempt_reregister(1, N));
  CHECK("reregister: N-1 → no",             !ptw_should_attempt_reregister(N - 1, N));

  // At and above threshold — the persistent-401 signal is clear, try recovery.
  CHECK("reregister: exactly N → yes",      ptw_should_attempt_reregister(N, N));
  CHECK("reregister: N+1 → yes",            ptw_should_attempt_reregister(N + 1, N));
  CHECK("reregister: N+100 → yes",          ptw_should_attempt_reregister(N + 100, N));

  // Degenerate: max=0 fires immediately (caller should never use this, but fail safe → yes).
  CHECK("reregister: max=0, count=0 → yes", ptw_should_attempt_reregister(0, 0));

  // Defensive: negative count must not fire (NVS corruption / wrap).
  CHECK("reregister: negative count → no",  !ptw_should_attempt_reregister(-1, N));
}

// ── ptw_use_slow_backoff ────────────────────────────────────────────────────

static void test_use_slow_backoff() {
  // No recovery attempted yet — normal path, do not slow-probe.
  CHECK("slow-backoff: not attempted, no response → no",
        !ptw_use_slow_backoff(false, false, false));
  CHECK("slow-backoff: not attempted, server reached, unregistered → no",
        !ptw_use_slow_backoff(false, true, false));
  CHECK("slow-backoff: not attempted, recovered → no",
        !ptw_use_slow_backoff(false, true, true));

  // Recovery attempted, but connectivity loss — NOT a server refusal; normal retry.
  CHECK("slow-backoff: attempted, server NOT reached, unregistered → no (connectivity)",
        !ptw_use_slow_backoff(true, false, false));

  // Recovery attempted and succeeded — normal cadence resumes.
  CHECK("slow-backoff: attempted, server reached, registered → no (recovery succeeded)",
        !ptw_use_slow_backoff(true, true, true));

  // Recovery attempted, server reached, STILL not registered → server refused.
  CHECK("slow-backoff: attempted, server reached, not registered → YES (server said no)",
        ptw_use_slow_backoff(true, true, false));

  // Composition: a 401 from /reading after recovery attempt = server refusal signal.
  CHECK("slow-backoff: attempted + 401-reached + unregistered → YES",
        ptw_use_slow_backoff(true, ptw_server_reached(401), false));

  // Composition: NOT attempted yet, even on a 401, should not slow-probe.
  CHECK("slow-backoff: not attempted + 401-reached → no",
        !ptw_use_slow_backoff(false, ptw_server_reached(401), false));
}

int main() {
  test_server_reached();
  test_response_has_interval();
  test_compose();
  test_claim_gate();
  test_report_cycle_ok();
  test_should_attempt_reregister();
  test_use_slow_backoff();
  std::printf("\ntransport host tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
