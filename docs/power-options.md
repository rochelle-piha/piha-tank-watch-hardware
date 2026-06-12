# Powering your Tank Watch

The reference build is powered by USB (5 V) and draws roughly 80 mA while running. What that means for each option:

## Mains USB adapter

The simple answer. Any 5 V USB charger works — the ESP32-C3 SuperMini board has a **USB-C** connector, so you'll need a USB-A→USB-C cable if using a USB-A charger. Use a weatherproof outdoor outlet or run the cable through conduit. This is the recommended setup for permanent installs.

## USB power bank

Fine for days, not months. At ~80 mA continuous, a 10,000 mAh bank lasts roughly 4–5 days. Good for a trial run or a tank awaiting a permanent power supply — not a long-term solution without deep-sleep mode (see below).

> **Note:** that draw estimate assumes the device is always on between readings (the current behaviour). Actual life depends on your bank's efficiency, self-discharge, and temperature.

## Solar + battery

Can work if the panel and charge controller maintain a stable 5 V USB output through NZ winters. The always-on draw is the constraint — a small panel (~5 W) may not keep up through overcast periods without a buffer battery. We have **not bench-validated a solar build**; if you try it, you're the test pilot.

## Deep sleep (not yet available)

Deep sleep between readings would dramatically extend battery and solar life and is on the roadmap. **It is not implemented in the current firmware** — the device stays on between readings for now. Battery and solar builds are therefore not recommended for long-term use until deep sleep lands.

---

*For more on supported boards and power wiring, see [hardware-compatibility.md](hardware-compatibility.md) and [wiring.md](wiring.md).*
