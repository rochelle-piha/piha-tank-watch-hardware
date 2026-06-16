#pragma once
// logic.h — pure firmware logic, host-testable.
//
// NO Arduino includes, NO globals, NO I/O (standard C headers only). Everything
// here compiles standalone with `g++ -Wall -Wextra -Werror` and is pinned by
// firmware/host_tests/ — so regressions in the maths that decide what a device
// reports (and how often) fail local checks on the host in seconds, without a
// bench or the ESP32 toolchain.

#include <math.h>  // roundf — present on both host and Arduino
//
// The .ino keeps thin wrappers that own the hardware/side-effect halves
// (readDistance() pings, delay(), Serial, the g_intervalMs global) and delegate
// the decisions to these functions. Keep it that way: anything added here must
// stay pure.

// ── Median reading ────────────────────────────────────────────────────────────
//
// From `count` raw pings, keep the plausible ones (strictly inside (lo, hi)),
// and return the median — robust to the outliers a single ultrasonic ping picks
// up off a moving water surface. Returns false when fewer than `min_valid`
// pings are plausible (caller reports a failed read, never a fake value).
//
// `count` is capped at 16 — callers pass SENSOR_SAMPLES (5 by default).
inline bool ptw_median_reading(const float* pings, int count, int min_valid,
                               float lo, float hi, float* out) {
  if (count > 16) count = 16;
  float samples[16];
  int n = 0;
  for (int i = 0; i < count; i++) {
    float d = pings[i];
    if (d > lo && d < hi) samples[n++] = d;  // keep only plausible pings
  }
  if (n < min_valid) return false;           // too few good pings — report failure

  // Insertion sort (n is tiny) then take the median.
  for (int i = 1; i < n; i++) {
    float key = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > key) { samples[j + 1] = samples[j]; j--; }
    samples[j + 1] = key;
  }
  *out = samples[n / 2];
  return true;
}

// ── Sensor acquisition (HAL-injected, host-testable) ─────────────────────────
//
// Echo pulse width (µs, from pulseIn) → distance in cm. us <= 0 means the pulse
// never returned (sensor timeout / no echo) → -1 sentinel, which the median's
// in-range gate then filters out. 0.034 cm/µs (speed of sound), halved for the
// round trip. A bug here corrupts every reading, so it's pinned by host tests.
inline float ptw_distance_from_echo_us(long us) {
  return us > 0 ? us * 0.034f / 2.0f : -1.0f;
}

// Acquire one reading: pull `count` raw pings from `next_ping` then take the
// median of the in-range ones. `next_ping` IS the HAL seam — the .ino
// passes a lambda doing readDistance()+settle-delay; host tests pass a canned
// echo sequence — so the full collect→median flow (incl. the all-invalid /
// too-few-valid failure paths) runs through the *same code the device runs*,
// on g++ in milliseconds, no bench. Returns false when too few pings
// are plausible. Templated on the ping source (same injection pattern as the
// payload builder); `count` capped at 16 to bound the stack buffer.
template <typename NextPing>
inline bool ptw_acquire_reading(NextPing next_ping, int count, int min_valid,
                                float lo, float hi, float* out) {
  if (count > 16) count = 16;
  float pings[16];
  for (int i = 0; i < count; i++) pings[i] = next_ping();
  return ptw_median_reading(pings, count, min_valid, lo, hi, out);
}

// ── Claim portal readiness ───────────────────────────────────────────────────
//
// The captive portal must not call /devices/send-otp before the device has a
// server-side bootstrap record. /devices/send-otp is intentionally
// device-authenticated and returns 401 when the server cannot find the record;
// that is correct server behavior, but it is a bad onboarding order.
//
// Keep the decision pure so the WebServer callbacks only own side effects:
//   - disconnected: keep showing the WiFi wait path
//   - connected but unregistered: run auto-register before claim/send-otp
//   - connected and registered: it is safe to expose the claim email flow
//
// Scope: this closes the first-setup race where g_registered=false and the
// portal can be used before auto-register has created the server record. A
// stale local g_registered=true with a missing server record is the existing
// server-record-loss/self-heal case; it is not broadened here because this gate
// has no safe local proof of server state without making the authenticated call.
enum PtwClaimGate {
  PTW_CLAIM_WAIT_WIFI,
  PTW_CLAIM_REGISTER_FIRST,
  PTW_CLAIM_ALLOW
};

inline PtwClaimGate ptw_claim_gate(bool wifi_connected, bool registered) {
  if (!wifi_connected) return PTW_CLAIM_WAIT_WIFI;
  if (!registered) return PTW_CLAIM_REGISTER_FIRST;
  return PTW_CLAIM_ALLOW;
}

// ── Reading payload assembly ─────────────────────────────────────────────────
//
// Which fields a reading POST carries, with what names, types, rounding and
// presence rules — the exact contract the API ingests. Templated on the document
// type so the .ino passes the real
// ArduinoJson `JsonDocument` while host tests pass a tiny fake recorder —
// the contract under test is OURS (fields/values/rules), not the serializer's.
//
// Presence rules pinned here (and by the host tests):
//   distance_cm        always — rounded to 1 dp
//   firmware_version   always
//   sensor_ok          always
//   rssi               only when connected (has_rssi)
//   battery_v (2 dp) + battery_pct (int)   only on battery builds (has_battery)
// A USB build must OMIT battery keys entirely — absent ≠ 0% (the app treats
// absence as "not applicable", a 0 would render as a critical-battery alarm).

struct PtwTelemetry {
  const char* firmware_version;
  bool sensor_ok;
  bool has_rssi;
  long rssi;
  bool has_battery;
  float battery_v;
  int battery_pct;
};

inline float ptw_round1(float v) { return roundf(v * 10.0f) / 10.0f; }
inline float ptw_round2(float v) { return roundf(v * 100.0f) / 100.0f; }

template <typename Doc>
inline void ptw_build_reading_payload(Doc& doc, float distance_cm, const PtwTelemetry& t) {
  doc["distance_cm"]      = ptw_round1(distance_cm);
  doc["firmware_version"] = t.firmware_version;
  doc["sensor_ok"]        = t.sensor_ok;
  if (t.has_rssi) doc["rssi"] = t.rssi;
  if (t.has_battery) {
    doc["battery_v"]   = ptw_round2(t.battery_v);
    doc["battery_pct"] = t.battery_pct;
  }
}

// Battery percent from pack voltage: linear between empty/full, clamped 0–100,
// rounded to the nearest integer (what the wire carries; the app thresholds
// at <20 warn / <10 critical, so integer precision is the contract).
inline int ptw_battery_pct(float v, float full_v, float empty_v) {
  if (full_v <= empty_v) return 0;  // misconfigured thresholds — fail safe
  float pct = (v - empty_v) / (full_v - empty_v) * 100.0f;
  if (pct < 0.0f)   pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return (int)roundf(pct);
}

// ── Sleep duration for the next cycle ────────────────────────────────────────
//
// A successful report sleeps the full server-driven interval; a failed cycle
// (sensor read failed, or the transport never reached the server) retries
// sooner — capped — so a transient failure doesn't blind the tank for a whole
// free-tier day. The cap never *extends* a short interval: if the interval is
// already below the cap, the retry just uses the interval.
inline unsigned long ptw_sleep_duration_ms(bool success, unsigned long interval_ms,
                                           unsigned long retry_cap_ms) {
  if (success) return interval_ms;
  return interval_ms < retry_cap_ms ? interval_ms : retry_cap_ms;
}

// ── Server-driven interval clamp ─────────────────────────────────────────────
//
// The API returns `next_interval_secs` per plan tier on every /reading
// response. Clamp it to [30 s, 24 h] and convert to milliseconds.
// Returns 0 when `secs` is missing/invalid (<= 0) — caller keeps the current
// cadence. The bounds prevent both overly chatty reporting and excessively long
// silence, and are pinned by host tests.
inline unsigned long ptw_clamp_interval_ms(long secs) {
  if (secs <= 0) return 0;            // field missing/invalid — keep current cadence
  if (secs < 30)    secs = 30;        // never faster than the API's 30s floor
  if (secs > 86400) secs = 86400;     // never slower than once a day
  return (unsigned long)secs * 1000UL;
}

// ── TLS clock-readiness gate ─────────────────────────────────────────────────
//
// TLS validates the server cert's notBefore/notAfter against the device clock.
// The ESP32 boots at epoch 0 (1970), so a handshake before SNTP has synced the
// clock hard-fails "certificate is not yet valid" *even with the correct CA* —
// a common reason naive TLS validation works on the bench but fails in the
// field. So every HTTPS call is gated on this: only attempt TLS once the clock
// is past a sane floor (2020-01-01 UTC). Below it the caller FAILS CLOSED — it
// does not fall back to an unvalidated connection. Pure + host-tested so the
// fail-closed boundary (the exact epoch where we start trusting the clock) can't
// regress silently. 1577836800 = 2020-01-01T00:00:00Z; any real Amazon root is
// already valid by then, and no device legitimately reports a pre-2020 clock.
inline bool ptw_clock_is_plausible(long epoch_secs) {
  return epoch_secs >= 1577836800L;  // 2020-01-01T00:00:00Z
}

// ── POST /reading response handling ──────────────────────────────────────────
//
// transportReport()'s two branch decisions, lifted out pure so they're host-
// testable without a bench or a live API. The HAL half (the actual POST + JSON
// parse) stays in the .ino; these are the *decisions* that, if they regressed,
// would silently break cadence or retry behaviour with no compile/E2E signal.

// Did an HTTP response arrive at all? <= 0 means the transport never reached the
// server (no WiFi / connect or TLS-handshake failure — beginSecure failing closed
// shows up here too). Drives the retry: a cycle that never reached the server
// retries sooner instead of sleeping a full (free-tier, up-to-24h) interval.
inline bool ptw_server_reached(int http_status) { return http_status > 0; }

// Does this response carry a `next_interval_secs` we must honor? The API
// sends the per-tier cadence on BOTH the 200 accept AND the 429 rate-limit reply,
// so 429 must honor it too; otherwise a rate-limited device keeps reporting at
// the old cadence after being told to back off. 422 (not assigned) / other errors
// carry no interval → leave cadence as-is.
inline bool ptw_response_has_interval(int http_status) {
  return http_status == 200 || http_status == 429;
}

// Did this whole report cycle SUCCEED — i.e. should we sleep the full interval
// rather than the short retry cap? Two signals must BOTH hold:
//   server_reached — the POST got an HTTP response at all (ptw_server_reached)
//   registered     — the device has a server-side record (g_registered)
// The subtlety this pins: "reached the server" is NOT "succeeded".
// An UNregistered device's /reading is answered with 401 — a real HTTP response,
// so ptw_server_reached() is true — but nothing was stored. If we treated that as
// success we'd sleep the full (free-tier, up-to-24h) interval, so autoRegister()
// at the top of loop() would only re-fire once a day. ANDing with `registered`
// makes an unregistered device fail the cycle → short retry cap → it re-tries
// registration every few minutes until it sticks. A registered device that gets a
// persistent 401 (revoked) stays "ok" and backs off the full interval on purpose —
// it shouldn't hammer 401s. Pure + host-tested so this composition can't silently
// regress to `ok = transportReport(...)` again.
inline bool ptw_report_cycle_ok(bool server_reached, bool registered) {
  return server_reached && registered;
}

// ── Self-recovery from persistent /reading 401 ───────────────────────────────
//
// ptw_report_cycle_ok(true, true) returns true when a registered device gets a
// 401 — because g_registered=true in NVS but the server lost the record.  That
// cycle looks ok so the device sleeps the full interval; autoRegister() never
// retries; the device is silently dark forever.
//
// Recovery layer: track consecutive /reading 401s while the device believes it
// is registered.  After N consecutive, make ONE re-registration attempt:
//   - autoRegister() succeeds → server had no record (genuine loss); confirmed
//     by the next /reading returning 200.
//   - autoRegister() fails → server explicitly refused (tombstone for revocation,
//     clone/hash-mismatch) → slow-probe cadence, not hammering.
//
// Both predicates are pure; the .ino owns the counters and the NVS state.

// Should we attempt re-registration after too many consecutive 401s while registered?
// consecutive_401_count: 401s on /reading while g_registered=true, since last success.
// max_401s: trigger threshold (recommended: 3 — distinguishes transient from persistent,
// limits silent dark time.  N=3 at 15-min Pro cadence = ~45 min worst-case exposure).
inline bool ptw_should_attempt_reregister(int consecutive_401_count, int max_401s) {
  return consecutive_401_count >= max_401s;
}

// Are we in slow-backoff after a failed recovery attempt?
// recovery_attempted: true once we cleared g_registered + called autoRegister().
// server_reached: /reading returned an HTTP status (not a connect/TLS failure).
// registered: g_registered after the recovery attempt (true = autoRegister succeeded).
//
// Returns true only when the server *explicitly* refused re-registration — not a
// connectivity loss (server_reached=false), which should fall through to the normal
// retry path.  Slow-probe on server refusal avoids hammering a legitimately-revoked
// device; connectivity outages recover on their own without a slow-probe penalty.
inline bool ptw_use_slow_backoff(bool recovery_attempted, bool server_reached, bool registered) {
  return recovery_attempted && server_reached && !registered;
}

// ── Device id from the eFuse MAC ─────────────────────────────────────────────
// The original id was 4 random bytes — a 32-bit space whose birthday-bound
// collision climbs to ~50% near 77k devices, and a collision SILENTLY BRICKS the
// loser (its locally-generated secret never matches the first device's stored
// hash, so every /reading is 401'd forever). The ESP32's eFuse MAC is 48 bits,
// Espressif-assigned, unique by construction — no RNG collision possible. The MAC
// isn't secret, but the random 32-byte SECRET is what authenticates, so a
// guessable id is fine.
//
// NB the deterministic id is NOT an NVS-wipe recovery path on its own: a wiped
// device re-derives the same id but a FRESH secret, and auto-register is a no-op
// on an existing id (the server keeps the old api_key_hash) → it 401s. So a wipe
// still orphans, same as today. True wipe-recovery needs a server-side re-key
// while the record is UNCLAIMED — a follow-up, deliberately NOT in scope here.
//
// Writes 12 lowercase hex chars + NUL into `out` (matching hexStr's casing).
// Pure + Arduino-free so host tests pin it; the eFuse read is the .ino's HAL seam.
inline void ptw_device_id_from_mac(const unsigned char mac[6], char out[13]) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 6; i++) {
    out[i * 2]     = hex[(mac[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hex[mac[i] & 0x0F];
  }
  out[12] = '\0';
}

// ── OTA confirmed-boot gate ─────────────────────────────────────────────────
//
// When an OTA image first boots it is PENDING: the bootloader will auto-revert to
// the previous (known-good) slot on the next reset UNLESS the running app actively
// marks itself valid. Marking valid is irreversible-adjacent: on a secure-boot-v2
// build with anti-rollback, esp_ota_mark_app_valid_cancel_rollback() also advances
// the eFuse SECURE_VERSION counter, which then permanently blocks booting the older
// slot. So "mark valid" must mean "this image has PROVEN it works in the field" —
// not "this image reached main()". If we mark valid early and the image is actually
// broken (can't reach the server, bad cert, wedged sensor loop that still pings),
// we'd both cancel the safety rollback AND burn the counter past the good slot.
//
// The proof we require is the one we already trust everywhere else: a full report
// cycle — ptw_report_cycle_ok: clock synced → TLS handshake → device
// registered → a /reading the server accepted. If the fresh image can do that, it
// is by definition healthy on this exact unit + network, so we mark valid (and only
// then let the counter advance). If it can't within `max_boots` attempts, we force a
// rollback to the known-good slot — and because we never marked valid, the counter
// never moved, so that slot still boots.
//
// Pure + host-tested so the brick-or-not boundary can't regress. The .ino owns the
// HAL half (esp_ota_get_state_partition / mark_app_valid / esp_ota_mark_app_invalid_
// rollback_and_reboot, and the boot counter persisted in NVS across the reverting
// resets); these decide WHEN it calls them.

// Mark the freshly-booted (pending) image valid iff it completed a real report cycle.
// Returns false → leave it pending (the bootloader's safety net stays armed, and the
// anti-rollback counter stays put). `report_cycle_ok` is ptw_report_cycle_ok's result
// for this boot. Deliberately tiny: the value is that the rule is NAMED, pure, and
// pinned — "valid" can never silently drift to "reached main()" again.
inline bool ptw_ota_should_mark_valid(bool report_cycle_ok) {
  return report_cycle_ok;
}

// Should we give up on a still-pending image and force the rollback now (rather than
// wait for yet another failed cycle)? `boots` = how many times the pending image has
// booted without ever achieving a good report cycle; `max_boots` = the budget. A
// healthy image is never rolled back (the first arg short-circuits), so a transient
// first-boot hiccup that recovers by boot 2 is fine. Exhausting the budget without a
// single good cycle is the signal to revert to the known-good slot.
inline bool ptw_ota_should_rollback(bool report_cycle_ok, int boots, int max_boots) {
  if (report_cycle_ok) return false;   // proved itself — keep it, mark valid instead
  return boots >= max_boots;           // out of chances without a good cycle → revert
}
