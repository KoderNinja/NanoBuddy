// ROLE 3: Calc. NanoCalc's evaluator, a unit converter, and part B of the
// offline encyclopedia. Boss messages: "do calc x = 5", "do 5 miles to km",
// "do how many cups in a gallon", "wiki who is newton".
// Copied from NanoCalc/NanoCalc.ino with Serial swapped for the reply box.
#include <math.h>
#include "wiki_lookup.h"
#include "wiki_b.h"

#define ROLE_NAME F("Calc")

Print *nout = &Serial;

const uint8_t MAX_VARS = 16;
const uint8_t NAME_LEN = 8;  // variable names

struct Var { char name[NAME_LEN + 1]; float value; };
Var vars[MAX_VARS];
uint8_t varCount = 0;


const char *p;          // parse cursor
const char *err;        // first error message, or nullptr

// ---------- variables ----------

int8_t findVar(const char *name) {
  for (uint8_t i = 0; i < varCount; i++)
    if (strcmp(vars[i].name, name) == 0) return i;
  return -1;
}

bool setVar(const char *name, float v) {
  int8_t i = findVar(name);
  if (i < 0) {
    if (varCount >= MAX_VARS) return false;
    i = varCount++;
    strncpy(vars[i].name, name, NAME_LEN);
    vars[i].name[NAME_LEN] = '\0';
  }
  vars[i].value = v;
  return true;
}

// ---------- parser (recursive descent) ----------
// expr   := term (('+'|'-') term)*
// term   := power (('*'|'/'|'%') power)*
// power  := unary ('^' power)?          right-associative
// unary  := '-' unary | primary
// primary:= number | name | name '(' expr ')' | '(' expr ')'

void skipSpaces() { while (*p == ' ' || *p == '\t') p++; }

void fail(const char *msg) { if (!err) err = msg; }

bool readName(char *out) {
  skipSpaces();
  if (!isalpha(*p) && *p != '_') return false;
  uint8_t n = 0;
  while (isalnum(*p) || *p == '_') {
    if (n < NAME_LEN) out[n++] = tolower(*p);
    p++;
  }
  out[n] = '\0';
  return true;
}

float expr();

float callFunc(const char *name, float x) {
  if (!strcmp(name, "sqrt")) { if (x < 0) fail("sqrt of negative"); return sqrt(x); }
  if (!strcmp(name, "abs"))   return fabs(x);
  if (!strcmp(name, "sin"))   return sin(x);
  if (!strcmp(name, "cos"))   return cos(x);
  if (!strcmp(name, "tan"))   return tan(x);
  if (!strcmp(name, "ln"))    { if (x <= 0) fail("ln of non-positive"); return log(x); }
  if (!strcmp(name, "log"))   { if (x <= 0) fail("log of non-positive"); return log10(x); }
  if (!strcmp(name, "round")) return round(x);
  if (!strcmp(name, "floor")) return floor(x);
  if (!strcmp(name, "ceil"))  return ceil(x);
  fail("unknown function");
  return 0;
}

float primary() {
  skipSpaces();
  if (*p == '(') {
    p++;
    float v = expr();
    skipSpaces();
    if (*p == ')') p++; else fail("missing )");
    return v;
  }
  if (isdigit(*p) || *p == '.') {
    char *end;
    float v = strtod(p, &end);
    if (end == p) fail("bad number");
    p = end;
    return v;
  }
  char name[NAME_LEN + 1];
  if (readName(name)) {
    skipSpaces();
    if (*p == '(') {
      p++;
      float arg = expr();
      skipSpaces();
      if (*p == ')') p++; else fail("missing )");
      return callFunc(name, arg);
    }
    if (!strcmp(name, "pi")) return M_PI;
    if (!strcmp(name, "e"))  return M_E;
    if (!strcmp(name, "ans")) { int8_t i = findVar("ans"); return i < 0 ? 0 : vars[i].value; }
    int8_t i = findVar(name);
    if (i < 0) { fail("unknown variable"); return 0; }
    return vars[i].value;
  }
  fail(*p ? "unexpected character" : "incomplete expression");
  return 0;
}

float unary() {
  skipSpaces();
  if (*p == '-') { p++; return -unary(); }
  if (*p == '+') { p++; return unary(); }
  return primary();
}

float power() {
  float base = unary();
  skipSpaces();
  if (*p == '^') { p++; return pow(base, power()); }
  return base;
}

float term() {
  float v = power();
  for (;;) {
    skipSpaces();
    char op = *p;
    if (op != '*' && op != '/' && op != '%') return v;
    p++;
    float r = power();
    if (op == '*') v *= r;
    else if (r == 0) fail("divide by zero");
    else if (op == '/') v /= r;
    else v = fmod(v, r);
  }
}

float expr() {
  float v = term();
  for (;;) {
    skipSpaces();
    if (*p == '+') { p++; v += term(); }
    else if (*p == '-') { p++; v -= term(); }
    else return v;
  }
}

// ---------- output ----------

void printNumber(float v) {
  if (isnan(v)) { nout->print(F("nan")); return; }
  if (isinf(v)) { nout->print(v > 0 ? F("inf") : F("-inf")); return; }
  if (v == (long)v && fabs(v) < 1e9) { nout->print((long)v); return; }
  // up to 4 decimals (32-bit float isn't accurate past that), trailing zeros trimmed
  char buf[20];
  dtostrf(v, 0, 4, buf);
  char *end = buf + strlen(buf) - 1;
  while (*end == '0') *end-- = '\0';
  if (*end == '.') *end = '\0';
  nout->print(buf);
}

void printHelp() {
  nout->println(F("NanoCalc - math with variables"));
  nout->println(F("  x = 5          set a variable"));
  nout->println(F("  x * 2 + 1      evaluate"));
  nout->println(F("  ops: + - * / % ^ ( )"));
  nout->println(F("  funcs: sqrt abs sin cos tan ln log round floor ceil"));
  nout->println(F("  consts: pi e ans (last result)"));
  nout->println(F("  cmds: vars  clear  help"));
}

// ---------- line handling ----------

void calcLine(char *s) {
  while (*s == ' ') s++;
  if (!*s) return;

  if (!strcmp(s, "help"))  { printHelp(); return; }
  if (!strcmp(s, "clear")) { varCount = 0; nout->println(F("ok")); return; }
  if (!strcmp(s, "vars")) {
    if (!varCount) nout->println(F("(no variables)"));
    for (uint8_t i = 0; i < varCount; i++) {
      nout->print(vars[i].name); nout->print(F(" = "));
      printNumber(vars[i].value); nout->println();
    }
    return;
  }

  err = nullptr;
  p = s;

  // assignment?  name = expr   (but not ==)
  char target[NAME_LEN + 1] = "";
  const char *save = p;
  if (readName(target)) {
    skipSpaces();
    if (*p == '=' && p[1] != '=') p++;
    else { p = save; target[0] = '\0'; }
  }
  if (target[0] && (!strcmp(target, "pi") || !strcmp(target, "e"))) {
    nout->println(F("error: can't assign to a constant"));
    return;
  }

  float v = expr();
  skipSpaces();
  if (!err && *p) fail("unexpected character");

  if (err) {
    nout->print(F("error: ")); nout->println(err);
    return;
  }
  if (target[0] && !setVar(target, v)) {
    nout->println(F("error: too many variables"));
    return;
  }
  setVar("ans", v);

  if (target[0]) { nout->print(target); nout->print(F(" = ")); }
  printNumber(v);
  nout->println();
}


// ---------- unit converter ----------
// Each unit: names (short, singular, plural, other spellings), kind (0 length, 1 mass, 2 volume, 3 time, 4 speed), size in that kind's base unit.
struct Unit { const char *names; uint8_t kind; float size; };
const char U0[] PROGMEM = "km kilometer kilometers kilometre kilometres";
const char U1[] PROGMEM = "m meter meters metre metres";
const char U2[] PROGMEM = "cm centimeter centimeters";
const char U3[] PROGMEM = "mm millimeter millimeters";
const char U4[] PROGMEM = "mi mile miles";
const char U5[] PROGMEM = "yd yard yards";
const char U6[] PROGMEM = "ft foot feet";
const char U7[] PROGMEM = "in inch inches";
const char U8[] PROGMEM = "kg kilogram kilograms kilo kilos";
const char U9[] PROGMEM = "g gram grams";
const char U10[] PROGMEM = "lb pound pounds lbs";
const char U11[] PROGMEM = "oz ounce ounces";
const char U12[] PROGMEM = "l liter liters litre litres";
const char U13[] PROGMEM = "ml milliliter milliliters";
const char U14[] PROGMEM = "gal gallon gallons";
const char U15[] PROGMEM = "qt quart quarts";
const char U16[] PROGMEM = "pt pint pints";
const char U17[] PROGMEM = "cup cup cups";
const char U18[] PROGMEM = "tbsp tablespoon tablespoons";
const char U19[] PROGMEM = "tsp teaspoon teaspoons";
const char U20[] PROGMEM = "s second seconds sec secs";
const char U21[] PROGMEM = "min minute minutes mins";
const char U22[] PROGMEM = "h hour hours hr hrs";
const char U23[] PROGMEM = "day day days";
const char U24[] PROGMEM = "week week weeks";
const char U25[] PROGMEM = "year year years";
const char U26[] PROGMEM = "kph kph kph kmh";
const char U27[] PROGMEM = "mph mph mph";
const char U28[] PROGMEM = "c celsius celsius centigrade";
const char U29[] PROGMEM = "f fahrenheit fahrenheit";
const Unit UNITS[] PROGMEM = {  // in flash to save RAM; read with unit()
  {U0, 0, 1000}, {U1, 0, 1}, {U2, 0, 0.01}, {U3, 0, 0.001}, {U4, 0, 1609.344}, {U5, 0, 0.9144},
  {U6, 0, 0.3048}, {U7, 0, 0.0254}, {U8, 1, 1}, {U9, 1, 0.001}, {U10, 1, 0.45359237}, {U11, 1, 0.028349523},
  {U12, 2, 1}, {U13, 2, 0.001}, {U14, 2, 3.785411784}, {U15, 2, 0.946352946}, {U16, 2, 0.473176473},
  {U17, 2, 0.2365882365}, {U18, 2, 0.0147867648}, {U19, 2, 0.00492892159}, {U20, 3, 1}, {U21, 3, 60},
  {U22, 3, 3600}, {U23, 3, 86400}, {U24, 3, 604800}, {U25, 3, 31557600}, {U26, 4, 1}, {U27, 4, 1.609344},
  {U28, 5, 0}, {U29, 5, 0},
};
const uint8_t N_UNITS = sizeof(UNITS) / sizeof(UNITS[0]);

Unit unit(uint8_t u) {
  Unit x;
  memcpy_P(&x, &UNITS[u], sizeof x);
  return x;
}

// Which unit is this word? 0xFF if none.
uint8_t unitOf(const char *word) {
  for (uint8_t u = 0; u < N_UNITS; u++) {
    PGM_P n = unit(u).names;
    while (pgm_read_byte(n)) {
      uint8_t i = 0;
      while (word[i] && pgm_read_byte(n + i) == word[i]) i++;
      char c = pgm_read_byte(n + i);
      if (!word[i] && (!c || c == ' ')) return u;
      while (pgm_read_byte(n) && pgm_read_byte(n) != ' ') n++;
      if (pgm_read_byte(n)) n++;
    }
  }
  return 0xFF;
}

// Print a unit's name: word 1 of its list is singular, word 2 is plural.
void printUnit(uint8_t u, bool plural, Print &out) {
  PGM_P c = unit(u).names;
  for (uint8_t skip = plural ? 2 : 1; skip; c++) if (pgm_read_byte(c) == ' ') skip--;
  for (; pgm_read_byte(c) && pgm_read_byte(c) != ' '; c++) out.print((char)pgm_read_byte(c));
}

// "5 miles to km", "70 f in c", "how many cups in a gallon". False if it isn't a conversion.
bool convert(const char *msg, Print &out) {
  char words[16][14];
  uint8_t nw = 0;
  for (const char *p = msg; *p && nw < 16;) {
    while (*p == ' ' || *p == '?' || *p == ',') p++;
    if (!*p) break;
    uint8_t n = 0;
    while (*p && *p != ' ' && *p != '?' && *p != ',') { if (n < 13) words[nw][n++] = *p; p++; }
    words[nw++][n] = '\0';
  }
  float amount = 1;
  uint8_t from = 0xFF, to = 0xFF;
  for (uint8_t i = 0; i < nw && to == 0xFF; i++) {
    char *w = words[i];
    bool number = isdigit(w[0]) || ((w[0] == '-' || w[0] == '.') && isdigit(w[1]));
    if (number) {
      amount = atof(w);
      while (*w && (isdigit(*w) || *w == '.' || *w == '-')) w++;  // "5km"
      if (!*w && i + 1 < nw) w = words[++i];
    }
    if (!number && !strcmp(w, "in")) continue;  // "cups in a gallon": "in" isn't inches here
    uint8_t u = unitOf(w);
    if (u == 0xFF) continue;
    if (from == 0xFF && (number || i == 0 || !strcmp(words[i - 1], "a") || !strcmp(words[i - 1], "an") ||
                         !strcmp(words[i - 1], "one"))) from = u;
    else if (from != 0xFF) to = u;
    else from = u;  // "how many cups in a gallon": first unit is the target
  }
  if (from == 0xFF || to == 0xFF) return false;
  if (!strncmp(msg, "how many", 8) || !strncmp(msg, "how much", 8)) {  // "how many X in a Y"
    uint8_t t = from; from = to; to = t;
  }
  if (unit(from).kind != unit(to).kind || from == to) return false;
  float r;
  if (unit(from).kind == 5) r = from == 28 ? amount * 1.8 + 32 : (amount - 32) / 1.8;
  else r = amount * unit(from).size / unit(to).size;
  printNumber(amount);
  out.print(' ');
  printUnit(from, amount != 1, out);
  out.print(F(" = "));
  printNumber(r);
  out.print(' ');
  printUnit(to, r != 1, out);
  return true;
}

void roleSetup() {}

void handle(char *msg, Print &out) {
  nout = &out;
  for (char *c = msg; *c; c++) *c = tolower(*c);
  if (!strncmp(msg, "wiki ", 5)) { lookup(WIKI_B, msg + 5, out); return; }
  if (!strncmp(msg, "do ", 3)) msg += 3;
  if (!strncmp(msg, "calc ", 5)) { calcLine(msg + 5); return; }
  convert(msg, out);  // anything else: answer only if it's a unit conversion
}
