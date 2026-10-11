// NanoBrain: a tiny neural-network language model running on an Arduino Nano.
// It reads the last 4 letters and predicts the next one, letter by letter,
// inventing new words (or names) that sound real. Trained on a Mac by train/train.c.
//
// Serial 9600 baud. Commands:
//   (Enter) or gen [n]   invent n words
//   start <letters>      invent words that begin with <letters>
//   score <word>         how real does a word sound to the model?
//   temp <0.1-2>         creativity (low = safe, high = wild)
//   mode words|names     switch brains
//   help

#include <avr/pgmspace.h>
#include <math.h>
#include "words_model.h"
#include "names_model.h"

const uint8_t V = 27, N = 4, E = 10, H = 64, NE = N * E;
const uint8_t MAX_WORD = 20;
const uint8_t LINE_LEN = 40;

struct Model {
  const char *name;
  const int8_t *C, *W1, *W2;
  const float *b1, *b2, *scale;
};
const Model models[] = {
  { "words", words_C, words_W1, words_W2, words_b1, words_b2, words_scale },
  { "names", names_C, names_W1, names_W2, names_b1, names_b2, names_scale },
};
const Model *m = &models[0];
float sC, sW1, sW2;
float temperature = 0.8;

char line[LINE_LEN + 1];
uint8_t lineLen = 0;
bool lastWasCR = false;

void useModel(const Model *next) {
  m = next;
  sC  = pgm_read_float(m->scale);
  sW1 = pgm_read_float(m->scale + 1);
  sW2 = pgm_read_float(m->scale + 2);
}

// Next-letter probabilities given the last N tokens (0 = '.', 1..26 = a..z).
void predict(const uint8_t *ctx, float *p, float temp) {
  float x[NE], h[H];
  for (uint8_t j = 0; j < N; j++)
    for (uint8_t e = 0; e < E; e++)
      x[j * E + e] = (int8_t)pgm_read_byte(m->C + ctx[j] * E + e) * sC;

  for (uint8_t i = 0; i < H; i++) {
    const int8_t *w = m->W1 + i * NE;
    float s = 0;
    for (uint8_t k = 0; k < NE; k++) s += (int8_t)pgm_read_byte(w + k) * x[k];
    h[i] = tanh(s * sW1 + pgm_read_float(m->b1 + i));
  }

  float mx = -1e30;
  for (uint8_t v = 0; v < V; v++) {
    const int8_t *w = m->W2 + v * H;
    float s = 0;
    for (uint8_t i = 0; i < H; i++) s += (int8_t)pgm_read_byte(w + i) * h[i];
    p[v] = (s * sW2 + pgm_read_float(m->b2 + v)) / temp;
    if (p[v] > mx) mx = p[v];
  }
  float z = 0;
  for (uint8_t v = 0; v < V; v++) z += p[v] = exp(p[v] - mx);
  for (uint8_t v = 0; v < V; v++) p[v] /= z;
}

void push(uint8_t *ctx, uint8_t t) {
  memmove(ctx, ctx + 1, N - 1);
  ctx[N - 1] = t;
}

uint8_t sample(const float *p) {
  float r = random(0x7FFFFFFFL) / 2147483648.0;
  for (uint8_t v = 0; v < V; v++) {
    r -= p[v];
    if (r <= 0) return v;
  }
  return V - 1;
}

void generate(const char *prefix) {
  uint8_t ctx[N] = {0};
  float p[V];
  uint8_t len = 0;
  for (; *prefix; prefix++) {
    Serial.print(*prefix);
    push(ctx, *prefix - 'a' + 1);
    len++;
  }
  for (;;) {
    predict(ctx, p, temperature);
    if (len == 0) p[0] = 0;          // don't end before the first letter
    uint8_t t = sample(p);
    if (t == 0 || len >= MAX_WORD) break;
    Serial.print((char)('a' + t - 1));
    push(ctx, t);
    len++;
  }
  Serial.println();
}

void score(const char *word) {
  uint8_t ctx[N] = {0};
  float p[V], nats = 0;
  uint8_t n = 0;
  for (const char *c = word;; c++) {
    uint8_t t = *c ? *c - 'a' + 1 : 0;
    predict(ctx, p, 1.0);
    nats -= log(p[t] + 1e-9);
    n++;
    if (!t) break;
    push(ctx, t);
  }
  float avg = nats / n;
  Serial.print(word);
  Serial.print(F(": surprise "));
  Serial.print(avg, 2);
  Serial.print(F(" per letter -> "));
  if (avg < 1.8) Serial.println(F("sounds very real"));
  else if (avg < 2.4) Serial.println(F("plausible"));
  else if (avg < 3.0) Serial.println(F("a bit odd"));
  else Serial.println(F("not a real-sounding word"));
}

// Lowercase and keep only a-z. Returns false if anything else was there.
bool cleanWord(char *s) {
  bool ok = true;
  char *out = s;
  for (; *s; s++) {
    char c = tolower(*s);
    if (c >= 'a' && c <= 'z') *out++ = c;
    else if (c != ' ') ok = false;
  }
  *out = '\0';
  return ok;
}

void printHelp() {
  Serial.println(F("NanoBrain - a tiny neural net that invents words"));
  Serial.println(F("  Enter / gen [n]     invent words"));
  Serial.println(F("  start <letters>     words beginning with <letters>"));
  Serial.println(F("  score <word>        how real does it sound?"));
  Serial.println(F("  temp <0.1-2>        creativity"));
  Serial.println(F("  mode words|names    switch brains"));
}

void prompt() {
  Serial.print(m->name);
  Serial.print(F("> "));
}

void handleLine(char *s) {
  while (*s == ' ') s++;
  digitalWrite(LED_BUILTIN, HIGH);   // "thinking"

  if (!*s) generate("");
  else if (!strncmp(s, "gen", 3)) {
    int n = atoi(s + 3);
    if (n < 1) n = 1;
    if (n > 50) n = 50;
    while (n--) generate("");
  }
  else if (!strncmp(s, "start ", 6)) {
    char *w = s + 6;
    if (!cleanWord(w) || !*w || strlen(w) > MAX_WORD) Serial.println(F("letters a-z only"));
    else for (uint8_t i = 0; i < 5; i++) generate(w);
  }
  else if (!strncmp(s, "score ", 6)) {
    char *w = s + 6;
    if (!cleanWord(w) || !*w) Serial.println(F("letters a-z only"));
    else score(w);
  }
  else if (!strncmp(s, "temp", 4)) {
    float t = atof(s + 4);
    if (t >= 0.1 && t <= 2) temperature = t;
    Serial.print(F("temp = ")); Serial.println(temperature, 2);
  }
  else if (!strncmp(s, "mode ", 5)) {
    bool found = false;
    for (const Model &mm : models)
      if (!strcmp(s + 5, mm.name)) { useModel(&mm); found = true; }
    if (!found) Serial.println(F("modes: words, names"));
  }
  else if (!strcmp(s, "help")) printHelp();
  else Serial.println(F("unknown command, type help"));

  digitalWrite(LED_BUILTIN, LOW);
}

// Accepts \r, \n or \r\n line endings; echoes as you type.
bool feedChar(char c) {
  if (c == '\n' && lastWasCR) { lastWasCR = false; return false; }
  lastWasCR = (c == '\r');
  if (c == '\r' || c == '\n') {
    line[lineLen] = '\0';
    Serial.println();
    return true;
  }
  if (c == 8 || c == 127) {
    if (lineLen) { lineLen--; Serial.print(F("\b \b")); }
    return false;
  }
  if (lineLen < LINE_LEN && c >= ' ') { line[lineLen++] = c; Serial.print(c); }
  return false;
}

void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  randomSeed(analogRead(A7) ^ (analogRead(A6) << 10) ^ micros());
  useModel(&models[0]);
  printHelp();
  prompt();
}

void loop() {
  while (Serial.available()) {
    if (feedChar(Serial.read())) {
      handleLine(line);
      lineLen = 0;
      prompt();
    }
  }
}
