#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>

// Atom Lite small joystick controller wiring found by setup wizard.
constexpr int STICK_H = 34;
constexpr int STICK_V = 33;
constexpr bool STICK_H_INVERT = true;
constexpr bool STICK_V_INVERT = true;
constexpr int ARM_UP_BUTTON = 21;
constexpr int ARM_DOWN_BUTTON = 25;
constexpr int ARM_CENTER_BUTTON = 22;
constexpr int STATUS_LED_PIN = 27;

constexpr uint8_t PAIR_ID = 2;
constexpr uint32_t PACKET_MAGIC = 0x4D4B5200 | PAIR_ID; // "MKR" + pair ID
constexpr uint8_t MODE_DRIVE = 0;
constexpr uint8_t MODE_ARM_UP = 1;
constexpr uint8_t MODE_ARM_DOWN = 2;
constexpr uint8_t MODE_ARM_CENTER = 3;
constexpr uint8_t MODE_ARM_SET = 4;
constexpr uint32_t SEND_INTERVAL_MS = 20;
constexpr int DRIVE_MAX_SPEED = 100;
constexpr int TURN_MAX_SPEED = 90;
constexpr int ARM_START_ANGLE = 90;
constexpr float DEADBAND = 0.14f;
constexpr int FILTER_SAMPLES = 8;
constexpr int CALIBRATION_SAMPLES = 80;
constexpr uint32_t PC_CONTROL_TIMEOUT_MS = 500;
constexpr uint32_t LED_OFF = 0x000000;
constexpr uint32_t LED_GREEN = 0x00ff00;
constexpr uint32_t LED_BLUE = 0x0000ff;
constexpr uint32_t LED_RED = 0xff0000;
constexpr uint32_t LED_YELLOW = 0xffff00;
constexpr uint32_t LED_PURPLE = 0xff00ff;

struct __attribute__((packed)) ControlPacket {
  uint32_t magic;
  uint32_t seq;
  int16_t leftSpeed;
  int16_t rightSpeed;
  uint8_t armAngle;
  uint8_t mode;
};

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

ControlPacket packet = {
  PACKET_MAGIC,
  0,
  0,
  0,
  ARM_START_ANGLE,
  MODE_DRIVE,
};

float hLog[FILTER_SAMPLES] = {};
float vLog[FILTER_SAMPLES] = {};
int filterIndex = 0;
int centerH = 2048;
int centerV = 2048;
uint32_t lastSendMs = 0;
uint32_t pcControlUntilMs = 0;
bool sendOk = false;
bool pcControlActive = false;
String serialLine;

void setLed(uint32_t color) {
  uint8_t red = (color >> 16) & 0xff;
  uint8_t green = (color >> 8) & 0xff;
  uint8_t blue = color & 0xff;
  neopixelWrite(STATUS_LED_PIN, red, green, blue);
}

int clampInt(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

float applyDeadband(float value) {
  if (value > -DEADBAND && value < DEADBAND) return 0.0f;
  if (value > 0.0f) return (value - DEADBAND) / (1.0f - DEADBAND);
  return (value + DEADBAND) / (1.0f - DEADBAND);
}

float normalizeAxis(int raw, int center, bool invert) {
  float span = raw >= center ? float(4095 - center) : float(center);
  if (span < 1.0f) span = 1.0f;
  float value = (raw - center) / span;
  if (invert) value = -value;
  return applyDeadband(clampFloat(value, -1.0f, 1.0f));
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

  Serial.printf("Calibrated center H=%d V=%d\n", centerH, centerV);
}

void readJoystick(float &joyH, float &joyV) {
  hLog[filterIndex] = normalizeAxis(analogRead(STICK_H), centerH, STICK_H_INVERT);
  vLog[filterIndex] = normalizeAxis(analogRead(STICK_V), centerV, STICK_V_INVERT);
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

void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) delay(100);
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (!esp_now_is_peer_exist(broadcastAddress)) {
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Add peer failed");
      while (true) delay(100);
    }
  }
}

void updateDrive(float joyH, float joyV) {
  int forward = static_cast<int>(joyV * DRIVE_MAX_SPEED);
  int turn = static_cast<int>(joyH * TURN_MAX_SPEED);
  packet.leftSpeed = clampInt(forward + turn, -100, 100);
  packet.rightSpeed = clampInt(forward - turn, -100, 100);
}

void updateDriveFromPhysicalStick(float physicalH, float physicalV) {
  float driveH = physicalV;
  float driveV = physicalH;
  updateDrive(driveH, driveV);
}

uint8_t readArmButtonMode() {
  static bool lastCenterPressed = false;
  bool upPressed = digitalRead(ARM_UP_BUTTON) == LOW;
  bool downPressed = digitalRead(ARM_DOWN_BUTTON) == LOW;
  bool centerPressed = digitalRead(ARM_CENTER_BUTTON) == LOW;

  if (centerPressed && !lastCenterPressed) {
    lastCenterPressed = centerPressed;
    return MODE_ARM_CENTER;
  }
  lastCenterPressed = centerPressed;

  if (upPressed && !downPressed) return MODE_ARM_UP;
  if (downPressed && !upPressed) return MODE_ARM_DOWN;
  return MODE_DRIVE;
}

bool parsePcCommand(const String &line) {
  int left = 0;
  int right = 0;
  int arm = ARM_START_ANGLE;

  if (sscanf(line.c_str(), "D %d %d %d", &left, &right, &arm) == 3) {
    packet.leftSpeed = clampInt(left, -100, 100);
    packet.rightSpeed = clampInt(right, -100, 100);
    packet.armAngle = clampInt(arm, 0, 180);
    packet.mode = MODE_DRIVE;
  } else if (sscanf(line.c_str(), "A %d", &arm) == 1) {
    packet.leftSpeed = 0;
    packet.rightSpeed = 0;
    packet.armAngle = clampInt(arm, 0, 180);
    packet.mode = MODE_ARM_SET;
  } else if (line == "S") {
    packet.leftSpeed = 0;
    packet.rightSpeed = 0;
    packet.mode = MODE_DRIVE;
  } else {
    return false;
  }

  pcControlActive = true;
  pcControlUntilMs = millis() + PC_CONTROL_TIMEOUT_MS;
  return true;
}

void readPcCommands() {
  while (Serial.available()) {
    char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;

    if (ch == '\n') {
      serialLine.trim();
      if (serialLine.length() > 0) {
        Serial.println(parsePcCommand(serialLine) ? "OK" : "ERR");
      }
      serialLine = "";
    } else if (serialLine.length() < 64) {
      serialLine += ch;
    }
  }

  if (pcControlActive && static_cast<int32_t>(millis() - pcControlUntilMs) > 0) {
    pcControlActive = false;
    packet.leftSpeed = 0;
    packet.rightSpeed = 0;
    packet.mode = MODE_DRIVE;
  }
}

void sendPacket() {
  packet.seq++;
  esp_err_t result = esp_now_send(broadcastAddress, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
  sendOk = result == ESP_OK;
}

const char *modeName(uint8_t mode) {
  if (mode == MODE_ARM_UP) return "ARM_UP";
  if (mode == MODE_ARM_DOWN) return "ARM_DOWN";
  if (mode == MODE_ARM_CENTER) return "CENTER";
  if (mode == MODE_ARM_SET) return "ARM_SET";
  return "DRIVE";
}

void showStatus(float joyH, float joyV) {
  static uint32_t lastPrintMs = 0;
  if (millis() - lastPrintMs > 250) {
    lastPrintMs = millis();
    Serial.printf("H=%.2f V=%.2f Src=%s Mode=%s L/R=%d/%d Arm=%d Send=%s\n",
                  joyH,
                  joyV,
                  pcControlActive ? "PC" : "STICK",
                  modeName(packet.mode),
                  packet.leftSpeed,
                  packet.rightSpeed,
                  packet.armAngle,
                  sendOk ? "OK" : "--");
  }
}

void updateLed() {
  if (!sendOk) {
    setLed(LED_YELLOW);
  } else if (packet.mode == MODE_ARM_UP || packet.mode == MODE_ARM_DOWN || packet.mode == MODE_ARM_CENTER || packet.mode == MODE_ARM_SET) {
    setLed(LED_RED);
  } else if (packet.leftSpeed != 0 || packet.rightSpeed != 0) {
    setLed(LED_BLUE);
  } else {
    setLed(LED_GREEN);
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  setLed(LED_PURPLE);

  pinMode(STICK_H, ANALOG);
  pinMode(STICK_V, ANALOG);
  pinMode(ARM_UP_BUTTON, INPUT_PULLUP);
  pinMode(ARM_DOWN_BUTTON, INPUT_PULLUP);
  pinMode(ARM_CENTER_BUTTON, INPUT_PULLUP);

  Serial.println("MiniKawa Atom Lite joystick controller");
  Serial.printf("PAIR_ID: %u\n", PAIR_ID);
  Serial.println("Keep stick centered while booting for calibration.");
  Serial.println("PC commands: D left right arm | A arm | S");

  calibrateStick();
  setupEspNow();
}

void loop() {
  M5.update();
  readPcCommands();

  float joyH = 0.0f;
  float joyV = 0.0f;
  readJoystick(joyH, joyV);

  bool centerRequested = false;
  if (M5.BtnA.wasPressed()) {
    centerRequested = true;
  }

  if (!pcControlActive) {
    updateDriveFromPhysicalStick(joyH, joyV);
    uint8_t armMode = readArmButtonMode();
    packet.mode = centerRequested ? MODE_ARM_CENTER : armMode;
  }

  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    sendPacket();
    showStatus(joyH, joyV);
    updateLed();
  }

  delay(4);
}
