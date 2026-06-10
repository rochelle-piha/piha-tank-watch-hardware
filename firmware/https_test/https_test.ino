// Quick test: does HTTPS work in STA-only vs AP+STA mode?
// Hard-code your WiFi below, flash once, read serial output.
// Change WIFI_SSID / WIFI_PASS before flashing.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>

#define WIFI_SSID "YOUR_SSID_HERE"
#define WIFI_PASS "YOUR_PASS_HERE"
#define API_HOST  "api.pihatankwatch.nz"
#define API_URL   "https://api.pihatankwatch.nz/devices/send-otp"

static void testHTTPS(const char* label) {
  Serial.printf("\n--- %s ---\n", label);

  IPAddress resolved;
  bool dnsOk = WiFi.hostByName(API_HOST, resolved);
  Serial.printf("DNS %s -> %s\n", API_HOST, dnsOk ? resolved.toString().c_str() : "FAILED");

  if (!dnsOk) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(12000);
  http.begin(client, API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", "test:test");
  int code = http.POST("{\"email\":\"test@test.com\"}");
  Serial.printf("HTTP result: %d  (401=auth failed=API reachable, -1=can't connect)\n", code);
  if (code > 0) Serial.printf("Response: %s\n", http.getString().c_str());
  http.end();
  Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== HTTPS connectivity test ===");

  // Test 1: STA-only mode
  Serial.println("\nTest 1: STA-only mode");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting");
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected: %s\n", WiFi.localIP().toString().c_str());
    testHTTPS("STA-only");
  } else {
    Serial.println("WiFi connect failed — check SSID/pass");
    return;
  }

  delay(2000);

  // Test 2: AP+STA mode (same as portal)
  Serial.println("\nTest 2: AP+STA mode");
  IPAddress apIP(10, 4, 0, 1);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("PTW-test");
  delay(500);
  Serial.printf("AP up at %s, STA: %s\n",
    WiFi.softAPIP().toString().c_str(),
    WiFi.localIP().toString().c_str());
  testHTTPS("AP+STA");
}

void loop() {}
