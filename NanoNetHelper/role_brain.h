// ROLE 1: Brain. A mini GPT (gpt_model.h, made by MiniGPT/export_gru.py): a tiny
// recurrent network that writes NanoChat's reply letter by letter when the boss
// doesn't recognize a message. "gen hello" -> "Hi there! how are you?"
// (The boss understands messages with its own model; this Nano writes replies.)
#include <math.h>
#include "gpt_model.h"

#define ROLE_NAME F("Brain")

void roleSetup() { randomSeed(analogRead(A7) ^ (analogRead(A6) << 10) ^ micros()); }

// ---------- mini GPT: a GRU that writes replies letter by letter ----------
// Weights are 4-bit with one bfloat16 scale per row. The memory h is kept as int8
// (h x 127) so each multiply is the Nano's fast 8x8-bit hardware multiply.

float bf16(uint16_t v) {
  union { uint32_t u; float f; } x;
  x.u = (uint32_t)v << 16;
  return x.f;
}

const int8_t NIB[16] = { 0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1 };  // 4-bit -> signed

int8_t nib(const uint8_t *w, uint16_t i) {
  uint8_t b = pgm_read_byte(w + (i >> 1));
  return NIB[i & 1 ? b >> 4 : b & 15];
}

// One weight row times x (int8), times the row's scale. Rows have an even length and
// each byte holds two weights of the row. Partial sums of 32 products fit in 16 bits
// (32 x 7 x 127 = 28448), which keeps the inner loop fast.
float rowDot(const uint8_t *w, const uint16_t *scale, uint16_t row, uint8_t n, const int8_t *x) {
  int32_t sum = 0;
  const uint8_t *p = w + (uint16_t)row * n / 2;
  for (uint8_t j = 0; j < n;) {
    int16_t part = 0;
    for (uint8_t k = 0; k < 32 && j < n; k += 2, j += 2) {
      uint8_t b = pgm_read_byte(p++);
      part += NIB[b & 15] * x[j] + NIB[b >> 4] * x[j + 1];
    }
    sum += part;
  }
  return sum * bf16(pgm_read_word(scale + row));
}

float bias(const int8_t *b, float scale, uint16_t i) { return (int8_t)pgm_read_byte(b + i) * scale; }

// Fast stand-ins for tanh and sigmoid (exp is slow on a Nano and these run ~300
// times per letter). Rational approximation of tanh, within ~0.02 of the real one.
float ftanh(float x) {
  if (x > 3) return 1;
  if (x < -3) return -1;
  float x2 = x * x;
  return x * (27 + x2) / (27 + 9 * x2);
}
float sigmoid(float x) { return 0.5 + 0.5 * ftanh(0.5 * x); }

int8_t gptH[GPT_H];  // the GRU's memory, x 127

// Feed one letter: update the memory (same math as TinyGRU in train_gpt.py).
void gptStep(uint8_t tok) {
  int8_t e[GPT_E], hn[GPT_H];
  // The embedding row is 4-bit too: e = nibble x es exactly, stored as nibble x 18 (|e| <= 126).
  float es = bf16(pgm_read_word(gpt_emb_s + tok)) / 18, hs = 1 / 127.0;
  for (uint8_t j = 0; j < GPT_E; j++) e[j] = nib(gpt_emb, tok * GPT_E + j) * 18;
  for (uint8_t i = 0; i < GPT_H; i++) {
    float r = sigmoid(rowDot(gpt_wi, gpt_wi_s, i, GPT_E, e) * es + bias(gpt_bi, gpt_bi_scale, i) +
                      rowDot(gpt_wh, gpt_wh_s, i, GPT_H, gptH) * hs + bias(gpt_bh, gpt_bh_scale, i));
    float z = sigmoid(rowDot(gpt_wi, gpt_wi_s, GPT_H + i, GPT_E, e) * es + bias(gpt_bi, gpt_bi_scale, GPT_H + i) +
                      rowDot(gpt_wh, gpt_wh_s, GPT_H + i, GPT_H, gptH) * hs + bias(gpt_bh, gpt_bh_scale, GPT_H + i));
    float n = ftanh(rowDot(gpt_wi, gpt_wi_s, 2 * GPT_H + i, GPT_E, e) * es + bias(gpt_bi, gpt_bi_scale, 2 * GPT_H + i) +
                    r * (rowDot(gpt_wh, gpt_wh_s, 2 * GPT_H + i, GPT_H, gptH) * hs + bias(gpt_bh, gpt_bh_scale, 2 * GPT_H + i)));
    float h = (1 - z) * n + z * gptH[i] * hs;
    hn[i] = (int8_t)(h * 127 + (h >= 0 ? 0.5 : -0.5));
  }
  memcpy(gptH, hn, sizeof gptH);
}

// Pick the next letter (temperature 0.5: fairly sure, a little creative).
uint8_t gptSample() {
  float p[GPT_V], mx = -1e30, z = 0;
  for (uint8_t v = 0; v < GPT_V; v++) {
    p[v] = (rowDot(gpt_out, gpt_out_s, v, GPT_H, gptH) / 127 + bias(gpt_bo, gpt_bo_scale, v)) / 0.5;
    if (p[v] > mx) mx = p[v];
  }
  for (uint8_t v = 0; v < GPT_V; v++) z += p[v] = exp(p[v] - mx);
  float r = random(0x7FFFFFFFL) / 2147483648.0 * z;
  uint8_t t = 0;
  while (t < GPT_V - 1 && (r -= p[t]) > 0) t++;
  return t;
}

uint8_t gptToken(char c) {
  for (uint8_t i = 0; i < GPT_V; i++) if (pgm_read_byte(GPT_VOCAB + i) == c) return i;
  return 0xFF;
}

// Read "message>" and write a reply until the model ends the line.
void gptReply(const char *msg, Print &out) {
  memset(gptH, 0, sizeof gptH);
  gptStep(gptToken('\n'));
  for (const char *c = msg; *c; c++) {
    uint8_t t = gptToken(tolower(*c));
    if (t != 0xFF && *c != '>') gptStep(t);
  }
  gptStep(gptToken('>'));
  for (uint8_t n = 0; n < 60; n++) {  // replies take ~3-9 s on a Nano
    uint8_t t = gptSample();
    char c = pgm_read_byte(GPT_VOCAB + t);
    if (c == '\n' || c == '>') break;
    out.print(n == 0 ? (char)toupper(c) : c);
    gptStep(t);
  }
}

void handle(char *msg, Print &out) {
  if (!strncmp(msg, "gen ", 4)) gptReply(msg + 4, out);
  else if (strncmp(msg, "do ", 3) && strncmp(msg, "wiki ", 5) && *msg != '?') gptReply(msg, out);  // Serial testing
}
