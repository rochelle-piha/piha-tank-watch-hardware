#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "mbedtls/md.h"
#include <time.h>     // time()/configTime — SNTP clock for TLS validity check
#include "config.h"
#include "certs.h"    // Amazon root CA bundle for TLS cert validation
#include "logic.h"   // pure, host-tested logic (median/clamp/payload/sleep)
#ifdef DEEP_SLEEP_ENABLED
#include <esp_sleep.h>   // timer + GPIO wake APIs
#endif

// A single acquired measurement. Kept as a struct (not a bare float) so future
// telemetry — battery, RSSI, sensor-fault, firmware_version — can ride
// along without changing the acquire→transport seam below.
//
// Declared at the top of the file on purpose: the Arduino .ino preprocessor
// auto-generates function prototypes (e.g. for acquireReading(Reading&)) and
// hoists them above the first function, so any user type they reference must be
// declared up here — otherwise the build fails with "'acquireReading' cannot be
// used as a function". Keep this above the first function definition.
struct Reading {
  float distance_cm;
};

Preferences prefs;
String      g_deviceId;
String      g_secret;
bool        g_registered;
// How long to wait between readings. Starts at the compile-time default but the
// API overrides it per-reading via `next_interval_secs` (see applyServerInterval).
uint32_t    g_intervalMs = READING_INTERVAL_MS;

static void autoRegister();
static String signinPage(const String& error);

// ── Crypto helpers ────────────────────────────────────────────────────────────

static String hexStr(const uint8_t* buf, size_t len) {
  String s;
  s.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    if (buf[i] < 0x10) s += '0';
    s += String(buf[i], HEX);
  }
  return s;
}

static String sha256Hex(const String& input) {
  uint8_t hash[32];
  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, info, 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const uint8_t*)input.c_str(), input.length());
  mbedtls_md_finish(&ctx, hash);
  mbedtls_md_free(&ctx);
  return hexStr(hash, 32);
}

// ── Secure transport (TLS cert validation) ───────────────────────────────────
//
// Every device HTTPS call goes through beginSecure() — there is no bare
// http.begin(url) and no setInsecure() anywhere. The server cert is validated
// against the embedded Amazon root bundle (certs.h), so a MITM on the customer's
// WiFi can't impersonate api.pihatankwatch.nz to harvest the device credential
// or inject fabricated readings.

// Sync the clock via SNTP so TLS can check the cert's validity dates. The ESP32
// boots at epoch 0 (1970); a handshake then fails "cert not yet valid" even with
// the right CA. Idempotent: if the clock is already plausible (e.g. RTC held it
// across a deep-sleep wake) it returns immediately without re-syncing. Returns
// false if a plausible clock can't be reached within the timeout — callers then
// fail the request closed rather than downgrade to an unvalidated connection.
static bool ensureClockSynced() {
  if (ptw_clock_is_plausible((long)time(nullptr))) return true;  // host-tested gate
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);    // UTC; cert dates are UTC
  unsigned long start = millis();
  while (!ptw_clock_is_plausible((long)time(nullptr)) &&
         millis() - start < NTP_SYNC_TIMEOUT_MS) {
    delay(200);
  }
  bool ok = ptw_clock_is_plausible((long)time(nullptr));
  if (!ok) Serial.println("SNTP: clock not synced within timeout");
  return ok;
}

// Configure `http` to talk to `url` over a CA-validated TLS connection, reusing
// the caller's WiFiClientSecure (must outlive the request). Returns false —
// FAIL CLOSED — if the clock isn't trustworthy yet or http.begin() fails, so the
// caller skips the call instead of leaking creds/readings over an unverified link.
static bool beginSecure(WiFiClientSecure& client, HTTPClient& http, const char* url) {
  if (!ensureClockSynced()) {
    Serial.println("TLS blocked: clock not synced — failing closed");
    return false;
  }
  client.setCACert(AMAZON_ROOT_CA_BUNDLE);
  return http.begin(client, url);
}

// ── Device credentials ────────────────────────────────────────────────────────

static void loadOrGenerateCredentials() {
  prefs.begin("ptw", false);
  g_deviceId   = prefs.getString("device_id", "");
  g_secret     = prefs.getString("secret",    "");
  g_registered = prefs.getBool("registered",  false);
  // Server-driven reporting interval survives reboots/deep-sleep: a
  // free-tier device wakes already knowing to sleep ~24h instead of burning a
  // post on the 60s compile-time default every cycle.
  g_intervalMs = prefs.getULong("interval_ms", READING_INTERVAL_MS);

  if (g_deviceId.isEmpty() || g_secret.isEmpty()) {
    // Device id from the eFuse MAC: the old 4-random-byte id had a 32-bit
    // space whose birthday-bound collision hit ~50% near 77k devices, and a
    // collision SILENTLY BRICKED the loser (its secret never matched the first
    // device's stored hash → 401 forever). The eFuse MAC is 48-bit, Espressif-
    // unique by construction — no RNG collision. The id isn't secret; the random
    // 32-byte SECRET below authenticates. Derivation is host-tested in logic.h;
    // this is the HAL seam. Existing provisioned devices keep their stored id —
    // this block only runs for a fresh or NVS-wiped device. (A wipe still orphans
    // the server record, same as today: the re-derived id matches but the secret
    // is new and auto-register won't re-key it — server-side wipe-recovery is a
    // follow-up, not done here.)
    // ESP.getEfuseMac() returns the 48-bit factory MAC (Arduino-native, no extra
    // include vs esp_efuse_mac_get_default). Extract its 6 bytes for the
    // host-tested derivation; LSB-first is fine — the id only needs to be unique
    // and stable per device, not a formatted MAC string.
    uint64_t macInt = ESP.getEfuseMac();
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)(macInt >> (8 * i));
    uint8_t secretBytes[32];
    esp_fill_random(secretBytes, sizeof(secretBytes));
    char idBuf[13];
    ptw_device_id_from_mac(mac, idBuf);
    g_deviceId = idBuf;
    g_secret   = hexStr(secretBytes, 32);
    prefs.putString("device_id", g_deviceId);
    prefs.putString("secret",    g_secret);
    prefs.putBool("registered",  false);
    g_registered = false;
    Serial.println("New device credentials generated (id from eFuse MAC)");
  }
  prefs.end();

  Serial.printf("\n=================================\nDevice ID: %s\n=================================\n\n", g_deviceId.c_str());
}

// ── Captive portal ────────────────────────────────────────────────────────────

static const char PORTAL_HTML[] PROGMEM =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>Piha Tank Watch</title>"
  "<style>"
  "*{box-sizing:border-box;margin:0;padding:0}"
  "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
       "background:#0f172a;color:#f1f5f9;padding:1.5rem 1rem;min-height:100vh}"
  ".brand{font-size:1.3rem;font-weight:600;margin-bottom:0.25rem}"
  ".sub{color:#94a3b8;font-size:0.9rem;margin-bottom:1.5rem}"
  ".net{background:#1e293b;border:1px solid #334155;border-radius:8px;"
       "padding:0.85rem 1rem;margin-bottom:0.5rem;cursor:pointer;"
       "display:flex;align-items:center;justify-content:space-between}"
  ".net:active{background:#334155}"
  ".sec{margin-top:1.5rem}"
  ".lbl{font-size:0.75rem;color:#94a3b8;text-transform:uppercase;"
        "letter-spacing:0.05em;margin-bottom:0.4rem}"
  "input{width:100%;padding:0.75rem;background:#1e293b;border:1px solid #334155;"
         "border-radius:8px;color:#f1f5f9;font-size:1rem;margin-bottom:0.75rem;outline:none}"
  "input:focus{border-color:#6366f1}"
  "button{width:100%;padding:0.9em;background:#6366f1;color:#fff;border:none;"
          "border-radius:8px;font-size:1rem;font-weight:600;cursor:pointer}"
  "button:disabled{opacity:0.6}"
  ".hint{color:#94a3b8;font-size:0.85rem;text-align:center;margin-top:1rem}"
  "</style></head><body>"
  "<div class='brand'>Piha Tank Watch</div>"
  "<div class='sub'>Connect sensor to your home WiFi</div>"
  "<div id='nets'><div class='hint'>Scanning for networks...</div></div>"
  "<form id='form' onsubmit='doSave(event)'>"
  "<div class='sec'>"
  "<div class='lbl'>Network</div>"
  "<input id='ssid' name='ssid' type='text' placeholder='Network name' autocomplete='off' required>"
  "<div class='lbl'>Password</div>"
  "<input id='pass' name='password' type='password' placeholder='Password'>"
  "<button id='btn' type='submit'>Connect</button>"
  "</div></form>"
  "<script>"
  "fetch('/scan').then(function(r){return r.json();}).then(function(nets){"
    "var el=document.getElementById('nets');"
    "if(!nets.length){el.innerHTML='<div class=\"hint\">No networks found</div>';return;}"
    "var html='';"
    "for(var i=0;i<nets.length;i++){"
      "var s=nets[i].ssid.replace(/\"/g,'&quot;').replace(/'/g,'&#39;');"
      "html+='<div class=\"net\" onclick=\"pick(\\'' +s+ '\\')\">';"
      "html+='<span>'+nets[i].ssid+'</span>';"
      "html+='<span style=\"color:#94a3b8;font-size:0.8rem\">'+sigBar(nets[i].rssi)+'</span>';"
      "html+='</div>';"
    "}"
    "el.innerHTML=html;"
  "}).catch(function(){document.getElementById('nets').innerHTML='';});"
  "function sigBar(r){return r>-50?'****':r>-65?'*** ':r>-75?'**  ':'*   ';}"
  "function pick(s){document.getElementById('ssid').value=s;document.getElementById('pass').focus();}"
  "function doSave(e){"
    "e.preventDefault();"
    "var btn=document.getElementById('btn');"
    "btn.textContent='Connecting...';btn.disabled=true;"
    "var fd=new FormData(document.getElementById('form'));"
    "fetch('/save',{method:'POST',body:new URLSearchParams(fd)})"
      ".then(function(r){return r.text();})"
      ".then(function(h){document.body.innerHTML=h;})"
      ".catch(function(){btn.textContent='Connect';btn.disabled=false;});"
  "}"
  "</script></body></html>";

static WebServer portalServer(80);
static DNSServer dnsServer;

static const char PAGE_CSS[] PROGMEM =
  "<meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>Piha Tank Watch</title>"
  "<style>"
  "*{box-sizing:border-box;margin:0;padding:0}"
  "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
       "background:#0f172a;color:#f1f5f9;padding:1.5rem 1rem;min-height:100vh}"
  ".brand{font-size:1.3rem;font-weight:600;margin-bottom:0.25rem}"
  ".sub{color:#94a3b8;font-size:0.9rem;margin-bottom:1.5rem}"
  ".lbl{font-size:0.75rem;color:#94a3b8;text-transform:uppercase;"
        "letter-spacing:0.05em;margin-bottom:0.4rem;margin-top:1rem}"
  "input[type=email],input[type=number],input[type=text]{"
    "width:100%;padding:0.75rem;background:#1e293b;border:1px solid #334155;"
    "border-radius:8px;color:#f1f5f9;font-size:1rem;outline:none;"
    "-webkit-appearance:none;appearance:none}"
  "input:focus{border-color:#6366f1}"
  "button[type=submit]{width:100%;padding:0.9em;background:#6366f1;color:#fff;border:none;"
    "border-radius:8px;font-size:1rem;font-weight:600;margin-top:1.25rem;"
    "-webkit-appearance:none;appearance:none}"
  ".err{color:#ef4444;font-size:0.85rem;margin-top:0.75rem}"
  ".hint{color:#94a3b8;font-size:0.85rem;margin-top:1rem}"
  "</style>";

static bool ensureRegisteredForClaim() {
  PtwClaimGate gate = ptw_claim_gate(WiFi.status() == WL_CONNECTED, g_registered);
  if (gate == PTW_CLAIM_WAIT_WIFI) return false;
  if (gate == PTW_CLAIM_REGISTER_FIRST) {
    autoRegister();
  }
  return g_registered;
}

static String registeringPage() {
  return signinPage("Sensor is registering with Piha Tank Watch. Wait a moment and try again.");
}

static String pageWrap(const String& body) {
  return String("<!DOCTYPE html><html><head>") + FPSTR(PAGE_CSS) +
         "</head><body><div class='brand'>Piha Tank Watch</div>" + body + "</body></html>";
}

static String signinPage(const String& error = "") {
  String err = error.length() ? "<div class='err'>" + error + "</div>" : "";
  return pageWrap(
    "<div class='sub'>Enter your account email to receive a verification code</div>"
    "<form action='/send-otp' method='post'>"
    "<div class='lbl'>Email</div>"
    "<input name='email' type='email' autocomplete='email' required>"
    + err +
    "<button type='submit'>Send verification code</button>"
    "</form>"
  );
}

static String codePage(const String& email, const String& error = "") {
  String err = error.length() ? "<div class='err'>" + error + "</div>" : "";
  return pageWrap(
    "<div class='sub'>Code sent to <b>" + email + "</b>. Check your inbox.</div>"
    "<form action='/link' method='post'>"
    "<input type='hidden' name='email' value='" + email + "'>"
    "<div class='lbl'>Verification code</div>"
    "<input name='otp' type='number' inputmode='numeric' placeholder='123456' autofocus required>"
    + err +
    "<button type='submit'>Link sensor to account</button>"
    "</form>"
    "<div class='hint'><a href='/signin' style='color:#94a3b8'>Wrong email? Start over</a></div>"
  );
}

static String donePage() {
  return pageWrap(
    "<div style='font-size:1.1rem;font-weight:600;margin-bottom:0.5rem'>All done!</div>"
    "<p class='sub'>Sensor linked. Open the Piha Tank Watch app to name your tank.</p>"
  );
}

static String connectingPage() {
  return String("<!DOCTYPE html><html><head>") + FPSTR(PAGE_CSS) +
    "<meta http-equiv='refresh' content='2;url=/signin'>"
    "</head><body>"
    "<div class='brand'>Piha Tank Watch</div>"
    "<div class='sub'>Connecting to WiFi&#8230;</div>"
    "<p style='color:#64748b;font-size:0.85rem'>This page will refresh automatically.</p>"
    "</body></html>";
}

// kept for linker — no longer used as PROGMEM
static const char SIGNIN_HTML[] PROGMEM =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>Piha Tank Watch</title>"
  "<style>"
  "*{box-sizing:border-box;margin:0;padding:0}"
  "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
       "background:#0f172a;color:#f1f5f9;padding:1.5rem 1rem;min-height:100vh}"
  ".brand{font-size:1.3rem;font-weight:600;margin-bottom:0.25rem}"
  ".sub{color:#94a3b8;font-size:0.9rem;margin-bottom:1.5rem}"
  ".lbl{font-size:0.75rem;color:#94a3b8;text-transform:uppercase;"
        "letter-spacing:0.05em;margin-bottom:0.4rem;margin-top:1rem}"
  "input{width:100%;padding:0.75rem;background:#1e293b;border:1px solid #334155;"
         "border-radius:8px;color:#f1f5f9;font-size:1rem;margin-bottom:0.25rem;outline:none}"
  "input:focus{border-color:#6366f1}"
  "button{width:100%;padding:0.9em;background:#6366f1;color:#fff;border:none;"
          "border-radius:8px;font-size:1rem;font-weight:600;cursor:pointer;margin-top:1.25rem}"
  "button:disabled{opacity:0.6}"
  ".err{color:#ef4444;font-size:0.85rem;margin-top:0.75rem;display:none}"
  ".hint{color:#94a3b8;font-size:0.85rem;margin-top:0.75rem}"
  "</style></head><body>"
  "<div class='brand'>Piha Tank Watch</div>"
  "<div id='view-email'>"
    "<div class='sub'>Enter your account email to receive a verification code</div>"
    "<div class='lbl'>Email</div>"
    "<input id='email' type='email' autocomplete='email' required>"
    "<div class='err' id='err1'></div>"
    "<button id='btn1' onclick='sendCode()'>Send verification code</button>"
  "</div>"
  "<div id='view-code' style='display:none'>"
    "<div class='sub' id='code-sub'></div>"
    "<div class='lbl'>6-digit code</div>"
    "<input id='code' type='number' inputmode='numeric' pattern='[0-9]{6}' placeholder='123456'>"
    "<div class='err' id='err2'></div>"
    "<button id='btn2' onclick='doLink()'>Link sensor to account</button>"
    "<div class='hint' onclick='showEmail()' style='cursor:pointer;text-align:center;margin-top:1rem'>"
      "Wrong email? Start over</div>"
  "</div>"
  "<script>"
  "function post(url,data,cb){"
    "var x=new XMLHttpRequest();"
    "x.open('POST',url,true);"
    "x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');"
    "x.onreadystatechange=function(){"
      "if(x.readyState===4){try{cb(null,JSON.parse(x.responseText));}catch(e){cb(e);}}"
    "};"
    "var p=Object.keys(data).map(function(k){return encodeURIComponent(k)+'='+encodeURIComponent(data[k]);}).join('&');"
    "x.send(p);"
  "}"
  "function showEmail(){"
    "document.getElementById('view-code').style.display='none';"
    "document.getElementById('view-email').style.display='block';"
  "}"
  "function sendCode(){"
    "var email=document.getElementById('email').value.trim();"
    "var err=document.getElementById('err1');"
    "if(!email){err.textContent='Please enter your email';err.style.display='block';return;}"
    "err.style.display='none';"
    "document.getElementById('view-email').style.display='none';"
    "document.getElementById('view-code').style.display='block';"
    "document.getElementById('code-sub').textContent='Sending code to '+email+'...';"
    "post('/send-otp',{email:email},function(err,d){"
      "if(err||!d||d.error){"
        "document.getElementById('view-code').style.display='none';"
        "document.getElementById('view-email').style.display='block';"
        "document.getElementById('err1').textContent=(d&&d.error)||'Could not send code. Check WiFi and try again.';"
        "document.getElementById('err1').style.display='block';"
      "}else{"
        "document.getElementById('code-sub').textContent='Code sent to '+email+'. Check your inbox.';"
        "document.getElementById('code').focus();"
      "}"
    "});"
  "}"
  "function doLink(){"
    "var email=document.getElementById('email').value.trim();"
    "var code=document.getElementById('code').value.trim();"
    "var btn=document.getElementById('btn2');"
    "var err=document.getElementById('err2');"
    "if(!code){err.textContent='Please enter the code';err.style.display='block';return;}"
    "btn.textContent='Linking...';btn.disabled=true;err.style.display='none';"
    "post('/link',{email:email,otp:code},function(e,d){"
      "if(d&&d.linked){"
        "document.body.innerHTML="
          "'<div style=\"font-family:-apple-system,sans-serif;background:#0f172a;color:#f1f5f9;padding:1.5rem;min-height:100vh\">"
          "<div style=\"font-size:1.3rem;font-weight:600;margin-bottom:0.5rem\">All done!</div>"
          "<p style=\"color:#94a3b8\">Sensor linked. Open the Piha Tank Watch app to name your tank.</p>"
          "</div>';"
      "}else{"
        "err.textContent=(d&&d.error)||'Something went wrong';"
        "err.style.display='block';btn.textContent='Link sensor to account';btn.disabled=false;"
      "}"
    "});"
  "}"
  "</script></body></html>";

static void runCaptivePortal() {
  String apName = "PihaTankWatch-" + g_deviceId.substring(0, 4);
  // Use 10.4.0.0/24 to avoid conflicting with any common home router subnet
  IPAddress apIP(10, 4, 0, 1);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName.c_str());
  Serial.printf("AP started: %s  IP: %s\n", apName.c_str(), apIP.toString().c_str());

  dnsServer.start(53, "*", apIP);

  bool wifiSaved = false;

  portalServer.on("/", HTTP_GET, [&]() {
    portalServer.send(200, "text/html", wifiSaved ? signinPage() : FPSTR(PORTAL_HTML));
  });
  portalServer.onNotFound([&]() {
    if (portalServer.hostHeader() != WiFi.softAPIP().toString().c_str()) {
      portalServer.sendHeader("Location", "http://" + apIP.toString());
      portalServer.send(302);
    } else {
      portalServer.send(200, "text/html", wifiSaved ? signinPage() : FPSTR(PORTAL_HTML));
    }
  });

  portalServer.on("/scan", HTTP_GET, [&]() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      ssid.replace("\"", "\\\"");
      if (i) json += ",";
      json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    WiFi.scanDelete();
    portalServer.send(200, "application/json", json);
  });

  // Step 1: save WiFi credentials and start connecting
  portalServer.on("/save", HTTP_POST, [&]() {
    String ssid = portalServer.arg("ssid");
    String pass = portalServer.arg("password");
    if (ssid.isEmpty()) { portalServer.send(400, "text/plain", "SSID required"); return; }

    prefs.begin("ptw", false);
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", pass);
    prefs.end();

    WiFi.begin(ssid.c_str(), pass.c_str());
    wifiSaved = true;

    portalServer.send(200, "text/html", connectingPage());
  });

  portalServer.on("/signin", HTTP_GET, [&]() {
    if (WiFi.status() != WL_CONNECTED) {
      portalServer.send(200, "text/html", connectingPage());
    } else if (!ensureRegisteredForClaim()) {
      portalServer.send(200, "text/html", registeringPage());
    } else {
      portalServer.send(200, "text/html", signinPage());
    }
  });

  // Step 2: call send-otp synchronously (WiFi is guaranteed connected by this point)
  portalServer.on("/send-otp", HTTP_POST, [&]() {
    String email = portalServer.arg("email");
    if (email.isEmpty()) {
      portalServer.send(200, "text/html", signinPage("Please enter your email."));
      return;
    }
    if (WiFi.status() != WL_CONNECTED) {
      portalServer.send(200, "text/html", signinPage("WiFi not connected — please wait and try again."));
      return;
    }
    if (!ensureRegisteredForClaim()) {
      portalServer.send(200, "text/html", registeringPage());
      return;
    }
    String staIP = WiFi.localIP().toString();
    IPAddress resolved;
    bool dnsOk = WiFi.hostByName(API_HOST, resolved);
    if (!dnsOk) {
      portalServer.send(200, "text/html", signinPage("DNS failed (STA IP: " + staIP + "). Try again."));
      return;
    }
    // Fail closed: the OTP + email (and the device creds on /link) must not ride
    // an unvalidated connection on the customer's WiFi. send-otp runs over
    // the already-joined station link (gated on WL_CONNECTED above), so there's no
    // third-party captive portal to accommodate — validate like every other call.
    WiFiClientSecure client;
    HTTPClient http;
    if (!beginSecure(client, http, "https://" API_HOST "/devices/send-otp")) {
      portalServer.send(200, "text/html",
                        signinPage("Secure connection failed (clock sync). Wait a moment and try again."));
      return;
    }
    http.setTimeout(12000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key", g_deviceId + ":" + g_secret);
    int code = http.POST("{\"email\":\"" + email + "\"}");
    http.end();
    if (code == 200) {
      portalServer.send(200, "text/html", codePage(email));
    } else {
      String err = "Failed (HTTP " + String(code) + ", STA:" + staIP + " DNS:" + resolved.toString() + " heap:" + String(ESP.getFreeHeap()) + ")";
      portalServer.send(200, "text/html", signinPage(err));
    }
  });

  // Step 2b: verify OTP — returns success or re-renders code page with error
  portalServer.on("/link", HTTP_POST, [&]() {
    String email = portalServer.arg("email");
    String otp   = portalServer.arg("otp");
    if (email.isEmpty() || otp.isEmpty()) {
      portalServer.send(200, "text/html", codePage(email, "Please enter the code."));
      return;
    }

    WiFiClientSecure client;
    HTTPClient http;
    if (!beginSecure(client, http, LINK_URL)) {
      portalServer.send(200, "text/html",
                        codePage(email, "Secure connection failed (clock sync). Try again."));
      return;
    }
    http.setTimeout(8000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key", g_deviceId + ":" + g_secret);
    int status = http.POST("{\"email\":\"" + email + "\",\"otp\":\"" + otp + "\"}");
    http.end();

    if (status == 200) {
      portalServer.send(200, "text/html", donePage());
    } else {
      portalServer.send(200, "text/html", codePage(email, "Invalid or expired code. Try again."));
    }
  });

  portalServer.begin();

  unsigned long start = millis();
  while (millis() - start < 300000UL) {
    dnsServer.processNextRequest();
    portalServer.handleClient();


    delay(10);
  }

  dnsServer.stop();
  portalServer.stop();
  WiFi.softAPdisconnect(true);
}

// ── WiFi setup ────────────────────────────────────────────────────────────────

static void setupWiFi() {
  prefs.begin("ptw", true);
  String ssid = prefs.getString("wifi_ssid", "");
  String pass = prefs.getString("wifi_pass", "");
  prefs.end();

  if (!ssid.isEmpty()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000UL) delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    runCaptivePortal();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected (%s)\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi connection failed — restarting");
    ESP.restart();
  }
}

// ── Auto-register ─────────────────────────────────────────────────────────────

static void autoRegister() {
  if (g_registered) return;

  WiFiClientSecure client;
  HTTPClient http;
  if (!beginSecure(client, http, REGISTER_URL)) {
    Serial.println("Registration skipped — secure connection unavailable, will retry next boot");
    return;  // fail closed; g_registered stays false so we retry
  }
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["device_id"]    = g_deviceId;
  doc["api_key_hash"] = sha256Hex(g_secret);
  String body;
  serializeJson(doc, body);

  int status = http.POST(body);
  http.end();

  if (status == 200 || status == 409) {
    prefs.begin("ptw", false);
    prefs.putBool("registered", true);
    prefs.end();
    g_registered = true;
    Serial.println("Registered with server");
  } else {
    Serial.printf("Registration failed (%d) — will retry next boot\n", status);
  }
}

// ── Sensor ────────────────────────────────────────────────────────────────────

static float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long us = pulseIn(ECHO_PIN, HIGH, 30000);
  return ptw_distance_from_echo_us(us);   // conversion is host-tested
}

// Acquire one reading from the sensor, independent of how it's transported.
// Returns false when too few pings are valid (caller skips reporting). This is
// the "acquire" half of the acquire→transport seam: swapping the sensor
// changes only this function; swapping the transport (WiFi → LoRa) changes only
// transportReport() below — neither touches the other.
//
// A single JSN-SR04T ping is noisy over a moving water surface (ripples, foam,
// condensation, spurious echoes), so we take SENSOR_SAMPLES pings and report the
// **median** of the in-range ones. The median rejects outliers a mean
// would smear in; with the per-tier cadence a free device stores one reading a
// day, so a single bad ping must not become that day's value.
static bool acquireReading(Reading& out) {
  // HAL-injected: ptw_acquire_reading owns the collect→median flow (and
  // is host-tested through the same code path); this lambda is the only HAL
  // part — one sensor ping plus the settle delay between pings.
  return ptw_acquire_reading(
      []() -> float { float d = readDistance(); delay(SENSOR_PING_GAP_MS); return d; },
      SENSOR_SAMPLES, SENSOR_MIN_VALID, 0.0f, 400.0f, &out.distance_cm);
}

// ── Reporting cadence ──────────────────────────────────────────────────────────

// The API tells us how often to report based on the tank's plan tier
// (free=daily, Plus=hourly, Pro+=per-minute) via `next_interval_secs` on every
// /reading response — including the 429 rate-limit reply. Honor it: a free-tier
// device posting every minute needlessly burns device power and ignores the
// server's cadence. Falls back to the current cadence if the field is absent.
static void applyServerInterval(long secs) {
  // Clamp/convert decision lives in logic.h: 30s floor,
  // 24h ceiling, 0 = field missing/invalid → keep current cadence.
  uint32_t ms = (uint32_t)ptw_clamp_interval_ms(secs);
  if (ms != 0 && ms != g_intervalMs) {
    g_intervalMs = ms;
    // Persist so the interval survives reboot/deep-sleep. NVS write only
    // on change — tier changes are rare, so flash wear is a non-issue.
    prefs.begin("ptw", false);
    prefs.putULong("interval_ms", g_intervalMs);
    prefs.end();
    Serial.printf("Reporting interval set to %lus (from server)\n",
                  (unsigned long)(ms / 1000UL));
  }
}

// ── Transport ───────────────────────────────────────────────────────────────
//
// The "transport" half of the acquire→transport seam: how an acquired
// Reading reaches the API. This is the WiFi-direct transport — the device
// POSTs the reading to the API itself and adapts cadence from the response.
//
// A LoRa long-range variant would supply an alternative transportReport()
// that transmits the Reading over radio to a house receiver, which POSTs it to
// the API on the tank unit's behalf. The acquisition and cadence code stay put;
// only this function is swapped.

// Sample the battery voltage through the configured divider. Only built
// when BATTERY_ADC_PIN is set — USB-powered units have no battery and skip this.
#ifdef BATTERY_ADC_PIN
static float readBatteryVoltage() {
  // analogReadMilliVolts() applies the ESP32's per-chip ADC calibration, so this
  // is the pin voltage; multiply back up through the divider for the pack voltage.
  float pin_v = analogReadMilliVolts(BATTERY_ADC_PIN) / 1000.0f;
  return pin_v * BATTERY_DIVIDER;
}
#endif

// Gather device-health telemetry from the HAL. The hardware reads live
// here; which fields the wire carries — names, rounding, presence rules — is
// the contract in logic.h's ptw_build_reading_payload, pinned by host tests
// All fields are auxiliary — the API keeps only valid ones
// and never fails the reading over them.
static PtwTelemetry gatherTelemetry(bool sensor_ok) {
  PtwTelemetry t = {};
  t.firmware_version = FIRMWARE_VERSION;
  t.sensor_ok        = sensor_ok;
  if (WiFi.status() == WL_CONNECTED) {
    t.has_rssi = true;
    t.rssi     = WiFi.RSSI();
  }
#ifdef BATTERY_ADC_PIN
  t.has_battery = true;
  t.battery_v   = readBatteryVoltage();
  t.battery_pct = ptw_battery_pct(t.battery_v, BATTERY_FULL_V, BATTERY_EMPTY_V);
#endif
  return t;
}

// Returns true when the server was reached (any HTTP status); false on a
// transport-level failure (no WiFi / connect error) — the deep-sleep path
// retries sooner on false instead of sleeping a full interval.
static bool transportReport(const Reading& r) {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(5000);
    return false;
  }

  WiFiClientSecure client;
  HTTPClient http;
  if (!beginSecure(client, http, API_URL)) {
    // Fail closed: no validated link → don't post the reading (and don't leak the
    // x-api-key). Returns false so the deep-sleep path retries sooner.
    Serial.println("POST /reading skipped — secure connection unavailable");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", g_deviceId + ":" + g_secret);

  JsonDocument doc;
  // Wire contract (field names/rounding/presence) lives in logic.h, host-tested
  // sensor_ok=true: a transported reading is by definition a good read.
  ptw_build_reading_payload(doc, r.distance_cm, gatherTelemetry(true));
  String body;
  serializeJson(doc, body);

  int status = http.POST(body);

  // Response-handling decisions are host-tested in logic.h.
  if (ptw_response_has_interval(status)) {   // 200 accept or 429 rate-limit
    JsonDocument resp;
    if (deserializeJson(resp, http.getString()) == DeserializationError::Ok) {
      if (status == 200)
        Serial.printf("Level: %.1f%%\n", resp["level_pct"].as<float>());
      else
        Serial.println("Rate-limited — backing off to the server's interval");
      applyServerInterval(resp["next_interval_secs"] | 0L);
    }
  } else if (status == 422) {
    Serial.println("Not assigned to a tank yet");
  } else {
    Serial.printf("POST /reading failed: %d\n", status);
  }
  http.end();
  return ptw_server_reached(status);   // >0 = a response arrived; <=0 = never reached the server
}

// ── BOOT button ───────────────────────────────────────────────────────────────

static void checkBootButton() {
  static unsigned long pressStart = 0;
  bool pressed = (digitalRead(BOOT_PIN) == LOW);
  if (pressed && pressStart == 0) {
    pressStart = millis();
  } else if (!pressed) {
    pressStart = 0;
  } else if (millis() - pressStart >= WIFI_RESET_HOLD_MS) {
    Serial.println("BOOT held — clearing WiFi credentials and restarting");
    prefs.begin("ptw", false);
    prefs.remove("wifi_ssid");
    prefs.remove("wifi_pass");
    prefs.end();
    ESP.restart();
  }
}

// ── Deep sleep ────────────────────────────────────────────────────────────────

#ifdef DEEP_SLEEP_ENABLED
// Sleep until the next reading cycle. Deep sleep ends in a full reboot —
// setup() runs again, creds/interval come back from NVS, and loop() does the
// next acquire→transport. Also arms a BOOT-press wake so a person at the tank
// can wake the device (and keep holding to reset WiFi — see setup()).
static void deepSleepFor(uint32_t ms) {
  Serial.printf("Deep-sleeping for %lus\n", (unsigned long)(ms / 1000UL));
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
#if defined(BOARD_ESP32C3_SUPERMINI)
  // C3 has no EXT0/EXT1 — it wakes via the GPIO wakeup API instead.
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BOOT_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BOOT_PIN, 0);
#endif
  esp_deep_sleep_start();
}
#endif

// ── Main ──────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BOOT_PIN, INPUT_PULLUP);

  loadOrGenerateCredentials();

#ifdef DEEP_SLEEP_ENABLED
  // In deep-sleep mode every cycle is a cold boot, so the BOOT-hold WiFi reset
  // moves here: a person wakes the device with a BOOT press; if they keep
  // holding through WIFI_RESET_HOLD_MS, clear the credentials (same behaviour
  // as the always-on loop's checkBootButton()).
  if (digitalRead(BOOT_PIN) == LOW) {
    unsigned long t0 = millis();
    while (digitalRead(BOOT_PIN) == LOW && millis() - t0 < WIFI_RESET_HOLD_MS) delay(50);
    if (millis() - t0 >= WIFI_RESET_HOLD_MS) {
      Serial.println("BOOT held — clearing WiFi credentials");
      prefs.begin("ptw", false);
      prefs.remove("wifi_ssid");
      prefs.remove("wifi_pass");
      prefs.end();
    }
  }
  // JSN-SR04T needs a moment after power-up before readings are stable; every
  // deep-sleep wake is a power-up. Bench validation may tune this.
  delay(SENSOR_WARMUP_MS);
#endif

  setupWiFi();
  autoRegister();
}

void loop() {
  // Self-healing registration: autoRegister() early-returns once
  // g_registered, so this is a no-op (no network) on every cycle after the
  // device is registered. It matters when registration FAILED at boot — a
  // transient server 5xx, SNTP not yet synced so beginSecure() failed closed
  // TLS validation, or a WiFi hiccup. In the always-on build setup() runs once and
  // never again, so without retrying here a boot-time registration failure
  // would leave the device with no server record → every /reading 401s
  // (_authenticate_device finds no device) → permanently dark until a manual
  // reflash. Retrying at the top of each cycle recovers it. (Deep-sleep re-runs
  // setup() every wake, so it already retried; this call is a harmless no-op
  // there.) The cadence of that recovery is set below — see ptw_report_cycle_ok.
  autoRegister();

  Reading r;
  bool ok = false;
  if (acquireReading(r)) {
    Serial.printf("Distance: %.1f cm\n", r.distance_cm);
    // A cycle is "successful" (→ full-interval sleep) only if the report reached
    // the server AND we're registered. transportReport() returns whether
    // the server was *reached* — but an UNregistered device's /reading is answered
    // 401, which IS a reached response, so treating "reached" as "succeeded" would
    // sleep the full (up-to-24h) interval and starve the autoRegister() retry above
    // to once a day (the boot-storm / 429-on-/register case). ANDing with
    // g_registered makes an unregistered device fail the cycle → short retry cap
    // → it re-registers within minutes. Host-tested so it can't regress.
    ok = ptw_report_cycle_ok(transportReport(r), g_registered);
  } else {
    Serial.println("Sensor read failed");
  }

#ifdef DEEP_SLEEP_ENABLED
  // Sleep decision is host-tested: success → the full
  // server-driven interval; failure → capped retry so a transient fault
  // doesn't blind the tank for a whole free-tier day. Never returns.
  deepSleepFor(ptw_sleep_duration_ms(ok, g_intervalMs, SLEEP_RETRY_CAP_MS));
#else
  // Always-on build: cadence is the busy-wait below. A failed cycle retries
  // sooner (capped) instead of waiting a full free-tier day — the same
  // reliability rule the deep-sleep path uses, via the same host-tested decision
  // (logic.h). Without this, an always-on free-tier device (24h cadence) that
  // hits a transient fault — WiFi blip, server 5xx, or beginSecure() failing
  // closed before SNTP synced — would go dark for a full day. On failure any
  // interval >= the cap retries at the cap (free daily + Plus hourly both back
  // off to 5 min, not their full cadence); only intervals < the cap (Pro
  // per-minute) are left unchanged.
  uint32_t waitMs = (uint32_t)ptw_sleep_duration_ms(ok, g_intervalMs, SLEEP_RETRY_CAP_MS);
  for (unsigned long t = millis(); millis() - t < waitMs; ) {
    checkBootButton();
    delay(100);
  }
#endif
}
