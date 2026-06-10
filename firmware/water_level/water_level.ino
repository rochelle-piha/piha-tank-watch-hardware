#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "mbedtls/md.h"
#include "config.h"

Preferences prefs;
String      g_deviceId;
String      g_secret;
bool        g_registered;

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

// ── Device credentials ────────────────────────────────────────────────────────

static void loadOrGenerateCredentials() {
  prefs.begin("ptw", false);
  g_deviceId   = prefs.getString("device_id", "");
  g_secret     = prefs.getString("secret",    "");
  g_registered = prefs.getBool("registered",  false);

  if (g_deviceId.isEmpty() || g_secret.isEmpty()) {
    uint8_t idBytes[4], secretBytes[32];
    esp_fill_random(idBytes,     sizeof(idBytes));
    esp_fill_random(secretBytes, sizeof(secretBytes));
    g_deviceId = hexStr(idBytes, 4);
    g_secret   = hexStr(secretBytes, 32);
    prefs.putString("device_id", g_deviceId);
    prefs.putString("secret",    g_secret);
    prefs.putBool("registered",  false);
    g_registered = false;
    Serial.println("New device credentials generated");
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
    String staIP = WiFi.localIP().toString();
    IPAddress resolved;
    bool dnsOk = WiFi.hostByName("api.pihatankwatch.nz", resolved);
    if (!dnsOk) {
      portalServer.send(200, "text/html", signinPage("DNS failed (STA IP: " + staIP + "). Try again."));
      return;
    }
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(12000);
    http.begin(client, "https://api.pihatankwatch.nz/devices/send-otp");
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

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(LINK_URL);
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

  HTTPClient http;
  http.begin(REGISTER_URL);
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
  return us > 0 ? us * 0.034f / 2.0f : -1.0f;
}

// ── Send reading ──────────────────────────────────────────────────────────────

static void sendReading(float distance_cm) {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(5000);
    return;
  }

  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", g_deviceId + ":" + g_secret);

  JsonDocument doc;
  doc["distance_cm"] = roundf(distance_cm * 10) / 10.0f;
  String body;
  serializeJson(doc, body);

  int status = http.POST(body);

  if (status == 200) {
    JsonDocument resp;
    if (deserializeJson(resp, http.getString()) == DeserializationError::Ok)
      Serial.printf("Level: %.1f%%\n", resp["level_pct"].as<float>());
  } else if (status == 422) {
    Serial.println("Not assigned to a tank yet");
  } else {
    Serial.printf("POST /reading failed: %d\n", status);
  }
  http.end();
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

// ── Main ──────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BOOT_PIN, INPUT_PULLUP);

  loadOrGenerateCredentials();
  setupWiFi();
  autoRegister();
}

void loop() {
  float d = readDistance();
  if (d > 0 && d < 400) {
    Serial.printf("Distance: %.1f cm\n", d);
    sendReading(d);
  } else {
    Serial.println("Sensor read failed");
  }

  for (unsigned long t = millis(); millis() - t < READING_INTERVAL_MS; ) {
    checkBootButton();
    delay(100);
  }
}
