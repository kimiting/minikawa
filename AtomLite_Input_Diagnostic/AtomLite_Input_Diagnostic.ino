#include <M5Unified.h>

constexpr int ANALOG_PINS[] = {
  32, 33, 34, 35, 36, 39,
  0, 2, 4, 12, 13, 14, 15, 25, 26, 27
};

constexpr int DIGITAL_PINS[] = {
  19, 21, 22, 23
};

constexpr uint32_t PRINT_INTERVAL_MS = 300;
uint32_t lastPrintMs = 0;

template <typename T, size_t N>
constexpr size_t arraySize(const T (&)[N]) {
  return N;
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(1000);

  for (int pin : ANALOG_PINS) {
    pinMode(pin, ANALOG);
  }
  for (int pin : DIGITAL_PINS) pinMode(pin, INPUT_PULLUP);

  Serial.println("Atom Lite input diagnostic");
  Serial.println("Move the stick and press buttons. Watch changing values.");
}

void printAnalogValues() {
  Serial.print("A:");
  for (int pin : ANALOG_PINS) {
    Serial.printf(" G%d=%4d", pin, analogRead(pin));
  }
  Serial.println();
}

void printPressedButtons() {
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
  if (millis() - lastPrintMs >= PRINT_INTERVAL_MS) {
    lastPrintMs = millis();
    printAnalogValues();
    printPressedButtons();
  }
  delay(10);
}
