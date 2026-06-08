#include <M5Unified.h>

constexpr int STICK_H = 34;
constexpr int STICK_V = 33;
constexpr bool STICK_H_INVERT = true;
constexpr bool STICK_V_INVERT = true;
constexpr int BUTTON_PINS[] = {19, 21, 22, 23, 25, 26, 32};
constexpr int BUTTON_PIN_COUNT = sizeof(BUTTON_PINS) / sizeof(BUTTON_PINS[0]);

constexpr int CALIBRATION_SAMPLES = 100;
constexpr int FILTER_SAMPLES = 8;
constexpr uint32_t SEND_INTERVAL_MS = 20;

float hLog[FILTER_SAMPLES] = {};
float vLog[FILTER_SAMPLES] = {};
int filterIndex = 0;
int centerH = 2048;
int centerV = 2048;
uint32_t lastSendMs = 0;

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

float normalizeAxis(int raw, int center, bool invert) {
  float span = raw >= center ? float(4095 - center) : float(center);
  if (span < 1.0f) span = 1.0f;
  float value = (raw - center) / span;
  if (invert) value = -value;
  return clampFloat(value, -1.0f, 1.0f);
}

void calibrateStick() {
  long hSum = 0;
  long vSum = 0;
  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    hSum += analogRead(STICK_H);
    vSum += analogRead(STICK_V);
    delay(5);
  }

  centerH = hSum / CALIBRATION_SAMPLES;
  centerV = vSum / CALIBRATION_SAMPLES;

  for (int i = 0; i < FILTER_SAMPLES; i++) {
    hLog[i] = 0.0f;
    vLog[i] = 0.0f;
  }

  Serial.printf("CENTER,%d,%d\n", centerH, centerV);
}

void readJoystick(float &joyH, float &joyV, int &rawH, int &rawV) {
  rawH = analogRead(STICK_H);
  rawV = analogRead(STICK_V);

  hLog[filterIndex] = normalizeAxis(rawH, centerH, STICK_H_INVERT);
  vLog[filterIndex] = normalizeAxis(rawV, centerV, STICK_V_INVERT);
  filterIndex = (filterIndex + 1) % FILTER_SAMPLES;

  float hSum = 0.0f;
  float vSum = 0.0f;
  for (int i = 0; i < FILTER_SAMPLES; i++) {
    hSum += hLog[i];
    vSum += vLog[i];
  }

  joyH = hSum / FILTER_SAMPLES;
  joyV = vSum / FILTER_SAMPLES;
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  pinMode(STICK_H, ANALOG);
  pinMode(STICK_V, ANALOG);
  for (int i = 0; i < BUTTON_PIN_COUNT; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
  }

  Serial.println("JOYSTICK_VISUALIZER_READY");
  Serial.println("Keep stick centered while booting.");
  calibrateStick();
}

void loop() {
  M5.update();

  if (millis() - lastSendMs < SEND_INTERVAL_MS) {
    delay(2);
    return;
  }
  lastSendMs = millis();

  float joyH = 0.0f;
  float joyV = 0.0f;
  int rawH = 0;
  int rawV = 0;
  readJoystick(joyH, joyV, rawH, rawV);

  uint32_t buttonMask = 0;
  for (int i = 0; i < BUTTON_PIN_COUNT; i++) {
    if (digitalRead(BUTTON_PINS[i]) == LOW) {
      buttonMask |= (1UL << i);
    }
  }

  Serial.printf("JOY,%d,%d,%.3f,%.3f,%lu\n", rawH, rawV, joyH, joyV, static_cast<unsigned long>(buttonMask));
}
