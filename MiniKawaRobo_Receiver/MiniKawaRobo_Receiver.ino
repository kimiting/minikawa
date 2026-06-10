#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <ESP32Servo.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// 4 Servo PCB for M5ATOM Matrix/S3
// Atom Matrix/Lite side: G33, G22, G23, G19
// AtomS3 side labels on the PCB are: G8, G5, G7, G6
constexpr int DEFAULT_ARM_SERVO_PIN = 33;
constexpr int DEFAULT_LEFT_SERVO_PIN = 22;
constexpr int DEFAULT_RIGHT_SERVO_PIN = 23;
constexpr int SPARE_SERVO_PIN = 19;
constexpr int STATUS_LED_PIN = 27;
constexpr int PAIR_NEXT_BUTTON = 39; // Atom Lite button

constexpr int ARM_MIN_ANGLE = 0;
constexpr int ARM_MAX_ANGLE = 180;
constexpr int ARM_START_ANGLE = 90;
constexpr int ARM_SERVO_MIN_US = 500;
constexpr int ARM_SERVO_MAX_US = 2400;
constexpr uint32_t ARM_DETACH_MS = 3000;
constexpr uint32_t ARM_SMOOTH_INTERVAL_MS = 15;
constexpr int ARM_NUDGE_DEGREES = 1;
constexpr float ARM_ESTIMATED_DEGREES_PER_MS = 0.22f;
constexpr int ARM_STOP_US = 1500;
constexpr int ARM_SPEED_US = 180;
constexpr bool ARM_SPEED_INVERT = false;

constexpr int SERVO_STOP_US = 1500;
constexpr int SERVO_SPEED_US = 350;
constexpr int LEFT_TRIM_US = 0;
constexpr int RIGHT_TRIM_US = 0;
constexpr bool LEFT_INVERT = false;
constexpr bool RIGHT_INVERT = true;

constexpr uint8_t PAIR_ID_START = 1;
constexpr uint8_t PAIR_ID_MIN = 1;
constexpr uint8_t PAIR_ID_MAX = 9;
constexpr uint32_t PACKET_MAGIC_BASE = 0x4D4B5200; // "MKR" + pair ID
constexpr uint8_t MODE_DRIVE = 0;
constexpr uint8_t MODE_ARM_UP = 1;
constexpr uint8_t MODE_ARM_DOWN = 2;
constexpr uint8_t MODE_ARM_CENTER = 3;
constexpr uint8_t MODE_ARM_SET = 4;
constexpr uint32_t FAILSAFE_MS = 500;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t DISPLAY_INTERVAL_MS = 100;
constexpr uint32_t FAILSAFE_DISPLAY_INTERVAL_MS = 250;
constexpr uint32_t LED_GREEN = 0x00ff00;
constexpr uint32_t LED_BLUE = 0x0000ff;
constexpr uint32_t LED_RED = 0xff0000;
constexpr uint32_t LED_YELLOW = 0xffff00;
constexpr uint32_t LED_PURPLE = 0xff00ff;
constexpr uint32_t LED_WHITE = 0xffffff;

struct __attribute__((packed)) ControlPacket {
  uint32_t magic;
  uint32_t seq;
  int16_t leftSpeed;
  int16_t rightSpeed;
  uint8_t armAngle;
  uint8_t mode;
};

Servo armServo;
Servo leftServo;
Servo rightServo;
Servo spareServo;

volatile bool packetAvailable = false;
ControlPacket latestPacket = {};
uint32_t lastPacketMs = 0;
uint32_t lastSeq = 0;
uint32_t lastRxLogMs = 0;
uint32_t lastArmWriteMs = 0;
uint32_t lastArmNudgeMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastFailsafeDisplayMs = 0;
uint32_t lastLedBlinkMs = 0;
bool ledBlinkOn = false;
uint8_t lastArmMode = MODE_DRIVE;
String serialLine;
int currentArmAngle = ARM_START_ANGLE;
float estimatedArmAngle = ARM_START_ANGLE;
uint32_t lastArmEstimateMs = 0;
bool armServoAttached = false;
uint8_t pairId = PAIR_ID_START;

int armServoPin = DEFAULT_ARM_SERVO_PIN;
int leftServoPin = DEFAULT_LEFT_SERVO_PIN;
int rightServoPin = DEFAULT_RIGHT_SERVO_PIN;
int spareServoPin = SPARE_SERVO_PIN;

void setLed(uint32_t color) {
  uint8_t red = (color >> 16) & 0xff;
  uint8_t green = (color >> 8) & 0xff;
  uint8_t blue = color & 0xff;
  neopixelWrite(STATUS_LED_PIN, red, green, blue);
}

void blinkLed(uint32_t color, uint32_t intervalMs = 250) {
  uint32_t now = millis();
  if (now - lastLedBlinkMs >= intervalMs) {
    lastLedBlinkMs = now;
    ledBlinkOn = !ledBlinkOn;
    setLed(ledBlinkOn ? color : 0x000000);
  }
}

int clampInt(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

int speedToPulse(int speed, bool invert, int trimUs) {
  speed = clampInt(speed, -100, 100);
  if (invert) speed = -speed;
  return SERVO_STOP_US + trimUs + (speed * SERVO_SPEED_US / 100);
}

uint32_t packetMagicForPair(uint8_t id) {
  return PACKET_MAGIC_BASE | id;
}

void stopAllMotion() {
  stopDriveServos();
  stopArmSpeed();
  lastArmMode = MODE_DRIVE;
}

void setPairId(uint8_t id) {
  pairId = id;
  packetAvailable = false;
  latestPacket = {};
  lastPacketMs = 0;
  lastSeq = 0;
  stopAllMotion();
  Serial.printf("PAIR_ID changed: %u\n", pairId);
  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.printf("PAIR ID %u\n", pairId);
  M5.Display.println("Waiting...");
  setLed(LED_PURPLE);
}

void incrementPairId() {
  uint8_t nextId = pairId + 1;
  if (nextId > PAIR_ID_MAX) nextId = PAIR_ID_MIN;
  setPairId(nextId);
}

void readPairButton() {
  if (M5.BtnA.wasPressed()) {
    incrementPairId();
  }
}

void stopDriveServos() {
  leftServo.writeMicroseconds(SERVO_STOP_US + LEFT_TRIM_US);
  rightServo.writeMicroseconds(SERVO_STOP_US + RIGHT_TRIM_US);
}

void attachArmServoIfNeeded() {
  if (armServoAttached) return;
  armServo.setPeriodHertz(50);
  armServo.attach(armServoPin, ARM_SERVO_MIN_US, ARM_SERVO_MAX_US);
  armServoAttached = true;
  lastArmEstimateMs = millis();
}

void updateArmEstimate() {
  uint32_t now = millis();
  if (lastArmEstimateMs == 0) {
    lastArmEstimateMs = now;
    return;
  }

  uint32_t elapsedMs = now - lastArmEstimateMs;
  lastArmEstimateMs = now;
  if (elapsedMs == 0) return;

  float target = currentArmAngle;
  float diff = target - estimatedArmAngle;
  float maxStep = elapsedMs * ARM_ESTIMATED_DEGREES_PER_MS;

  if (diff > maxStep) {
    estimatedArmAngle += maxStep;
  } else if (diff < -maxStep) {
    estimatedArmAngle -= maxStep;
  } else {
    estimatedArmAngle = target;
  }
}

void moveArmTo(int angle) {
  angle = clampInt(angle, ARM_MIN_ANGLE, ARM_MAX_ANGLE);
  updateArmEstimate();
  attachArmServoIfNeeded();
  armServo.write(angle);
  currentArmAngle = angle;
  lastArmWriteMs = millis();
}

void nudgeArm(int delta, bool immediate) {
  uint32_t now = millis();
  if (!immediate && now - lastArmNudgeMs < ARM_SMOOTH_INTERVAL_MS) return;
  lastArmNudgeMs = now;
  updateArmEstimate();
  int baseAngle = clampInt(static_cast<int>(estimatedArmAngle + 0.5f), ARM_MIN_ANGLE, ARM_MAX_ANGLE);
  currentArmAngle = baseAngle;
  estimatedArmAngle = baseAngle;
  moveArmTo(baseAngle + delta);
}

void detachArmServoIfIdle() {
  if (armServoAttached && millis() - lastArmWriteMs > ARM_DETACH_MS) {
    updateArmEstimate();
    currentArmAngle = clampInt(static_cast<int>(estimatedArmAngle + 0.5f), ARM_MIN_ANGLE, ARM_MAX_ANGLE);
    estimatedArmAngle = currentArmAngle;
    armServo.detach();
    armServoAttached = false;
  }
}

void stopArmServoNow() {
  if (!armServoAttached) return;
  updateArmEstimate();
  currentArmAngle = clampInt(static_cast<int>(estimatedArmAngle + 0.5f), ARM_MIN_ANGLE, ARM_MAX_ANGLE);
  estimatedArmAngle = currentArmAngle;
  armServo.detach();
  armServoAttached = false;
}

void writeArmSpeed(int speed) {
  speed = clampInt(speed, -100, 100);
  if (ARM_SPEED_INVERT) speed = -speed;

  attachArmServoIfNeeded();
  int pulse = ARM_STOP_US + (speed * ARM_SPEED_US / 100);
  armServo.writeMicroseconds(pulse);
  lastArmWriteMs = millis();
}

void stopArmSpeed() {
  attachArmServoIfNeeded();
  armServo.writeMicroseconds(ARM_STOP_US);
  lastArmWriteMs = millis();
}

void attachServos() {
  armServo.detach();
  armServoAttached = false;
  leftServo.detach();
  rightServo.detach();
  spareServo.detach();

  leftServo.setPeriodHertz(50);
  rightServo.setPeriodHertz(50);
  spareServo.setPeriodHertz(50);

  leftServo.attach(leftServoPin, 1000, 2000);
  rightServo.attach(rightServoPin, 1000, 2000);
  spareServo.attach(spareServoPin, 1000, 2000);

  currentArmAngle = ARM_START_ANGLE;
  estimatedArmAngle = ARM_START_ANGLE;
  lastArmEstimateMs = millis();
  stopDriveServos();
  spareServo.writeMicroseconds(SERVO_STOP_US);
}

void printPins() {
  Serial.printf("PINS arm=%d left=%d right=%d spare=%d\n",
                armServoPin, leftServoPin, rightServoPin, spareServoPin);
}

void testOnePin(int pin) {
  Servo s;
  s.setPeriodHertz(50);
  s.attach(pin, 500, 2400);
  Serial.printf("TEST pin %d\n", pin);
  s.writeMicroseconds(1300);
  delay(700);
  s.writeMicroseconds(1700);
  delay(700);
  s.writeMicroseconds(1500);
  delay(400);
  s.detach();
}

void runServoTest() {
  Serial.println("TEST all 4 Atom servo pins");
  testOnePin(33);
  testOnePin(22);
  testOnePin(23);
  testOnePin(19);
  attachServos();
  Serial.println("TEST done");
}

void applyArmMode(uint8_t mode, int requestedAngle) {
  bool modeJustChanged = mode != lastArmMode;
  lastArmMode = mode;

  if (mode == MODE_ARM_CENTER) {
    moveArmTo(ARM_START_ANGLE);
  } else if (mode == MODE_ARM_SET) {
    moveArmTo(requestedAngle);
  } else if (mode == MODE_ARM_UP) {
    writeArmSpeed(100);
  } else if (mode == MODE_ARM_DOWN) {
    writeArmSpeed(-100);
  } else if (modeJustChanged) {
    stopArmSpeed();
  }
}

const char *modeName(uint8_t mode) {
  if (mode == MODE_ARM_UP) return "ARM_UP";
  if (mode == MODE_ARM_DOWN) return "ARM_DOWN";
  if (mode == MODE_ARM_CENTER) return "CENTER";
  if (mode == MODE_ARM_SET) return "ARM_SET";
  return "DRIVE";
}

void updateStatusDisplay(const ControlPacket &packet, int leftSpeed, int rightSpeed) {
  if (millis() - lastDisplayMs < DISPLAY_INTERVAL_MS) return;
  lastDisplayMs = millis();

  M5.Display.fillRect(0, 58, 320, 120, BLACK);
  M5.Display.setCursor(0, 58);
  M5.Display.printf("Seq: %lu\n", static_cast<unsigned long>(packet.seq));
  M5.Display.printf("L/R: %d / %d\n", leftSpeed, rightSpeed);
  M5.Display.printf("Arm: %d\n", currentArmAngle);
  M5.Display.printf("Mode: %s\n", modeName(packet.mode));
}

void applyPacket(const ControlPacket &packet) {
  int arm = clampInt(packet.armAngle, ARM_MIN_ANGLE, ARM_MAX_ANGLE);
  applyArmMode(packet.mode, arm);

  int leftSpeed = clampInt(packet.leftSpeed, -100, 100);
  int rightSpeed = clampInt(packet.rightSpeed, -100, 100);
  int leftPulse = speedToPulse(leftSpeed, LEFT_INVERT, LEFT_TRIM_US);
  int rightPulse = speedToPulse(rightSpeed, RIGHT_INVERT, RIGHT_TRIM_US);
  leftServo.writeMicroseconds(leftPulse);
  rightServo.writeMicroseconds(rightPulse);

  updateStatusDisplay(packet, leftSpeed, rightSpeed);

  if (packet.mode == MODE_ARM_UP || packet.mode == MODE_ARM_DOWN || packet.mode == MODE_ARM_CENTER || packet.mode == MODE_ARM_SET) {
    setLed(LED_RED);
  } else if (leftSpeed != 0 || rightSpeed != 0) {
    setLed(LED_BLUE);
  } else {
    setLed(LED_GREEN);
  }
}

void printRxLog(const ControlPacket &packet) {
  if (millis() - lastRxLogMs < 100) return;
  lastRxLogMs = millis();
  Serial.printf("ID=%u RX seq=%lu L/R=%d/%d Arm=%d Mode=%s age=0ms\n",
                pairId,
                static_cast<unsigned long>(packet.seq),
                packet.leftSpeed,
                packet.rightSpeed,
                currentArmAngle,
                modeName(packet.mode));
}

bool parseSerialCommand(const String &line, ControlPacket &packet) {
  char mode = 0;
  int left = 0;
  int right = 0;
  int arm = ARM_START_ANGLE;
  int armPin = 0;
  int leftPin = 0;
  int rightPin = 0;
  int sparePin = 0;
  int armDelta = 0;

  if (sscanf(line.c_str(), "D %d %d %d", &left, &right, &arm) == 3) {
    mode = MODE_DRIVE;
  } else if (sscanf(line.c_str(), "D %d %d", &left, &right) == 2) {
    mode = MODE_DRIVE;
    arm = currentArmAngle;
  } else if (sscanf(line.c_str(), "A %d", &arm) == 1) {
    mode = MODE_ARM_SET;
    left = 0;
    right = 0;
  } else if (sscanf(line.c_str(), "R %d", &armDelta) == 1) {
    mode = MODE_ARM_SET;
    left = 0;
    right = 0;
    arm = currentArmAngle + armDelta;
  } else if (line == "S") {
    mode = MODE_DRIVE;
    left = 0;
    right = 0;
    arm = currentArmAngle;
  } else if (sscanf(line.c_str(), "P %d %d %d %d", &armPin, &leftPin, &rightPin, &sparePin) == 4) {
    armServoPin = armPin;
    leftServoPin = leftPin;
    rightServoPin = rightPin;
    spareServoPin = sparePin;
    attachServos();
    printPins();
    return false;
  } else if (line == "P?") {
    printPins();
    return false;
  } else if (line == "T") {
    runServoTest();
    return false;
  } else {
    return false;
  }

  packet.magic = packetMagicForPair(pairId);
  packet.seq = lastSeq + 1;
  packet.leftSpeed = clampInt(left, -100, 100);
  packet.rightSpeed = clampInt(right, -100, 100);
  packet.armAngle = clampInt(arm, ARM_MIN_ANGLE, ARM_MAX_ANGLE);
  packet.mode = mode;
  return true;
}

void readSerialCommands() {
  while (Serial.available()) {
    char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;

    if (ch == '\n') {
      serialLine.trim();
      if (serialLine.length() > 0) {
        ControlPacket packet;
        if (parseSerialCommand(serialLine, packet)) {
          latestPacket = packet;
          packetAvailable = true;
          Serial.println("OK");
        } else if (serialLine != "T" && serialLine != "P?" && !serialLine.startsWith("P ")) {
          Serial.println("ERR");
        }
      }
      serialLine = "";
    } else if (serialLine.length() < 64) {
      serialLine += ch;
    }
  }
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  if (len != sizeof(ControlPacket)) return;

  ControlPacket packet;
  memcpy(&packet, data, sizeof(packet));
  if (packet.magic != packetMagicForPair(pairId)) return;

  latestPacket = packet;
  packetAvailable = true;
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(SERIAL_BAUD);
  setLed(LED_PURPLE);
  M5.Display.setTextSize(2);
  M5.Display.println("MiniKawa Receiver");

  attachServos();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    M5.Display.println("ESP-NOW init failed");
    while (true) delay(100);
  }

  esp_now_register_recv_cb(onDataRecv);

  M5.Display.printf("MAC:\n%s\n", WiFi.macAddress().c_str());
  M5.Display.println("4 Servo PCB PWM");
  M5.Display.println("Waiting...");
  Serial.println("MiniKawa Receiver ready");
  Serial.printf("PAIR_ID: %u\n", pairId);
  Serial.printf("Press G%d / Atom button to change PAIR_ID.\n", PAIR_NEXT_BUTTON);
  Serial.println("Commands: D left right [arm] | A arm | R delta | S | P arm left right spare | P? | T");
  printPins();
}

void loop() {
  M5.update();
  readPairButton();
  readSerialCommands();

  if (packetAvailable) {
    noInterrupts();
    ControlPacket packet = latestPacket;
    packetAvailable = false;
    interrupts();

    lastPacketMs = millis();
    lastSeq = packet.seq;
    applyPacket(packet);
    printRxLog(packet);
  }

  if (millis() - lastPacketMs > FAILSAFE_MS) {
    stopDriveServos();
    blinkLed(LED_WHITE);
    if (millis() - lastFailsafeDisplayMs > FAILSAFE_DISPLAY_INTERVAL_MS) {
      lastFailsafeDisplayMs = millis();
      M5.Display.fillRect(0, 178, 320, 32, BLACK);
      M5.Display.setCursor(0, 178);
      M5.Display.printf("Failsafe stop");
    }
  }

  detachArmServoIfIdle();
  delay(1);
}
