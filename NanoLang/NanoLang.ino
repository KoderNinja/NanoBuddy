// NanoLang: a tiny programming language for the Arduino Nano.
// Talk to it over Serial at 9600 baud.
//
//   x = 5                      assign
//   x * 2 + 1                  evaluate and show
//   print "x is", x            print text and values
//   input n                    ask for a value
//   if x > 3 ... else ... end  branch
//   while i < 10 ... end       loop
//   led 1 / led 0              onboard LED
//   wait 500                   pause in ms
//
// Program mode: `prog` to start typing a program, `.` to finish,
// then `run`, `list`, `new`, `save` (to EEPROM), `load`.
// Send any line while a program runs to stop it.

#include <EEPROM.h>
#include <math.h>

const uint8_t MAX_VARS = 16;
const uint8_t NAME_LEN = 8;
const uint8_t LINE_LEN = 64;
const uint16_t PROG_SIZE = 448;   // program + room for one immediate block
const uint16_t PROG_MAX = PROG_SIZE - LINE_LEN * 2;
const uint8_t MAX_DEPTH = 8;
const uint8_t EE_MAGIC = 0x4E;

struct Var { char name[NAME_LEN + 1]; float value; };
Var vars[MAX_VARS];
uint8_t varCount = 0;

char prog[PROG_SIZE];   // lines, each ending in '\n'
uint16_t progLen = 0;   // stored program occupies prog[0..progLen)
uint16_t scratchLen = 0;// immediate block lives right after it
uint8_t pendingDepth = 0;
bool entering = false;

char line[LINE_LEN + 1];  // serial input
uint8_t lineLen = 0;
char exec[LINE_LEN + 1];  // line being executed

const char *p;
const char *err;
bool lastWasCR = false;   // for \r\n line endings

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

// ---------- lexing helpers ----------

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

// Consume `word` if it's next (as a whole word).
bool matchWord(const char *word) {
  skipSpaces();
  uint8_t n = strlen(word);
  if (strncasecmp(p, word, n) == 0 && !isalnum(p[n]) && p[n] != '_') {
    p += n;
    return true;
  }
  return false;
}

bool isKeyword(const char *s) {
  static const char *const kws[] = {
    "if", "else", "end", "while", "print", "input", "led", "wait",
    "and", "or", "not", "pi", "e"
  };
  for (const char *k : kws) if (!strcmp(s, k)) return true;
  return false;
}

// ---------- expressions ----------
// cond    := andE ('or' andE)*
// andE    := notE ('and' notE)*
// notE    := 'not' notE | compare
// compare := expr (('=='|'!='|'<'|'>'|'<='|'>=') expr)?
// expr    := term (('+'|'-') term)*
// term    := power (('*'|'/'|'%') power)*
// power   := unary ('^' power)?
// unary   := '-' unary | primary
// primary := number | name | func '(' cond ')' | '(' cond ')'

float cond();

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
  if (!strcmp(name, "rand"))  return random((long)x);   // 0 .. x-1
  fail("unknown function");
  return 0;
}

float primary() {
  skipSpaces();
  if (*p == '(') {
    p++;
    float v = cond();
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
      float arg = cond();
      skipSpaces();
      if (*p == ')') p++; else fail("missing )");
      return callFunc(name, arg);
    }
    if (!strcmp(name, "pi")) return M_PI;
    if (!strcmp(name, "e"))  return M_E;
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

float compare() {
  float a = expr();
  skipSpaces();
  if (p[0] == '=' && p[1] == '=') { p += 2; return a == expr(); }
  if (p[0] == '!' && p[1] == '=') { p += 2; return a != expr(); }
  if (p[0] == '<' && p[1] == '=') { p += 2; return a <= expr(); }
  if (p[0] == '>' && p[1] == '=') { p += 2; return a >= expr(); }
  if (p[0] == '<') { p++; return a < expr(); }
  if (p[0] == '>') { p++; return a > expr(); }
  return a;
}

float notE() {
  if (matchWord("not")) return notE() == 0;
  return compare();
}

float andE() {
  float v = notE();
  while (matchWord("and")) { float r = notE(); v = (v != 0 && r != 0); }
  return v;
}

float cond() {
  float v = andE();
  while (matchWord("or")) { float r = andE(); v = (v != 0 || r != 0); }
  return v;
}

// ---------- output ----------

void printNumber(float v) {
  if (isnan(v)) { Serial.print(F("nan")); return; }
  if (isinf(v)) { Serial.print(v > 0 ? F("inf") : F("-inf")); return; }
  if (v == (long)v && fabs(v) < 1e9) { Serial.print((long)v); return; }
  // up to 4 decimals (32-bit float isn't accurate past that)
  char buf[20];
  dtostrf(v, 0, 4, buf);
  char *end = buf + strlen(buf) - 1;
  while (*end == '0') *end-- = '\0';
  if (*end == '.') *end = '\0';
  Serial.print(buf);
}

void printHelp() {
  Serial.println(F("NanoLang"));
  Serial.println(F(" x = 5 | x*2+1 | print \"hi\", x | input n"));
  Serial.println(F(" if c ... else ... end | while c ... end"));
  Serial.println(F(" led 1 | wait 500"));
  Serial.println(F(" ops: + - * / % ^ == != < > <= >= and or not"));
  Serial.println(F(" funcs: sqrt abs sin cos tan ln log round floor ceil rand"));
  Serial.println(F(" cmds: prog . run list new save load vars clear help"));
}

// ---------- program structure ----------

// +1 for lines that open a block, -1 for `end`, 0 otherwise.
int8_t blockDelta(const char *s) {
  p = s;
  if (matchWord("if") || matchWord("while")) return 1;
  if (matchWord("end")) return -1;
  return 0;
}

// Copy the line at prog[pos] into exec; return position of the next line.
uint16_t fetchLine(uint16_t pos, uint16_t end) {
  uint8_t n = 0;
  while (pos < end && prog[pos] != '\n') {
    if (n < LINE_LEN) exec[n++] = prog[pos];
    pos++;
  }
  exec[n] = '\0';
  return pos < end ? pos + 1 : pos;
}

// From `pos`, skip to just past the matching `end` (or `else`, if allowed).
// Sets *hitElse when it stopped at an else. Returns 0xFFFF if no end found.
uint16_t skipBlock(uint16_t pos, uint16_t end, bool stopAtElse, bool *hitElse) {
  uint8_t depth = 0;
  *hitElse = false;
  while (pos < end) {
    pos = fetchLine(pos, end);
    int8_t d = blockDelta(exec);
    if (d > 0) depth++;
    else if (d < 0) { if (depth == 0) return pos; depth--; }
    else if (depth == 0 && stopAtElse) {
      p = exec;
      if (matchWord("else")) { *hitElse = true; return pos; }
    }
  }
  return 0xFFFF;
}

uint8_t lineNumber(uint16_t start, uint16_t pos) {
  uint8_t n = 1;
  for (uint16_t i = start; i < pos; i++) if (prog[i] == '\n') n++;
  return n;
}

bool stopRequested() {
  if (Serial.peek() == '\n' && lastWasCR) { Serial.read(); lastWasCR = false; }
  if (!Serial.available()) return false;
  while (Serial.available()) { Serial.read(); delay(2); }
  return true;
}

// Feed one received char into the line buffer, echoing it.
// Returns true when a full line is ready. Accepts \r, \n or \r\n endings.
bool feedChar(char c) {
  if (c == '\n' && lastWasCR) { lastWasCR = false; return false; }
  lastWasCR = (c == '\r');
  if (c == '\r' || c == '\n') {
    line[lineLen] = '\0';
    Serial.println();
    return true;
  }
  if (c == 8 || c == 127) {          // backspace
    if (lineLen) { lineLen--; Serial.print(F("\b \b")); }
    return false;
  }
  if (lineLen < LINE_LEN && c >= ' ') { line[lineLen++] = c; Serial.print(c); }
  return false;
}

void readLineBlocking() {
  lineLen = 0;
  for (;;) if (Serial.available() && feedChar(Serial.read())) break;
}

// Execute the lines in prog[start..end). `echo` shows bare expression results.
void run(uint16_t start, uint16_t end, bool echo) {
  struct Frame { uint16_t pos; bool isWhile; };
  Frame stack[MAX_DEPTH];
  uint8_t sp = 0;
  uint16_t pc = start;

  while (pc < end) {
    if (stopRequested()) { Serial.println(F("stopped")); return; }

    uint16_t here = pc;
    pc = fetchLine(pc, end);
    err = nullptr;
    p = exec;
    skipSpaces();
    if (!*p || *p == '#') continue;   // blank line or comment

    if (matchWord("if")) {
      float c = cond();
      skipSpaces();
      if (!err && *p) fail("unexpected character");
      if (!err) {
        if (c != 0) {
          if (sp >= MAX_DEPTH) fail("nested too deep");
          else stack[sp++] = { here, false };
        } else {
          bool hitElse;
          uint16_t next = skipBlock(pc, end, true, &hitElse);
          if (next == 0xFFFF) fail("if without end");
          else {
            pc = next;
            if (hitElse) {
              if (sp >= MAX_DEPTH) fail("nested too deep");
              else stack[sp++] = { here, false };
            }
          }
        }
      }
    } else if (matchWord("while")) {
      float c = cond();
      skipSpaces();
      if (!err && *p) fail("unexpected character");
      if (!err) {
        if (c != 0) {
          if (sp >= MAX_DEPTH) fail("nested too deep");
          else stack[sp++] = { here, true };
        } else {
          bool hitElse;
          uint16_t next = skipBlock(pc, end, false, &hitElse);
          if (next == 0xFFFF) fail("while without end");
          else pc = next;
        }
      }
    } else if (matchWord("else")) {
      if (!sp || stack[sp - 1].isWhile) fail("else without if");
      else {
        bool hitElse;
        uint16_t next = skipBlock(pc, end, false, &hitElse);
        if (next == 0xFFFF) fail("if without end");
        else { pc = next; sp--; }
      }
    } else if (matchWord("end")) {
      if (!sp) fail("end without if/while");
      else {
        Frame f = stack[--sp];
        if (f.isWhile) pc = f.pos;   // re-check the condition
      }
    } else if (matchWord("print")) {
      bool first = true;
      for (;;) {
        skipSpaces();
        if (!*p) break;
        if (!first) Serial.print(' ');
        first = false;
        if (*p == '"') {
          p++;
          while (*p && *p != '"') Serial.print(*p++);
          if (*p == '"') p++; else fail("missing \"");
        } else {
          float v = cond();
          if (!err) printNumber(v);
        }
        if (err) break;
        skipSpaces();
        if (*p == ',') p++;
        else if (*p) { fail("expected ,"); break; }
      }
      Serial.println();
    } else if (matchWord("input")) {
      char name[NAME_LEN + 1];
      if (!readName(name) || isKeyword(name)) fail("input needs a variable");
      else {
        Serial.print(name); Serial.print(F("? "));
        readLineBlocking();
        const char *saveP = p;
        p = line;
        float v = cond();
        skipSpaces();
        if (!err && *p) fail("bad input");
        p = saveP;
        if (!err && !setVar(name, v)) fail("too many variables");
      }
    } else if (matchWord("led")) {
      float v = cond();
      if (!err) digitalWrite(LED_BUILTIN, v != 0 ? HIGH : LOW);
    } else if (matchWord("wait")) {
      float v = cond();
      if (!err) {
        unsigned long until = millis() + (unsigned long)max(v, 0.0f);
        while ((long)(until - millis()) > 0)
          if (Serial.available()) break;   // stop check happens next line
      }
    } else {
      // assignment or bare expression
      char target[NAME_LEN + 1] = "";
      const char *save = p;
      if (readName(target)) {
        skipSpaces();
        if (*p == '=' && p[1] != '=') p++;
        else { p = save; target[0] = '\0'; }
      }
      if (target[0] && isKeyword(target)) fail("can't assign to a keyword");
      float v = 0;
      if (!err) v = cond();
      skipSpaces();
      if (!err && *p) fail("unexpected character");
      if (!err && target[0] && !setVar(target, v)) fail("too many variables");
      if (!err && echo && !target[0]) {
        setVar("ans", v);
        printNumber(v); Serial.println();
      } else if (!err && echo && target[0] && end - start == pc - here) {
        // single-line assignment typed directly: confirm it
        Serial.print(target); Serial.print(F(" = ")); printNumber(v); Serial.println();
      }
    }

    if (err) {
      Serial.print(F("error"));
      if (!echo) { Serial.print(F(" line ")); Serial.print(lineNumber(start, here)); }
      Serial.print(F(": ")); Serial.println(err);
      return;
    }
  }
  if (sp) Serial.println(F("error: missing end"));
}

// ---------- commands ----------

void listProgram() {
  if (!progLen) { Serial.println(F("(empty)")); return; }
  uint16_t pos = 0;
  uint8_t n = 1;
  while (pos < progLen) {
    pos = fetchLine(pos, progLen);
    if (n < 10) Serial.print(' ');
    Serial.print(n++); Serial.print(F("  ")); Serial.println(exec);
  }
}

void saveProgram() {
  EEPROM.update(0, EE_MAGIC);
  EEPROM.update(1, progLen & 0xFF);
  EEPROM.update(2, progLen >> 8);
  for (uint16_t i = 0; i < progLen; i++) EEPROM.update(3 + i, prog[i]);
  Serial.print(F("saved ")); Serial.print(progLen); Serial.println(F(" bytes"));
}

void loadProgram() {
  uint16_t len = EEPROM.read(1) | (EEPROM.read(2) << 8);
  if (EEPROM.read(0) != EE_MAGIC || len > PROG_MAX) {
    Serial.println(F("nothing saved"));
    return;
  }
  for (uint16_t i = 0; i < len; i++) prog[i] = EEPROM.read(3 + i);
  progLen = len;
  Serial.print(F("loaded ")); Serial.print(len); Serial.println(F(" bytes"));
}

bool appendTo(uint16_t base, uint16_t *len, uint16_t limit, const char *s) {
  uint16_t n = strlen(s);
  if (base + *len + n + 1 > limit) return false;
  memcpy(prog + base + *len, s, n);
  *len += n;
  prog[base + (*len)++] = '\n';
  return true;
}

void prompt() {
  if (entering) Serial.print(F("prog> "));
  else if (pendingDepth) Serial.print(F("...  "));
  else Serial.print(F("> "));
}

void handleLine(char *s) {
  while (*s == ' ') s++;

  if (entering) {
    if (!strcmp(s, ".")) {
      entering = false;
      Serial.print(F("ok, ")); Serial.print(progLen); Serial.println(F(" bytes. type run"));
    } else if (!appendTo(0, &progLen, PROG_MAX, s)) {
      Serial.println(F("error: program full"));
    }
    return;
  }

  if (pendingDepth) {
    if (!appendTo(progLen, &scratchLen, PROG_SIZE, s)) {
      Serial.println(F("error: block too long"));
      pendingDepth = 0; scratchLen = 0;
      return;
    }
    pendingDepth += blockDelta(s);
    if (!pendingDepth) {
      run(progLen, progLen + scratchLen, true);
      scratchLen = 0;
    }
    return;
  }

  if (!*s) return;
  if (!strcmp(s, "help"))  { printHelp(); return; }
  if (!strcmp(s, "vars")) {
    if (!varCount) Serial.println(F("(no variables)"));
    for (uint8_t i = 0; i < varCount; i++) {
      Serial.print(vars[i].name); Serial.print(F(" = "));
      printNumber(vars[i].value); Serial.println();
    }
    return;
  }
  if (!strcmp(s, "clear")) { varCount = 0; Serial.println(F("ok")); return; }
  if (!strcmp(s, "new"))   { progLen = 0; Serial.println(F("ok")); return; }
  if (!strcmp(s, "prog"))  {
    entering = true;
    Serial.println(F("type lines, finish with ."));
    return;
  }
  if (!strcmp(s, "list"))  { listProgram(); return; }
  if (!strcmp(s, "run"))   { run(0, progLen, false); return; }
  if (!strcmp(s, "save"))  { saveProgram(); return; }
  if (!strcmp(s, "load"))  { loadProgram(); return; }

  scratchLen = 0;
  appendTo(progLen, &scratchLen, PROG_SIZE, s);
  if (blockDelta(s) > 0) { pendingDepth = 1; return; }
  run(progLen, progLen + scratchLen, true);
  scratchLen = 0;
}

void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  randomSeed(analogRead(A7));
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
