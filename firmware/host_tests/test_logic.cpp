// test_logic.cpp — host-side unit tests for firmware/water_level/logic.h.
//
// Plain C++, no framework, no Arduino: build + run with
//   g++ -std=c++17 -Wall -Wextra -Werror -o t firmware/host_tests/test_logic.cpp && ./t
// Exit code is non-zero on any failure.

#include "../water_level/logic.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int g_passed = 0, g_failed = 0;

#define CHECK(name, cond)                                          \
  do {                                                              \
    if (cond) { g_passed++; }                                       \
    else { g_failed++; std::printf("FAIL  %s\n", name); }           \
  } while (0)

static bool feq(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// ── ptw_median_reading ────────────────────────────────────────────────────────

static void test_median() {
  float out = -1.0f;

  // Happy path: 5 valid pings, unsorted — median of sorted {98,99,100,101,102}.
  {
    const float p[] = {101.0f, 98.0f, 102.0f, 99.0f, 100.0f};
    CHECK("median: 5 valid returns true", ptw_median_reading(p, 5, 3, 0.0f, 400.0f, &out));
    CHECK("median: 5 valid value", feq(out, 100.0f));
  }

  // Outlier rejection: a wild spike is filtered, median taken from the rest.
  // Valid set sorted: {99,100,101,102} (4 values) → median = samples[2] = 101.
  {
    const float p[] = {100.0f, 399.9f * 2, 99.0f, 101.0f, 102.0f};  // 799.8 is out of range
    CHECK("median: spike filtered", ptw_median_reading(p, 5, 3, 0.0f, 400.0f, &out));
    CHECK("median: spike-filtered value", feq(out, 101.0f));
  }

  // Failed-ping sentinel (-1 from a pulseIn timeout) is excluded.
  {
    const float p[] = {-1.0f, 150.0f, -1.0f, 151.0f, 152.0f};
    CHECK("median: -1 sentinels excluded, 3 valid passes",
          ptw_median_reading(p, 5, 3, 0.0f, 400.0f, &out));
    CHECK("median: sentinel-filtered value", feq(out, 151.0f));
  }

  // Exactly min_valid valid pings → still passes (boundary).
  {
    const float p[] = {-1.0f, -1.0f, 10.0f, 20.0f, 30.0f};
    CHECK("median: exactly min_valid passes", ptw_median_reading(p, 5, 3, 0.0f, 400.0f, &out));
    CHECK("median: exactly-min value", feq(out, 20.0f));
  }

  // One fewer than min_valid → fails, and out is left untouched.
  {
    out = -42.0f;
    const float p[] = {-1.0f, -1.0f, -1.0f, 20.0f, 30.0f};
    CHECK("median: below min_valid fails", !ptw_median_reading(p, 5, 3, 0.0f, 400.0f, &out));
    CHECK("median: out untouched on failure", feq(out, -42.0f));
  }

  // All pings invalid → fails.
  {
    const float p[] = {-1.0f, 0.0f, 400.0f, 500.0f, -3.0f};
    CHECK("median: all invalid fails", !ptw_median_reading(p, 5, 3, 0.0f, 400.0f, &out));
  }

  // Bounds are strict: exactly lo (0) and exactly hi (400) are NOT plausible —
  // 0 is the sensor's failure value and 400 is beyond the JSN-SR04T's range.
  {
    const float p[] = {0.0f, 400.0f, 100.0f, 100.0f, 100.0f};
    CHECK("median: bounds exclusive, 3 valid", ptw_median_reading(p, 5, 3, 0.0f, 400.0f, &out));
    CHECK("median: bounds-exclusive value", feq(out, 100.0f));
    const float q[] = {0.0f, 400.0f, 0.0f, 400.0f, 100.0f};
    CHECK("median: only 1 in-bounds fails", !ptw_median_reading(q, 5, 3, 0.0f, 400.0f, &out));
  }

  // Even count of valid pings: median = samples[n/2] (upper of the two middles)
  // — pins the firmware's actual behaviour, not a textbook average.
  {
    const float p[] = {10.0f, 20.0f, 30.0f, 40.0f, -1.0f};
    CHECK("median: even count passes", ptw_median_reading(p, 5, 3, 0.0f, 400.0f, &out));
    CHECK("median: even count takes upper-middle", feq(out, 30.0f));
  }

  // Duplicates are fine.
  {
    const float p[] = {100.0f, 100.0f, 100.0f, 100.0f, 100.0f};
    CHECK("median: duplicates", ptw_median_reading(p, 5, 3, 0.0f, 400.0f, &out));
    CHECK("median: duplicates value", feq(out, 100.0f));
  }

  // count cap: >16 pings doesn't overflow the internal buffer (first 16 used).
  {
    float p[32];
    for (int i = 0; i < 32; i++) p[i] = 100.0f + i;  // all valid
    CHECK("median: count capped at 16 (no overflow)",
          ptw_median_reading(p, 32, 3, 0.0f, 400.0f, &out));
    CHECK("median: capped median from first 16", feq(out, 108.0f));  // {100..115}[8]
  }
}

// ── ptw_clamp_interval_ms ─────────────────────────────────────────────────────

static void test_clamp() {
  struct Case { long secs; unsigned long want_ms; const char* name; };
  const Case cases[] = {
      {0,      0UL,            "clamp: 0 keeps current (returns 0)"},
      {-1,     0UL,            "clamp: negative keeps current"},
      {-86400, 0UL,            "clamp: large negative keeps current"},
      {1,      30000UL,        "clamp: 1s floors to 30s"},
      {29,     30000UL,        "clamp: 29s floors to 30s"},
      {30,     30000UL,        "clamp: exactly 30s passes"},
      {31,     31000UL,        "clamp: 31s passes through"},
      {60,     60000UL,        "clamp: 60s (Pro tier)"},
      {3600,   3600000UL,      "clamp: 3600s (Plus tier)"},
      {86399,  86399000UL,     "clamp: 86399s passes through"},
      {86400,  86400000UL,     "clamp: exactly 24h passes (Free tier)"},
      {86401,  86400000UL,     "clamp: 86401s ceilings to 24h"},
      {999999999L, 86400000UL, "clamp: huge value ceilings to 24h"},
  };
  for (const auto& c : cases) CHECK(c.name, ptw_clamp_interval_ms(c.secs) == c.want_ms);
}

// ── ptw_sleep_duration_ms ────────────────────────────────────────────────────

static void test_sleep_duration() {
  const unsigned long CAP = 300000UL;            // 5 min retry cap (config default)
  struct Case { bool ok; unsigned long interval; unsigned long want; const char* name; };
  const Case cases[] = {
      {true,  86400000UL, 86400000UL, "sleep: success sleeps full daily interval"},
      {true,  3600000UL,  3600000UL,  "sleep: success sleeps full hourly interval"},
      {true,  30000UL,    30000UL,    "sleep: success sleeps short interval"},
      {false, 86400000UL, 300000UL,   "sleep: failure caps daily to 5min retry"},
      {false, 3600000UL,  300000UL,   "sleep: failure caps hourly to 5min retry"},
      {false, 300000UL,   300000UL,   "sleep: failure at exactly the cap"},
      {false, 30000UL,    30000UL,    "sleep: failure never EXTENDS a short interval"},
  };
  for (const auto& c : cases)
    CHECK(c.name, ptw_sleep_duration_ms(c.ok, c.interval, CAP) == c.want);
}

// ── ptw_device_id_from_mac ───────────────────────────────────────────────────

static void test_device_id_from_mac() {
  char id[13];

  // 6 MAC bytes → 12 lowercase hex chars, byte order preserved, high nibble first.
  {
    const unsigned char mac[6] = {0x24, 0x6f, 0x28, 0xab, 0xcd, 0xef};
    ptw_device_id_from_mac(mac, id);
    CHECK("mac id: 12 lowercase hex of the MAC bytes", std::strcmp(id, "246f28abcdef") == 0);
    CHECK("mac id: NUL-terminated at 12", id[12] == '\0' && std::strlen(id) == 12);
  }

  // Zero-padding of low bytes + nibble order (0x0a → "0a", not "a0").
  {
    const unsigned char mac[6] = {0x00, 0x01, 0x0a, 0xff, 0x10, 0x00};
    ptw_device_id_from_mac(mac, id);
    CHECK("mac id: zero-padding + nibble order", std::strcmp(id, "00010aff1000") == 0);
  }

  // The whole point: distinct MACs (differing in one byte) → distinct ids.
  {
    const unsigned char a[6] = {1, 2, 3, 4, 5, 6};
    const unsigned char b[6] = {1, 2, 3, 4, 5, 7};
    char ia[13], ib[13];
    ptw_device_id_from_mac(a, ia);
    ptw_device_id_from_mac(b, ib);
    CHECK("mac id: distinct MACs → distinct ids (no collision)", std::strcmp(ia, ib) != 0);
  }
}

// ── ptw_ota_* confirmed-boot gate ────────────────────────────────────────────

static void test_ota_gate() {
  // mark-valid is gated SOLELY on a real report cycle — the brick-or-not boundary.
  CHECK("ota: good report cycle → mark valid",  ptw_ota_should_mark_valid(true));
  CHECK("ota: failed report cycle → stay pending (NOT valid)",
        !ptw_ota_should_mark_valid(false));

  // A failed boot must NOT mark valid — which is
  // what keeps the anti-rollback eFuse counter from advancing past the good slot.
  // (mark-valid false ⇒ the .ino never calls mark_app_valid ⇒ counter unchanged.)
  CHECK("ota: failed boot does not self-validate (counter cannot advance)",
        ptw_ota_should_mark_valid(false) == false);

  // A healthy image is NEVER rolled back, regardless of the boot counter.
  CHECK("ota: healthy image never rolls back (boots 0)",
        !ptw_ota_should_rollback(true, 0, 3));
  CHECK("ota: healthy image never rolls back (boots at budget)",
        !ptw_ota_should_rollback(true, 3, 3));
  CHECK("ota: healthy image never rolls back (boots over budget)",
        !ptw_ota_should_rollback(true, 9, 3));

  // A pending (unhealthy) image rolls back only once it exhausts its boot budget —
  // a transient first-boot hiccup that recovers later is not punished.
  CHECK("ota: unhealthy under budget keeps trying", !ptw_ota_should_rollback(false, 1, 3));
  CHECK("ota: unhealthy one below budget keeps trying",
        !ptw_ota_should_rollback(false, 2, 3));
  CHECK("ota: unhealthy at budget rolls back", ptw_ota_should_rollback(false, 3, 3));
  CHECK("ota: unhealthy over budget rolls back", ptw_ota_should_rollback(false, 4, 3));

  // One-shot budget boundary (max_boots == 1): first failed boot triggers rollback.
  CHECK("ota: one-shot budget rolls back on first failed boot",
        ptw_ota_should_rollback(false, 1, 1));
}

int main() {
  test_median();
  test_clamp();
  test_sleep_duration();
  test_device_id_from_mac();
  test_ota_gate();
  std::printf("\nhost tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
