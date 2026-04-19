## AfrikaBurn Egg — Lighting Behaviour System

### Project Overview
You are writing Arduino code for an interactive art installation called **the Egg** for AfrikaBurn. The egg is a large three-dimensional sculpture covered in transparent material, with a ~4.5 metre single-colour LED strip spiralling around a central internal pole. The egg should feel **alive** and respond to people nearby.

---

### Hardware
- **Microcontroller:** Arduino Nano
- **Output:** One single-colour PWM LED strip, ~4.5m long, controlled via a single `analogWrite()` pin
- **Sensors:** Three LD2410 radar distance sensors connected via SoftwareSerial, each facing a different direction around the egg
- Each sensor is accessed via the `MyLD2410` library with the following interface:
  ```cpp
  sensor.check()                   // returns MyLD2410::DATA when a fresh frame arrives
  sensor.presenceDetected()        // bool
  sensor.movingTargetDetected()    // bool
  sensor.stationaryTargetDetected() // bool
  sensor.movingTargetDistance()    // int, cm
  sensor.stationaryTargetDistance() // int, cm
  ```

---

### Sensor Aggregation
The system reads all three sensors each loop. Two derived values should be computed and made available to all behaviour modes:

- **`closestDistance`** — the smallest distance reading across all active sensors (or `-1` if no presence detected)
- **`activeSensorCount`** — the number of sensors currently detecting a presence (0–3)

These are the only inputs behaviour modes should use. Do not pass raw sensor objects into mode logic.

---

### Behaviour Mode System
The system should support two operating modes:

1. **Manual mode** — a single behaviour is selected and runs indefinitely (selectable via a constant or pin at compile/runtime)
2. **Auto mode** — the system intelligently activates different behaviours based on interaction state, e.g. transitioning between an idle behaviour when no one is present, an engagement behaviour when someone approaches, and an intensity behaviour when multiple people are close

Transitions between behaviours should be smooth (crossfade the LED output, do not cut abruptly).

#### Auto Mode Configuration
- Each behaviour should have an **enable/disable flag** for auto mode (a boolean array), allowing the user to include or exclude specific behaviours from the auto rotation at compile time.
- When the desired behaviour is disabled, the system should **fall through to the next enabled alternative** from a prioritised fallback list.
- **Switching triggers** — auto mode should only switch behaviour when:
  1. The state transitions between **idle** (no sensors active) and **active** (≥1 sensor active), OR
  2. The **max dwell time** expires (e.g. 3 minutes) — ensuring gradual variety over the course of an evening
- The egg should **not** react to fluctuations within the active state (e.g. 1→2→1 sensors). Once a behaviour is selected for the "active" state, it stays until everyone leaves or the dwell timer expires. This makes the egg feel like a slow, living creature rather than a reactive machine.

---

### Behaviour Modes (Confirmed)

All 14 modes below are implemented:

#### Biological
| Mode | Description | Suits |
|------|-------------|-------|
| **Heartbeat** | Rhythmic double-pulse. Rate increases as people get closer. | Idle → Single person |
| **Breathing** | Smooth sine-wave inhale/exhale. Deepens with proximity. | Idle → Single person |
| **Startle** | Soft bright swell when a new presence is first detected, then settles into a glow. | Single person (trigger) |

#### Playful
| Mode | Description | Suits |
|------|-------------|-------|
| **Excitement** | Smooth pulsing that intensifies with more people and closer distance. | Single person → Crowd |
| **Peek-a-boo** | Dims to near-dark, then pulses brightly when someone is close. | Single person |
| **Swarm** | Layered irregular pulsing that increases in frequency with `activeSensorCount`. | Crowd |
| **Shy** | Dims and retreats as people approach, slowly blooms when they step away. Push-pull dynamic. | Single person → Crowd |

#### Meditative
| Mode | Description | Suits |
|------|-------------|-------|
| **Tidal** | Very slow rise and fall (10s cycle). Unaffected by proximity. | Idle |
| **Glow Drift** | Gentle random wandering between low brightness levels, like embers. | Idle |
| **Deep Pulse** | Ultra-slow single pulse (18s), barely perceptible change. | Idle |
| **Ember** | Warm low glow with very subtle random drift. Presence causes a slow gentle swell — like blowing on coals. Never urgent, always warm. | Idle → Single person |

#### Dramatic
| Mode | Description | Suits |
|------|-------------|-------|
| **Awakening** | Dark when alone. Slowly blooms to full brightness as someone approaches. | Idle → Single person |
| **Supernova** | When 3 sensors active and close, builds to a sustained warm blaze. | Crowd |
| **Eclipse** | Bright when alone, dims as people crowd around (inverted response). | Single person → Crowd |

---

### Auto Mode Mapping

| State | Primary behaviour | Fallbacks |
|-------|-------------------|-----------|
| No presence | Cycles through Tidal → Breathing → Glow Drift → Deep Pulse → Ember (30s each) | Next enabled in list |
| 1 person, far | Awakening | Heartbeat, Peek-a-boo, Ember |
| 1 person, mid | Heartbeat | Awakening, Peek-a-boo, Ember |
| 1 person, close | Peek-a-boo | Heartbeat, Awakening, Ember |
| 2 people, close | Excitement | Shy, Heartbeat, Ember |
| 2 people, far | Shy | Excitement, Heartbeat, Ember |
| 3 sensors, close | Supernova | Swarm, Excitement, Ember |
| 3 sensors, far | Swarm | Supernova, Excitement, Ember |

---

### Key Implementation Details

#### Timing & Structure
- **Non-blocking timing** — never use `delay()`. All timing must use `millis()`.
- **Opacity abstraction** — work in 0–100 opacity values internally, convert to 0–255 only at the `analogWrite()` call using `(int)round(opacity / 100.0 * 255)`
- **`map()` and `constrain()`** — use Arduino builtins for range mapping and clamping

#### Phase & Cycle Tracking

- **Fixed-rate cycles** (e.g. Tidal at 10000ms, Deep Pulse at 18000ms) can safely use modulo: `(elapsed % cycleDuration) * 100 / cycleDuration`
- **Variable-rate cycles** (where cycle duration depends on proximity or crowd) **must use a phase accumulator** to avoid erratic jumps:
  ```cpp
  // Each frame, advance phase proportionally to dt and current cycle duration
  unsigned long dt = now - lastUpdateTime;
  lastUpdateTime = now;
  phase += (dt / (float)cycleDuration) * 100.0;
  if (phase >= 100) phase -= 100;
  ```
  The modulo approach (`elapsed % cycleDuration`) breaks when `cycleDuration` changes between frames — the position within the cycle jumps unpredictably. This is especially visible in patterns with narrow active windows (e.g. Heartbeat's double-pulse only occupies 30% of the cycle).

#### Sensor Handling
- **Sensor caching & sentinel value** — each sensor's last valid distance is cached and only updated when `sensor.check()` returns `MyLD2410::DATA`. A cached reading is cleared to `-1` only after a configurable timeout (e.g. 2–3 seconds) has elapsed without a fresh detection frame from that sensor. All modes must handle `-1` gracefully, typically by defaulting to an idle/minimum state.

#### Aesthetic Guidelines
- **Favour relaxed over frantic** — even "energetic" modes should feel playful, not aggressive. Minimum cycle times should stay above ~400ms. Avoid random jitter/flicker.
- **Cap peak brightness** — energetic modes should cap around 75–85% opacity, not 100%. Reserve full brightness for rare dramatic moments (Supernova at full crowd).
- **Startle should swell, not flash** — ramp up over ~200ms to ~80%, then decay slowly. Instant full-brightness flashes feel mechanical.
- **Crossfade duration** — 800ms works well for smooth transitions between behaviours.

---

### Deliverable
1. A single, complete Arduino `.ino` script implementing all 14 modes plus the full system described above
2. The script should be clean, well-commented, and ready to compile on Arduino Nano
3. Optionally: a companion browser-based simulation (`egg-simulation.html`) for previewing behaviour without hardware
