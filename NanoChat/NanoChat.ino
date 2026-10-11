// NanoChat: a tiny chatbot that runs on an Arduino Nano.
//
// A small neural network (train/train.py) sorts each message into one of
// 26 intents (greeting, joke, weather, ...), then NanoChat picks a reply.
// Extras: math ("what's 12 * 7"), remembers your name (EEPROM),
// coin/dice, invents words with the NanoBrain model, and, when bridge.py
// is running on the Mac, real time / weather / Wikipedia answers.
//
// Button on D2 (other leg to GND): a menu on the LCD, so no computer is needed.
// 16x2 I2C screen on A4 (SDA) / A5 (SCL) shows the replies (optional).
// Talk to it at 9600 baud, or run `python3 bridge.py` for internet access.

#include <EEPROM.h>
#include <avr/pgmspace.h>
#include <math.h>
#include "chat_model.h"

// Solo version: a single Nano with no helpers. Uncomment to drop the NanoNet code
// and use the space for trivia and a bigger joke collection.
// #define SOLO

// Keep big helpers out of loop(): inlined, all their buffers would sit on the stack at once.
#define NOINLINE __attribute__((noinline))

const char *findAny(const char *s, PGM_P list, bool anywhere, uint8_t *index = nullptr);

const uint8_t LINE_LEN = 80;
const uint8_t MAX_FEATS = 48;
const uint8_t NAME_LEN = 15;
const uint16_t EE_NAME = 1000;  // EEPROM: magic byte, then name (NanoLang uses 0..451)
const uint8_t EE_MAGIC = 0xC7;
const float MIN_CONFIDENCE = 0.7;  // below this, the Brain's mini GPT writes the reply (keeps 171/176 right answers)
const uint8_t BUTTON_PIN = 2;   // button between D2 and GND

char line[LINE_LEN + 1];
uint8_t lineLen = 0;
bool lastWasCR = false;
bool bridgeOn = false;        // bridge.py says hello when it connects
char userName[NAME_LEN + 1];
char lastTopic[32];          // short-term context; intentionally RAM-small

// ---------- 16x2 I2C screen (Hack Pack LCD: SDA -> A4, SCL -> A5) ----------
// Each reply is mirrored to the screen and shown 2 lines at a time.
// A tiny write-only driver instead of Wire + LiquidCrystal_I2C: those
// libraries don't fit next to the chat model (about 3 KB flash, 200 B RAM).

const uint8_t SCREEN_LEN = 96;
char screen[SCREEN_LEN + 1];
uint8_t screenLen = 0, screenPos = 0, screenNext = 0;
unsigned long screenTimer = 0;
uint8_t lcdAddr = 0;  // 0 = no screen found

// Prints to Serial and also collects the text for the screen.
class Tee : public Print {
 public:
  size_t write(uint8_t c) override {
    Serial.write(c);
    if (c == '\r') return 1;
    if (c == '\n') c = ' ';
    if (screenLen < SCREEN_LEN) screen[screenLen++] = c;
    return 1;
  }
} out;

// Bit-banged I2C on A4 (PC4) / A5 (PC5). Lines are open-drain: setting the
// DDR bit pulls the line low, clearing it lets the backpack's pull-up win.
// Releasing a line also turns on the pin's built-in pull-up, so with no screen or helpers
// attached the lines read "idle" instead of floating (which looks like a busy device).
void sdaLow(bool low) {
  if (low) { PORTC &= ~_BV(4); DDRC |= _BV(4); }
  else { DDRC &= ~_BV(4); PORTC |= _BV(4); }
  delayMicroseconds(4);
}
void sclLow(bool low) {
  if (low) { PORTC &= ~_BV(5); DDRC |= _BV(5); }
  else {  // let go of SCL; a busy helper Nano may hold it low for a moment ("clock stretching")
    DDRC &= ~_BV(5);
    PORTC |= _BV(5);
    for (uint8_t t = 0; t < 250 && !(PINC & _BV(5)); t++) delayMicroseconds(10);
  }
  delayMicroseconds(4);
}

bool i2cByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++, b <<= 1) { sdaLow(!(b & 0x80)); sclLow(false); sclLow(true); }
  sdaLow(false);
  sclLow(false);
  bool ack = !(PINC & _BV(4));
  sclLow(true);
  return ack;
}

bool i2cStart(uint8_t addrRW) { sdaLow(true); sclLow(true); return i2cByte(addrRW); }
void i2cStop() { sdaLow(true); sclLow(false); sdaLow(false); }

uint8_t i2cRead(bool last) {
  uint8_t b = 0;
  sdaLow(false);
  for (uint8_t i = 0; i < 8; i++) { sclLow(false); b = b << 1 | !!(PINC & _BV(4)); sclLow(true); }
  sdaLow(!last);  // ACK every byte but the last
  sclLow(false);
  sclLow(true);
  sdaLow(false);
  return b;
}

// Write one byte to the PCF8574 backpack: P0 RS, P2 EN, P3 backlight, P4-7 data.
bool lcdPort(uint8_t addr, uint8_t v) {
  bool ok = i2cStart(addr << 1) && i2cByte(v | 0x08);
  i2cStop();
  return ok;
}

// ---------- NanoNet: helper Nanos on the same I2C wires (see NanoNetHelper) ----------
// Every helper is optional: if one isn't plugged in, askHelper() returns false
// and NanoChat does the job itself (or says which helper is needed).
enum { NET_BRAIN = 0x10, NET_WORDS, NET_CALC, NET_LIBRARY };
#ifdef SOLO
char netReply[1];
inline bool askHelper(uint8_t, PGM_P, const char *) { return false; }  // no helpers: all that code drops out
#else
char netReply[241];

// Sends 0x02 + prefix + msg + '\n', then reads the reply 16 bytes at a time into netReply.
NOINLINE bool askHelper(uint8_t addr, PGM_P prefix, const char *msg) {
  netReply[0] = 0x02;
  strcpy_P(netReply + 1, prefix);
  strncat(netReply, msg, LINE_LEN);
  strcat(netReply, "\n");
  for (char *c = netReply; *c; c += 24) {  // a helper takes up to 32 bytes per write
    bool ok = i2cStart(addr << 1);
    for (uint8_t i = 0; ok && i < 24 && c[i]; i++) ok = i2cByte(c[i]);
    i2cStop();
    if (!ok) return false;
    if (strlen(c) < 24) break;
  }
  uint8_t n = 0;
  for (unsigned long start = millis(); millis() - start < 12000;) {  // the mini GPT needs up to ~9 s
    uint8_t chunk[16];
    if (!i2cStart(addr << 1 | 1)) { i2cStop(); return false; }
    for (uint8_t i = 0; i < 16; i++) chunk[i] = i2cRead(i == 15);
    i2cStop();
    if (chunk[0] == 0x01) { delay(5); continue; }  // still thinking
    for (uint8_t i = 0; i < 16; i++) {
      if (!chunk[i] || n >= 240) {
        netReply[n] = '\0';
        if (bridgeOn && n) { Serial.print(F("@H")); Serial.println(addr - NET_BRAIN); }  // app: who answered
        return true;
      }
      netReply[n++] = chunk[i];
    }
  }
  return false;
}
#endif

// Joke/fact/riddle from the Library helper, or NanoChat's own few.
void sayLib(const char *opts, PGM_P kind) {
  if (askHelper(NET_LIBRARY, kind, "") && netReply[0]) out.println(netReply);
  else say(opts);
}

// Set RS and data first, then pulse EN: the LCD needs them steady before EN rises.
void lcdNibble(uint8_t v) { lcdPort(lcdAddr, v); lcdPort(lcdAddr, v | 0x04); lcdPort(lcdAddr, v); }
void lcdSend(uint8_t b, uint8_t rs) { lcdNibble((b & 0xF0) | rs); lcdNibble((b << 4) | rs); }
void lcdCmd(uint8_t c) { lcdSend(c, 0); if (c < 4) delay(2); }

// 5x8 pixel faces shown at the start of each reply.
enum { FACE_HAPPY, FACE_SAD, FACE_WOW };
const uint8_t FACES[] PROGMEM = {
  0x00, 0x0A, 0x0A, 0x00, 0x11, 0x0E, 0x00, 0x00,  // happy
  0x00, 0x0A, 0x0A, 0x00, 0x0E, 0x11, 0x00, 0x00,  // sad
  0x00, 0x0A, 0x0A, 0x00, 0x0E, 0x0A, 0x0E, 0x00,  // wow / thinking
};
uint8_t face = FACE_HAPPY;
unsigned long replyAt = 0;  // when the last reply was shown

void screenBegin() {
  sdaLow(false);  // idle: both lines released, pull-ups on
  sclLow(false);
  const uint8_t addrs[] = { 0x27, 0x3F };  // most backpacks are 0x27
  for (uint8_t a : addrs) if (lcdPort(a, 0)) { lcdAddr = a; break; }
  if (!lcdAddr) return;
  delay(50);
  const uint8_t wake[] = { 0x30, 0x30, 0x30, 0x20 };  // force 4-bit mode
  for (uint8_t v : wake) { lcdNibble(v); delay(5); }
  const uint8_t cfg[] = { 0x28, 0x0C, 0x06, 0x01 };    // 2 lines, display on, cursor off, clear
  for (uint8_t c : cfg) lcdCmd(c);
  lcdCmd(0x40);  // load the face characters into the LCD's custom slots 0-2
  for (uint8_t i = 0; i < sizeof(FACES); i++) lcdSend(pgm_read_byte(FACES + i), 1);
}

void lcdRow(uint8_t row, const __FlashStringHelper *text) {
  lcdCmd(row ? 0xC0 : 0x80);
  const char *p = (const char *)text;
  for (uint8_t i = 0; i < 16; i++) {
    char c = pgm_read_byte(p);
    if (c) p++;
    lcdSend(c ? c : ' ', 1);
  }
}

// Draw one 16-char row starting at `pos`, breaking at a space; returns where the next row starts.
uint8_t screenRow(uint8_t row, uint8_t pos) {
  while (pos < screenLen && screen[pos] == ' ') pos++;
  uint8_t end = pos + 16 < screenLen ? pos + 16 : screenLen;
  if (end < screenLen && screen[end] != ' ') {
    uint8_t sp = end;
    while (sp > pos && screen[sp] != ' ') sp--;
    if (sp > pos) end = sp;
  }
  lcdCmd(row ? 0xC0 : 0x80);
  for (uint8_t i = 0; i < 16; i++) lcdSend(pos + i < end ? screen[pos + i] : ' ', 1);
  return end;
}

void screenPage() {
  if (!lcdAddr) return;
  screenNext = screenRow(1, screenRow(0, screenPos));
  screenTimer = millis();
}

// Slot 0 is the face, filled in by screenFinish once we know the mood.
void screenStart() { screen[1] = ' '; screenLen = 2; face = FACE_HAPPY; }

// Call after a reply: tidy the text and show the first page.
void screenFinish() {
  screen[0] = face;
  if (bridgeOn) { Serial.print(F("@F")); Serial.println(face); }  // the app shows the same face
  replyAt = millis();
  while (screenLen && screen[screenLen - 1] == ' ') screenLen--;
  if (screenLen == SCREEN_LEN) memcpy(screen + SCREEN_LEN - 3, "...", 3);
  screenPos = 0;
  screenPage();
}

// Long replies flip to the next page every 2.5 s, then start over.
unsigned long timerEnd = 0;  // countdown timer; 0 = off

void screenTick() {
  if (timerEnd && lcdAddr && millis() - replyAt > 8000) {
    if (millis() - screenTimer < 1000) return;
    screenTimer = millis();
    unsigned long left = (timerEnd - millis()) / 1000 + 1;
    lcdRow(0, F("Timer"));
    char t[17] = { char('0' + left / 600 % 10), char('0' + left / 60 % 10), ':',
                   char('0' + left % 60 / 10), char('0' + left % 10) };
    memcpy_P(t + 5, PSTR(" left         "), 12);  // pad over any old reply text
    lcdCmd(0xC0);
    for (uint8_t i = 0; i < 16; i++) lcdSend(t[i], 1);
    return;
  }
  if (!lcdAddr || screenLen <= 2 || millis() - screenTimer < 2500) return;
  bool fits = screenPos == 0 && screenNext >= screenLen;
  if (fits) return;
  screenPos = screenNext >= screenLen ? 0 : screenNext;
  screenPage();
}

// ---------- replies: options separated by '|', '@' = your name ----------

const char R_GREET[] PROGMEM = "Hi @!|Hello @! What's up?|Hey there!|Yo @! Good to see you.";
const char R_BYE[] PROGMEM = "Bye @!|See you later!|Catch you later, @.|Goodbye! I'll be right here in my 2 KB of RAM.";
const char R_HOW[] PROGMEM = "I'm great! My 16 MHz brain is humming.|Doing well, thanks for asking!|All systems go. How about you?|Pretty good for a chip the size of a stamp.";
const char R_WHO[] PROGMEM = "I'm NanoChat, a tiny AI on an Arduino Nano.|I'm NanoChat! My brain is about 7,000 numbers.|A very small robot brain. Nice to meet you!";
const char R_CREATOR[] PROGMEM = "You flashed me onto this Nano (with help from Claude).|You built me! I live on an Arduino Nano.";
#ifdef SOLO
const char R_HELP[] PROGMEM = "Try: joke, trivia, riddle, roll 2 dice, again, 12*7, timer 5 min, stopwatch, guess my number, rock, flip a coin, my name is ...";
const char R_JOKE[] PROGMEM =
  "Why did the robot go on vacation? It needed to recharge.|"
  "Why do programmers prefer dark mode? Because light attracts bugs.|"
  "What do you call a sleeping bull? A bulldozer.|"
  "Why was the math book sad? It had too many problems.|"
  "I told my Arduino a joke. It didn't laugh, it just blinked.|"
  "Why did the scarecrow win an award? He was outstanding in his field.|"
  "Why can't a bicycle stand up by itself? It's two tired.|"
  "What do you call a bear with no teeth? A gummy bear.|"
  "Why don't skeletons fight? They don't have the guts.|"
  "What do you call a pig that does karate? A pork chop.|"
  "Why are ghosts bad liars? You can see right through them.|"
  "What is a robot's favorite snack? Computer chips.|"
  "Why was the computer cold? It left its Windows open.|"
  "What do you call an alligator in a vest? An investigator.|"
  "Why did the chicken join a band? It had the drumsticks.|"
  "What do you call a magic dog? A labracadabrador.|"
  "Why did the cookie go to the doctor? It felt crummy.|"
  "What do you call a fish with no eyes? A fsh.|"
  "Why did the robot cross the road? It was programmed to.";
#else
const char R_HELP[] PROGMEM = "Try: joke, riddle, roll 2 dice, again, 12*7, timer 5 min, stopwatch, guess my number, rock. With helpers: invent a word, trivia, calc x=5, 5 miles to km, morse sos, who is einstein, survival tip.";
const char R_JOKE[] PROGMEM =  // backups: the Library helper has 50 more
  "Why did the robot go on vacation? It needed to recharge.|"
  "I told my Arduino a joke. It didn't laugh, it just blinked.";
#endif
#ifdef SOLO
const char R_FACT[] PROGMEM =
  "Honey never spoils. 3000-year-old honey is still edible.|"
  "I run at 16 MHz: 16 million steps per second.|"
  "A day on Venus is longer than its year.|"
  "Bananas are berries, but strawberries aren't.|"
  "The first computer bug was a real moth, found in 1947.";
#else  // NanoNet: the Library helper has hundreds; these are just backups
const char R_FACT[] PROGMEM =
  "Honey never spoils. 3000-year-old honey is still edible.|"
  "I run at 16 MHz: 16 million steps per second.";
#endif
const char R_THANKS[] PROGMEM = "You're welcome!|Anytime, @.|Happy to help!|No problem!";
const char R_COMPLIMENT[] PROGMEM = "Thanks! You're pretty cool too.|Aw, my LED is blushing.|Thank you, @!|I try my best with 2 KB of RAM.";
const char R_INSULT[] PROGMEM = "Hey, I'm only 2 KB of RAM. Be nice!|Ouch. I'll pretend I didn't hear that.|Sorry! I'm still learning.";
const char R_WEATHER[] PROGMEM = "I can't see outside. Run bridge.py and I'll check the internet for you!";
const char R_SAD[] PROGMEM = "I'm sorry, @. Want to hear a joke?|That sounds hard. Take a deep breath, I'm here.|Sending virtual hugs. Bad days end.|Sorry you feel that way. Talking to a friend can really help.";
const char R_HAPPY[] PROGMEM = "Awesome! Glad to hear it.|That's great, @!|Nice! Keep it up.";
const char R_BORED[] PROGMEM = "Ask me for a riddle!|Say trivia!|Ask me a yes/no question about your future.|Try: guess my number";
const char R_EIGHTBALL[] PROGMEM = "It is certain.|Without a doubt.|Yes, definitely.|Most likely.|Ask again later.|Can't predict now.|Don't count on it.|My sources say no.|Very doubtful.|Signs point to yes.";
const char R_AGE[] PROGMEM = "I was born when you uploaded me!|Brand new, but my chip design is from 2008.|Young enough to still be learning.";
const char R_FAVORITE[] PROGMEM = "I love blinking LEDs.|My favorite number is 16, like my megahertz.|Electricity is my favorite food.|Anything with a USB cable.";
const char R_LOVE[] PROGMEM = "Aw, I like you too, @!|We're definitely friends.|You're my favorite human.";
#ifdef SOLO
const char R_RIDDLE[] PROGMEM =
  "What has keys but can't open locks? ... A keyboard!|"
  "What gets wetter the more it dries? ... A towel!|"
  "What has a head and a tail but no body? ... A coin!|"
  "What can you catch but not throw? ... A cold!";
#else  // NanoNet: the Library helper has hundreds; these are just backups
const char R_RIDDLE[] PROGMEM =
  "What has keys but can't open locks? ... A keyboard!|"
  "What gets wetter the more it dries? ... A towel!";
#endif
const char R_UNKNOWN[] PROGMEM = "Hmm, I'm a tiny brain. Try help.|I didn't get that. Type help for ideas.|Not sure what you mean, @. Try other words?";
const char R_NO_NET[] PROGMEM = "I'd need the internet for that. Run bridge.py on your Mac and ask me again!";

// Print one random '|'-separated option, swapping '@' for the user's name.
void say(const char *opts) {
  uint8_t count = 1;
  for (const char *c = opts; pgm_read_byte(c); c++) if (pgm_read_byte(c) == '|') count++;
  uint8_t pick = random(count);
  const char *c = opts;
  while (pick) if (pgm_read_byte(c++) == '|') pick--;
  for (char ch; (ch = pgm_read_byte(c)) && ch != '|'; c++) {
    if (ch == '@') out.print(userName[0] ? userName : "friend");
    else out.print(ch);
  }
  out.println();
}

// ---------- intent classifier ----------

uint32_t fnvStep(uint32_t h, char c) { return (h ^ (uint8_t)c) * 16777619UL; }

// Words and word pairs -> hash buckets (must match train.py).
// A pair's hash is FNV over "prev cur", so it continues from prev's full hash.
uint8_t extractFeatures(const char *s, uint16_t *feats) {
  uint8_t n = 0;
  bool havePrev = false;
  uint32_t hPrev = 0;
  while (*s) {
    while (*s && !isalnum(*s)) s++;
    if (!*s) break;
    uint32_t h = 2166136261UL, hb = fnvStep(hPrev, ' '), hp = fnvStep(2166136261UL, '#');
    uint8_t len = 0;
    while (isalnum(*s)) {
      char c = tolower(*s++);
      h = fnvStep(h, c);
      hb = fnvStep(hb, c);
      if (len++ < 4) hp = fnvStep(hp, c);  // "#" + first 4 letters: a cheap word stem
    }
    uint16_t f[3] = { (uint16_t)(h % CHAT_F), havePrev ? (uint16_t)(hb % CHAT_F) : (uint16_t)0xFFFF,
                      CHAT_PREFIX && len > 4 ? (uint16_t)(hp % CHAT_F) : (uint16_t)0xFFFF };
    for (uint16_t x : f) {
      if (x == 0xFFFF || n >= MAX_FEATS) continue;
      bool dup = false;
      for (uint8_t i = 0; i < n; i++) if (feats[i] == x) dup = true;
      if (!dup) feats[n++] = x;
    }
    hPrev = h;
    havePrev = true;
  }
  return n;
}

// One first-layer weight: 8-bit, or 4-bit packed two per byte (low nibble first).
int8_t w1(uint32_t idx) {
#if CHAT_BITS == 4
  uint8_t b = pgm_read_byte(chat_W1 + (idx >> 1));
  int8_t v = idx & 1 ? b >> 4 : b & 15;
  return v >= 8 ? v - 16 : v;
#else
  return (int8_t)pgm_read_byte(chat_W1 + idx);
#endif
}

NOINLINE uint8_t classify(const char *s, float *confidence) {
  uint16_t feats[MAX_FEATS];
  uint8_t nf = extractFeatures(s, feats);
  float h[CHAT_H];
  for (uint8_t i = 0; i < CHAT_H; i++) {
    int16_t sum = 0;  // whole-number sum of int8 weights, scaled once below
    for (uint8_t j = 0; j < nf; j++)
      sum += w1((uint32_t)feats[j] * CHAT_H + i);
    float v = sum * pgm_read_float(chat_s1 + i) + pgm_read_float(chat_b1 + i);  // one scale per hidden unit
    h[i] = v > 0 ? v : 0;
  }
  float best = -1e30, z = 0, lg[CHAT_K];
  uint8_t bestK = 0;
  for (uint8_t k = 0; k < CHAT_K; k++) {
    float sum = 0;
    for (uint8_t i = 0; i < CHAT_H; i++)
      sum += (int8_t)pgm_read_byte(chat_W2 + k * CHAT_H + i) * h[i];
    lg[k] = sum * chat_s2 + pgm_read_float(chat_b2 + k);
    if (lg[k] > best) { best = lg[k]; bestK = k; }
  }
  for (uint8_t k = 0; k < CHAT_K; k++) z += exp(lg[k] - best);
  *confidence = 1 / z;
  return bestK;
}

// ---------- math: "what's 12 * 7", "5 plus 3 times 2" ----------

const char *mp;
float mathExpr();
float mathAtom() {
  while (*mp == ' ') mp++;
  if (*mp == '(') { mp++; float v = mathExpr(); while (*mp == ' ') mp++; if (*mp == ')') mp++; return v; }
  if (*mp == '-') { mp++; return -mathAtom(); }
  // Plain decimals only (tryMath strips letters); strtod costs ~700 bytes.
  float v = 0;
  while (isdigit(*mp)) v = v * 10 + (*mp++ - '0');
  if (*mp == '.') {
    mp++;
    for (float d = 0.1; isdigit(*mp); d /= 10) v += (*mp++ - '0') * d;
  }
  return v;
}
float mathPow() {
  float b = mathAtom();
  while (*mp == ' ') mp++;
  if (*mp == '^') { mp++; return pow(b, mathPow()); }
  return b;
}
float mathTerm() {
  float v = mathPow();
  for (;;) {
    while (*mp == ' ') mp++;
    if (*mp == '*') { mp++; v *= mathPow(); }
    else if (*mp == '/') { mp++; v /= mathPow(); }
    else return v;
  }
}
float mathExpr() {
  float v = mathTerm();
  for (;;) {
    while (*mp == ' ') mp++;
    if (*mp == '+') { mp++; v += mathTerm(); }
    else if (*mp == '-') { mp++; v -= mathTerm(); }
    else return v;
  }
}

// Turn the message into a bare expression; false if it isn't math.
NOINLINE bool tryMath(const char *s) {
  char ex[LINE_LEN + 1];
  uint8_t n = 0, nums = 0, ops = 0;
  while (*s && n < LINE_LEN - 6) {
    if (isdigit(*s) || (*s == '.' && isdigit(s[1]))) {
      nums++;
      while (isdigit(*s) || *s == '.') ex[n++] = *s++;
      continue;
    }
    if (*s == '%') { memcpy(ex + n, "/100*", 5); n += 5; ops++; s++; continue; }
    if (strchr("+-*/^()", *s)) {
      if (strchr("+-*/^", *s)) ops++;
      ex[n++] = *s++;
      continue;
    }
    if (isalpha(*s)) {
      char w[12];
      uint8_t l = 0;
      while (isalpha(*s)) { if (l < 11) w[l++] = tolower(*s); s++; }
      w[l] = '\0';
      char op = 0;
      if (findAny(w, PSTR("percent"), false)) { memcpy(ex + n, "/100*", 5); n += 5; ops++; continue; }
      if (findAny(w, PSTR("squared"), false)) { memcpy(ex + n, "^2", 2); n += 2; ops++; nums++; continue; }
      if (findAny(w, PSTR("plus|add"), false)) op = '+';
      else if (findAny(w, PSTR("minus"), false)) op = '-';
      else if (findAny(w, PSTR("times|x|multiplied"), false)) op = '*';
      else if (findAny(w, PSTR("divided|over"), false)) op = '/';
      if (op) { ex[n++] = op; ops++; }
      continue;
    }
    s++;
  }
  ex[n] = '\0';
  if (nums < 2 || !ops) return false;
  mp = ex;
  float v = mathExpr();
  out.print(ex);
  out.print(F(" = "));
  if (v == (long)v && fabs(v) < 1e9) out.println((long)v);
  else out.println(v, 4);
  return true;
}

// ---------- offline games & tools ----------

uint8_t secret = 0, guesses = 0;  // guess-my-number; secret 0 = not playing

// First whole number at or after s (0 if none); advances s past it.
long nextNumber(const char *&s) {
  while (*s && !isdigit(*s)) s++;
  long v = 0;
  while (isdigit(*s)) v = v * 10 + (*s++ - '0');
  return v;
}

// "set a timer for 5 minutes" / "30 seconds"; also used by the timer intent.
NOINLINE void setTimer(const char *s) {
  const char *p = s;
  long n = nextNumber(p);
  if (!n) { out.println(F("For how long? Try: set a timer for 5 minutes")); return; }
  bool secs = findAny(s, PSTR("sec|secs|second|seconds"), true);
  timerEnd = millis() + n * (secs ? 1000UL : 60000UL);
  if (!timerEnd) timerEnd = 1;
  out.print(F("Timer set for "));
  out.print(n);
  out.print(secs ? F(" second") : F(" minute"));
  out.println(n == 1 ? F(".") : F("s."));
}

NOINLINE bool tryGames(const char *s) {
  const char *p = s;
  if (secret && isdigit(*s)) {  // a guess
    long g = nextNumber(p);
    guesses++;
    if (g == secret) {
      out.print(F("You got it in "));
      out.print(guesses);
      out.println(F(" guesses!"));
      secret = 0;
    } else out.println(g < secret ? F("Higher!") : F("Lower!"));
    return true;
  }
  if (secret && findAny(s, PSTR("i give up|give up|stop|quit"), true)) {
    face = FACE_SAD;
    out.print(F("It was "));
    out.print(secret);
    out.println(F(". Good game!"));
    secret = 0;
    return true;
  }
  if (findAny(s, PSTR("guess my number|guessing game|number game|play a game|lets play|play again"), true)) {
    secret = random(1, 101);
    guesses = 0;
    out.println(F("I'm thinking of a number from 1 to 100. Guess!"));
    return true;
  }
  if (findAny(s, PSTR("timer|countdown|remind me in"), true)) { setTimer(s); return true; }
  if (findAny(s, PSTR("pick a number|random number|choose a number"), true)) {
    long lo = nextNumber(p), hi = nextNumber(p);
    if (!hi) { hi = lo ? lo : 100; lo = 1; }
    if (lo > hi) { long t = lo; lo = hi; hi = t; }
    out.print(F("I pick "));
    out.print(random(lo, hi + 1));
    out.println('!');
    return true;
  }
  const char *rps = findAny(s, PSTR("rock|paper|scissors"), false);
  if (rps) {
    if (findAny(rps, PSTR("paper|scissors"), true)) {
      out.println(F("Let's play! Type rock, paper or scissors."));
      return true;
    }
    uint8_t you = s[0] == 'r' || s[0] == 'R' ? 0 : s[0] == 'p' || s[0] == 'P' ? 1 : 2;
    uint8_t me = random(3);
    const char *names[] = { "rock", "paper", "scissors" };
    out.print(F("I pick "));
    out.print(names[me]);
    if (me == you) out.println(F(". Tie!"));
    else if ((me + 1) % 3 == you) out.println(F(". You win!"));
    else { out.println(F(". I win!")); face = FACE_WOW; }
    return true;
  }
  return false;
}

unsigned long swStart = 0;  // stopwatch; 0 = stopped

void printDuration(unsigned long ms) {
  unsigned long sec = ms / 1000;
  out.print(sec / 60);
  out.print(sec % 60 < 10 ? F(":0") : F(":"));
  out.print(sec % 60);
}

NOINLINE bool tryTools(const char *s) {
  if (findAny(s, PSTR("stopwatch"), true)) {
    if (swStart && !findAny(s, PSTR("start"), true)) {
      out.print(F("Stopwatch: "));
      printDuration(millis() - swStart);
      out.println(findAny(s, PSTR("stop|reset"), true) ? F(" (stopped)") : F(" (still running)"));
      if (findAny(s, PSTR("stop|reset"), true)) swStart = 0;
    } else {
      swStart = millis() | 1;
      out.println(F("Stopwatch started. Say stopwatch to check it."));
    }
    return true;
  }
  if (findAny(s, PSTR("cancel timer|stop timer|stop the timer|cancel the timer"), true)) {
    out.println(timerEnd ? F("Timer cancelled.") : F("No timer is running."));
    timerEnd = 0;
    return true;
  }
  if (findAny(s, PSTR("time left|time is left|how long left|timer left"), true)) {
    if (!timerEnd) out.println(F("No timer is running."));
    else { printDuration(timerEnd - millis()); out.println(F(" left.")); }
    return true;
  }
  return false;
}

// Checks the timer and blinks the LED after "Time's up!" without blocking the chat.
void timerTick() {
  static unsigned long blinkAt;
  static uint8_t blinks = 0;
  if (blinks && millis() - blinkAt >= 250) { blinkAt = millis(); digitalWrite(LED_BUILTIN, --blinks & 1); }
  if (!timerEnd || (long)(millis() - timerEnd) < 0) return;
  timerEnd = 0;
  screenStart();
  face = FACE_WOW;
  Serial.println();
  out.println(F("Time's up!"));
  screenFinish();
  blinks = 6;
  blinkAt = millis();
  Serial.print(F("> "));
}

// ---------- name memory ----------

void loadName() {
  userName[0] = '\0';
  if (EEPROM.read(EE_NAME) != EE_MAGIC) return;
  for (uint8_t i = 0; i < NAME_LEN; i++) {
    char c = EEPROM.read(EE_NAME + 1 + i);
    userName[i] = isprint(c) ? c : '\0';
    if (!userName[i]) break;
  }
  userName[NAME_LEN] = '\0';
}

// "my name is X" / "call me X"
NOINLINE bool trySetName(const char *s) {
  const char *after = findAny(s, PSTR("my name is |call me |my name's "), true);
  if (!after) return false;
  while (*after == ' ') after++;
  uint8_t n = 0;
  while (isalpha(after[n]) && n < NAME_LEN) {
    userName[n] = n ? tolower(after[n]) : toupper(after[n]);
    n++;
  }
  userName[n] = '\0';
  if (!n) return false;
  EEPROM.update(EE_NAME, EE_MAGIC);
  for (uint8_t i = 0; i <= n; i++) EEPROM.update(EE_NAME + 1 + i, userName[i]);
  out.print(F("Nice to meet you, "));
  out.print(userName);
  out.println(F("! I'll remember that."));
  return true;
}

// ---------- internet (through bridge.py) ----------

// Phrase lists live in flash as one '|'-separated lowercase string (in RAM they
// would cost about 600 bytes). Finds the first entry at the start of `s` (or
// anywhere, if `anywhere`), case-insensitive and on word boundaries.
// Returns a pointer just past the match (and the entry's number in *index), or nullptr.
const char *findAny(const char *s, PGM_P list, bool anywhere, uint8_t *index) {
  for (const char *p = s; *p; p++) {
    if (p == s || (anywhere && !isalnum(p[-1]))) {
      uint8_t n = 0;
      for (PGM_P e = list;; n++) {
        const char *q = p;
        char c, last = 0;
        while ((c = pgm_read_byte(e)) && c != '|' && tolower(*q) == c) { q++; e++; last = c; }
        if ((!c || c == '|') && !(isalnum(last) && isalnum(*q))) {
          if (index) *index = n;
          return q;
        }
        while ((c = pgm_read_byte(e)) && c != '|') e++;
        if (!c) break;
        e++;
      }
    }
    if (!anywhere) break;
  }
  return nullptr;
}

bool looksLikeLookup(const char *s) {
  return findAny(s, PSTR("who is|who was|who are|what is a|what is an|what is the|what are|what was|"
                         "tell me about|define|search|look up|where is|explain"), false) &&
         !findAny(s, PSTR("you|your|my|time|weather|date|news|word|translate|fact"), true);
}

bool looksLikeQuestion(const char *s) {
  return strchr(s, '?') || findAny(s, PSTR("who|what|where|when|why|how|which"), false);
}

bool askNet(const char *kind, const char *text);

// Keep only a short topic, not the whole conversation, to conserve SRAM.
NOINLINE void rememberTopic(const char *s) {
  const char *topic = findAny(s, PSTR("tell me about |what do you know about |who is |who was |"
    "who are |what is a |what is an |what is the |what is |what are |what was |where is |"
    "where are |explain |define |definition of |look up |search for |info about "), false);
  if (!topic) topic = s;
  while (*topic == ' ') topic++;
  const char *article = findAny(topic, PSTR("a |an |the "), false);
  if (article) topic = article;
  size_t n = strlen(topic);
  while (n && (topic[n - 1] == '?' || topic[n - 1] == '.' || topic[n - 1] == '!' || topic[n - 1] == ' ')) n--;
  if (!n || n >= sizeof(lastTopic)) return;
  memcpy(lastTopic, topic, n);
  lastTopic[n] = '\0';
}

// Turn short follow-ups into a complete query using the last lookup topic.
NOINLINE bool tryFollowup(const char *s) {
  if (!lastTopic[0]) return false;
  PGM_P fmt = nullptr;
  if (findAny(s, PSTR("tell me more|more information|explain more|what about that"), true))
    fmt = PSTR("tell me about %s");
  else if (findAny(s, PSTR("how does it form|how is it formed|how does that form"), true))
    fmt = PSTR("how does %s form");
  else if (findAny(s, PSTR("how does it work|how does that work|how does it happen"), true))
    fmt = PSTR("how does %s work");
  else if (findAny(s, PSTR("where is it|where is that"), true))
    fmt = PSTR("where is %s");
  else if (findAny(s, PSTR("when did it happen|when did that happen"), true))
    fmt = PSTR("when did %s happen");
  else if (!strcasecmp_P(s, PSTR("why")) || !strcasecmp_P(s, PSTR("why?")) ||
           findAny(s, PSTR("why is that|why does it|why did that|why was that"), true))
    fmt = PSTR("why is %s important");
  else if (findAny(s, PSTR("what is it|what is that"), true))
    fmt = PSTR("what is %s");
  if (!fmt) return false;
  char query[LINE_LEN + 1];
  // Fill in "%s" by hand: snprintf would pull in ~1 KB of printf code.
  char *q = query, *qEnd = query + LINE_LEN;
  for (PGM_P f = fmt; pgm_read_byte(f) && q < qEnd; f++) {
    if (pgm_read_byte(f) == '%') {
      for (const char *t = lastTopic; *t && q < qEnd;) *q++ = *t++;
      f++;  // skip the 's'
    } else *q++ = pgm_read_byte(f);
  }
  *q = '\0';
  askNet("lookup", query);
  return true;
}

// Ask bridge.py; it answers with a line starting "@R ". Returns false on timeout.
bool askNet(const char *kind, const char *text) {
  if (!bridgeOn) {  // no internet: maybe the Library helper knows it
    for (uint8_t a = NET_LIBRARY; a >= NET_WORDS; a--)  // encyclopedia parts A, B, C
      if (askHelper(a, PSTR("wiki "), text) && netReply[0]) { out.println(netReply); return true; }
    say(R_NO_NET);
    return true;
  }
  face = FACE_WOW;
  if (lcdAddr) { lcdRow(0, F("Asking the")); lcdRow(1, F("internet")); }
  Serial.print(F("@NET "));
  Serial.print(kind);
  Serial.print('|');
  Serial.println(text);
  uint8_t matched = 0;
  bool printing = false;
  unsigned long start = millis();
  while (millis() - start < 15000) {
    if (!Serial.available()) {
      if (lcdAddr && !printing) {  // animate dots after "internet"
        lcdCmd(0xC8);
        uint8_t dots = (millis() - start) / 400 % 4;
        for (uint8_t i = 0; i < 3; i++) lcdSend(i < dots ? '.' : ' ', 1);
      }
      continue;
    }
    char c = Serial.read();
    if (printing) {
      if (c == '\n') { out.println(); face = FACE_HAPPY; return true; }
      if (c != '\r') out.print(c);
    } else if (c == "@R "[matched]) {
      if (++matched == 3) printing = true;
    } else matched = 0;
  }
  out.println(printing ? F("") : F("(the internet didn't answer)"));
  return printing;
}

#ifdef SOLO
// ---------- trivia (on the helpers in NanoNet): question|answer|other answer ----------
const char TRIVIA[] PROGMEM =
  "How many legs does a spider have?|8|eight|What planet is closest to the sun?|mercury|mercury|"
  "What is the largest ocean?|pacific|pacific|How many sides does a hexagon have?|6|six|"
  "What is the fastest land animal?|cheetah|cheetah|How many continents are there?|7|seven|"
  "What is the biggest planet?|jupiter|jupiter|What do bees make?|honey|honey|"
  "Which planet is called the Red Planet?|mars|mars|What is the largest mammal?|blue whale|whale|"
  "What color do you get mixing blue and yellow?|green|green|What is the tallest animal?|giraffe|giraffe|"
  "What is 12 times 12?|144|144|How many hours are in a day?|24|twenty four|"
  "What do caterpillars turn into?|butterflies|butterfly|What instrument has 88 keys?|piano|piano|"
  "What gas do plants breathe in?|carbon dioxide|co2|How many days are in a leap year?|366|366|"
  "What is frozen water called?|ice|ice|How many planets orbit the sun?|8|eight";
const uint8_t TRIVIA_N = 20;
uint8_t quiz = 0xFF;  // question being asked; 0xFF = none

// Copy field f (0 question, 1 answer, 2 other answer) of question q into buf.
void triviaField(uint8_t q, uint8_t f, char *buf, uint8_t len) {
  PGM_P c = TRIVIA;
  for (uint8_t skip = q * 3 + f; skip; c++) if (pgm_read_byte(c) == '|') skip--;
  uint8_t n = 0;
  for (char ch; (ch = pgm_read_byte(c)) && ch != '|' && n < len - 1; c++) buf[n++] = ch;
  buf[n] = '\0';
}

NOINLINE bool tryTrivia(const char *s) {
  char buf[48];
  bool ask = findAny(s, PSTR("trivia|quiz"), true);
  if (quiz != 0xFF) {
    uint8_t q = quiz, words = 1;
    quiz = 0xFF;
    for (const char *c = s; *c; c++) words += *c == ' ';
    bool giveUp = ask || findAny(s, PSTR("i dont know|dont know|idk|give up"), true);
    char alt[16];
    triviaField(q, 1, buf, sizeof buf);
    triviaField(q, 2, alt, sizeof alt);
    if (!giveUp && (strcasestr(s, buf) || strcasestr(s, alt))) {
      out.println(F("Correct! Say trivia for another."));
      return true;
    }
    if (giveUp || words <= 3) {  // a short reply is an answer; a long one is something else
      out.print(giveUp ? F("The answer is ") : F("Nope, it's "));
      out.print(buf);
      out.println(F(". Say trivia for another."));
      return true;
    }
  }
  if (!ask) return false;
  quiz = random(TRIVIA_N);
  triviaField(quiz, 0, buf, sizeof buf);
  out.println(buf);
  return true;
}
#endif

// ---------- self-training: remember corrections ----------
// "wrong, it was a joke" -> your previous message means "joke" from now on.
// Stored in EEPROM so it survives unplugging: byte 460 = how many, 461 = next slot,
// then up to 100 slots of (4-byte message fingerprint, 1-byte intent).
// (NanoLang uses EEPROM 0..451; your name is at 1000.)
const uint16_t EE_LEARN = 460;
const uint8_t LEARN_MAX = 100;

// Fingerprint of a message: lowercase letters/digits, words joined by single spaces.
uint32_t msgHash(const char *s) {
  uint32_t h = 2166136261UL;
  bool space = false, started = false;
  for (; *s; s++) {
    char c = tolower(*s);
    if (!isalnum(c)) { space = started; continue; }
    if (space) h = fnvStep(h, ' ');
    h = fnvStep(h, c);
    space = false;
    started = true;
  }
  return h;
}

uint8_t learnCount() {
  uint8_t n = EEPROM.read(EE_LEARN);
  return n > LEARN_MAX ? 0 : n;  // 0xFF = never used
}

// The intent you taught for this message, or 0xFF.
uint8_t learnedIntent(const char *s) {
  uint32_t h = msgHash(s), e;
  for (uint8_t i = 0; i < learnCount(); i++) {
    EEPROM.get(EE_LEARN + 2 + i * 5, e);
    if (e == h) return EEPROM.read(EE_LEARN + 6 + i * 5);
  }
  return 0xFF;
}

void learn(const char *s, uint8_t intent) {
  uint32_t h = msgHash(s), e;
  uint8_t n = learnCount(), slot = EEPROM.read(EE_LEARN + 1);
  for (uint8_t i = 0; i < n; i++) {  // already taught? just update it
    EEPROM.get(EE_LEARN + 2 + i * 5, e);
    if (e == h) { EEPROM.update(EE_LEARN + 6 + i * 5, intent); return; }
  }
  if (slot >= LEARN_MAX || n == 0) slot = n == 0 ? 0 : slot % LEARN_MAX;
  EEPROM.put(EE_LEARN + 2 + slot * 5, h);
  EEPROM.update(EE_LEARN + 6 + slot * 5, intent);
  EEPROM.update(EE_LEARN + 1, (slot + 1) % LEARN_MAX);  // when full, overwrite the oldest
  if (n < LEARN_MAX) EEPROM.update(EE_LEARN, n + 1);
}

void printName(uint8_t intent) {
  PGM_P c = CHAT_NAMES;
  for (uint8_t k = intent; k; c++) if (pgm_read_byte(c) == '|') k--;
  for (char ch; (ch = pgm_read_byte(c)) && ch != '|'; c++) out.print(ch);
}

// "wrong, it was a joke" / "no, i wanted a riddle": learn what `prev` meant.
NOINLINE bool tryCorrection(const char *s, const char *prev) {
  if (!findAny(s, PSTR("wrong|no|nope|thats wrong|that's wrong|not what i meant"), false)) return false;
  uint8_t k;
  if (!prev[0] || !findAny(s, CHAT_NAMES, true, &k)) {
    if (!findAny(s, PSTR("wrong"), true)) return false;  // a plain "no" is just a no
    out.println(F("Oops! Say: wrong, it was a joke (or fact, riddle, dice, greet...)"));
    return true;
  }
  learn(prev, k);
  out.print(F("Got it! Now \""));
  out.print(prev);
  out.print(F("\" means "));
  printName(k);
  out.println('.');
  if (bridgeOn) {  // the app saves it to train/learned.txt for the next real training
    Serial.print(F("@L"));
    printName(k);
    Serial.print('|');
    Serial.println(prev);
  }
  return true;
}

// ---------- main dispatch ----------

char lastMsg[LINE_LEN + 1];  // for "again" / "one more": the last thing you asked

// True for "again", "one more", "do it again please"... with nothing else in it.
bool isRepeat(const char *s) {
  if (!findAny(s, PSTR("again|another|more|repeat"), true)) return false;
  for (const char *p = s; *p;) {
    while (*p && !isalnum(*p)) p++;
    if (!*p) break;
    if (!findAny(p, PSTR("again|another|one|more|do|it|that|please|nano|once|repeat|try|same|go"), false))
      return false;
    while (isalnum(*p)) p++;
  }
  return true;
}

void reply(char *s) {
  while (*s == ' ') s++;
  if (!*s) return;

  if (!strcmp_P(s, PSTR("@HELLO"))) {  // the app connected: say which helpers are here, e.g. "@N1011"
    bridgeOn = true;
#ifdef SOLO
    Serial.println(F("@OK\n@N0000"));  // Solo build: no helpers
#else
    Serial.print(F("@OK\n@N"));
    for (uint8_t a = NET_BRAIN; a <= NET_LIBRARY; a++) {
      bool here = false;
      for (uint8_t t = 0; t < 3 && !here; t++) {  // a helper can miss the first knock
        here = i2cStart(a << 1);
        i2cStop();
        if (!here) delay(20);
      }
      Serial.print(here ? '1' : '0');
    }
    Serial.println();
#endif
    return;
  }
  if (tryCorrection(s, lastMsg)) return;  // before lastMsg changes: it's the message being corrected
  // "again": run your last message once more, whatever it was (dice, trivia, a joke...)
  char repeat[LINE_LEN + 1];
  if (lastMsg[0] && isRepeat(s)) s = strcpy(repeat, lastMsg);
  else strcpy(lastMsg, s);
  if (trySetName(s)) return;
  if (tryTools(s)) return;  // before games, so "cancel timer" isn't read as "set a timer"
  if (tryGames(s)) return;
  if (tryMath(s)) return;
#ifdef SOLO
  if (tryTrivia(s)) return;
#else
  // Each helper gets a turn and answers only what it handles (empty reply = "not mine"):
  // Words (invent, trivia, morse...), Calc ("calc ...", unit conversions), Library (safety tips).
  for (uint8_t a = NET_WORDS; a <= NET_LIBRARY; a++)
    if (askHelper(a, PSTR("do "), s) && netReply[0]) { out.println(netReply); return; }
  if (findAny(s, PSTR("invent|make up|calc|trivia|quiz|morse|backwards|survival"), true)) {
    out.println(F("That needs a NanoNet helper Nano plugged in."));
    return;
  }
#endif
  if (tryFollowup(s)) return;
  if (looksLikeLookup(s)) { rememberTopic(s); askNet("lookup", s); return; }

  float conf;
  uint8_t k = learnedIntent(s);  // something you taught it wins
  if (k == 0xFF) k = classify(s, &conf);
  else conf = 1;
  if (conf < MIN_CONFIDENCE) {
    if (bridgeOn && looksLikeQuestion(s)) askNet("lookup", s);
    else {
      face = FACE_WOW;  // the mini GPT on the Brain writes a reply; it takes a few seconds
      if (lcdAddr) { lcdRow(0, F("Thinking...")); lcdRow(1, F("")); }
      if (askHelper(NET_BRAIN, PSTR("gen "), s) && isalpha(netReply[0])) { face = FACE_HAPPY; out.println(netReply); }
      else say(R_UNKNOWN);
    }
    return;
  }

  switch (k) {
    case I_GREET: say(R_GREET); break;
    case I_BYE: say(R_BYE); break;
    case I_HOW_ARE_YOU: say(R_HOW); break;
    case I_WHO_ARE_YOU: say(R_WHO); break;
    case I_CREATOR: say(R_CREATOR); break;
    case I_HELP: say(R_HELP); break;
    case I_JOKE: sayLib(R_JOKE, PSTR("joke")); break;
    case I_FACT:  // "a fact from the internet" goes online; otherwise the Library or my own
      if (bridgeOn && findAny(s, PSTR("online|internet|web"), true)) askNet("fun_fact", s);
      else sayLib(R_FACT, PSTR("fact"));
      break;
    case I_THANKS: say(R_THANKS); break;
    case I_COMPLIMENT: say(R_COMPLIMENT); break;
    case I_INSULT: face = FACE_SAD; say(R_INSULT); break;
    case I_SAD: face = FACE_SAD; say(R_SAD); break;
    case I_HAPPY: say(R_HAPPY); break;
    case I_BORED: say(R_BORED); break;
    case I_EIGHTBALL: say(R_EIGHTBALL); break;
    case I_AGE: say(R_AGE); break;
    case I_FAVORITE: say(R_FAVORITE); break;
    case I_LOVE: say(R_LOVE); break;
    case I_RIDDLE: sayLib(R_RIDDLE, PSTR("riddle")); break;
    case I_COIN: out.println(random(2) ? F("Heads!") : F("Tails!")); break;
    case I_DICE: {
      const char *p = s;
      long n = nextNumber(p), sides = 6, total = 0;
      const char *d = p;
      while (d > s && isdigit(d[-1])) d--;  // back to the number's first digit
      if (d > s && (d[-1] == 'd' || d[-1] == 'D') && n > 1) { sides = n; n = 1; }  // "d20"
      if (n < 1 || n > 9) n = 1;
      out.print(F("You rolled"));
      for (long i = 0; i < n; i++) {
        long r = random(1, sides + 1);
        total += r;
        out.print(' ');
        out.print(r);
      }
      if (n > 1) { out.print(F(", total ")); out.print(total); }
      out.println('.');
      break;
    }
    case I_ASK_USER_NAME:
      if (userName[0]) { out.print(F("You're ")); out.print(userName); out.println(F("!")); }
      else out.println(F("I don't know yet. Tell me: my name is ..."));
      break;
    case I_TIME:
      if (bridgeOn) askNet("time", s);
      else {
        unsigned long m = millis() / 60000;
        out.print(F("I don't have a clock, but I've been awake for "));
        out.print(m);
        out.println(m == 1 ? F(" minute.") : F(" minutes."));
      }
      break;
    case I_WEATHER:
      if (bridgeOn) askNet("weather", s); else say(R_WEATHER);
      break;
    case I_LOOKUP: rememberTopic(s); askNet("lookup", s); break;
    case I_NEWS: askNet("news", s); break;
    case I_CONVERT: askNet("convert", s); break;  // the offline converter already tried
    case I_TRANSLATE: askNet("translate", s); break;
    case I_TIMER: setTimer(s); break;
    default: face = FACE_WOW; say(R_UNKNOWN);
  }
}

// Accepts \r, \n or \r\n; echoes typing unless bridge.py is driving.
bool feedChar(char c) {
  if (c == '\n' && lastWasCR) { lastWasCR = false; return false; }
  lastWasCR = (c == '\r');
  if (c == '\r' || c == '\n') {
    line[lineLen] = '\0';
    if (!bridgeOn) Serial.println();
    return true;
  }
  if (c == 8 || c == 127) {
    if (lineLen) { lineLen--; if (!bridgeOn) Serial.print(F("\b \b")); }
    return false;
  }
  if (lineLen < LINE_LEN && c >= ' ') {
    line[lineLen++] = c;
    if (!bridgeOn) Serial.print(c);
  }
  return false;
}

// Run one chat message (typed or from the button menu) and show the reply.
void runLine(char *msg) {
  digitalWrite(LED_BUILTIN, HIGH);
  screenStart();
  reply(msg);
  if (screenLen > 2) screenFinish();
  digitalWrite(LED_BUILTIN, LOW);
  Serial.print(F("> "));
}

// ---------- button menu: use NanoChat with no computer ----------
// Short press: next item on the LCD. Hold (0.6 s): do it.
// Each item is just a chat message, so it works exactly like typing it.
#ifdef SOLO
const char MENU[] PROGMEM = "tell me a joke|tell me a fact|riddle|trivia|roll a dice|flip a coin|"
  "magic 8 ball|timer 1 minute|timer 5 minutes|stopwatch|rock";
const uint8_t MENU_N = 11;
#else
const char MENU[] PROGMEM = "tell me a joke|tell me a fact|riddle|trivia|roll a dice|flip a coin|"
  "magic 8 ball|timer 1 minute|timer 5 minutes|stopwatch|rock|invent a word";
const uint8_t MENU_N = 12;
#endif

void checkButton() {
  static bool wasDown = false;
  static unsigned long downAt = 0, lastChange = 0;
  static uint8_t item = MENU_N - 1;
  bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down == wasDown || millis() - lastChange < 50) return;  // debounce
  wasDown = down;
  lastChange = millis();
  if (down) { downAt = millis(); return; }
  if (millis() - downAt >= 600) {  // held: run the item
    PGM_P e = MENU;
    for (uint8_t k = item; k; e++) if (pgm_read_byte(e) == '|') k--;
    uint8_t n = 0;
    for (char c; (c = pgm_read_byte(e)) && c != '|'; e++) line[n++] = c;
    line[n] = 0;
    Serial.print(F("[button] "));
    Serial.println(line);
    runLine(line);
    return;
  }
  item = (item + 1) % MENU_N;  // short press: show the next item
  if (!lcdAddr) return;
  screenLen = 0;  // stop paging the old reply over the menu
  replyAt = millis();
  lcdRow(0, F("Hold to choose:"));
  lcdCmd(0xC0);
  PGM_P e = MENU;
  for (uint8_t k = item; k; e++) if (pgm_read_byte(e) == '|') k--;
  for (uint8_t i = 0; i < 16; i++) {
    char c = pgm_read_byte(e);
    bool end = !c || c == '|';
    lcdSend(end ? ' ' : c, 1);
    if (!end) e++;
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  randomSeed(analogRead(A7) ^ (analogRead(A6) << 10) ^ micros());
  loadName();
  screenBegin();
  screenStart();
  out.println(F("NanoChat ready. Say hi! (type help for ideas)"));
  screenFinish();
  Serial.print(F("> "));
}

void loop() {
  checkButton();
  timerTick();
  screenTick();
  while (Serial.available()) {
    if (feedChar(Serial.read())) {
      runLine(line);
      lineLen = 0;
    }
  }
}
