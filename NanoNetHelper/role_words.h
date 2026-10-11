// ROLE 2: Words. NanoBrain's letter-by-letter models invent words or names.
// Messages: "invent a word", "make up 3 words", "invent a name", "a word starting with bl".
#include <math.h>
#include "words_model.h"
#include "names_model.h"

#define ROLE_NAME F("Words")

const uint8_t V = 27, N = 4, E = 10, H = 64, NE = N * E, MAX_WORD = 14;

struct Model { const int8_t *C, *W1, *W2; const float *b1, *b2, *scale; };
const Model WORDS = { words_C, words_W1, words_W2, words_b1, words_b2, words_scale };
const Model NAMES = { names_C, names_W1, names_W2, names_b1, names_b2, names_scale };

void roleSetup() { randomSeed(analogRead(A7) ^ (analogRead(A6) << 10) ^ micros()); }

// Next-letter probabilities for the last N letters (0 = start/end, 1..26 = a..z).
void predict(const Model &m, const uint8_t *ctx, float *p) {
  float sC = pgm_read_float(m.scale), sW1 = pgm_read_float(m.scale + 1), sW2 = pgm_read_float(m.scale + 2);
  for (uint8_t v = 0; v < V; v++) p[v] = 0;
  for (uint8_t i = 0; i < H; i++) {  // add each hidden unit straight into the outputs (saves RAM)
    int32_t s = 0;
    for (uint8_t k = 0; k < NE; k++)
      s += (int16_t)(int8_t)pgm_read_byte(m.W1 + i * NE + k) *
           (int8_t)pgm_read_byte(m.C + ctx[k / E] * E + k % E);
    float h = tanh(s * sC * sW1 + pgm_read_float(m.b1 + i));
    for (uint8_t v = 0; v < V; v++) p[v] += (int8_t)pgm_read_byte(m.W2 + v * H + i) * h;
  }
  float mx = -1e30, z = 0;
  for (uint8_t v = 0; v < V; v++) {
    p[v] = (p[v] * sW2 + pgm_read_float(m.b2 + v)) / 0.8;  // 0.8 = a little creative
    if (p[v] > mx) mx = p[v];
  }
  for (uint8_t v = 0; v < V; v++) z += p[v] = exp(p[v] - mx);
  for (uint8_t v = 0; v < V; v++) p[v] /= z;
}

void invent(const Model &m, const char *prefix, bool capital, Print &out) {
  uint8_t ctx[N] = {0}, len = 0;
  float p[V];
  for (; *prefix && len < MAX_WORD; prefix++, len++) {
    out.print(len == 0 && capital ? (char)toupper(*prefix) : *prefix);
    memmove(ctx, ctx + 1, N - 1);
    ctx[N - 1] = *prefix - 'a' + 1;
  }
  for (;;) {
    predict(m, ctx, p);
    if (len < 3) p[0] = 0;  // at least 3 letters
    float r = random(0x7FFFFFFFL) / 2147483648.0, total = 0;
    for (uint8_t v = 0; v < V; v++) total += p[v];
    r *= total;
    uint8_t t = 0;
    while (t < V - 1 && (r -= p[t]) > 0) t++;
    if (t == 0 || len >= MAX_WORD) break;
    out.print((char)((len == 0 && capital ? 'A' : 'a') + t - 1));
    memmove(ctx, ctx + 1, N - 1);
    ctx[N - 1] = t;
    len++;
  }
}

void handle(char *msg, Print &out) {
  for (char *c = msg; *c; c++) *c = tolower(*c);
  bool names = strstr(msg, "name");
  uint8_t count = 1;
  for (char *c = msg; *c; c++) if (isdigit(*c)) { count = constrain(atoi(c), 1, 5); break; }
  // "starting with bl" / "that starts with bl"
  char prefix[6] = "";
  char *s = strstr(msg, "start");
  if (s) {
    s = strstr(s, "with ");
    if (s) {
      s += 5;
      uint8_t n = 0;
      while (*s >= 'a' && *s <= 'z' && n < 5) prefix[n++] = *s++;
      prefix[n] = '\0';
    }
  }
  out.print(names ? F("Name idea") : F("New word"));
  out.print(count > 1 ? F("s: ") : F(": "));
  for (uint8_t i = 0; i < count; i++) {
    if (i) out.print(F(", "));
    invent(names ? NAMES : WORDS, prefix, names, out);
  }
}
