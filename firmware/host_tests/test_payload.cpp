// test_payload.cpp — host tests for the reading-payload wire contract.
//
// Pins WHICH fields a reading POST carries — exact names, types, rounding, and
// presence rules — against logic.h's ptw_build_reading_payload. The doc is a
// tiny fake recorder (operator[]= into maps), NOT ArduinoJson: the contract
// under test is ours (field names, types, rounding, and presence rules);
// the serializer is upstream's job and the ESP32 compile check covers the
// real-library path.
//
// Build + run:
//   g++ -std=c++17 -Wall -Wextra -Werror -o t firmware/host_tests/test_payload.cpp && ./t

#include "../water_level/logic.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>

static int g_passed = 0, g_failed = 0;

#define CHECK(name, cond)                                          \
  do {                                                              \
    if (cond) { g_passed++; }                                       \
    else { g_failed++; std::printf("FAIL  %s\n", name); }           \
  } while (0)

static bool feq(double a, double b) { return std::fabs(a - b) < 1e-4; }

// ── fake document: records key → typed value, like JsonDocument's operator[] ──

struct FakeDoc {
  std::map<std::string, double>      nums;
  std::map<std::string, std::string> strs;
  std::map<std::string, bool>        bools;

  struct Slot {
    FakeDoc* d;
    std::string key;
    void operator=(float v)       { d->nums[key] = v; }
    void operator=(long v)        { d->nums[key] = (double)v; }
    void operator=(int v)         { d->nums[key] = (double)v; }
    void operator=(bool v)        { d->bools[key] = v; }
    void operator=(const char* v) { d->strs[key] = v; }
  };
  Slot operator[](const char* k) { return Slot{this, k}; }

  std::set<std::string> keys() const {
    std::set<std::string> out;
    for (auto& kv : nums)  out.insert(kv.first);
    for (auto& kv : strs)  out.insert(kv.first);
    for (auto& kv : bools) out.insert(kv.first);
    return out;
  }
};

static PtwTelemetry usb_connected() {
  PtwTelemetry t = {};
  t.firmware_version = "1.1.0";
  t.sensor_ok = true;
  t.has_rssi = true;
  t.rssi = -67;
  t.has_battery = false;
  return t;
}

// ── presence rules ────────────────────────────────────────────────────────────

static void test_presence() {
  // USB build, connected: exactly these keys — battery keys MUST be absent
  // (absent ≠ 0%: the app renders absence as "not applicable"; a 0 would show
  // a critical-battery alarm on every mains-powered unit).
  {
    FakeDoc d;
    ptw_build_reading_payload(d, 142.55f, usb_connected());
    const std::set<std::string> want =
        {"distance_cm", "firmware_version", "sensor_ok", "rssi"};
    CHECK("presence: USB+connected = exactly 4 keys", d.keys() == want);
  }

  // Not connected: rssi omitted too.
  {
    FakeDoc d;
    PtwTelemetry t = usb_connected();
    t.has_rssi = false;
    ptw_build_reading_payload(d, 100.0f, t);
    const std::set<std::string> want = {"distance_cm", "firmware_version", "sensor_ok"};
    CHECK("presence: no-rssi = exactly 3 keys", d.keys() == want);
  }

  // Battery build: battery_v + battery_pct appear.
  {
    FakeDoc d;
    PtwTelemetry t = usb_connected();
    t.has_battery = true;
    t.battery_v = 3.912f;
    t.battery_pct = 68;
    ptw_build_reading_payload(d, 100.0f, t);
    const std::set<std::string> want = {"distance_cm", "firmware_version",
                                        "sensor_ok", "rssi",
                                        "battery_v", "battery_pct"};
    CHECK("presence: battery build = exactly 6 keys", d.keys() == want);
  }
}

// ── exact field names = the API contract ─────────────────────────────────────

static void test_field_names() {
  // These names are read verbatim by the API. A rename here silently drops the
  // field server-side — this test makes that a host-test failure instead.
  FakeDoc d;
  PtwTelemetry t = usb_connected();
  t.has_battery = true;
  t.battery_v = 4.0f;
  t.battery_pct = 78;
  ptw_build_reading_payload(d, 50.0f, t);
  CHECK("name: distance_cm",      d.nums.count("distance_cm") == 1);
  CHECK("name: firmware_version", d.strs.count("firmware_version") == 1);
  CHECK("name: sensor_ok (bool)", d.bools.count("sensor_ok") == 1);
  CHECK("name: rssi (numeric)",   d.nums.count("rssi") == 1);
  CHECK("name: battery_v",        d.nums.count("battery_v") == 1);
  CHECK("name: battery_pct",      d.nums.count("battery_pct") == 1);
}

// ── values: rounding + passthrough ────────────────────────────────────────────

static void test_values() {
  {
    FakeDoc d;
    ptw_build_reading_payload(d, 142.55f, usb_connected());
    CHECK("value: distance rounds to 1dp (142.55→142.6)", feq(d.nums["distance_cm"], 142.6));
    CHECK("value: firmware_version passthrough", d.strs["firmware_version"] == "1.1.0");
    CHECK("value: sensor_ok true", d.bools["sensor_ok"] == true);
    CHECK("value: rssi passthrough", feq(d.nums["rssi"], -67.0));
  }
  {
    FakeDoc d;
    ptw_build_reading_payload(d, 99.96f, usb_connected());
    CHECK("value: distance 99.96→100.0", feq(d.nums["distance_cm"], 100.0));
  }
  {
    FakeDoc d;
    PtwTelemetry t = usb_connected();
    t.sensor_ok = false;          // future health-ping path — must serialize as false
    ptw_build_reading_payload(d, 10.0f, t);
    CHECK("value: sensor_ok false preserved", d.bools["sensor_ok"] == false);
  }
  {
    FakeDoc d;
    PtwTelemetry t = usb_connected();
    t.has_battery = true;
    t.battery_v = 3.9157f;        // → 3.92 at 2dp
    t.battery_pct = 68;
    ptw_build_reading_payload(d, 10.0f, t);
    CHECK("value: battery_v rounds to 2dp (3.9157→3.92)", feq(d.nums["battery_v"], 3.92));
    CHECK("value: battery_pct integer passthrough", feq(d.nums["battery_pct"], 68.0));
  }
}

// ── ptw_battery_pct (the % the wire carries; app thresholds <20/<10) ─────────

static void test_battery_pct() {
  const float FULL = 4.2f, EMPTY = 3.3f;   // 1S LiPo defaults from config.h
  struct Case { float v; int want; const char* name; };
  const Case cases[] = {
      {4.2f,  100, "pct: full = 100"},
      {3.3f,  0,   "pct: empty = 0"},
      {3.75f, 50,  "pct: midpoint = 50"},
      {5.0f,  100, "pct: above full clamps to 100"},
      {3.0f,  0,   "pct: below empty clamps to 0"},
      {0.0f,  0,   "pct: zero volts clamps to 0"},
      {3.93f, 70,  "pct: 3.93V → 70 (rounded)"},
      {3.48f, 20,  "pct: 3.48V → 20 (the warn threshold)"},
      {3.39f, 10,  "pct: 3.39V → 10 (the critical threshold)"},
  };
  for (const auto& c : cases)
    CHECK(c.name, ptw_battery_pct(c.v, FULL, EMPTY) == c.want);
  // Misconfigured thresholds must fail safe (0%) rather than ±∞ or NaN.
  CHECK("pct: equal thresholds → 0%",   ptw_battery_pct(4.2f, 4.2f, 4.2f) == 0);
  CHECK("pct: inverted thresholds → 0%",ptw_battery_pct(4.2f, 3.3f, 4.2f) == 0);
  CHECK("pct: zero/zero thresholds → 0%",ptw_battery_pct(0.0f, 0.0f, 0.0f) == 0);
}

int main() {
  test_presence();
  test_field_names();
  test_values();
  test_battery_pct();
  std::printf("\npayload host tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
