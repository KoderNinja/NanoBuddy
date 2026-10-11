// ROLE 2: Words. Word fun: invent words and names (NanoBrain's models), trivia,
// morse code, spell backwards, count letters, plus part C of the encyclopedia.
// Boss messages: "do invent a word", "do make up 3 names", "do trivia", "do morse sos",
// "do spell cat backwards", "do how many letters in banana", "wiki ...".
#include <math.h>
#include "wiki_lookup.h"
#include "wiki_c.h"
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

// ---------- trivia: question|answer|other accepted answer ----------
const char TRIVIA[] PROGMEM =
  "How many legs does a spider have?|8|eight|"
  "What planet is closest to the sun?|mercury|mercury|"
  "What is the largest ocean?|pacific|pacific|"
  "How many sides does a hexagon have?|6|six|"
  "What gas do plants breathe in?|carbon dioxide|co2|"
  "What is the fastest land animal?|cheetah|cheetah|"
  "How many continents are there?|7|seven|"
  "What is frozen water called?|ice|ice|"
  "What is the biggest planet?|jupiter|jupiter|"
  "How many days are in a leap year?|366|366|"
  "What do bees make?|honey|honey|"
  "What is the capital of France?|paris|paris|"
  "How many hours are in a day?|24|twenty four|"
  "Which planet is called the Red Planet?|mars|mars|"
  "What is the largest mammal?|blue whale|whale|"
  "What color do you get mixing blue and yellow?|green|green|"
  "How many strings does a normal guitar have?|6|six|"
  "What is the hardest natural material?|diamond|diamond|"
  "Which animal is known as the king of the jungle?|lion|lion|"
  "What is H2O better known as?|water|water|"
  "How many players are on a soccer team on the field?|11|eleven|"
  "What is the tallest animal?|giraffe|giraffe|"
  "What is 12 times 12?|144|144|"
  "Who painted the Mona Lisa?|leonardo da vinci|da vinci|"
  "What is the closest star to Earth?|sun|the sun|"
  "How many bones are in an adult human body?|206|206|"
  "What is the largest country by area?|russia|russia|"
  "What instrument has 88 keys?|piano|piano|"
  "What do caterpillars turn into?|butterflies|butterfly|"
  "Which ocean is the Titanic in?|atlantic|atlantic";
const uint8_t TRIVIA_N = 30;
uint8_t quiz = 0xFF;  // question being asked; 0xFF = none

// Copy field f (0 question, 1 answer, 2 other answer) of question q into buf.
void triviaField(uint8_t q, uint8_t f, char *buf, uint8_t len) {
  PGM_P c = TRIVIA;
  for (uint16_t skip = q * 3 + f; skip; c++) if (pgm_read_byte(c) == '|') skip--;
  uint8_t n = 0;
  for (char ch; (ch = pgm_read_byte(c)) && ch != '|' && n < len - 1; c++) buf[n++] = ch;
  buf[n] = '\0';
}

bool trivia(const char *msg, Print &out) {
  char buf[60];
  bool ask = strstr(msg, "trivia") || strstr(msg, "quiz");
  if (quiz != 0xFF) {
    uint8_t words = 1;
    for (const char *c = msg; *c; c++) words += *c == ' ';
    bool giveUp = ask || strstr(msg, "dont know") || strstr(msg, "give up") || strstr(msg, "idk");
    char a1[24], a2[24];
    triviaField(quiz, 1, a1, sizeof a1);
    triviaField(quiz, 2, a2, sizeof a2);
    if (!giveUp && (strstr(msg, a1) || strstr(msg, a2))) {
      out.print(F("Correct! Say trivia for another."));
      quiz = 0xFF;
      return true;
    }
    bool command = strstr(msg, "morse") || strstr(msg, "backward") || strstr(msg, "invent") ||
                   strstr(msg, "make up") || strstr(msg, "letters");
    if (!command && (giveUp || words <= 3)) {  // a short reply is an answer; a long one is something else
      out.print(giveUp ? F("The answer is ") : F("Nope, it's "));
      out.print(a1);
      out.print(F(". Say trivia for another."));
      quiz = 0xFF;
      return true;
    }
    quiz = 0xFF;  // they moved on
  }
  if (!ask) return false;
  quiz = random(TRIVIA_N);
  triviaField(quiz, 0, buf, sizeof buf);
  out.print(buf);
  return true;
}

// ---------- morse code ----------
// Each letter: high 3 bits = length, low bits = dots (0) and dashes (1), first symbol lowest.
const uint8_t MORSE[] PROGMEM = {
  0x42, 0x81, 0x85, 0x61, 0x20, 0x84, 0x63, 0x80, 0x40, 0x8E, 0x65, 0x82, 0x43,
  0x41, 0x67, 0x86, 0x8B, 0x62, 0x60, 0x21, 0x64, 0x88, 0x66, 0x89, 0x8D, 0x83 };

void morse(const char *text, Print &out) {
  for (const char *c = text; *c; c++) {
    if (*c == ' ') { out.print(F("/ ")); continue; }
    if (*c < 'a' || *c > 'z') continue;
    uint8_t m = pgm_read_byte(MORSE + (*c - 'a'));
    for (uint8_t i = 0; i < m >> 5; i++) out.print(m >> i & 1 ? '-' : '.');
    out.print(' ');
  }
}

// Remove the request words, keep the rest: "spell cat backwards" -> "cat".
void stripWords(const char *msg, const char *const *drop, uint8_t nDrop, char *out, uint8_t len) {
  uint8_t n = 0;
  for (const char *p = msg; *p;) {
    while (*p == ' ') p++;
    const char *start = p;
    while (*p && *p != ' ') p++;
    uint8_t wl = p - start;
    bool skip = false;
    for (uint8_t i = 0; i < nDrop; i++) skip |= wl == strlen(drop[i]) && !strncmp(start, drop[i], wl);
    if (skip || !wl) continue;
    if (n && n < len - 1) out[n++] = ' ';
    for (uint8_t i = 0; i < wl && n < len - 1; i++) out[n++] = start[i];
  }
  out[n] = '\0';
}

void handle(char *msg, Print &out) {
  for (char *c = msg; *c; c++) *c = tolower(*c);
  if (!strncmp(msg, "wiki ", 5)) { lookup(WIKI_C, msg + 5, out); return; }
  if (!strncmp(msg, "do ", 3)) msg += 3;
  if (trivia(msg, out)) return;
  char rest[64];
  if (strstr(msg, "morse")) {
    const char *drop[] = { "morse", "code", "for", "in", "say", "write", "what", "is", "the", "please" };
    stripWords(msg, drop, 10, rest, sizeof rest);
    if (!rest[0]) { out.print(F("Try: morse sos")); return; }
    morse(rest, out);
    return;
  }
  if (strstr(msg, "backward") || !strncmp(msg, "reverse ", 8)) {
    const char *drop[] = { "say", "spell", "write", "reverse", "backwards", "backward", "the", "word",
                           "please", "can", "you", "nano", "in", "it" };
    stripWords(msg, drop, 14, rest, sizeof rest);
    for (int8_t i = strlen(rest) - 1; i >= 0; i--) out.print(rest[i]);
    return;
  }
  char *letters = strstr(msg, "how many letters");
  if (letters) {
    char *w = strrchr(msg, ' ');  // last word: "how many letters in banana"
    w = w ? w + 1 : msg;
    uint8_t n = 0;
    for (char *c = w; *c; c++) n += isalpha(*c) != 0;
    out.print(w);
    out.print(F(" has "));
    out.print(n);
    out.print(F(" letters."));
    return;
  }
  bool inventing = strstr(msg, "invent") || strstr(msg, "make up") || strstr(msg, "made up") ||
                   strstr(msg, "new word") || strstr(msg, "fake word") || strstr(msg, "name idea") ||
                   strstr(msg, "baby name");
  if (!inventing) return;  // not mine: empty reply
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
