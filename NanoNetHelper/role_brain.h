// ROLE 1: Brain. A mini GPT (gpt_model.h, made by MiniGPT/export_gru.py): a tiny
// recurrent network that writes NanoChat's reply letter by letter when the boss
// doesn't recognize a message. "gen hello" -> "Hi there! how are you?"
// (The boss understands messages with its own model; this Nano writes replies.)
#include <math.h>
#include "gpt_model.h"

#define ROLE_NAME F("Brain")

void roleSetup() { randomSeed(analogRead(A7) ^ (analogRead(A6) << 10) ^ micros()); }

// ---------- mini GPT: a GRU that writes replies letter by letter ----------
// Weights are 4-bit with one bfloat16 scale per row; the memory h is fixed-point
// (x 4096) so the inner loops use fast whole-number math.

float bf16(uint16_t v) {
  union { uint32_t u; float f; } x;
  x.u = (uint32_t)v << 16;
  return x.f;
}

int8_t nib(const uint8_t *w, uint16_t i) {
  uint8_t b = pgm_read_byte(w + (i >> 1));
  int8_t v = i & 1 ? b >> 4 : b & 15;
  return v >= 8 ? v - 16 : v;
}

// One weight row times x (fixed-point), as a float.
float rowDot(const uint8_t *w, const uint16_t *scale, uint16_t row, uint8_t n, const int16_t *x) {
  int32_t sum = 0;
  uint16_t base = row * n;
  for (uint8_t j = 0; j < n; j++) sum += nib(w, base + j) * x[j];
  return sum * bf16(pgm_read_word(scale + row)) / 4096.0;
}

float bias(const int8_t *b, float scale, uint16_t i) { return (int8_t)pgm_read_byte(b + i) * scale; }
float sigmoid(float x) { return 1 / (1 + exp(-x)); }

int16_t gptH[GPT_H];  // the GRU's memory

// Feed one letter: update the memory (same math as TinyGRU in train_gpt.py).
void gptStep(uint8_t tok) {
  int16_t e[GPT_E], hn[GPT_H];
  float es = bf16(pgm_read_word(gpt_emb_s + tok));
  for (uint8_t j = 0; j < GPT_E; j++) e[j] = nib(gpt_emb, tok * GPT_E + j) * es * 4096;
  for (uint8_t i = 0; i < GPT_H; i++) {
    float r = sigmoid(rowDot(gpt_wi, gpt_wi_s, i, GPT_E, e) + bias(gpt_bi, gpt_bi_scale, i) +
                      rowDot(gpt_wh, gpt_wh_s, i, GPT_H, gptH) + bias(gpt_bh, gpt_bh_scale, i));
    float z = sigmoid(rowDot(gpt_wi, gpt_wi_s, GPT_H + i, GPT_E, e) + bias(gpt_bi, gpt_bi_scale, GPT_H + i) +
                      rowDot(gpt_wh, gpt_wh_s, GPT_H + i, GPT_H, gptH) + bias(gpt_bh, gpt_bh_scale, GPT_H + i));
    float n = tanh(rowDot(gpt_wi, gpt_wi_s, 2 * GPT_H + i, GPT_E, e) + bias(gpt_bi, gpt_bi_scale, 2 * GPT_H + i) +
                   r * (rowDot(gpt_wh, gpt_wh_s, 2 * GPT_H + i, GPT_H, gptH) + bias(gpt_bh, gpt_bh_scale, 2 * GPT_H + i)));
    hn[i] = ((1 - z) * n + z * gptH[i] / 4096.0) * 4096;
  }
  memcpy(gptH, hn, sizeof gptH);
}

// Pick the next letter (temperature 0.5: fairly sure, a little creative).
uint8_t gptSample() {
  float p[GPT_V], mx = -1e30, z = 0;
  for (uint8_t v = 0; v < GPT_V; v++) {
    p[v] = (rowDot(gpt_out, gpt_out_s, v, GPT_H, gptH) + bias(gpt_bo, gpt_bo_scale, v)) / 0.5;
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
  for (uint8_t n = 0; n < 100; n++) {
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
