# LoRa long-range variant (DIY-experimental)

> **Status: DIY-experimental — not officially supported, not bench-validated.**
> The firmware doesn't *block* a long-range LoRa build, and this page explains how a
> self-builder would do it. We have **not** tested range, link reliability, power draw, or
> the receiver path on real hardware. Treat everything here as a starting point you verify
> yourself, not a tested product.

## When you'd want this

WiFi can't reach the tank and a repeater/powerline adapter isn't enough (long rural runs, no
power at the tank). A point-to-point LoRa link reaches ~1–5 km line-of-sight. No LoRaWAN /
network server needed — it's just two radios talking to each other.

```
[Tank unit] --LoRa--> [House receiver] --WiFi/LAN--> [API]
  ESP32 + JSN-SR04T      ESP32 + WiFi
  + LoRa module          + LoRa module
```

- **Tank unit:** ESP32 + JSN-SR04T + a LoRa module (e.g. SX1276, ~$5). Acquires a reading and
  **transmits it over radio** instead of posting to the API.
- **House receiver:** ESP32 + LoRa module + WiFi. Listens on LoRa and **POSTs the reading to the
  API on the tank unit's behalf**, presenting the tank unit's credentials.
- **NZ frequency: 915 MHz** (AS923 region). Use a module + antenna rated for it.

## Why the firmware doesn't get in the way

The reading loop is built around an **acquire↔transport seam**. `loop()` is:

```cpp
Reading r;
if (acquireReading(r)) {     // sensor → Reading{ distance_cm }  — unchanged for LoRa
  transportReport(r);        // how the Reading leaves the device — THIS is what you swap
}
// ... wait g_intervalMs ...  // cadence — unchanged for LoRa
```

Only **`transportReport()`** touches the network. Acquisition, the median filter, the
cadence wait loop, the board/pin config, and (on the receiver) the captive-portal WiFi
provisioning all stay exactly as they are. So a LoRa build is a **contained swap of one
function plus a small receiver sketch** — not a fork of the firmware.

## What you change

### Tank unit — replace `transportReport()` with a LoRa transmit

Instead of an HTTPS POST, frame the reading and send it over the radio. Illustrative outline
(you supply the LoRa library — e.g. `sandeepmistry/LoRa` or RadioHead — it is **not** bundled):

```cpp
// Replaces the WiFi transportReport() in water_level.ino.
static void transportReport(const Reading& r) {
  StaticJsonDocument<128> doc;
  doc["id"]          = g_deviceId;     // tank unit's identity (from loadOrGenerateCredentials())
  doc["secret"]      = g_secret;       // ⚠ see the security note below
  doc["distance_cm"] = roundf(r.distance_cm * 10) / 10.0f;
  String packet; serializeJson(doc, packet);

  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();                    // fire-and-forget; no API response comes back
}
```

A tank unit that only transmits has **no WiFi**, so you'd also drop `setupWiFi()` / `autoRegister()`
from `setup()` and let the **receiver** do the device registration/linking against the API.

### House receiver — a thin "listen + POST" sketch

A separate small sketch (not yet provided): keep the existing WiFi provisioning + credential model,
then in `loop()` receive a LoRa packet and forward it to the **unchanged** `/reading` API contract:

```cpp
// On LoRa packet received:
//   parse { id, secret, distance_cm }
//   http.begin(API_URL);
//   http.addHeader("x-api-key", id + ":" + secret);   // the tank unit's credentials
//   http.POST("{\"distance_cm\": <value>}");
```

The backend authenticates on `device_id:secret` and doesn't care whether the POST came from the
tank unit or a proxy — so no backend change is needed.

## Honest limitations (read before you build)

1. **No return path in the simple build → no cadence adaptation.** The WiFi firmware reads
   `next_interval_secs` from each `/reading` response to set its reporting interval. A
   fire-and-forget LoRa transmit gets **no response back**, so the tank unit reports at the fixed
   compile-time `READING_INTERVAL_MS`. To honour the server cadence you'd need the receiver to relay
   the interval back over LoRa (a bidirectional link — more work).
2. **Security: credentials travel over the air.** The tank unit's `device_id:secret` is in the LoRa
   packet, and plain point-to-point LoRa is **unauthenticated and unencrypted** — anyone in radio
   range could capture the credential or inject fake readings. Encrypt the payload (a shared AES key
   between the paired units) at minimum.
3. **Telemetry round-trips don't come back either.** Device-health fields can still be *sent*
   if you add them to the packet, but anything derived from the API response won't reach the tank unit.
4. **Power isn't free yet.** The "tank unit sleeps between transmits" benefit depends on deep-sleep,
   which isn't shipped yet.
5. **Untested.** We haven't validated range, antenna choice, packet-loss behaviour, or the receiver
   sketch. NZ 915 MHz / SX1276 are the sane defaults, but verify on your own bench.

## What you do *not* need to change

`acquireReading()` and the median filter, the cadence wait loop, the `config.h` board/pin block,
and the receiver's captive-portal WiFi provisioning. The seam is the whole point: the sensor and
cadence code don't know or care that the transport is now a radio.

---

*This is DIY territory. If you build it and it works, a write-up / PR improving this page (or a
reference receiver sketch) is very welcome — but we can't promise support for it.*
