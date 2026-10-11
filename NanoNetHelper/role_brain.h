// ROLE 1: Brain. A bigger copy of NanoChat's chat model (brain_model.h, made by
// NanoChat/train/train.py with a larger F and H). The boss sends "?message";
// the reply is "intent confidence intents", e.g. "6 93 30" = intent 6, 93% sure,
// out of 30 intents (so the boss can check both were trained on the same list).
#include <math.h>
#include "brain_model.h"

#define ROLE_NAME F("Brain")

const uint8_t MAX_FEATS = 48;

void roleSetup() {}

uint32_t fnvStep(uint32_t h, char c) { return (h ^ (uint8_t)c) * 16777619UL; }

// Words and word pairs -> hash buckets (same as NanoChat and train.py).
uint8_t extractFeatures(const char *s, uint16_t *feats) {
  uint8_t n = 0;
  bool havePrev = false;
  uint32_t hPrev = 0;
  while (*s) {
    while (*s && !isalnum(*s)) s++;
    if (!*s) break;
    uint32_t h = 2166136261UL, hb = fnvStep(hPrev, ' ');
    while (isalnum(*s)) {
      char c = tolower(*s++);
      h = fnvStep(h, c);
      hb = fnvStep(hb, c);
    }
    uint16_t f[2] = { (uint16_t)(h % CHAT_F), havePrev ? (uint16_t)(hb % CHAT_F) : (uint16_t)0xFFFF };
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

uint8_t classify(const char *s, float *confidence) {
  uint16_t feats[MAX_FEATS];
  uint8_t nf = extractFeatures(s, feats);
  float h[CHAT_H];
  for (uint8_t i = 0; i < CHAT_H; i++) {
    int16_t sum = 0;
    for (uint8_t j = 0; j < nf; j++)
      sum += (int8_t)pgm_read_byte(chat_W1 + (uint32_t)feats[j] * CHAT_H + i);
    float v = sum * chat_s1 + pgm_read_float(chat_b1 + i);
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

void handle(char *msg, Print &out) {
  if (!strncmp(msg, "do ", 3) || !strncmp(msg, "wiki ", 5)) return;  // not the Brain's job
  if (*msg == '?') msg++;
  float conf;
  uint8_t k = classify(msg, &conf);
  out.print(k);
  out.print(' ');
  out.print((int)(conf * 100));
  out.print(' ');
  out.print(CHAT_K);
}
