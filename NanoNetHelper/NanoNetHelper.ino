// NanoNetHelper: one program for all four NanoNet helper Nanos.
//
// The boss (NanoChat, with the LCD) sends a text message over I2C and reads
// back a text reply. Each helper has one job, picked by ROLE below:
//
//   ROLE 1  Brain    mini GPT: writes replies the boss can't pick itself   (I2C 0x10)
//   ROLE 2  Words    word inventor, trivia, morse, encyclopedia part C      (I2C 0x11)
//   ROLE 3  Calc     NanoCalc math, unit converter, encyclopedia part B     (I2C 0x12)
//   ROLE 4  Library  jokes, facts, riddles, survival tips, encyclopedia A   (I2C 0x13)
//
// Wiring: every Nano's A4 together, A5 together, GND together (and 5V together
// to power them all from one USB). The boss is the I2C master.
//
// Messages from the boss: "gen text" (Brain), "do text" (handle it if it's yours,
// else reply nothing), "wiki question" (encyclopedia), "joke"/"fact"/"riddle".
// You can also test a helper on its own: open Serial at 9600 and type a message
// (Words and Calc treat a plain message like "do ...").

#ifndef ROLE
#define ROLE 1  // change this before uploading to each helper
#endif

#include <Wire.h>
#include <avr/pgmspace.h>

const uint8_t ADDR = 0x10 + ROLE - 1;
const uint8_t MSG_LEN = 90;   // boss message (80) plus a short prefix like "wiki "
const uint8_t BOX_LEN = 240;
const uint8_t CHUNK = 16;  // bytes per I2C read; must match the boss

// Replies are printed into this box, then the boss reads it out in chunks.
char outbox[BOX_LEN + 1];
volatile uint8_t outLen = 0, outPos = 0;
volatile bool outReady = true;

class Box : public Print {
 public:
  size_t write(uint8_t c) override {
    if (c == '\r') return 1;
    if (c == '\n') c = ' ';
    if (outLen < BOX_LEN) outbox[outLen++] = c;
    return 1;
  }
} box;

#if ROLE == 1
#include "role_brain.h"
#elif ROLE == 2
#include "role_words.h"
#elif ROLE == 3
#include "role_calc.h"
#elif ROLE == 4
#include "role_library.h"
#else
#error "ROLE must be 1, 2, 3 or 4"
#endif

// ---------- I2C: the boss writes 0x02 + message + '\n', then reads 16-byte chunks ----------

char inbox[MSG_LEN + 1];
volatile uint8_t inLen = 0;
volatile bool inReady = false;

void onReceive(int) {
  while (Wire.available()) {
    char c = Wire.read();
    if (c == 0x02) { inLen = 0; outReady = false; }  // start of a new message
    else if (c == '\n') { inbox[inLen] = '\0'; inReady = true; }
    else if (inLen < MSG_LEN) inbox[inLen++] = c;
  }
}

// Each read gets 16 bytes: reply text, then 0s once it's done. All 0x01 = still busy.
void onRequest() {
  uint8_t buf[CHUNK];
  for (uint8_t i = 0; i < CHUNK; i++)
    buf[i] = !outReady ? 0x01 : outPos < outLen ? outbox[outPos++] : 0;
  Wire.write(buf, CHUNK);
}

void answer(char *msg) {
  outLen = outPos = 0;
  handle(msg, box);
  while (outLen && outbox[outLen - 1] == ' ') outLen--;
  outbox[outLen] = '\0';
}

// ---------- Serial, for testing a helper by itself ----------

char line[MSG_LEN + 1];
uint8_t lineLen = 0;

void setup() {
  Serial.begin(9600);
  roleSetup();
  Wire.begin(ADDR);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Serial.print(F("NanoNet helper "));
  Serial.print(ROLE_NAME);
  Serial.print(F(" at I2C 0x"));
  Serial.println(ADDR, HEX);
}

void loop() {
  if (inReady) {
    inReady = false;
    answer(inbox);
    outReady = true;
    digitalWrite(LED_BUILTIN, HIGH);  // brief blink: "I answered"
    delay(20);
    digitalWrite(LED_BUILTIN, LOW);
  }
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (!lineLen) continue;
      line[lineLen] = '\0';
      lineLen = 0;
      noInterrupts();  // don't let the boss read a half-written box
      outReady = false;
      interrupts();
      answer(line);
      Serial.println(outbox);
      outReady = true;
    } else if (lineLen < MSG_LEN) line[lineLen++] = c;
  }
}
