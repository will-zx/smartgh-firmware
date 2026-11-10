#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

// Standalone timed pump-rate check; no flow sensor is used.
constexpr uint8_t PINS[] = {27, 13, 14, 32};
constexpr char *NAMES[] = {(char *)"FILL", (char *)"A", (char *)"B", (char *)"DIST"};
constexpr uint32_t MAX_TEST_SECONDS = 120;
constexpr uint8_t ON = HIGH;   // Match MOSFET_INVERTED=0; verify driver polarity.
constexpr uint8_t OFF = LOW;

int running = -1;
uint32_t stopAt = 0;
char line[40];
size_t length = 0;

void allOff() {
  for (uint8_t pin : PINS) digitalWrite(pin, OFF);
  running = -1;
}

void command(char *text) {
  char name[12] = {};
  unsigned long seconds = 0;
  if (sscanf(text, "%11s %lu", name, &seconds) != 2) {
    if (!strcmp(text, "STOP")) { allOff(); Serial.println("STOP: semua pompa OFF"); }
    else Serial.println("Gunakan FILL/A/B/DIST <detik>, maks 120; atau STOP");
    return;
  }
  for (char *p = name; *p; ++p) *p = (char)toupper((unsigned char)*p);
  int selected = !strcmp(name, "FILL") ? 0 : !strcmp(name, "A") ? 1 :
                 !strcmp(name, "B") ? 2 : !strcmp(name, "DIST") ? 3 : -1;
  if (selected < 0 || seconds == 0 || seconds > MAX_TEST_SECONDS) {
    Serial.println("Perintah ditolak: nama pompa tidak valid atau durasi di luar 1..120 s.");
    return;
  }
  allOff();
  running = selected;
  stopAt = millis() + seconds * 1000UL;
  digitalWrite(PINS[selected], ON);
  Serial.printf("%s ON %lu s. Ukur volume aktual, lalu ulangi 3x.\n",
                NAMES[selected], seconds);
}

void setup() {
  Serial.begin(115200);
  for (uint8_t pin : PINS) { pinMode(pin, OUTPUT); digitalWrite(pin, OFF); }
  Serial.println("Pompa calibration test ready; all outputs OFF. 115200 baud, Newline.");
  Serial.println("Perintah: FILL 30 | A 30 | B 30 | DIST 30 | STOP (durasi maks 120 s)");
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { line[length] = 0; command(line); length = 0; }
    else if (length < sizeof(line) - 1) line[length++] = c;
    else { length = 0; Serial.println("Baris terlalu panjang; dibuang."); }
  }
  if (running >= 0 && (int32_t)(millis() - stopAt) >= 0) {
    Serial.printf("%s OFF otomatis (timer habis).\n", NAMES[running]);
    allOff();
  }
  delay(2);
}
