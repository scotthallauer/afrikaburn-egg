/*
 * AfrikaBurn Egg — Lighting Behaviour System (Debug Edition)
 * ===========================================================
 * Single-colour LED strip controlled via PWM on Arduino Nano.
 * Three LD2410 radar sensors provide proximity data.
 * 14 behaviour modes with crossfade transitions.
 *
 * Hardware:
 *   - Arduino Nano (ATmega328P, 16MHz)
 *   - 1x PWM LED strip on LED_PIN
 *   - 3x LD2410 radar sensors on SoftwareSerial
 *
 * ╔══════════════════════════════════════════════════════════════════════╗
 * ║  DESERT DEBUGGING GUIDE — READ THIS BEFORE YOU LEAVE FOR THE BURN  ║
 * ╠══════════════════════════════════════════════════════════════════════╣
 * ║                                                                    ║
 * ║  WHAT YOU NEED:                                                    ║
 * ║  - A laptop with Arduino IDE installed (works offline)             ║
 * ║  - A USB cable to connect to the Nano                              ║
 * ║  - Open Serial Monitor at 115200 baud                              ║
 * ║                                                                    ║
 * ║  STARTUP SEQUENCE:                                                 ║
 * ║  On power-up the LED will blink a pattern to show sensor status:   ║
 * ║    3 quick blinks = booted OK                                      ║
 * ║    Then for each enabled sensor:                                   ║
 * ║      1 long blink  = sensor OK                                     ║
 * ║      3 fast blinks = sensor FAILED (check wiring/baud rate)        ║
 * ║                                                                    ║
 * ║  SERIAL MONITOR OUTPUT (every 500ms):                              ║
 * ║    [12345ms] BH:Tidal TGT:Breathing O:42 PWM:107 CF:100%          ║
 * ║    S1:150cm S2:--- S3:80cm | closest:80cm active:2                 ║
 * ║    (sensor reads: ok=45 fail=2 timeout=0)                          ║
 * ║                                                                    ║
 * ║  IF NOTHING HAPPENS (LED stays dark):                              ║
 * ║  1. Check 5V power to Nano (is the power LED on?)                  ║
 * ║  2. Check LED_PIN wiring (pin 9 → MOSFET gate / LED driver)       ║
 * ║  3. Try MANUAL_BEHAVIOUR = BH_TIDAL to rule out sensor issues      ║
 * ║                                                                    ║
 * ║  IF SENSORS SHOW ALL "---" (no readings):                          ║
 * ║  1. *** MOST LIKELY CAUSE: BAUD RATE MISMATCH ***                  ║
 * ║     The LD2410 ships at 256000 baud. SoftwareSerial on Nano        ║
 * ║     CANNOT reliably receive above 57600 baud.                      ║
 * ║     YOU MUST pre-configure sensors to SENSOR_BAUD (38400) using    ║
 * ║     the MyLD2410 "set_baud_rate" example on an ESP32 or via        ║
 * ║     HardwareSerial first.                                          ║
 * ║  2. If baud is correct: check TX/RX wiring (sensor TX → Nano RX)  ║
 * ║  3. Try enabling one sensor at a time in sensorEnabled[]           ║
 * ║  4. Try SENSOR_BAUD values: 38400, 57600, 9600, 115200            ║
 * ║                                                                    ║
 * ║  IF SENSORS READ BUT VALUES SEEM WRONG:                            ║
 * ║  - Distances in cm. LD2410 max range ~600cm.                       ║
 * ║  - If you see values like 999 constantly, the sensor detects       ║
 * ║    presence but can't get a distance — check for interference.     ║
 * ║                                                                    ║
 * ║  QUICK FIXES:                                                      ║
 * ║  - Set AUTO_MODE false + MANUAL_BEHAVIOUR to test individual modes ║
 * ║  - Set sensorEnabled[] to {true, false, false} to isolate sensors  ║
 * ║  - Increase SENSOR_TIMEOUT_MS if sensors drop in/out frequently    ║
 * ║  - Decrease CROSSFADE_MS for snappier transitions while testing    ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 */

#include <SoftwareSerial.h>
#include <MyLD2410.h>

// ─── PIN CONFIGURATION ───────────────────────────────────────────────────────
#define LED_PIN        9

#define SENSOR1_RX     2
#define SENSOR1_TX     3
#define SENSOR2_RX     4
#define SENSOR2_TX     5
#define SENSOR3_RX     6
#define SENSOR3_TX     7

// ─── SYSTEM CONFIGURATION ────────────────────────────────────────────────────
#define AUTO_MODE          true   // false = manual mode (use MANUAL_BEHAVIOUR below)
#define MANUAL_BEHAVIOUR   0     // index into Behaviour enum for manual mode

// *** DEBUG is ON by default — you NEED this in the desert ***
// Comment out ONLY if you're sure everything works and want to save ~2KB flash
#define DEBUG

// ┌─────────────────────────────────────────────────────────────────────┐
// │ SENSOR_BAUD: This is the MOST CRITICAL setting.                    │
// │                                                                    │
// │ LD2410 ships at 256000 baud. SoftwareSerial on Arduino Nano        │
// │ CANNOT reliably receive at 115200 or above (confirmed Arduino      │
// │ bug — RX data is garbage at 115200 on ATmega328P).                 │
// │                                                                    │
// │ BEFORE deploying, you MUST reconfigure each LD2410 sensor to       │
// │ 38400 baud using the MyLD2410 "set_baud_rate" example sketch.      │
// │ This requires a one-time setup via HardwareSerial (e.g. on an      │
// │ ESP32, or using the Nano's hardware Serial pins 0/1 with one       │
// │ sensor at a time).                                                 │
// │                                                                    │
// │ If you haven't reconfigured the sensors yet, try these values      │
// │ in order: 256000, 115200, 57600, 38400, 9600                       │
// │ (256000 is factory default but will NOT work with SoftwareSerial)  │
// │                                                                    │
// │ ALTERNATIVE: If you have an ESP32, use that instead — it has 3     │
// │ hardware UARTs and can run all sensors at 256000 natively.         │
// └─────────────────────────────────────────────────────────────────────┘
#define SENSOR_BAUD    38400  // was 115200 — CHANGED: see note above

#define CROSSFADE_MS       800   // crossfade duration between behaviours
#define SENSOR_TIMEOUT_MS  2500  // clear cached reading after this long without data
#define MAX_DWELL_MS       180000 // max time (3 min) before auto mode picks a new behaviour

// Set to false to disable a broken sensor — useful for isolating issues
// TIP: start with {true, false, false} to test one sensor at a time
bool sensorEnabled[3] = {true, false, false};

// ─── BEHAVIOUR INDICES ───────────────────────────────────────────────────────
enum Behaviour {
  BH_HEARTBEAT = 0,
  BH_BREATHING,
  BH_STARTLE,
  BH_EXCITEMENT,
  BH_PEEKABOO,
  BH_SWARM,
  BH_TIDAL,
  BH_GLOW_DRIFT,
  BH_DEEP_PULSE,
  BH_AWAKENING,
  BH_SUPERNOVA,
  BH_ECLIPSE,
  BH_SHY,
  BH_EMBER,
  BH_COUNT
};

// Human-readable names for debug output
#ifdef DEBUG
const char* behaviourNames[] = {
  "Heartbeat", "Breathing", "Startle", "Excitement",
  "Peekaboo", "Swarm", "Tidal", "GlowDrift",
  "DeepPulse", "Awakening", "Supernova", "Eclipse",
  "Shy", "Ember"
};
#endif

// ─── AUTO MODE BEHAVIOUR ENABLE/DISABLE ──────────────────────────────────────
// Set to true to allow a behaviour in auto mode, false to exclude it
bool autoModeEnabled[BH_COUNT] = {
  true,   // Heartbeat
  true,   // Breathing
  true,   // Startle
  true,   // Excitement
  true,   // Peek-a-boo
  true,   // Swarm
  true,   // Tidal
  true,   // Glow Drift
  true,   // Deep Pulse
  true,   // Awakening
  true,   // Supernova
  true,   // Eclipse
  true,   // Shy
  true    // Ember
};

// ─── SENSOR SETUP ────────────────────────────────────────────────────────────
// NOTE: Only ONE SoftwareSerial port can actively listen at a time on Nano.
// The round-robin approach in readSensors() handles this correctly by calling
// listen() before each read. If you get no data, the delay after listen()
// may need increasing — try 2-5ms instead of 1ms.
SoftwareSerial ss1(SENSOR1_RX, SENSOR1_TX);
SoftwareSerial ss2(SENSOR2_RX, SENSOR2_TX);
SoftwareSerial ss3(SENSOR3_RX, SENSOR3_TX);

MyLD2410 sensor1(ss1);
MyLD2410 sensor2(ss2);
MyLD2410 sensor3(ss3);

// Cached sensor state (-1 = no reading)
int cachedDistance[3] = {-1, -1, -1};
unsigned long lastDataTime[3] = {0, 0, 0};

// Aggregated values available to all modes
int closestDistance = -1;
int activeSensorCount = 0;

// ─── DEBUG COUNTERS ──────────────────────────────────────────────────────────
#ifdef DEBUG
unsigned long sensorOkCount[3] = {0, 0, 0};    // successful reads
unsigned long sensorFailCount[3] = {0, 0, 0};  // check() returned FAIL
unsigned long sensorTimeoutCount[3] = {0, 0, 0}; // timed out waiting for data
unsigned long loopCount = 0;
unsigned long lastLoopTime = 0;
bool sensorInitOk[3] = {false, false, false};
#endif

// ─── BEHAVIOUR STATE ─────────────────────────────────────────────────────────
int currentBehaviour = BH_TIDAL;
int targetBehaviour = BH_TIDAL;
float currentOpacity = 0;
float targetOpacity = 0;
float crossfadeProgress = 100; // 100 = fully transitioned
unsigned long crossfadeStart = 0;
float prevOpacity = 0;

// Mode-specific state
unsigned long modeStartTime = 0;
unsigned long targetModeStartTime = 0;
unsigned long lastSwitchTime = 0;
bool wasIdle = true;
unsigned long lastHeartbeatTime = 0;
float heartbeatPhase = 0;
unsigned long lastExcitementTime = 0;
float excitementPhase = 0;
bool startleTriggered = false;
unsigned long startleTime = 0;
unsigned long lastPeekabooTime = 0;
float peekabooPhase = 0;
float glowDriftTarget = 20;
float glowDriftCurrent = 20;
unsigned long glowDriftNextChange = 0;
float emberCurrent = 15;
float emberDriftTarget = 15;
unsigned long emberNextDrift = 0;

// ─── SETUP ───────────────────────────────────────────────────────────────────
void setup() {
  pinMode(LED_PIN, OUTPUT);
  analogWrite(LED_PIN, 0);

  // Seed random from floating analog pin for varied behaviour each boot
  // NOTE: if A0 is connected to something, this won't be very random — but
  // it's better than the default (same sequence every time)
  randomSeed(analogRead(A0));

  #ifdef DEBUG
  // 115200 for debug serial is fine — this is HardwareSerial (pins 0/1),
  // NOT SoftwareSerial. HardwareSerial handles 115200 perfectly.
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {} // wait up to 2s for serial monitor
  Serial.println(F("\n══════════════════════════════════════"));
  Serial.println(F("  AfrikaBurn Egg — Lighting System"));
  Serial.println(F("  Debug Build"));
  Serial.println(F("══════════════════════════════════════"));
  Serial.print(F("Sensor baud: ")); Serial.println(SENSOR_BAUD);
  Serial.print(F("Mode: ")); Serial.println(AUTO_MODE ? F("AUTO") : F("MANUAL"));
  if (!AUTO_MODE) {
    Serial.print(F("Manual behaviour: ")); Serial.println(behaviourNames[MANUAL_BEHAVIOUR]);
  }
  Serial.print(F("Sensors enabled: "));
  for (int i = 0; i < 3; i++) {
    Serial.print(sensorEnabled[i] ? F("ON ") : F("OFF "));
  }
  Serial.println();
  #endif

  // ┌─────────────────────────────────────────────────────────────────┐
  // │ STARTUP LED BLINK: 3 quick blinks = "I'm alive"                │
  // │ This confirms the Nano booted and LED_PIN wiring works.        │
  // │ If you don't see these blinks, check power and LED wiring.     │
  // └─────────────────────────────────────────────────────────────────┘
  for (int i = 0; i < 3; i++) {
    analogWrite(LED_PIN, 128);
    delay(100);
    analogWrite(LED_PIN, 0);
    delay(100);
  }
  delay(300);

  // ┌─────────────────────────────────────────────────────────────────┐
  // │ SENSOR INITIALIZATION                                          │
  // │                                                                │
  // │ sensor.begin() returns true if the sensor responds.            │
  // │ If it returns false, the sensor is not communicating.           │
  // │ Most likely cause: baud rate mismatch (see SENSOR_BAUD above). │
  // │                                                                │
  // │ LED feedback per sensor:                                       │
  // │   1 long blink (300ms)  = sensor initialized OK                │
  // │   3 fast blinks (80ms)  = sensor FAILED                        │
  // └─────────────────────────────────────────────────────────────────┘
  SoftwareSerial* serials[] = {&ss1, &ss2, &ss3};
  MyLD2410* sensors[] = {&sensor1, &sensor2, &sensor3};

  for (int i = 0; i < 3; i++) {
    if (!sensorEnabled[i]) {
      #ifdef DEBUG
      Serial.print(F("Sensor ")); Serial.print(i + 1); Serial.println(F(": DISABLED (skipped)"));
      #endif
      continue;
    }

    serials[i]->begin(SENSOR_BAUD);
    serials[i]->listen();
    delay(50); // give serial port time to stabilize

    bool ok = sensors[i]->begin();

    #ifdef DEBUG
    sensorInitOk[i] = ok;
    Serial.print(F("Sensor ")); Serial.print(i + 1);
    Serial.print(F(" (pins RX=")); Serial.print(SENSOR1_RX + i * 2);
    Serial.print(F(" TX=")); Serial.print(SENSOR1_TX + i * 2);
    Serial.print(F("): "));
    if (ok) {
      Serial.println(F("OK ✓"));
    } else {
      Serial.println(F("FAILED ✗ — check wiring and baud rate!"));
      Serial.println(F("  → Is sensor powered? (needs 5V)"));
      Serial.println(F("  → Is TX/RX swapped? (sensor TX → Nano RX pin)"));
      Serial.print(F("  → Is sensor set to ")); Serial.print(SENSOR_BAUD);
      Serial.println(F(" baud? (factory default is 256000)"));
    }
    #endif

    // LED feedback: 1 long blink = OK, 3 fast blinks = FAIL
    if (ok) {
      analogWrite(LED_PIN, 180);
      delay(300);
      analogWrite(LED_PIN, 0);
      delay(200);
    } else {
      for (int j = 0; j < 3; j++) {
        analogWrite(LED_PIN, 255);
        delay(80);
        analogWrite(LED_PIN, 0);
        delay(80);
      }
      delay(200);
    }
  }

  #ifdef DEBUG
  Serial.println(F("──────────────────────────────────────"));
  Serial.println(F("Setup complete. Entering main loop."));
  Serial.println(F("Debug output every 500ms. Format:"));
  Serial.println(F("  [time] BH:current TGT:target O:opacity% PWM:0-255 CF:fade%"));
  Serial.println(F("  S1:dist S2:dist S3:dist | closest:dist active:count"));
  Serial.println(F("──────────────────────────────────────\n"));
  #endif

  modeStartTime = millis();
  lastSwitchTime = millis();
  #ifdef DEBUG
  lastLoopTime = millis();
  #endif
}

// ─── SENSOR READING ──────────────────────────────────────────────────────────
// Round-robin: read one sensor per loop iteration to give SoftwareSerial time
// to buffer a full frame between reads.
//
// NOTE: On Arduino Nano, only ONE SoftwareSerial can listen() at a time.
// When you call listen() on one, the others stop receiving. The round-robin
// means each sensor gets read every 3rd loop iteration (~30ms apart with
// the 10ms delay). This is fine for the LD2410 which sends frames every ~100ms.
//
// If sensors seem flaky, try increasing LISTEN_SETTLE_MS below.
// The LD2410 sends ~30-byte frames. At 38400 baud that's ~8ms per frame.
// We need at least one full frame in the buffer after listen().
#define LISTEN_SETTLE_MS 2  // ms to wait after listen() before reading
                            // TRY: 1, 2, 5, 10 if sensors are unreliable

int currentSensorIndex = 0;

void readSensors() {
  unsigned long now = millis();
  MyLD2410* sensors[] = {&sensor1, &sensor2, &sensor3};
  SoftwareSerial* serials[] = {&ss1, &ss2, &ss3};

  int i = currentSensorIndex;
  if (sensorEnabled[i]) {
    serials[i]->listen();
    delay(LISTEN_SETTLE_MS);

    MyLD2410::Response result = sensors[i]->check();
    if (result == MyLD2410::DATA) {
      if (sensors[i]->presenceDetected()) {
        int d = 999;
        if (sensors[i]->movingTargetDetected()) {
          d = min(d, sensors[i]->movingTargetDistance());
        }
        if (sensors[i]->stationaryTargetDetected()) {
          d = min(d, sensors[i]->stationaryTargetDistance());
        }
        cachedDistance[i] = d;
      } else {
        cachedDistance[i] = -1;
      }
      lastDataTime[i] = now;
      #ifdef DEBUG
      sensorOkCount[i]++;
      #endif
    } else {
      // check() returned FAIL — no complete frame available yet.
      // This is normal; the sensor sends frames every ~100ms and we poll
      // every ~30ms, so ~2/3 of polls will return FAIL.
      #ifdef DEBUG
      sensorFailCount[i]++;
      #endif

      // Timeout: if we haven't received ANY data for SENSOR_TIMEOUT_MS,
      // clear the cached reading. This prevents stale data from a
      // disconnected sensor keeping the egg in "someone is here" mode.
      if (now - lastDataTime[i] > SENSOR_TIMEOUT_MS) {
        if (cachedDistance[i] != -1) {
          cachedDistance[i] = -1;
          #ifdef DEBUG
          sensorTimeoutCount[i]++;
          Serial.print(F("⚠ Sensor ")); Serial.print(i + 1);
          Serial.println(F(" timed out — no data, clearing cached distance"));
          #endif
        }
      }
    }
  } else {
    cachedDistance[i] = -1;
  }

  currentSensorIndex = (currentSensorIndex + 1) % 3;

  // Compute aggregated values
  closestDistance = -1;
  activeSensorCount = 0;
  for (int j = 0; j < 3; j++) {
    if (cachedDistance[j] > 0) {
      activeSensorCount++;
      if (closestDistance == -1 || cachedDistance[j] < closestDistance) {
        closestDistance = cachedDistance[j];
      }
    }
  }
}

// ─── UTILITY ─────────────────────────────────────────────────────────────────
float proximityFactor() {
  // Returns 0.0 (far/absent) to 1.0 (very close)
  // Maps 400cm → 0.0, 30cm → 1.0
  if (closestDistance <= 0) return 0.0;
  return constrain(map(closestDistance, 400, 30, 0, 100), 0, 100) / 100.0;
}

int cycleProgress(unsigned long cycleDuration) {
  // Returns 0–100 representing position within a repeating cycle
  // Safe for fixed-rate cycles. For variable-rate, use phase accumulators instead.
  unsigned long elapsed = (millis() - modeStartTime) % cycleDuration;
  return (int)((elapsed * 100UL) / cycleDuration);
}

float sinPulse(int progress) {
  // Sine wave from progress 0–100, returns 0.0–1.0
  return (sin(progress / 100.0 * TWO_PI - HALF_PI) + 1.0) / 2.0;
}

// ─── BEHAVIOUR IMPLEMENTATIONS ──────────────────────────────────────────────

// Heartbeat: double-pulse, rate increases with proximity
float behaviourHeartbeat() {
  float prox = proximityFactor();
  unsigned long cycle = map((int)(prox * 100), 0, 100, 1200, 500);

  unsigned long now = millis();
  unsigned long dt = now - lastHeartbeatTime;
  lastHeartbeatTime = now;
  heartbeatPhase += (dt / (float)cycle) * 100.0;
  if (heartbeatPhase >= 100) heartbeatPhase -= 100;
  int p = (int)heartbeatPhase;

  float v = 0;
  if (p < 15) {
    v = sinPulse(map(p, 0, 15, 0, 100));
  } else if (p >= 20 && p < 35) {
    v = sinPulse(map(p, 20, 35, 0, 100)) * 0.6;
  }

  float base = 5 + prox * 15;
  return base + v * (60 + prox * 35);
}

// Breathing: smooth sine inhale/exhale, deepens with proximity
float behaviourBreathing() {
  float prox = proximityFactor();
  unsigned long cycle = map((int)(prox * 100), 0, 100, 5000, 3000);
  int p = cycleProgress(cycle);

  float v = sinPulse(p);
  float low = 5;
  float high = 40 + prox * 55;
  return low + v * (high - low);
}

// Startle: bright flash on new detection, then settle
// NOTE: startleTriggered resets when activeSensorCount drops to 0.
// If sensors are flaky (dropping in/out), this could cause repeated startles.
// If that happens, consider adding a cooldown timer here.
float behaviourStartle() {
  unsigned long now = millis();

  if (activeSensorCount > 0 && !startleTriggered) {
    startleTriggered = true;
    startleTime = now;
  }
  if (activeSensorCount == 0) {
    startleTriggered = false;
  }

  if (startleTriggered && (now - startleTime) < 200) {
    return 40 + ((now - startleTime) / 200.0) * 40;
  } else if (startleTriggered) {
    unsigned long elapsed = now - startleTime - 200;
    float decay = constrain(80 - (elapsed / 40.0), 15, 80);
    return decay;
  }
  return 5;
}

// Excitement: playful pulsing that intensifies with people/proximity
float behaviourExcitement() {
  float prox = proximityFactor();
  float crowd = activeSensorCount / 3.0;
  float intensity = max(prox, crowd);

  unsigned long cycle = map((int)(intensity * 100), 0, 100, 1200, 400);
  unsigned long now = millis();
  unsigned long dt = now - lastExcitementTime;
  lastExcitementTime = now;
  excitementPhase += (dt / (float)cycle) * 100.0;
  if (excitementPhase >= 100) excitementPhase -= 100;
  int p = (int)excitementPhase;

  float pulse = sinPulse(p);
  float base = 15 + intensity * 25;
  return constrain(base + pulse * (25 + intensity * 25), 0, 80);
}

// Peek-a-boo: dims to near-dark, pulses brightly when someone close
float behaviourPeekaboo() {
  float prox = proximityFactor();
  unsigned long now = millis();
  unsigned long dt = now - lastPeekabooTime;
  lastPeekabooTime = now;

  if (prox < 0.1) {
    peekabooPhase += (dt / 4000.0) * 100.0;
    if (peekabooPhase >= 100) peekabooPhase -= 100;
    return 3 + sinPulse((int)peekabooPhase) * 5;
  }

  unsigned long cycle = map((int)(prox * 100), 10, 100, 2000, 600);
  peekabooPhase += (dt / (float)cycle) * 100.0;
  if (peekabooPhase >= 100) peekabooPhase -= 100;
  int p = (int)peekabooPhase;
  return 20 + sinPulse(p) * (50 + prox * 30);
}

// Swarm: fast irregular pulsing, frequency increases with activeSensorCount
float behaviourSwarm() {
  float crowd = activeSensorCount / 3.0;
  unsigned long cycle = map((int)(crowd * 100), 0, 100, 2000, 600);
  int p = cycleProgress(cycle);

  float v = sinPulse(p);
  int p2 = cycleProgress(max(cycle * 2UL / 3, 400UL));
  float v2 = sinPulse(p2) * 0.25;

  float base = 10 + crowd * 20;
  return constrain(base + (v + v2) * (20 + crowd * 20), 0, 75);
}

// Tidal: very slow rise and fall, unaffected by proximity
float behaviourTidal() {
  int p = cycleProgress(10000);
  float v = sinPulse(p);
  return 8 + v * 35;
}

// Glow drift: gentle random wandering between low brightness levels
float behaviourGlowDrift() {
  unsigned long now = millis();

  if ((long)(now - glowDriftNextChange) >= 0) {
    glowDriftTarget = random(8, 35);
    glowDriftNextChange = now + random(2000, 5000);
  }

  if (glowDriftCurrent < glowDriftTarget) {
    glowDriftCurrent += 0.05;
  } else {
    glowDriftCurrent -= 0.05;
  }
  glowDriftCurrent = constrain(glowDriftCurrent, 5, 40);
  return glowDriftCurrent;
}

// Deep pulse: ultra-slow single pulse, barely perceptible
float behaviourDeepPulse() {
  int p = cycleProgress(18000);
  float v = sinPulse(p);
  return 4 + v * 18;
}

// Awakening: dark when alone, blooms as someone approaches
float behaviourAwakening() {
  float prox = proximityFactor();
  if (prox < 0.05) return 2;

  float target = 10 + prox * 85;
  if (prox > 0.5) {
    int p = cycleProgress(3000);
    target += sinPulse(p) * 10;
  }
  return constrain(target, 2, 100);
}

// Supernova: builds to sustained maximum blaze with 3 sensors active and close
float behaviourSupernova() {
  float prox = proximityFactor();
  float crowd = activeSensorCount / 3.0;
  float intensity = prox * 0.4 + crowd * 0.6;

  if (intensity > 0.8) {
    int p = cycleProgress(2000);
    return 75 + sinPulse(p) * 10;
  }

  float base = 10 + intensity * 55;
  int p = cycleProgress(3000);
  return base + sinPulse(p) * (8 + intensity * 12);
}

// Eclipse: bright when alone, dims as people crowd around
float behaviourEclipse() {
  float prox = proximityFactor();
  float crowd = activeSensorCount / 3.0;
  float presence = max(prox, crowd);

  float brightness = 85 - presence * 75;
  int p = cycleProgress(6000);
  brightness += sinPulse(p) * 8;
  return constrain(brightness, 5, 95);
}

// Shy: dims as people approach, blooms when they step away
float behaviourShy() {
  float prox = proximityFactor();

  if (prox > 0.1) {
    float target = 50 - prox * 45;
    int p = cycleProgress(4000);
    return constrain(target + sinPulse(p) * 5, 3, 55);
  }

  int p = cycleProgress(7000);
  float bloom = sinPulse(p);
  return 30 + bloom * 50;
}

// Ember: warm low glow with subtle drift, presence causes gentle swell
float behaviourEmber() {
  unsigned long now = millis();
  float prox = proximityFactor();

  if ((long)(now - emberNextDrift) >= 0) {
    emberDriftTarget = random(12, 22);
    emberNextDrift = now + random(3000, 7000);
  }
  if (emberCurrent < emberDriftTarget) emberCurrent += 0.02;
  else emberCurrent -= 0.02;
  emberCurrent = constrain(emberCurrent, 10, 25);

  if (prox > 0.05) {
    float swell = prox * 25;
    int p = cycleProgress(6000);
    swell += sinPulse(p) * prox * 10;
    return constrain(emberCurrent + swell, 10, 55);
  }

  return emberCurrent;
}

// ─── MODE DISPATCHER ─────────────────────────────────────────────────────────
float computeOpacity(int behaviour) {
  switch (behaviour) {
    case BH_HEARTBEAT:  return behaviourHeartbeat();
    case BH_BREATHING:  return behaviourBreathing();
    case BH_STARTLE:    return behaviourStartle();
    case BH_EXCITEMENT: return behaviourExcitement();
    case BH_PEEKABOO:   return behaviourPeekaboo();
    case BH_SWARM:      return behaviourSwarm();
    case BH_TIDAL:      return behaviourTidal();
    case BH_GLOW_DRIFT: return behaviourGlowDrift();
    case BH_DEEP_PULSE: return behaviourDeepPulse();
    case BH_AWAKENING:  return behaviourAwakening();
    case BH_SUPERNOVA:  return behaviourSupernova();
    case BH_ECLIPSE:    return behaviourEclipse();
    case BH_SHY:        return behaviourShy();
    case BH_EMBER:      return behaviourEmber();
    default:            return 0;
  }
}

// ─── AUTO MODE LOGIC ─────────────────────────────────────────────────────────
int pickFromPool(const int* pool, int count) {
  int enabled[BH_COUNT];
  int enabledCount = 0;
  for (int i = 0; i < count; i++) {
    if (autoModeEnabled[pool[i]]) enabled[enabledCount++] = pool[i];
  }
  if (enabledCount == 0) return pool[0];

  int others[BH_COUNT];
  int othersCount = 0;
  for (int i = 0; i < enabledCount; i++) {
    if (enabled[i] != currentBehaviour) others[othersCount++] = enabled[i];
  }

  if (othersCount > 0) return others[random(0, othersCount)];
  return enabled[random(0, enabledCount)];
}

int selectAutoBehaviour() {
  if (activeSensorCount == 0) {
    int pool[] = {BH_TIDAL, BH_BREATHING, BH_GLOW_DRIFT, BH_DEEP_PULSE, BH_EMBER};
    return pickFromPool(pool, 5);
  } else if (activeSensorCount == 1) {
    int pool[] = {BH_AWAKENING, BH_HEARTBEAT, BH_PEEKABOO, BH_SHY, BH_EMBER};
    return pickFromPool(pool, 5);
  } else if (activeSensorCount == 2) {
    int pool[] = {BH_EXCITEMENT, BH_SHY, BH_HEARTBEAT, BH_AWAKENING, BH_EMBER};
    return pickFromPool(pool, 5);
  } else {
    int pool[] = {BH_SUPERNOVA, BH_SWARM, BH_EXCITEMENT, BH_HEARTBEAT, BH_EMBER};
    return pickFromPool(pool, 5);
  }
}

// ─── CROSSFADE ───────────────────────────────────────────────────────────────
void updateCrossfade() {
  if (crossfadeProgress < 100) {
    unsigned long elapsed = millis() - crossfadeStart;
    crossfadeProgress = constrain((elapsed * 100UL) / CROSSFADE_MS, 0, 100);
  }
}

void startCrossfade(int newBehaviour) {
  if (newBehaviour != currentBehaviour) {
    #ifdef DEBUG
    Serial.print(F("→ Switching: "));
    Serial.print(behaviourNames[currentBehaviour]);
    Serial.print(F(" → "));
    Serial.println(behaviourNames[newBehaviour]);
    #endif

    prevOpacity = currentOpacity;
    targetBehaviour = newBehaviour;
    crossfadeStart = millis();
    crossfadeProgress = 0;
    targetModeStartTime = millis();

    if (newBehaviour == BH_HEARTBEAT) { heartbeatPhase = 0; lastHeartbeatTime = millis(); }
    if (newBehaviour == BH_EXCITEMENT) { excitementPhase = 0; lastExcitementTime = millis(); }
  }
}

// ─── MAIN LOOP ───────────────────────────────────────────────────────────────
void loop() {
  readSensors();

  // Determine target behaviour
  if (AUTO_MODE) {
    bool isIdle = (activeSensorCount == 0);
    bool stateTransition = (isIdle != wasIdle);
    bool dwellExpired = (millis() - lastSwitchTime) > MAX_DWELL_MS;
    wasIdle = isIdle;

    if ((stateTransition || dwellExpired) && crossfadeProgress >= 100) {
      int desired = selectAutoBehaviour();
      lastSwitchTime = millis();
      if (desired != currentBehaviour) {
        startCrossfade(desired);
      }
    }
  } else {
    if (MANUAL_BEHAVIOUR != currentBehaviour && crossfadeProgress >= 100) {
      startCrossfade(MANUAL_BEHAVIOUR);
    }
  }

  updateCrossfade();

  // Compute opacity from target behaviour
  unsigned long savedModeStartTime = modeStartTime;
  modeStartTime = targetModeStartTime;
  targetOpacity = computeOpacity(targetBehaviour);
  modeStartTime = savedModeStartTime;

  // Apply crossfade blending
  float output;
  if (crossfadeProgress >= 100) {
    if (currentBehaviour != targetBehaviour) {
      modeStartTime = targetModeStartTime;
      currentBehaviour = targetBehaviour;
    }
    output = targetOpacity;
  } else {
    float oldOpacity = computeOpacity(currentBehaviour);
    float blend = crossfadeProgress / 100.0;
    output = oldOpacity * (1.0 - blend) + targetOpacity * blend;
  }

  // Clamp and write
  output = constrain(output, 0, 100);
  currentOpacity = output;
  int pwm = (int)round(output / 100.0 * 255);
  analogWrite(LED_PIN, pwm);

  // ─── DEBUG OUTPUT ────────────────────────────────────────────────────────
  #ifdef DEBUG
  loopCount++;
  static unsigned long lastDebugTime = 0;
  unsigned long now = millis();
  if (now - lastDebugTime > 500) {
    // Line 1: timing + behaviour state
    Serial.print(F("["));
    Serial.print(now / 1000); Serial.print(F("s] "));
    Serial.print(F("BH:")); Serial.print(behaviourNames[currentBehaviour]);
    if (currentBehaviour != targetBehaviour) {
      Serial.print(F(" →")); Serial.print(behaviourNames[targetBehaviour]);
      Serial.print(F(" CF:")); Serial.print((int)crossfadeProgress); Serial.print(F("%"));
    }
    Serial.print(F(" O:")); Serial.print((int)output);
    Serial.print(F("% PWM:")); Serial.println(pwm);

    // Line 2: sensor distances
    Serial.print(F("  "));
    for (int i = 0; i < 3; i++) {
      Serial.print(F("S")); Serial.print(i + 1); Serial.print(F(":"));
      if (!sensorEnabled[i]) {
        Serial.print(F("OFF "));
      } else if (cachedDistance[i] < 0) {
        Serial.print(F("--- "));
      } else {
        Serial.print(cachedDistance[i]); Serial.print(F("cm "));
      }
    }
    Serial.print(F("| closest:"));
    if (closestDistance < 0) Serial.print(F("none"));
    else { Serial.print(closestDistance); Serial.print(F("cm")); }
    Serial.print(F(" active:")); Serial.println(activeSensorCount);

    // Line 3: sensor health (every 5 seconds to reduce noise)
    static unsigned long lastHealthTime = 0;
    if (now - lastHealthTime > 5000) {
      lastHealthTime = now;
      unsigned long elapsed = now - lastDebugTime;
      Serial.print(F("  health: "));
      for (int i = 0; i < 3; i++) {
        if (!sensorEnabled[i]) continue;
        Serial.print(F("S")); Serial.print(i + 1);
        Serial.print(F("[ok=")); Serial.print(sensorOkCount[i]);
        Serial.print(F(" fail=")); Serial.print(sensorFailCount[i]);
        Serial.print(F(" tout=")); Serial.print(sensorTimeoutCount[i]);
        Serial.print(F("] "));
      }
      Serial.print(F("loops/s:"));
      Serial.println(loopCount * 1000UL / max(now, 1UL));
    }

    lastDebugTime = now;
  }
  #endif

  delay(10); // Loop pacing — keeps ~80Hz update rate
             // NOTE: this + LISTEN_SETTLE_MS = total blocking time per loop.
             // If sensors seem unreliable, try reducing this to 5 or removing it,
             // but the LED update rate will be unnecessarily fast.
}
