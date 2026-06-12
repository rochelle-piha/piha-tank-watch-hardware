# Enclosure and mounting

The sensor needs to be weatherproof and aimed straight down at the water surface. Here's what works.

## Choosing an enclosure

Use an **IP65-rated ABS enclosure** sized to fit all the electronics plus cable slack. Two boards go *inside* the sealed box:

- the **ESP32-C3 SuperMini** (~22 × 18 mm), and
- the **JSN-SR04T driver board** (~41 × 29 mm) — the small PCB the transducer's 4-wire cable plugs into. This is the part that's easy to miss: only the round **transducer** probe mounts *outside* (pointing down through a hole in the lid); its driver board stays *inside* with the ESP32.

On a **battery or solar** build the battery pack also goes inside — size the box up for whatever pack you use (an 18650 holder or LiPo adds noticeably to the footprint).

What to look for:
- External dimensions around **100 × 68 × 50 mm** fit a USB-powered build — the ESP32 board *and* the JSN-SR04T driver board, with cable slack. A battery/solar build needs a larger box for the pack.
- A removable lid (four screws) makes it easy to re-flash or rewire.
- Wall-mount flanges if you need to attach it to a bracket rather than the tank lid directly.

Jaycar, Altronics, and generic ABS enclosures from trade suppliers all work — pick by size and mounting style.

## Cable gland

Use an **M12 cable gland** where the sensor lead enters the box. This keeps the seal tight around the cable and prevents moisture ingress. Drill the hole before wiring — it's much easier.

## Mounting position and blind zone

The transducer must be **mounted at least 25 cm above the full water line**. The JSN-SR04T can't measure distances shorter than ~25 cm — anything closer reads as zero (full). If your tank reaches the lid when full, mount the sensor on a short standoff or bracket inside the lid to achieve the minimum clearance.

Mount the transducer pointing **straight down** at the water surface. A few degrees of tilt is fine; significant angles degrade the reflection and produce noisy readings.

## Tank material

It doesn't matter — the sensor measures the air gap between the transducer and the water surface, not through the tank wall. Poly, fibreglass, concrete, steel: all work the same.

## Sealing the transducer hole

The transducer body itself is waterproof (that's the point of the JSN-SR04T vs the HC-SR04). The hole in the enclosure lid should be a close fit around the transducer body; add a bead of silicone sealant around the outside if there's any gap. The cable gland handles the cable entry separately.

---

*See [wiring.md](wiring.md) for the electrical connections, and [hardware-compatibility.md](hardware-compatibility.md) for board and sensor options.*
