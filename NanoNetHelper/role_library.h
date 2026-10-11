// ROLE 4: Library. Jokes, facts, riddles, an offline encyclopedia and survival
// tips, all kept in flash so the boss doesn't have to fit them.
// Messages: "joke", "fact", "riddle", "survival tip", "wiki <question>".
// Entries are separated by '|'. Add your own; keep each under ~150 characters.
// wiki_data.h is made by make_wiki.py; survival_data.h is written by hand.
#include "wiki_data.h"
#include "survival_data.h"

#define ROLE_NAME F("Library")

const char JOKES[] PROGMEM =
  "Why did the robot go on vacation? It needed to recharge.|"
  "Why do programmers prefer dark mode? Because light attracts bugs.|"
  "What do you call a sleeping bull? A bulldozer.|"
  "Why was the math book sad? It had too many problems.|"
  "Why did the scarecrow win an award? He was outstanding in his field.|"
  "What do you call a fish with no eyes? A fsh.|"
  "Why can't a bicycle stand up by itself? It's two tired.|"
  "What did the ocean say to the beach? Nothing, it just waved.|"
  "Why did the computer go to the doctor? It had a virus.|"
  "What do you call a bear with no teeth? A gummy bear.|"
  "Why don't skeletons fight each other? They don't have the guts.|"
  "What do you call cheese that isn't yours? Nacho cheese.|"
  "Why did the cookie go to the nurse? It felt crummy.|"
  "What do you call a dinosaur that is sleeping? A dino-snore.|"
  "Why did the golfer bring two pairs of pants? In case he got a hole in one.|"
  "What has ears but cannot hear? A cornfield.|"
  "Why did the tomato turn red? It saw the salad dressing.|"
  "What do you call a pig that does karate? A pork chop.|"
  "Why are ghosts bad liars? You can see right through them.|"
  "What did one wall say to the other? I'll meet you at the corner.|"
  "Why did the student eat his homework? The teacher said it was a piece of cake.|"
  "What do you call a cow with no legs? Ground beef.|"
  "Why do bees have sticky hair? They use honeycombs.|"
  "What kind of tree fits in your hand? A palm tree.|"
  "Why was the broom late? It overswept.|"
  "What did the robot say to the gas pump? Take your finger out of your ear and listen to me.|"
  "How does a computer get drunk? It takes screenshots.|"
  "Why did the Arduino cross the road? To get to the other side of the breadboard.|"
  "What is a robot's favorite snack? Computer chips.|"
  "Why was the computer cold? It left its Windows open.|"
  "What do you call a lazy kangaroo? A pouch potato.|"
  "Why did the banana go to the doctor? It wasn't peeling well.|"
  "What do you call an alligator in a vest? An investigator.|"
  "Why do cows wear bells? Because their horns don't work.|"
  "What do elves learn in school? The elf-abet.|"
  "Why did the music teacher need a ladder? To reach the high notes.|"
  "What do you call a snowman with a six-pack? An abdominal snowman.|"
  "Why did the chicken join a band? Because it had the drumsticks.|"
  "What do you call a duck that gets all A's? A wise quacker.|"
  "Why can't you give Elsa a balloon? She will let it go.|"
  "What did the left eye say to the right eye? Between you and me, something smells.|"
  "Why do fish live in salt water? Because pepper makes them sneeze.|"
  "What has four wheels and flies? A garbage truck.|"
  "Why did the robot get angry? Someone kept pushing its buttons.|"
  "What do you call a belt made of watches? A waist of time.|"
  "Why are robots never afraid? They have nerves of steel.|"
  "What do you call a magic dog? A labracadabrador.|"
  "Why did the phone wear glasses? It lost its contacts.|"
  "What did the zero say to the eight? Nice belt.|"
  "Why do seagulls fly over the sea? If they flew over the bay they would be bagels.";

const char FACTS[] PROGMEM =
  "Octopuses have three hearts and blue blood.|"
  "Honey never spoils. 3000-year-old honey from Egypt is still edible.|"
  "A day on Venus is longer than its year.|"
  "Bananas are berries, but strawberries aren't.|"
  "The first computer bug was a real moth, found in 1947.|"
  "Sharks existed before trees did.|"
  "A group of flamingos is called a flamboyance.|"
  "Wombat poop is shaped like little cubes.|"
  "Sloths can hold their breath longer than dolphins can.|"
  "The Eiffel Tower can be about 15 cm taller in summer because metal expands in heat.|"
  "There are more stars in the universe than grains of sand on all of Earth's beaches.|"
  "A bolt of lightning is about five times hotter than the surface of the sun.|"
  "Cows have best friends and get stressed when they are apart.|"
  "Your brain uses about 20 percent of your body's energy.|"
  "A blue whale's heart is about the size of a small car.|"
  "Butterflies taste with their feet.|"
  "Some turtles can breathe through their butts.|"
  "The moon has moonquakes.|"
  "An ostrich's eye is bigger than its brain.|"
  "Hot water can freeze faster than cold water. It's called the Mpemba effect.|"
  "Koalas sleep up to 22 hours a day.|"
  "Sea otters hold hands while they sleep so they don't drift apart.|"
  "The Arduino Nano's chip runs 16 million steps every second.|"
  "There are more possible chess games than atoms in the known universe.|"
  "A snail can sleep for three years.|"
  "Jupiter is so big that all the other planets could fit inside it.|"
  "Humans share about 60 percent of their DNA with bananas.|"
  "The shortest war in history lasted about 38 minutes.|"
  "Penguins propose to their mates with a pebble.|"
  "Light from the sun takes about 8 minutes to reach Earth.|"
  "A cloud can weigh more than a million pounds.|"
  "Crows can recognize human faces and remember them for years.|"
  "Earth is the only planet not named after a god.|"
  "Polar bears have black skin under their white fur.|"
  "The first webcam was used to watch a coffee pot.|"
  "Your nose can remember about 50,000 different smells.|"
  "A teaspoon of a neutron star would weigh about a billion tons.|"
  "Giraffes have the same number of neck bones as humans: seven.|"
  "Some cats are allergic to people.|"
  "Water can boil and freeze at the same time. It's called the triple point.|"
  "Mars has the tallest volcano in the solar system, Olympus Mons.|"
  "The heart of a shrimp is located in its head.|"
  "Rubber ducks were first made in the 1800s.|"
  "Bees can recognize human faces.|"
  "Saturn would float in a big enough bathtub, because it is less dense than water.|"
  "The longest time between two twins being born is 87 days.|"
  "A day on Earth used to be only about 6 hours long, billions of years ago.|"
  "Pineapples take about two years to grow.|"
  "The fingerprints of a koala are almost the same as a human's.|"
  "Electric eels can make shocks of up to 600 volts.|"
  "The Great Wall of China is not visible from space with the naked eye.|"
  "Some frogs can freeze solid in winter and thaw out alive in spring.|"
  "Dolphins give each other names using special whistles.|"
  "A hummingbird's heart can beat over 1,200 times a minute.|"
  "There is enough DNA in your body to stretch to the sun and back many times.|"
  "Lobsters were once so common they were fed to prisoners.|"
  "The word robot comes from a Czech word meaning forced work.|"
  "Venus spins backwards compared to most planets.|"
  "Cheetahs can go from 0 to 60 mph in about 3 seconds.|"
  "Your stomach gets a new lining every few days so it doesn't digest itself.";

const char RIDDLES[] PROGMEM =
  "What has keys but can't open locks? ... A keyboard!|"
  "What gets wetter the more it dries? ... A towel!|"
  "What has a head and a tail but no body? ... A coin!|"
  "The more you take, the more you leave behind. What am I? ... Footsteps!|"
  "What can you catch but not throw? ... A cold!|"
  "What has hands but can't clap? ... A clock!|"
  "What has to be broken before you can use it? ... An egg!|"
  "I'm tall when I'm young and short when I'm old. What am I? ... A candle!|"
  "What month has 28 days? ... All of them!|"
  "What is full of holes but still holds water? ... A sponge!|"
  "What goes up but never comes down? ... Your age!|"
  "What can you hear but not touch or see? ... Your voice!|"
  "What has many teeth but can't bite? ... A comb!|"
  "What has one eye but can't see? ... A needle!|"
  "What runs but never walks? ... Water!|"
  "What has a neck but no head? ... A bottle!|"
  "What can travel around the world while staying in a corner? ... A stamp!|"
  "What has legs but doesn't walk? ... A table!|"
  "What belongs to you but others use it more? ... Your name!|"
  "What can fill a room but takes up no space? ... Light!|"
  "If you drop me I crack, but smile at me and I smile back. What am I? ... A mirror!|"
  "What has a thumb and four fingers but isn't alive? ... A glove!|"
  "What gets bigger the more you take away? ... A hole!|"
  "What has a bottom at the top? ... Your legs!|"
  "What kind of band never plays music? ... A rubber band!|"
  "What has words but never speaks? ... A book!|"
  "What comes once in a minute, twice in a moment, but never in a thousand years? ... The letter M!|"
  "Where does today come before yesterday? ... In the dictionary!|"
  "What invention lets you look right through a wall? ... A window!|"
  "What is always in front of you but can't be seen? ... The future!|"
  "What has a ring but no finger? ... A phone!|"
  "What starts with T, ends with T, and has T in it? ... A teapot!|"
  "What can you break without touching it? ... A promise!|"
  "What goes through cities and fields but never moves? ... A road!|"
  "What has 13 hearts but no other organs? ... A deck of cards!";

void roleSetup() { randomSeed(analogRead(A7) ^ (analogRead(A6) << 10) ^ micros()); }

// Print a random '|'-separated entry, never the same as last time.
void pick(PGM_P list, Print &out) {
  static uint16_t last = 0xFFFF;
  uint16_t count = 1;
  for (PGM_P c = list; pgm_read_byte(c); c++) if (pgm_read_byte(c) == '|') count++;
  uint16_t n;
  do n = random(count); while (n == last && count > 1);
  last = n;
  PGM_P c = list;
  while (n) if (pgm_read_byte(c++) == '|') n--;
  for (char ch; (ch = pgm_read_byte(c)) && ch != '|'; c++) out.print(ch);
}

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

// Like pick(), but skips the "keys:" part of each entry.
void pickTip(Print &out) {
  uint8_t count = 1;
  for (PGM_P c = SURVIVAL; pgm_read_byte(c); c++) if (pgm_read_byte(c) == '|') count++;
  PGM_P c = SURVIVAL;
  for (uint8_t n = random(count); n; c++) if (pgm_read_byte(c) == '|') n--;
  while (pgm_read_byte(c) != ':') c++;
  for (char ch; (ch = pgm_read_byte(++c)) && ch != '|';) out.print(ch);
}

void handle(char *msg, Print &out) {
  for (char *c = msg; *c; c++) *c = tolower(*c);
  bool safety = strstr(msg, "surviv") || strstr(msg, "safe") || strstr(msg, "tip") ||
                strstr(msg, "what do i do") || strstr(msg, "what should i do") || strstr(msg, "first aid") ||
                strstr(msg, "how do i") || strstr(msg, "emergency");
  if (!strncmp(msg, "wiki ", 5) || safety) {
    // Safety questions check the tips first ("earthquake tip"), others the encyclopedia.
    if (safety ? lookup(SURVIVAL, msg, out) || lookup(WIKI, msg, out)
               : lookup(WIKI, msg, out) || lookup(SURVIVAL, msg, out)) return;
    if (strstr(msg, "surviv") || strstr(msg, "tip")) pickTip(out);  // "survival tip": a random one
    return;  // nothing found: empty reply, so the boss says it needs the internet
  }
  if (strstr(msg, "riddle") || strstr(msg, "puzzle")) pick(RIDDLES, out);
  else if (strstr(msg, "fact")) pick(FACTS, out);
  else if (strstr(msg, "joke")) pick(JOKES, out);
  // anything else: empty reply, so the boss handles it itself
}
