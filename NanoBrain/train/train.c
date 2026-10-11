// Trains a tiny character-level neural language model and exports it
// as int8 weights for the Arduino Nano.
//
//   context of N previous letters -> embeddings -> tanh hidden layer -> next letter
//
// usage: train <wordlist> <name> <epochs> <out.h>
// Words are lowercased; anything that isn't a-z is skipped.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define V 27   // '.' (start/end) + a-z
#define N 4    // context letters
#define E 10   // embedding size
#ifndef H
#define H 64   // hidden units (override with -DH=32)
#endif
#define NE (N * E)
#define MAXLEN 14

static float C[V][E], W1[H][NE], b1[H], W2[V][H], b2[V];

static char **words;
static int nwords;

static double rnd(void) { return (rand() + 0.5) / ((double)RAND_MAX + 1); }
static float gauss(void) { return (float)(sqrt(-2 * log(rnd())) * cos(2 * M_PI * rnd())); }

static int tok(char c) { return c == '.' ? 0 : c - 'a' + 1; }

static void load(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) { perror(path); exit(1); }
  char buf[256];
  int cap = 1024;
  words = malloc(cap * sizeof *words);
  while (fgets(buf, sizeof buf, f)) {
    buf[strcspn(buf, "\r\n")] = 0;
    int n = strlen(buf), ok = n >= 3 && n <= MAXLEN;
    for (int i = 0; ok && i < n; i++) {
      buf[i] = tolower((unsigned char)buf[i]);
      if (buf[i] < 'a' || buf[i] > 'z') ok = 0;
    }
    if (!ok) continue;
    if (nwords == cap) words = realloc(words, (cap *= 2) * sizeof *words);
    words[nwords++] = strdup(buf);
  }
  fclose(f);
  // shuffle, then dedupe isn't needed (list is unique per case)
  for (int i = nwords - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    char *t = words[i]; words[i] = words[j]; words[j] = t;
  }
}

// One forward (+ optional backward/SGD step) on a single example.
// Returns -log p(target).
static float step(const int *ctx, int target, float lr) {
  float x[NE], h[H], lg[V], p[V];
  for (int j = 0; j < N; j++) memcpy(x + j * E, C[ctx[j]], sizeof(float) * E);
  for (int i = 0; i < H; i++) {
    float s = b1[i];
    for (int k = 0; k < NE; k++) s += W1[i][k] * x[k];
    h[i] = tanhf(s);
  }
  float mx = -1e30f;
  for (int v = 0; v < V; v++) {
    float s = b2[v];
    for (int i = 0; i < H; i++) s += W2[v][i] * h[i];
    lg[v] = s;
    if (s > mx) mx = s;
  }
  float z = 0;
  for (int v = 0; v < V; v++) z += p[v] = expf(lg[v] - mx);
  for (int v = 0; v < V; v++) p[v] /= z;
  float loss = -logf(p[target] + 1e-12f);
  if (lr == 0) return loss;

  float dh[H] = {0}, dx[NE] = {0};
  p[target] -= 1;
  for (int v = 0; v < V; v++) {
    float g = p[v];
    for (int i = 0; i < H; i++) { dh[i] += W2[v][i] * g; W2[v][i] -= lr * g * h[i]; }
    b2[v] -= lr * g;
  }
  for (int i = 0; i < H; i++) {
    float g = dh[i] * (1 - h[i] * h[i]);
    for (int k = 0; k < NE; k++) { dx[k] += W1[i][k] * g; W1[i][k] -= lr * g * x[k]; }
    b1[i] -= lr * g;
  }
  for (int j = 0; j < N; j++)
    for (int e = 0; e < E; e++) C[ctx[j]][e] -= lr * dx[j * E + e];
  return loss;
}

static float runWords(int from, int to, float lr) {
  double total = 0;
  long count = 0;
  for (int w = from; w < to; w++) {
    int ctx[N] = {0};
    const char *s = words[w];
    for (int i = 0;; i++) {
      int t = s[i] ? tok(s[i]) : 0;
      total += step(ctx, t, lr);
      count++;
      if (!t) break;
      memmove(ctx, ctx + 1, sizeof(int) * (N - 1));
      ctx[N - 1] = t;
    }
  }
  return (float)(total / count);
}

// ---- int8 quantization ----

static float quant(float *a, int n, int8_t *q) {
  float m = 0;
  for (int i = 0; i < n; i++) if (fabsf(a[i]) > m) m = fabsf(a[i]);
  float s = m / 127;
  for (int i = 0; i < n; i++) {
    int v = (int)lrintf(a[i] / s);
    q[i] = (int8_t)(v > 127 ? 127 : v < -127 ? -127 : v);
  }
  return s;
}

static void dequant(float *a, int n, const int8_t *q, float s) {
  for (int i = 0; i < n; i++) a[i] = q[i] * s;
}

static void writeArr(FILE *f, const char *type, const char *name, const int8_t *q, int n) {
  fprintf(f, "const %s %s[%d] PROGMEM = {", type, name, n);
  for (int i = 0; i < n; i++) fprintf(f, "%s%d", i % 24 ? "," : i ? ",\n  " : "\n  ", q[i]);
  fprintf(f, "\n};\n");
}

static void writeF(FILE *f, const char *name, const float *a, int n) {
  fprintf(f, "const float %s[%d] PROGMEM = {", name, n);
  for (int i = 0; i < n; i++) fprintf(f, "%s%.6g", i % 8 ? "," : i ? ",\n  " : "\n  ", a[i]);
  fprintf(f, "\n};\n");
}

static void sample(int count) {
  for (int c = 0; c < count; c++) {
    int ctx[N] = {0};
    char out[32];
    int n = 0;
    for (;;) {
      float x[NE], h[H], p[V], z = 0, mx = -1e30f;
      for (int j = 0; j < N; j++) memcpy(x + j * E, C[ctx[j]], sizeof(float) * E);
      for (int i = 0; i < H; i++) {
        float s = b1[i];
        for (int k = 0; k < NE; k++) s += W1[i][k] * x[k];
        h[i] = tanhf(s);
      }
      for (int v = 0; v < V; v++) {
        float s = b2[v];
        for (int i = 0; i < H; i++) s += W2[v][i] * h[i];
        p[v] = s; if (s > mx) mx = s;
      }
      for (int v = 0; v < V; v++) z += p[v] = expf(p[v] - mx);
      double r = rnd() * z;
      int t = 0;
      while (t < V - 1 && (r -= p[t]) > 0) t++;
      if (!t || n >= 20) break;
      out[n++] = 'a' + t - 1;
      memmove(ctx, ctx + 1, sizeof(int) * (N - 1));
      ctx[N - 1] = t;
    }
    out[n] = 0;
    printf("%s ", out);
  }
  printf("\n");
}

int main(int argc, char **argv) {
  if (argc != 5) { fprintf(stderr, "usage: train <wordlist> <name> <epochs> <out.h>\n"); return 1; }
  srand(1234);
  load(argv[1]);
  const char *name = argv[2];
  int epochs = atoi(argv[3]);
  int nval = nwords / 20, ntrain = nwords - nval;
  printf("%s: %d words (%d train, %d val)\n", name, nwords, ntrain, nval);

  for (int v = 0; v < V; v++) for (int e = 0; e < E; e++) C[v][e] = gauss();
  for (int i = 0; i < H; i++) for (int k = 0; k < NE; k++) W1[i][k] = gauss() * (5.0f / 3) / sqrtf(NE) * 0.5f;
  for (int v = 0; v < V; v++) for (int i = 0; i < H; i++) W2[v][i] = gauss() * 0.05f;

  printf("untrained val loss %.3f\n", runWords(ntrain, nwords, 0));
  for (int ep = 0; ep < epochs; ep++) {
    float lr = 0.02f * powf(0.1f, (float)ep / (epochs > 1 ? epochs - 1 : 1));
    for (int i = ntrain - 1; i > 0; i--) {   // reshuffle training words
      int j = rand() % (i + 1);
      char *t = words[i]; words[i] = words[j]; words[j] = t;
    }
    float tr = runWords(0, ntrain, lr);
    printf("epoch %2d  lr %.4f  train %.3f  val %.3f\n", ep + 1, lr, tr, runWords(ntrain, nwords, 0));
    fflush(stdout);
  }

  static int8_t qC[V * E], qW1[H * NE], qW2[V * H];
  float sC = quant(&C[0][0], V * E, qC);
  float sW1 = quant(&W1[0][0], H * NE, qW1);
  float sW2 = quant(&W2[0][0], V * H, qW2);
  dequant(&C[0][0], V * E, qC, sC);
  dequant(&W1[0][0], H * NE, qW1, sW1);
  dequant(&W2[0][0], V * H, qW2, sW2);
  printf("int8 val loss %.3f\nsamples: ", runWords(ntrain, nwords, 0));
  sample(15);

  FILE *f = fopen(argv[4], "w");
  fprintf(f, "// Generated by train.c from %s: %d words, %d epochs.\n", argv[1], nwords, epochs);
  fprintf(f, "#pragma once\n#include <avr/pgmspace.h>\n\n");
  char nm[64];
  snprintf(nm, sizeof nm, "%s_C", name);  writeArr(f, "int8_t", nm, qC, V * E);
  snprintf(nm, sizeof nm, "%s_W1", name); writeArr(f, "int8_t", nm, qW1, H * NE);
  snprintf(nm, sizeof nm, "%s_W2", name); writeArr(f, "int8_t", nm, qW2, V * H);
  snprintf(nm, sizeof nm, "%s_b1", name); writeF(f, nm, b1, H);
  snprintf(nm, sizeof nm, "%s_b2", name); writeF(f, nm, b2, V);
  fprintf(f, "const float %s_scale[3] PROGMEM = {%.8g, %.8g, %.8g};  // C, W1, W2\n", name, sC, sW1, sW2);
  fclose(f);
  return 0;
}
