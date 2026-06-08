#include <M5Unified.h>

constexpr int ANALOG_PINS[] = {
  32, 33, 34, 35, 36, 39,
  0, 2, 4, 12, 13, 14, 15, 25, 26, 27
};

constexpr int DIGITAL_PINS[] = {
  0, 2, 4, 5, 12, 13, 14, 15, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39
};

constexpr uint32_t PRINT_INTERVAL_MS = 200;
uint32_t lastPrintMs = 0;

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(1000);

  for (int pin : ANALOG_PINS) {
    pinMode(pin, ANALOG);
  }
  for (int pin : DIGITAL_PINS) {
    pinMode(pin, INPUT_PULLUP);
  }

  Serial.println("ATOM_CONTROLLER_SETUP_READY");
}

void printSnapshot() {
  Serial.print("A:");
  for (int pin : ANALOG_PINS) {
    Serial.printf(" G%d=%d", pin, analogRead(pin));
  }
  Serial.println();

  Serial.print("LOW:");
  bool any = false;
  for (int pin : DIGITAL_PINS) {
    if (digitalRead(pin) == LOW) {
      Serial.printf(" G%d", pin);
      any = true;
    }
  }
  if (!any) {
    Serial.print(" none");
  }
  Serial.println();
}

void loop() {
  M5.update();

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command == "SNAP") {
      printSnapshot();
    }
  }

  if (millis() - lastPrintMs >= PRINT_INTERVAL_MS) {
    lastPrintMs = millis();
    printSnapshot();
  }

  delay(10);
}
