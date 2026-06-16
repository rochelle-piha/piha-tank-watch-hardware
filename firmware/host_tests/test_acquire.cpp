// test_acquire.cpp — host tests for the HAL-injected sensor acquisition.
//
// Exercises ptw_distance_from_echo_us (the echo→cm conversion) and
// ptw_acquire_reading (collect-N-pings → median) by injecting a *mock ping
// source* — so the full acquire flow, including the all-invalid / too-few-valid
// failure paths, runs through the exact code the device runs, on g++ in ms,
// with no bench. This is the mocked-HAL layer: the "HAL" is the ping callback.
//
// Build + run:
//   g++ -std=c++17 -Wall -Wextra -Werror -o t firmware/host_tests/test_acquire.cpp && ./t

#include "../water_level/logic.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int g_passed = 0, g_failed = 0;

#define CHECK(name, cond)                                          \
  do {                                                              \
    if (cond) { g_passed++; }                                       \
    else { g_failed++; std::printf("FAIL  %s\n", name); }           \
  } while (0)

static bool feq(float a, float b) { return std::fabs(a - b) < 1e-3f; }

// Mock HAL: a canned ping sequence. Stands in for readDistance() — returns each
// value in turn, then -1 (timeout) once exhausted, exactly like a dead sensor.
struct MockPings {
  std::vector<float> seq;
  size_t i = 0;
  float operator()() { return i < seq.size() ? seq[i++] : -1.0f; }
};

// ── echo → cm conversion ──────────────────────────────────────────────────────

static void test_echo_conversion() {
  CHECK("echo: timeout (us=0) → -1 sentinel",  feq(ptw_distance_from_echo_us(0), -1.0f));
  CHECK("echo: negative us → -1 sentinel",     feq(ptw_distance_from_echo_us(-5), -1.0f));
  CHECK("echo: 2000us → 34.0cm",               feq(ptw_distance_from_echo_us(2000), 34.0f));
  CHECK("echo: 10000us → 170.0cm",             feq(ptw_distance_from_echo_us(10000), 170.0f));
  CHECK("echo: 5882us → ~100cm",               feq(ptw_distance_from_echo_us(5882), 99.994f));
  CHECK("echo: 1us → tiny but positive",       ptw_distance_from_echo_us(1) > 0.0f);
  // The conversion is monotonic — a longer echo is always a greater distance.
  CHECK("echo: monotonic", ptw_distance_from_echo_us(3000) > ptw_distance_from_echo_us(2999));
}

// ── full acquire flow through the mock HAL ─────────────────────────────────────

static void test_acquire_via_mock_hal() {
  float out = -1.0f;
  const int N = 5, MIN = 3;

  // All five pings good, unsorted → median of {98,99,100,101,102}.
  {
    MockPings hal{{101, 98, 102, 99, 100}};
    CHECK("acquire: 5 good pings → true", ptw_acquire_reading(hal, N, MIN, 0, 400, &out));
    CHECK("acquire: 5 good pings → median 100", feq(out, 100.0f));
  }

  // A wild outlier (sensor spike) is filtered; median taken from the rest.
  // In-range {99,100,101,102} sorted → samples[2] = 101.
  {
    MockPings hal{{100, 950, 99, 101, 102}};   // 950 > 400, dropped
    CHECK("acquire: outlier filtered → true", ptw_acquire_reading(hal, N, MIN, 0, 400, &out));
    CHECK("acquire: outlier filtered → 101", feq(out, 101.0f));
  }

  // Two sensor timeouts (-1) excluded; exactly MIN good remain → still succeeds.
  {
    MockPings hal{{-1, 150, -1, 151, 152}};
    CHECK("acquire: timeouts excluded, exactly MIN → true",
          ptw_acquire_reading(hal, N, MIN, 0, 400, &out));
    CHECK("acquire: timeout-filtered median 151", feq(out, 151.0f));
  }

  // One fewer than MIN valid → failure, out untouched.
  {
    out = -42.0f;
    MockPings hal{{-1, -1, -1, 20, 30}};
    CHECK("acquire: below MIN valid → false", !ptw_acquire_reading(hal, N, MIN, 0, 400, &out));
    CHECK("acquire: out untouched on failure", feq(out, -42.0f));
  }

  // A fully dead sensor (every ping times out) → failure, never a fake value.
  {
    MockPings hal{};  // exhausted immediately → all -1
    CHECK("acquire: dead sensor → false", !ptw_acquire_reading(hal, N, MIN, 0, 400, &out));
  }
}

// ── integration: echo µs → conversion → acquire (the real readDistance path) ──

static void test_echo_to_acquire_integration() {
  float out = -1.0f;
  // Feed raw echo *microseconds* through the conversion the way readDistance
  // does, into acquire — proving the two host-tested halves compose into the
  // device's real sensor path. 2000us=34, 2059us≈35, 2118us≈36, plus a timeout
  // (0us→-1, filtered) and a spike (60000us≈1020cm, filtered).
  struct EchoToCm {
    std::vector<long> us; size_t i = 0;
    float operator()() { return ptw_distance_from_echo_us(i < us.size() ? us[i++] : 0); }
  };
  EchoToCm hal{{2000, 0, 2059, 60000, 2118}};   // valid: 34, 35, 36 → median 35
  CHECK("integration: echo→acquire → true", ptw_acquire_reading(hal, 5, 3, 0, 400, &out));
  CHECK("integration: median of {34,35,36} = 35", feq(out, 35.003f));
}

int main() {
  test_echo_conversion();
  test_acquire_via_mock_hal();
  test_echo_to_acquire_integration();
  std::printf("\nacquire host tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
