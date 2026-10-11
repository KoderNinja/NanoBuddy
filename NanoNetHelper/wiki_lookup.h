// Shared by the helpers that hold part of the offline encyclopedia (wiki_a/b/c.h)
// and the survival tips. Entries look like "key,key:text|key:text|...".
#pragma once

// Find the entry whose key appears in msg (whole words; longest key wins) and print
// its text. Returns false if no key matches.
bool lookup(PGM_P list, const char *msg, Print &out) {
  PGM_P best = nullptr;
  uint8_t bestLen = 0;
  PGM_P e = list;
  while (pgm_read_byte(e)) {
    PGM_P text = e;
    while (pgm_read_byte(text) != ':') text++;  // keys end at ':'
    for (PGM_P k = e; k < text;) {
      char key[32];
      uint8_t n = 0;
      while (k < text && pgm_read_byte(k) != ',' && n < 31) key[n++] = pgm_read_byte(k++);
      key[n] = '\0';
      k++;
      for (const char *hit = strstr(msg, key); hit; hit = strstr(hit + 1, key)) {
        bool whole = (hit == msg || !isalnum(hit[-1])) && !isalnum(hit[n]);
        if (whole && n > bestLen) { best = text + 1; bestLen = n; }
      }
    }
    while (pgm_read_byte(e) && pgm_read_byte(e) != '|') e++;  // next entry
    if (pgm_read_byte(e)) e++;
  }
  if (!best) return false;
  for (char c; (c = pgm_read_byte(best)) && c != '|'; best++) out.print(c);
  return true;
}

