"""Download short Wikipedia summaries into wiki_data.h for the Library helper,
so NanoChat can answer "who is ..." / "what is ..." with no internet.

Edit TOPICS (Wikipedia title: extra words people might use), then run:
    python3 make_wiki.py
Standard library only. Keep the total under ~16 KB so it fits in the Nano.
"""
import json
import re
import time
import urllib.error
import unicodedata
import urllib.parse
import urllib.request

MAX_CHARS = 140  # per entry; the boss shows replies up to 160 characters
UA = {"User-Agent": "NanoChat-wiki-maker/1.0 (Arduino hobby project)"}

TOPICS = {
    # space
    "Sun": "sun", "Moon": "moon", "Mercury (planet)": "mercury", "Venus": "venus", "Earth": "earth",
    "Mars": "mars", "Jupiter": "jupiter", "Saturn": "saturn", "Uranus": "uranus", "Neptune": "neptune",
    "Pluto": "pluto", "Black hole": "black hole", "Milky Way": "milky way", "Galaxy": "galaxy",
    "Star": "star,stars", "Comet": "comet", "Solar System": "solar system", "Apollo 11": "apollo 11,moon landing",
    # earth
    "Volcano": "volcano,volcanoes", "Earthquake": "earthquake", "Tornado": "tornado", "Tropical cyclone": "hurricane",
    "Lightning": "lightning", "Rainbow": "rainbow", "Glacier": "glacier", "Ocean": "ocean",
    "Pacific Ocean": "pacific ocean,pacific", "Mount Everest": "everest,tallest mountain",
    "Amazon River": "amazon river", "Nile": "nile", "Sahara": "sahara", "Antarctica": "antarctica",
    # living things
    "Photosynthesis": "photosynthesis", "DNA": "dna", "Cell (biology)": "cell", "Dinosaur": "dinosaur,dinosaurs",
    "Tyrannosaurus": "t rex,tyrannosaurus", "Shark": "shark,sharks", "Blue whale": "blue whale,whale,biggest animal",
    "Octopus": "octopus", "Elephant": "elephant", "Cheetah": "cheetah", "Penguin": "penguin",
    "Honey bee": "bee,bees", "Evolution": "evolution", "Virus": "virus", "Bacteria": "bacteria",
    "Heart": "heart", "Human brain": "brain",
    # science and tech
    "Gravity": "gravity", "Atom": "atom", "Electricity": "electricity", "Magnet": "magnet",
    "Light": "light", "Sound": "sound", "Water": "water", "Oxygen": "oxygen", "Computer": "computer",
    "Internet": "internet", "Arduino": "arduino", "Robot": "robot", "Artificial intelligence": "artificial intelligence,ai",
    "Transistor": "transistor", "Electric battery": "battery", "Pi": "pi", "Prime number": "prime number",
    # people
    "Albert Einstein": "einstein", "Isaac Newton": "newton", "Marie Curie": "marie curie,curie",
    "George Washington": "george washington,washington", "Abraham Lincoln": "lincoln",
    "Martin Luther King Jr.": "martin luther king,mlk", "Leonardo da Vinci": "da vinci,leonardo",
    "Thomas Edison": "edison", "Nikola Tesla": "tesla", "Ada Lovelace": "ada lovelace,lovelace",
    "Alan Turing": "turing", "Neil Armstrong": "neil armstrong,armstrong", "Galileo Galilei": "galileo",
    "Charles Darwin": "darwin", "Wright brothers": "wright brothers", "Wolfgang Amadeus Mozart": "mozart",
    "William Shakespeare": "shakespeare", "Cleopatra": "cleopatra", "Napoleon": "napoleon",
    # places and history
    "United States": "united states,usa,america", "Canada": "canada", "Mexico": "mexico", "China": "china",
    "Japan": "japan", "India": "india", "France": "france", "Egypt": "egypt", "Australia": "australia",
    "Europe": "europe", "Africa": "africa", "Great Pyramid of Giza": "pyramids,pyramid",
    "Eiffel Tower": "eiffel tower", "Great Wall of China": "great wall", "Statue of Liberty": "statue of liberty",
    "World War II": "world war 2,world war ii,ww2", "Titanic": "titanic", "Roman Empire": "roman empire,rome",
}


def ascii_only(text):
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    text = re.sub(r"\s*\([^)]*\)", "", text)  # drop "(born 1879 ...)" style asides
    return re.sub(r"\s+", " ", text).strip()


def summary(title):
    url = "https://en.wikipedia.org/api/rest_v1/page/summary/" + urllib.parse.quote(title.replace(" ", "_"))
    for attempt in range(5):  # Wikipedia asks us to slow down sometimes (HTTP 429)
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=10) as r:
                text = ascii_only(json.load(r).get("extract", ""))
            break
        except urllib.error.HTTPError as e:
            if e.code != 429 or attempt == 4:
                raise
            time.sleep(2 ** attempt)
    time.sleep(0.3)
    out = ""
    for sentence in re.split(r"(?<=[.!?])\s+", text):
        if len(out) + len(sentence) + 1 > MAX_CHARS:
            break
        out += sentence + " "
    out = out.strip() or text[:MAX_CHARS - 3].rsplit(" ", 1)[0] + "..."
    return out.replace("|", "/").replace('"', "'")


def main():
    entries = []
    for title, keys in TOPICS.items():
        try:
            text = summary(title)
        except Exception as e:
            print(f"skip {title}: {e}")
            continue
        entries.append(f"{keys}:{text}")
        print(f"{title}: {text}")
    data = "|".join(entries)
    with open("wiki_data.h", "w") as f:
        f.write(f"// Made by make_wiki.py: {len(entries)} Wikipedia summaries, {len(data)} bytes.\n")
        f.write("// Format: key,key:summary|key:summary|...  (keys are lowercase)\n")
        f.write("// Text from Wikipedia (https://en.wikipedia.org), shortened. Licensed CC BY-SA 4.0:\n")
        f.write("// https://creativecommons.org/licenses/by-sa/4.0/  -- this file only; the code is MIT.\n")
        f.write("const char WIKI[] PROGMEM =\n")
        for i in range(0, len(entries)):
            sep = "|" if i < len(entries) - 1 else ""
            f.write('  "' + entries[i].replace("\\", "\\\\") + sep + '"\n')
        f.write(";\n")
    print(f"\nwrote wiki_data.h: {len(entries)} topics, {len(data)} bytes")


if __name__ == "__main__":
    main()
