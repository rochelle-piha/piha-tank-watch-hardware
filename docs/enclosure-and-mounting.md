# Enclosure and mounting

The sensor needs to be weatherproof and aimed straight down at the water surface. Here's what works.

## Choosing an enclosure

Use an **IP65-rated ABS enclosure** sized to fit the ESP32 board plus cable slack. The JSN-SR04T transducer lives *outside* the box — it points down through a hole in the lid. The electronics (ESP32 + USB power wiring) stay inside, sealed.

What to look for:
- External dimensions around 100 × 68 × 50 mm fits the ESP32-C3 SuperMini with room to spare.
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
