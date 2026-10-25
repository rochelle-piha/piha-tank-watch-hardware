#include "../water_level/config.h"
#include "../water_level/logic.h"
#include <cstdio>

static_assert(SETUP_PORTAL_TIMEOUT_MS == 3600000UL, "Setup must allow 60 minutes");
static_assert(SLEEP_RETRY_CAP_MS == 300000UL, "Reporting retry cap is unrelated");
static int passed = 0, failed = 0;
#define CHECK(name, condition) do { if (condition) ++passed; else { ++failed; std::printf("FAIL %s\n", name); } } while (0)

int main() {
  const uint32_t start = 1000;
  CHECK("opens at start", ptw_setup_window_open(start, start, SETUP_PORTAL_TIMEOUT_MS, false));
  CHECK("still open after former five-minute deadline", ptw_setup_window_open(start + 300000, start, SETUP_PORTAL_TIMEOUT_MS, false));
  CHECK("open just before sixty minutes", ptw_setup_window_open(start + 3599999, start, SETUP_PORTAL_TIMEOUT_MS, false));
  CHECK("closes at sixty minutes", !ptw_setup_window_open(start + 3600000, start, SETUP_PORTAL_TIMEOUT_MS, false));
  CHECK("completed setup exits promptly", !ptw_setup_window_open(start + 100, start, SETUP_PORTAL_TIMEOUT_MS, true));
  const uint32_t wrappedStart = UINT32_MAX - 1000;
  CHECK("rollover preserves setup window", ptw_setup_window_open(500, wrappedStart, SETUP_PORTAL_TIMEOUT_MS, false));
  CHECK("rollover expires at sixty minutes", !ptw_setup_window_open(static_cast<uint32_t>(wrappedStart + 3600000U), wrappedStart, SETUP_PORTAL_TIMEOUT_MS, false));
  CHECK("confirmed link completes", ptw_link_succeeded(200, true, true));
  CHECK("wrong code keeps setup open", !ptw_link_succeeded(200, true, false));
  CHECK("invalid response keeps setup open", !ptw_link_succeeded(200, false, true));
  CHECK("HTTP failure keeps setup open", !ptw_link_succeeded(500, true, true));
  std::printf("setup tests: %d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
