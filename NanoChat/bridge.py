"""Chat with NanoChat and give it internet access through your Mac.

The Nano does all the chatting itself. When it needs something it can't
know (time, weather, "who is ..."), it prints "@NET kind|message"; this
script looks it up and replies "@R answer".

Sources: your Mac's clock, wttr.in (weather), Wikipedia (lookups), BBC RSS
(news), MyMemory (translate), uselessfacts.jsph.pl (fun facts). Unit
conversion is plain math here on the Mac.
No installs needed (standard library only).

usage: python3 bridge.py [/dev/cu.usbserial-XXX]
"""
import glob
import json
import os
import re
import sys
import termios
import threading
import time
import random
import unicodedata
import xml.etree.ElementTree as ET
import urllib.parse
import urllib.request
from datetime import datetime

UA = {"User-Agent": "NanoChat-bridge/1.0 (Arduino hobby project)"}


def ascii_only(text):
    text = text.replace("°", " deg ")
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    return re.sub(r"\s+", " ", text).strip()


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=8) as r:
        return r.read().decode("utf-8", "replace")


def short(text, limit=240):
    sentences = re.split(r"(?<=[.!?])\s+", text)
    out = ""
    for s in sentences:
        if len(out) + len(s) > limit:
            break
        out += s + " "
    return (out.strip() or text[:limit].rsplit(" ", 1)[0] + "...")


def answer_time(_):
    now = datetime.now()
    return now.strftime("It's %-I:%M %p on %A, %B %-d, %Y.")


def answer_weather(msg):
    place = ""
    m = re.search(r"\b(?:in|at|for)\s+([a-zA-Z .'-]+?)\s*\??$", msg)
    if m and m.group(1).lower() not in ("today", "tomorrow", "now", "the morning"):
        place = m.group(1).strip()
    url = "https://wttr.in/" + urllib.parse.quote(place) + "?format=%l:+%C,+%t+(feels+like+%f),+humidity+%h"
    return get(url)


TOPIC_PREFIX = re.compile(
    r"^(please\s+)?(can you\s+)?("
    r"who|what|where|when)\s+(is|was|are|were)\s+((a|an|the)\s+)?|"
    r"^(please\s+)?(tell me more about|tell me about|what do you know about|search( wikipedia)? for|search|look up|"
    r"define|definition of|explain|how does|how do|how is|why is|why does|when did|info about|information about)\s+((a|an|the)\s+)?",
    re.I)


def answer_lookup(msg):
    topic = TOPIC_PREFIX.sub("", msg.strip()).strip(" ?!.")
    topic = re.sub(r"^what does (.+) mean$", r"\1", topic, flags=re.I)
    if not topic:
        return "What should I look up?"
    q = urllib.parse.urlencode({"action": "query", "list": "search", "srsearch": topic,
                                "srlimit": 1, "format": "json"})
    hits = json.loads(get("https://en.wikipedia.org/w/api.php?" + q))["query"]["search"]
    if not hits:
        return f"I couldn't find anything about {topic}."
    title = hits[0]["title"]
    page = json.loads(get("https://en.wikipedia.org/api/rest_v1/page/summary/"
                          + urllib.parse.quote(title.replace(" ", "_"))))
    extract = page.get("extract", "")
    return short(extract) if extract else f"Wikipedia has a page on {title}, but no summary."


def answer_news(_):
    root = ET.fromstring(get("https://feeds.bbci.co.uk/news/rss.xml"))
    titles = [t.text.strip() for t in root.iter("title") if t.text][2:5]  # skip the feed's own titles
    if not titles:
        return "I couldn't get the news right now."
    return "Top news: " + " | ".join(titles)


# unit -> (kind, size in that kind's base unit). Temperature is handled separately.
UNITS = {}
for kind, size, names in [
    ("len", 1000, "km kilometer kilometers kilometre kilometres"), ("len", 1, "m meter meters metre metres"),
    ("len", 0.01, "cm centimeter centimeters"), ("len", 0.001, "mm millimeter millimeters"),
    ("len", 1609.344, "mi mile miles"), ("len", 0.9144, "yd yard yards"), ("len", 0.3048, "ft foot feet"),
    ("len", 0.0254, "in inch inches"),
    ("mass", 1, "kg kilo kilos kilogram kilograms"), ("mass", 0.001, "g gram grams"),
    ("mass", 0.45359237, "lb lbs pound pounds"), ("mass", 0.028349523, "oz ounce ounces"),
    ("mass", 6.35029, "stone stones"),
    ("vol", 1, "l liter liters litre litres"), ("vol", 0.001, "ml milliliter milliliters millilitre"),
    ("vol", 3.785411784, "gal gallon gallons"), ("vol", 0.946352946, "qt quart quarts"),
    ("vol", 0.473176473, "pt pint pints"), ("vol", 0.2365882365, "cup cups"),
    ("vol", 0.0147867648, "tbsp tablespoon tablespoons"), ("vol", 0.00492892159, "tsp teaspoon teaspoons"),
    ("time", 1, "s sec secs second seconds"), ("time", 60, "min mins minute minutes"),
    ("time", 3600, "h hr hrs hour hours"), ("time", 86400, "day days"), ("time", 604800, "week weeks"),
    ("time", 31557600, "year years"),
    ("speed", 1, "kph kmh kilometers per hour"), ("speed", 1.609344, "mph miles per hour"),
]:
    for n in names.split():
        UNITS[n] = (kind, size)
TEMPS = {"c": "C", "celsius": "C", "centigrade": "C", "f": "F", "fahrenheit": "F", "k": "K", "kelvin": "K"}


def answer_convert(msg):
    m = msg.lower().replace("degrees ", "")
    # "how many feet in a mile" -> 1 mile to feet
    q = re.search(r"how many (\w+) (?:are )?in (?:a |an |one )?(\w+)", m)
    if q:
        amount, src, dst = 1.0, q.group(2), q.group(1)
    else:
        q = re.search(r"(-?\d+(?:\.\d+)?)\s*(\w+)\s+(?:to|in|into|as)\s+(\w+)", m)
        if not q:
            q = re.search(r"how (?:many|much) (\w+) is (-?\d+(?:\.\d+)?)\s*(\w+)", m)
            if not q:
                return "Try: convert 5 miles to km, or how many cups in a gallon."
            amount, src, dst = float(q.group(2)), q.group(3), q.group(1)
        else:
            amount, src, dst = float(q.group(1)), q.group(2), q.group(3)
    if src in TEMPS and dst in TEMPS:
        a, b = TEMPS[src], TEMPS[dst]
        c = amount if a == "C" else (amount - 32) / 1.8 if a == "F" else amount - 273.15
        r = c if b == "C" else c * 1.8 + 32 if b == "F" else c + 273.15
        return f"{amount:g} {a} = {r:.4g} {b}"
    if src not in UNITS or dst not in UNITS:
        return f"I don't know how to convert {src} to {dst}."
    (k1, s1), (k2, s2) = UNITS[src], UNITS[dst]
    if k1 != k2:
        return f"I can't turn {src} into {dst}, they measure different things."
    return f"{amount:g} {src} = {amount * s1 / s2:.4g} {dst}"


LANGS = {"spanish": "es", "french": "fr", "german": "de", "italian": "it", "portuguese": "pt",
         "japanese": "ja", "chinese": "zh-CN", "korean": "ko", "russian": "ru", "arabic": "ar",
         "dutch": "nl", "hindi": "hi", "greek": "el", "swedish": "sv", "polish": "pl",
         "turkish": "tr", "latin": "la", "english": "en", "hebrew": "he", "vietnamese": "vi"}


def answer_translate(msg):
    m = msg.lower().strip(" ?!.")
    lang = next((l for l in LANGS if re.search(rf"\b{l}\b", m)), None)
    text = None
    for pat in [r"(?:\w+ )?word for (.+?)(?: in \w+)?$", r"translate (.+?) (?:to|into|in) \w+$", r"how (?:do|would|can) (?:you|i) say (.+?) in \w+$",
                r"what is (.+?) in \w+$", r"say (.+?) in \w+$", r"what does (.+?) mean(?: in \w+)?$",
                r"translate (.+)$"]:
        q = re.search(pat, m)
        if q:
            text = q.group(1).strip(" '\"")
            break
    if not text:
        return "Try: translate thank you to spanish."
    def mymemory(pair):
        data = json.loads(get("https://api.mymemory.translated.net/get?" +
                              urllib.parse.urlencode({"q": text, "langpair": pair})))
        raw = data.get("responseData", {}).get("translatedText", "")
        if raw and not ascii_only(raw):
            return "(" + lang.title() + " uses letters my screen can't show)"
        return ascii_only(raw)

    if "mean" in m and (not lang or m.endswith("english")):
        # "what does hola mean": MyMemory can't detect the language, so try common ones
        lang, out = "english", ""
        for src in ("es", "fr", "de", "it", "pt"):
            out = mymemory(src + "|en")
            if out and out.lower() != text.lower():
                break
    elif not lang:
        return "Which language? Try: translate hello to french."
    else:
        out = mymemory("en|" + LANGS[lang])
    if not out:
        return "The translator didn't answer."
    return f'"{text}" in {lang.title()}: {out}'


def answer_fun_fact(_):
    if random.random() < 0.5:
        data = json.loads(get("https://uselessfacts.jsph.pl/api/v2/facts/random?language=en"))
        return "Fun fact: " + data["text"]
    data = json.loads(get("https://official-joke-api.appspot.com/random_joke"))
    return f"{data['setup']} ... {data['punchline']}"


HANDLERS = {"time": answer_time, "weather": answer_weather, "lookup": answer_lookup,
            "news": answer_news, "convert": answer_convert, "translate": answer_translate,
            "fun_fact": answer_fun_fact}


def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    a = termios.tcgetattr(fd)
    a[0] = a[1] = a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[4] = a[5] = termios.B9600
    a[6][termios.VMIN] = 0
    a[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd


def main():
    ports = sys.argv[1:] or sorted(glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("No Arduino found. Plug in the Nano and try again.")
    fd = open_port(ports[0])
    print(f"Connecting to {ports[0]} ...")
    time.sleep(2.5)  # opening the port resets the Nano
    os.write(fd, b"@HELLO\n")

    def net_reply(req):
        kind, _, msg = req.partition("|")
        try:
            ans = HANDLERS.get(kind, lambda m: "I don't know how to do that.")(msg)
        except Exception as e:
            ans = f"Internet error ({type(e).__name__})."
        os.write(fd, ("@R " + ascii_only(ans)[:300] + "\n").encode())

    def reader():
        buf = b""
        ready = False  # hide startup chatter until the Nano confirms the bridge
        while True:
            data = os.read(fd, 256)
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("ascii", "replace").rstrip("\r")
                if text.startswith("@NET "):
                    print("(looking it up...)", flush=True)
                    threading.Thread(target=net_reply, args=(text[5:],), daemon=True).start()
                elif text.startswith(("@F", "@N", "@H")):
                    pass  # face / network codes for the app
                elif "@OK" in text:
                    ready = True
                    print("NanoChat online with internet. Type to chat, Ctrl+C to quit.", flush=True)
                elif ready:
                    print(text, flush=True)
            # show the "> " prompt, which has no newline after it
            if ready and buf.endswith(b"> "):
                print("> ", end="", flush=True)
                buf = b""

    threading.Thread(target=reader, daemon=True).start()
    try:
        while True:
            msg = input()
            os.write(fd, (msg + "\n").encode())
    except (KeyboardInterrupt, EOFError):
        print("\nbye!")


if __name__ == "__main__":
    main()
