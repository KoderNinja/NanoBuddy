# NanoBuddy

A tiny AI chatbot that runs entirely on an **Arduino Nano** (2 KB of RAM, 32 KB of flash), with a 16x2 LCD that shows little pixel faces. Add more Nanos and they become a **NanoNet**: a bigger brain, a word inventor, a calculator and an offline encyclopedia, all talking over two wires.

No internet needed. Plug in a Mac and it can also check the weather, the news, Wikipedia and translations.

## What it can do

On the Nano by itself:
- Chat: a small neural network understands ~30 kinds of messages (greetings, jokes, feelings, questions...) and replies with a face on the LCD
- Games: guess my number, rock paper scissors, trivia, magic 8-ball, dice (`roll 3 dice`, `roll a d20`), coin flips
- Tools: timer (`set a timer for 5 minutes`, countdown on the LCD), stopwatch, math (`20 percent of 50`)
- Remembers your name (`my name is Sam`)
- `again` repeats your last message
- Self-training: if it gets something wrong, say `wrong, it was a joke` (or fact, riddle, dice, greet, bye, sad, happy...). It remembers that message from then on, even after unplugging (up to 100 corrections, kept in EEPROM). With the Mac app running, corrections are also saved to `NanoChat/train/learned.txt`, and `train.py` learns from them the next time you retrain, so similar wordings get understood too
- A **button menu**: use it with no computer at all

With helper Nanos (NanoNet, all optional):
| Helper | What it adds |
|---|---|
| Brain | a mini GPT: when NanoChat doesn't recognize a message, it writes its own reply letter by letter |
| Words | invents words and names, 30-question trivia, `morse sos`, `spell cat backwards`, `how many letters in banana` |
| Calc | math with variables (`calc x = 5`), unit converter (`5 miles to km`, `how many cups in a gallon`) |
| Library | hundreds of jokes/facts/riddles and survival tips (`what do i do if i get lost`) |

All three of Words, Calc and Library also hold part of a ~200-topic offline encyclopedia (`who is newton`, `what is a volcano`).

How it works: the boss offers each message to Words, Calc and Library ("do ..."); a helper answers only what it handles, so you can add features to a helper without changing the boss. When the boss's own model doesn't recognize a message, it asks the Brain to write a reply ("gen ..."). Every helper is optional.

With a Mac (`bridge.py` or the NanoChat app): time, weather, Wikipedia, news headlines, translation, unit conversion, random fun facts.

## Hardware

- 1 to 5 Arduino Nano boards (ATmega328P; cheap USB-C clones work)
- Optional: 16x2 LCD with an I2C backpack (PCF8574, address 0x27 or 0x3F)
- Optional: a push button

Wiring:
| From | To |
|---|---|
| LCD GND / VSS | Nano GND |
| LCD VCC / VDD | Nano 5V |
| LCD SDA | Nano A4 |
| LCD SCL / SCK | Nano A5 |
| Button | D2 and GND |

**NanoNet:** connect every Nano's **A4** together, every **A5** together, and every **GND** together. Also join the **5V** pins so one USB cable powers them all. The Nano with the LCD runs NanoChat (the boss); the others run NanoNetHelper.

## Just one Nano? Use Solo

Uncomment `#define SOLO` near the top of `NanoChat/NanoChat.ino` before uploading. It drops the NanoNet code and uses the space for trivia (20 questions) and a bigger joke collection. Everything else (chat, faces, LCD, games, timer, button menu, self-training) works the same. With arduino-cli:
```bash
arduino-cli compile -u -p /dev/cu.usbserial-XXXX -b arduino:avr:nano --build-property "compiler.cpp.extra_flags=-DSOLO" NanoChat
```

## Upload

Install the [Arduino IDE](https://www.arduino.cc/en/software) (or `arduino-cli`), board **Arduino Nano**. If uploads fail with "not in sync", choose **Processor → ATmega328P (Old Bootloader)**.

1. Boss: open `NanoChat/NanoChat.ino` and upload.
2. Each helper: open `NanoNetHelper/NanoNetHelper.ino`, set `#define ROLE` at the top (1 Brain, 2 Words, 3 Calc, 4 Library), and upload. Use each role once.

With arduino-cli:
```bash
arduino-cli compile -u -p /dev/cu.usbserial-XXXX -b arduino:avr:nano NanoChat
arduino-cli compile -u -p /dev/cu.usbserial-XXXX -b arduino:avr:nano --build-property "compiler.cpp.extra_flags=-DROLE=4" NanoNetHelper
```

## Use it

- **No computer:** short press the button = next menu item on the LCD, hold = do it.
- **Serial Monitor:** 9600 baud, type to chat. Type `help` for ideas.
- **Phone app:** with the app running, scan the QR code on the Mac's chat page with your phone (same Wi-Fi). On an iPhone, tap **Share → Add to Home Screen** and NanoChat becomes an app with its own icon: full screen, the same faces, LCD and helper lights, and the chat is shared with the Mac (open it later and the conversation is still there). The link uses the Mac's `.local` name, so it keeps working when the Wi-Fi changes its address. For phone chat with no computer, run the app on a Raspberry Pi (it finds the Nano on Linux too), or add a BLE module such as an HM-10 to the Boss's RX/TX pins and use a BLE terminal app (iPhones can't talk to the Nano's USB chip directly).
- **Mac app:** double-click `NanoChat.app` (keep it next to the `NanoChat` folder). It opens a chat page in your browser with a live copy of the LCD, shows which NanoNet helpers are online and which one answered, and gives the Nano internet access. Or run `python3 NanoChat/bridge.py` in Terminal. Both use only Python's standard library.

## Train your own

```bash
cd NanoChat/train
F=576 H=21 QBITS=4 PREFIX=1 FILL=0.3 WD=0.0005 SEED=2 EXTRA_TESTS=tests-extra.txt,tests-fresh.txt python3 train.py intents-more-training.txt ../chat_model.h   # NanoNet boss model
F=512 H=18 QBITS=4 PREFIX=1 FILL=0.3 WD=0.0005 SEED=8 python3 train.py intents-more-training.txt ../solo_model.h   # smaller Solo model
python3 audit.py intents-more-training.txt                       # find confusing examples
```
Add examples under `# intent_name` in `intents-more-training.txt`. `tests-more-training.txt`, `tests-extra.txt` and `tests-fresh.txt` hold phrases that are *not* in the training data, for an honest score (boss model: 290/311, 93%). `tests-fresh.txt` was written separately and never used to pick training examples. Corrections you taught NanoChat (`learned.txt`) are added automatically.

`FILL=0.3` randomly adds filler words ("hey", "nano", "um") while training so the model learns to ignore them.

To change the offline encyclopedia, edit `TOPICS` in `NanoNetHelper/make_wiki.py` and run it. It compresses the text (the most common pieces of text become one byte, listed in `wiki_dict.h`) and splits it into `wiki_a.h` (Library), `wiki_b.h` (Calc) and `wiki_c.h` (Words) to fit each helper's free flash.

## Mini GPT

`MiniGPT/` trains the Brain's reply writer: a small recurrent network (GRU, ~22K weights) that reads your message and writes a reply one letter at a time. It's trained with 4-bit weights in mind ("quantization-aware training") so it survives being squeezed into the Nano's flash, and runs with whole-number math so a reply takes a few seconds. It writes friendly, real sentences, though not always on-topic: it's a toy GPT, not ChatGPT.

```bash
python3 -m venv .venv && .venv/bin/pip install torch      # once
cd MiniGPT
python3 make_corpus.py                                     # message>reply training text
../.venv/bin/python train_gpt.py gru 14000                 # ~20 min on a laptop
../.venv/bin/python export_gru.py                          # writes NanoNetHelper/gpt_model.h
```
`train_gpt.py transformer` trains a tiny real transformer for comparison; at Nano size it can only remember ~12 letters, so the GRU writes much better replies.

## Other sketches

- `NanoBrain`: the letter-by-letter word/name inventor on its own
- `NanoCalc`: calculator language with variables
- `NanoLang`: a tiny programming language for the Nano

## Safety note

The survival tips are general safety basics, not a substitute for real first-aid training. In an emergency, call your local emergency number.

## License

Code: [MIT](LICENSE), copyright KoderNinja.
`NanoNetHelper/wiki_a.h`, `wiki_b.h`, `wiki_c.h` and `wiki_dict.h` contain shortened text from [Wikipedia](https://en.wikipedia.org) and is licensed [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
The word and name models were trained on the public word lists in macOS (`/usr/share/dict`).

Arduino is a trademark of Arduino SA. This project isn't affiliated with or endorsed by Arduino.
