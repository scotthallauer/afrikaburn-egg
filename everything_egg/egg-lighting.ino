/*
 * AfrikaBurn Egg — Lighting Behaviour System
 * ============================================
 * Single-colour LED strip controlled via PWM on Arduino Nano.
 * Three LD2410 radar sensors provide proximity data.
 * 14 behaviour modes with crossfade transitions.
 *
 * Hardware:
 *   - Arduino Nano
 *   - 1x PWM LED strip on LED_PIN
 *   - 3x LD2410 radar sensors on SoftwareSerial
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
#define AUTO_MODE          true   // false = manual mode
#define MANUAL_BEHAVIOUR   0     // index into behaviours[] for manual mode
// #define DEBUG                    // uncomment to enable serial debug output

#define CROSSFADE_MS       800   // crossfade duration between behaviours
#define SENSOR_TIMEOUT_MS  2500  // clear cached reading after this long without data
#define MAX_DWELL_MS       180000 // max time (3 min) before auto mode picks a new behaviour

// Set to false to disable a broken sensor
bool sensorEnabled[3] = {true, true, true};

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
SoftwareSerial ss1(SENSOR1_RX, SENSOR1_TX);
SoftwareSerial ss2(SENSOR2_RX, SENSOR2_TX);
SoftwareSerial ss3(SENSOR3_RX, SENSOR3_TX);

MyLD2410 sensor1(ss1);
MyLD2410 sensor2(ss2);
MyLD2410 sensor3(ss3);

// Cached sensor state
int cachedDistance[3] = {-1, -1, -1};
unsigned long lastDataTime[3] = {0, 0, 0};

// Aggregated values available to all modes
int closestDistance = -1;
int activeSensorCount = 0;

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
unsigned long targetModeStartTime = 0; // start time for incoming behaviour during crossfade
unsigned long lastSwitchTime = 0;
bool wasIdle = true;  // tracks previous idle/active state for transition detection
unsigned long lastHeartbeatTime = 0;
float heartbeatPhase = 0;
unsigned long lastExcitementTime = 0;
float excitementPhase = 0;
bool startleTriggered = false;
unsigned long startleTime = 0;
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

  #ifdef DEBUG
  Serial.begin(9600);
  #endif

  ss1.begin(115200);
  ss2.begin(115200);
  ss3.begin(115200);

  sensor1.begin();
  sensor2.begin();
  sensor3.begin();

  modeStartTime = millis();
}

// ─── SENSOR READING ──────────────────────────────────────────────────────────
// Round-robin: read one sensor per loop iteration to give SoftwareSerial time
// to buffer a full frame between reads.
int currentSensorIndex = 0;

void readSensors() {
  unsigned long now = millis();
  MyLD2410* sensors[] = {&sensor1, &sensor2, &sensor3};
  SoftwareSerial* serials[] = {&ss1, &ss2, &ss3};

  // Only read the current sensor this iteration
  int i = currentSensorIndex;
  if (sensorEnabled[i]) {
    serials[i]->listen();
    delay(1); // brief yield to allow buffering
    if (sensors[i]->check() == MyLD2410::DATA) {
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
    } else if (now - lastDataTime[i] > SENSOR_TIMEOUT_MS) {
      cachedDistance[i] = -1;
    }
  } else {
    cachedDistance[i] = -1;
  }

  // Advance to next sensor for next loop iteration
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
  if (closestDistance <= 0) return 0.0;
  return constrain(map(closestDistance, 400, 30, 0, 100), 0, 100) / 100.0;
}

int cycleProgress(unsigned long cycleDuration) {
  // Returns 0–100 representing position within a repeating cycle
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

  // Double pulse: two bumps within one cycle
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
    // Softer flash — ramp up to 80 instead of instant 100
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

  if (prox < 0.1) {
    // Dim idle state with subtle movement
    int p = cycleProgress(4000);
    return 3 + sinPulse(p) * 5;
  }

  // "Noticed you" — bright pulse
  unsigned long cycle = map((int)(prox * 100), 10, 100, 2000, 600);
  int p = cycleProgress(cycle);
  return 20 + sinPulse(p) * (50 + prox * 30);
}

// Swarm: fast irregular pulsing, frequency increases with activeSensorCount
float behaviourSwarm() {
  float crowd = activeSensorCount / 3.0;
  unsigned long cycle = map((int)(crowd * 100), 0, 100, 2000, 600);
  int p = cycleProgress(cycle);

  float v = sinPulse(p);
  // Layer a second slower oscillation
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

  // Smooth drift toward target
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

  // Smooth bloom
  float target = 10 + prox * 85;
  // Add gentle pulse at high proximity
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
    // Sustained glow with gentle shimmer
    int p = cycleProgress(2000);
    return 75 + sinPulse(p) * 10;
  }

  // Building phase — slower pulse
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
  // Subtle slow pulse
  int p = cycleProgress(6000);
  brightness += sinPulse(p) * 8;
  return constrain(brightness, 5, 95);
}

// Shy: dims as people approach, blooms when they step away
float behaviourShy() {
  float prox = proximityFactor();

  if (prox > 0.1) {
    // Retreat — dim proportional to closeness
    float target = 50 - prox * 45;
    int p = cycleProgress(4000);
    return constrain(target + sinPulse(p) * 5, 3, 55);
  }

  // No one close — bloom back slowly
  int p = cycleProgress(7000);
  float bloom = sinPulse(p);
  return 30 + bloom * 50;
}

// Ember: warm low glow with subtle drift, presence causes gentle swell
float behaviourEmber() {
  unsigned long now = millis();
  float prox = proximityFactor();

  // Subtle random drift in base glow
  if ((long)(now - emberNextDrift) >= 0) {
    emberDriftTarget = random(12, 22);
    emberNextDrift = now + random(3000, 7000);
  }
  if (emberCurrent < emberDriftTarget) emberCurrent += 0.02;
  else emberCurrent -= 0.02;
  emberCurrent = constrain(emberCurrent, 10, 25);

  // Presence causes slow gentle swell
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
// Pick a random enabled behaviour from a pool, preferring one different from current
int pickFromPool(const int* pool, int count) {
  // Collect enabled options
  int enabled[BH_COUNT];
  int enabledCount = 0;
  for (int i = 0; i < count; i++) {
    if (autoModeEnabled[pool[i]]) enabled[enabledCount++] = pool[i];
  }
  if (enabledCount == 0) return pool[0];

  // Prefer something different from current
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
    prevOpacity = currentOpacity;
    targetBehaviour = newBehaviour;
    crossfadeStart = millis();
    crossfadeProgress = 0;
    targetModeStartTime = millis(); // new behaviour starts phase from 0

    // Reset phase state for time-delta behaviours to avoid spike on re-entry
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

  // Update crossfade state
  updateCrossfade();

  // Compute opacity from target behaviour
  unsigned long savedModeStartTime = modeStartTime;
  modeStartTime = targetModeStartTime;
  targetOpacity = computeOpacity(targetBehaviour);
  modeStartTime = savedModeStartTime;

  // Apply crossfade blending
  float output;
  if (crossfadeProgress >= 100) {
    // Fully transitioned
    if (currentBehaviour != targetBehaviour) {
      modeStartTime = targetModeStartTime;
      currentBehaviour = targetBehaviour;
    }
    output = targetOpacity;
  } else {
    // Blending: also compute the old behaviour's current value
    float oldOpacity = computeOpacity(currentBehaviour);
    float blend = crossfadeProgress / 100.0;
    output = oldOpacity * (1.0 - blend) + targetOpacity * blend;
  }

  // Clamp and write
  output = constrain(output, 0, 100);
  currentOpacity = output;
  int pwm = (int)round(output / 100.0 * 255);
  analogWrite(LED_PIN, pwm);

  #ifdef DEBUG
  static unsigned long lastDebugTime = 0;
  if (millis() - lastDebugTime > 500) {
    lastDebugTime = millis();
    Serial.print(F("B:"));   Serial.print(currentBehaviour);
    Serial.print(F(" T:"));  Serial.print(targetBehaviour);
    Serial.print(F(" O:"));  Serial.print((int)output);
    Serial.print(F(" CF:")); Serial.print((int)crossfadeProgress);
    Serial.print(F(" D:"));  Serial.print(closestDistance);
    Serial.print(F(" S:"));  Serial.print(activeSensorCount);
    Serial.print(F(" S1:")); Serial.print(cachedDistance[0]);
    Serial.print(F(" S2:")); Serial.print(cachedDistance[1]);
    Serial.print(F(" S3:")); Serial.println(cachedDistance[2]);
  }
  #endif

  delay(10); // Small yield — not blocking, just loop pacing
}
