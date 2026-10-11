// NanoCalc: a tiny calculator language with variables.
// Type lines over Serial (9600 baud), e.g.
//   x = 5
//   y = x * 3 + 2
//   y ^ 2 / (x - 1)
//   sqrt(y) + abs(-3)
// Commands: vars, clear, help

#include <Wire.h>
#include <math.h>

const uint8_t MAX_VARS = 16;
const uint8_t NAME_LEN = 8;
const uint8_t LINE_LEN = 64;

struct Var { char name[NAME_LEN + 1]; float value; };
Var vars[MAX_VARS];
uint8_t varCount = 0;

char line[LINE_LEN + 1];
uint8_t lineLen = 0;

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
  if (isnan(v)) { Serial.print(F("nan")); return; }
  if (isinf(v)) { Serial.print(v > 0 ? F("inf") : F("-inf")); return; }
  if (v == (long)v && fabs(v) < 1e9) { Serial.print((long)v); return; }
  // up to 4 decimals (32-bit float isn't accurate past that), trailing zeros trimmed
  char buf[20];
  dtostrf(v, 0, 4, buf);
  char *end = buf + strlen(buf) - 1;
  while (*end == '0') *end-- = '\0';
  if (*end == '.') *end = '\0';
  Serial.print(buf);
}

void printHelp() {
  Serial.println(F("NanoCalc - math with variables"));
  Serial.println(F("  x = 5          set a variable"));
  Serial.println(F("  x * 2 + 1      evaluate"));
  Serial.println(F("  ops: + - * / % ^ ( )"));
  Serial.println(F("  funcs: sqrt abs sin cos tan ln log round floor ceil"));
  Serial.println(F("  consts: pi e ans (last result)"));
  Serial.println(F("  cmds: vars  clear  help"));
}

// ---------- line handling ----------

void runLine(char *s) {
  while (*s == ' ') s++;
  if (!*s) return;

  if (!strcmp(s, "help"))  { printHelp(); return; }
  if (!strcmp(s, "clear")) { varCount = 0; Serial.println(F("ok")); return; }
  if (!strcmp(s, "vars")) {
    if (!varCount) Serial.println(F("(no variables)"));
    for (uint8_t i = 0; i < varCount; i++) {
      Serial.print(vars[i].name); Serial.print(F(" = "));
      printNumber(vars[i].value); Serial.println();
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
    Serial.println(F("error: can't assign to a constant"));
    return;
  }

  float v = expr();
  skipSpaces();
  if (!err && *p) fail("unexpected character");

  if (err) {
    Serial.print(F("error: ")); Serial.println(err);
    return;
  }
  if (target[0] && !setVar(target, v)) {
    Serial.println(F("error: too many variables"));
    return;
  }
  setVar("ans", v);

  if (target[0]) { Serial.print(target); Serial.print(F(" = ")); }
  printNumber(v);
  Serial.println();
}

void scanI2C() {
  Wire.begin();
  Serial.print(F("i2c devices:"));
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F(" 0x")); if (addr < 16) Serial.print('0');
      Serial.print(addr, HEX); found++;
    }
  }
  if (!found) Serial.print(F(" none"));
  Serial.println();
}

void setup() {
  Serial.begin(9600);
  scanI2C();
  printHelp();
  Serial.print(F("> "));
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[lineLen] = '\0';
      Serial.println(line);
      runLine(line);
      lineLen = 0;
      Serial.print(F("> "));
    } else if (lineLen < LINE_LEN) {
      line[lineLen++] = c;
    }
  }
}
