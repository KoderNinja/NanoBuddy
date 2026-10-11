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

MAX_CHARS = 230  # per entry; helpers and the boss pass replies up to 240 characters
UA = {"User-Agent": "NanoChat-wiki-maker/1.0 (Arduino hobby project)"}

TOPICS = {
    # space
    "Sun": "sun", "Moon": "moon", "Mercury (planet)": "mercury", "Venus": "venus", "Earth": "earth",
    "Mars": "mars", "Jupiter": "jupiter", "Saturn": "saturn", "Uranus": "uranus", "Neptune": "neptune",
    "Pluto": "pluto", "Black hole": "black hole", "Milky Way": "milky way", "Galaxy": "galaxy",
    "Star": "star,stars", "Comet": "comet", "Solar System": "solar system", "Apollo 11": "apollo 11,moon landing",
    "International Space Station": "space station,iss", "Asteroid": "asteroid", "Eclipse": "eclipse",
    "NASA": "nasa", "Astronaut": "astronaut",
    # earth and weather
    "Volcano": "volcano,volcanoes", "Earthquake": "earthquake", "Tornado": "tornado", "Tropical cyclone": "hurricane",
    "Lightning": "lightning", "Rainbow": "rainbow", "Glacier": "glacier", "Ocean": "ocean",
    "Pacific Ocean": "pacific ocean,pacific", "Atlantic Ocean": "atlantic", "Mount Everest": "everest,tallest mountain",
    "Amazon River": "amazon river", "Amazon rainforest": "rainforest,amazon rainforest", "Nile": "nile",
    "Sahara": "sahara", "Antarctica": "antarctica", "Arctic": "arctic,north pole", "Desert": "desert",
    "Climate change": "climate change,global warming", "Water cycle": "water cycle", "Cloud": "cloud,clouds",
    "Snow": "snow", "Tsunami": "tsunami", "Grand Canyon": "grand canyon",
    # living things
    "Photosynthesis": "photosynthesis", "DNA": "dna", "Cell (biology)": "cell", "Dinosaur": "dinosaur,dinosaurs",
    "Tyrannosaurus": "t rex,tyrannosaurus", "Shark": "shark,sharks", "Blue whale": "blue whale,whale,biggest animal",
    "Octopus": "octopus", "Elephant": "elephant", "Cheetah": "cheetah", "Penguin": "penguin",
    "Honey bee": "bee,bees", "Evolution": "evolution", "Virus": "virus", "Bacteria": "bacteria",
    "Lion": "lion", "Tiger": "tiger", "Dog": "dog,dogs", "Cat": "cat,cats", "Horse": "horse", "Dolphin": "dolphin",
    "Snake": "snake,snakes", "Spider": "spider", "Butterfly": "butterfly", "Frog": "frog", "Owl": "owl",
    "Bear": "bear", "Wolf": "wolf", "Giraffe": "giraffe", "Kangaroo": "kangaroo", "Panda": "giant panda,panda",
    "Tree": "tree,trees", "Mushroom": "mushroom", "Ant": "ant,ants",
    # human body and health
    "Heart": "heart", "Human brain": "brain", "Lung": "lungs,lung", "Skeleton": "skeleton,bones",
    "Blood": "blood", "Muscle": "muscle,muscles", "Sleep": "sleep", "Immune system": "immune system",
    "Vitamin": "vitamin,vitamins", "Common cold": "cold,common cold", "Influenza": "flu,influenza",
    "Allergy": "allergy,allergies", "Tooth": "teeth,tooth", "Nutrition": "nutrition,healthy eating",
    # science and tech
    "Gravity": "gravity", "Atom": "atom", "Electricity": "electricity", "Magnet": "magnet,magnets",
    "Light": "light", "Sound": "sound", "Water": "water", "Oxygen": "oxygen", "Carbon dioxide": "carbon dioxide",
    "Computer": "computer", "Internet": "internet", "Arduino": "arduino", "Robot": "robot",
    "Artificial intelligence": "artificial intelligence,ai", "Transistor": "transistor", "Electric battery": "battery",
    "Pi": "pi", "Prime number": "prime number", "Solar energy": "solar power,solar energy",
    "Wind power": "wind power", "Telephone": "telephone,phone", "Television": "television,tv",
    "Airplane": "airplane,plane", "Car": "car,cars", "Rocket": "rocket", "Microscope": "microscope",
    "Periodic table": "periodic table", "Temperature": "temperature", "Speed of light": "speed of light",
    "Photosynthesis": "photosynthesis", "Recycling": "recycling", "Plastic": "plastic", "Wheel": "wheel",
    "Printing press": "printing press", "Steam engine": "steam engine", "Video game": "video game,video games",
    "Minecraft": "minecraft",
    # people
    "Albert Einstein": "einstein", "Isaac Newton": "newton", "Marie Curie": "marie curie,curie",
    "George Washington": "george washington,washington", "Abraham Lincoln": "lincoln",
    "Martin Luther King Jr.": "martin luther king,mlk", "Leonardo da Vinci": "da vinci,leonardo",
    "Thomas Edison": "edison", "Nikola Tesla": "tesla", "Ada Lovelace": "ada lovelace,lovelace",
    "Alan Turing": "turing", "Neil Armstrong": "neil armstrong,armstrong", "Galileo Galilei": "galileo",
    "Charles Darwin": "darwin", "Wright brothers": "wright brothers", "Wolfgang Amadeus Mozart": "mozart",
    "William Shakespeare": "shakespeare", "Cleopatra": "cleopatra", "Napoleon": "napoleon",
    "Benjamin Franklin": "ben franklin,benjamin franklin,franklin", "Harriet Tubman": "harriet tubman",
    "Rosa Parks": "rosa parks", "Christopher Columbus": "columbus", "Julius Caesar": "julius caesar,caesar",
    "Amelia Earhart": "amelia earhart,earhart", "Steve Jobs": "steve jobs", "Walt Disney": "walt disney,disney",
    # places
    "United States": "united states,usa,america", "Canada": "canada", "Mexico": "mexico", "China": "china",
    "Japan": "japan", "India": "india", "France": "france", "Egypt": "egypt", "Australia": "australia",
    "United Kingdom": "united kingdom,uk,england,britain", "Germany": "germany", "Italy": "italy",
    "Brazil": "brazil", "Russia": "russia", "Spain": "spain", "South Africa": "south africa",
    "Europe": "europe", "Africa": "africa", "Asia": "asia", "South America": "south america",
    "Great Pyramid of Giza": "pyramids,pyramid", "Eiffel Tower": "eiffel tower", "Great Wall of China": "great wall",
    "Statue of Liberty": "statue of liberty", "New York City": "new york", "Washington, D.C.": "washington dc",
    "Yellowstone National Park": "yellowstone",
    # history
    "World War I": "world war 1,world war i,ww1", "World War II": "world war 2,world war ii,ww2",
    "Titanic": "titanic", "Roman Empire": "roman empire,rome", "Ancient Egypt": "ancient egypt",
    "American Revolution": "american revolution", "Industrial Revolution": "industrial revolution",
    "Stone Age": "stone age", "Vikings": "vikings,viking", "Olympic Games": "olympics,olympic games",
    # everyday things
    "Chocolate": "chocolate", "Pizza": "pizza", "Bread": "bread", "Money": "money", "Soccer": "soccer,football",
    "Basketball": "basketball", "Baseball": "baseball", "Chess": "chess", "Music": "music", "Library": "library",
}


def ascii_only(text):
    text = re.sub(r"\s*[\u2013\u2014]\s*", " - ", text)  # long dashes would vanish and glue words
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
    time.sleep(1.0)
    out = ""
    for sentence in re.split(r"(?<=[.!?])\s+", text):
        if len(out) + len(sentence) + 1 > MAX_CHARS:
            break
        out += sentence + " "
    out = out.strip() or text[:MAX_CHARS - 3].rsplit(" ", 1)[0] + "..."
    return out.replace("|", "/").replace('"', "'")


# The encyclopedia is split across helpers that have free flash, in this order.
PARTS = [("wiki_a.h", "WIKI_A", 12500),   # Library
         ("wiki_b.h", "WIKI_B", 14500),   # Calc
         ("wiki_c.h", "WIKI_C", 8000)]    # Words


def write_part(path, name, entries):
    data = "|".join(entries)
    with open(path, "w") as f:
        f.write(f"// Made by make_wiki.py: {len(entries)} Wikipedia summaries, {len(data)} bytes.\n")
        f.write("// Format: key,key:summary|key:summary|...  (keys are lowercase)\n")
        f.write("// Text from Wikipedia (https://en.wikipedia.org), shortened. Licensed CC BY-SA 4.0:\n")
        f.write("// https://creativecommons.org/licenses/by-sa/4.0/  -- this file only; the code is MIT.\n")
        f.write(f"const char {name}[] PROGMEM =\n")
        for i, e in enumerate(entries):
            f.write('  "' + e.replace("\\", "\\\\") + ("|" if i < len(entries) - 1 else "") + '"\n')
        f.write("  ;\n" if entries else '  "";\n')


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
    part, budget_used, parts = 0, 0, [[] for _ in PARTS]
    for e in entries:
        while part < len(PARTS) and budget_used + len(e) + 1 > PARTS[part][2]:
            part, budget_used = part + 1, 0
        if part == len(PARTS):
            print(f"out of space: dropped {e.split(':')[0]}")
            continue
        parts[part].append(e)
        budget_used += len(e) + 1
    for (path, name, _), items in zip(PARTS, parts):
        write_part(path, name, items)
        print(f"wrote {path}: {len(items)} topics, {sum(len(x) + 1 for x in items)} bytes")


if __name__ == "__main__":
    main()
