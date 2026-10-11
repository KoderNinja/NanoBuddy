"""Train NanoChat's intent classifier and export it as int8 weights.

Message -> words + word pairs, hashed into F buckets (a "bag of words")
        -> hidden layer of H relu units -> one score per intent.

Pure Python (no numpy) since the network is tiny and inputs are sparse.
usage: python3 train.py intents.txt ../chat_model.h
"""
import math
import random
import re
import sys

F = int(__import__("os").environ.get("F", 512))  # hash buckets
H = int(__import__("os").environ.get("H", 12))   # hidden units
ENV = __import__("os").environ
EPOCHS = int(ENV.get("EPOCHS", 120))
LR = float(ENV.get("LR", 0.05))
DROP = float(ENV.get("DROP", 0.15))  # randomly drop words during training so it copes with new phrasing
WD = float(ENV.get("WD", 0))         # weight decay on the first layer (keeps it from over-trusting one word)
SMOOTH = float(ENV.get("SMOOTH", 0))  # label smoothing: aim for less than 100% confidence
FILL = float(ENV.get("FILL", 0))      # chance of adding a filler word ("hey", "nano"...) to an example
FILLERS = "hey nano please um ok so hi yo uh well like plz pls".split()
BIGRAMS = __import__("os").environ.get("BI", "1") == "1"


def fnv1a(s):
    h = 2166136261
    for b in s.encode():
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def tokens(text):
    return re.findall(r"[a-z0-9]+", text.lower())


def features(words):
    feats = {fnv1a(w) % F for w in words}
    if BIGRAMS:
        feats |= {fnv1a(a + " " + b) % F for a, b in zip(words, words[1:])}
    return sorted(feats)


def load(path):
    intents, data, cur = [], [], None
    for line in open(path):
        line = line.strip()
        if line.startswith("# ") and " " not in line[2:] and line[2:].islower():
            cur = len(intents)
            intents.append(line[2:])
        elif line and not line.startswith("#") and cur is not None:
            data.append((tokens(line), cur))
    return intents, data


def fit(data, K, log=print):
    """Train on [(words, intent)], returning (W1, b1, W2, b2, forward)."""
    data = list(data)
    W1 = [[random.gauss(0, 0.3) for _ in range(H)] for _ in range(F)]  # [F][H]
    b1 = [0.0] * H
    W2 = [[random.gauss(0, 0.3) for _ in range(H)] for _ in range(K)]  # [K][H]
    b2 = [0.0] * K

    def forward(feats):
        h = b1[:]
        for f in feats:
            row = W1[f]
            for i in range(H):
                h[i] += row[i]
        h = [x if x > 0 else 0.0 for x in h]
        lg = [b2[k] + sum(w * x for w, x in zip(W2[k], h)) for k in range(K)]
        m = max(lg)
        e = [math.exp(x - m) for x in lg]
        z = sum(e)
        return h, [x / z for x in e]

    for ep in range(EPOCHS):
        lr = LR * (0.1 ** (ep / (EPOCHS - 1)))
        random.shuffle(data)
        loss = correct = 0
        for words, y in data:
            kept = [w for w in words if random.random() > DROP] or words
            if FILL and random.random() < FILL:  # teach it to ignore chit-chat words
                f = random.choice(FILLERS)
                kept = [f] + kept if random.random() < 0.5 else kept + [f]
            feats = features(kept)
            h, p = forward(feats)
            loss -= math.log(p[y] + 1e-12)
            correct += max(range(K), key=p.__getitem__) == y
            p = [x - SMOOTH / K for x in p]
            p[y] -= 1 - SMOOTH
            dh = [0.0] * H
            for k in range(K):
                g = p[k]
                row = W2[k]
                for i in range(H):
                    dh[i] += row[i] * g
                    row[i] -= lr * (g * h[i] + 1e-4 * row[i])
                b2[k] -= lr * g
            for i in range(H):
                if h[i] <= 0:
                    dh[i] = 0.0
                b1[i] -= lr * dh[i]
            for f in feats:
                row = W1[f]
                for i in range(H):
                    row[i] -= lr * (dh[i] + WD * row[i])
        if ep % 20 == 0 or ep == EPOCHS - 1:
            log(f"epoch {ep + 1:3d}  loss {loss / len(data):.3f}  acc {correct / len(data):.2%}")

    return W1, b1, W2, b2, forward


def main():
    random.seed(int(ENV.get("SEED", 7)))
    intents, data = load(sys.argv[1])
    K = len(intents)
    print(f"{K} intents, {len(data)} examples")
    W1, b1, W2, b2, forward = fit(data, K)

    # int8 quantization, then measure with the quantized weights
    def quant(rows):
        m = max(abs(x) for r in rows for x in r) / 127
        return [[max(-127, min(127, round(x / m))) for x in r] for r in rows], m

    qW1, s1 = quant(W1)
    qW2, s2 = quant(W2)
    for f in range(F):
        W1[f] = [q * s1 for q in qW1[f]]
    for k in range(K):
        W2[k] = [q * s2 for q in qW2[k]]
    acc = sum(max(range(K), key=forward(features(w))[1].__getitem__) == y for w, y in data)
    print(f"int8 train accuracy {acc / len(data):.2%}")

    # EXTRA_TESTS=a.txt,b.txt scores more held-out files (only the main one is listed)
    test_files = [sys.argv[1].replace("intents", "tests")] + [f for f in ENV.get("EXTRA_TESTS", "").split(",") if f]
    for n, path in enumerate(test_files):
        tests = [l.split("|") for l in open(path) if "|" in l and not l.startswith("#")]
        ok = 0
        for text, want in tests:
            h, p = forward(features(tokens(text)))
            k = max(range(K), key=p.__getitem__)
            hit = intents[k] == want.strip()
            ok += hit
            if n == 0:
                print(f"  {'ok ' if hit else 'BAD'} {p[k]:.2f} {intents[k]:<14} <- {text}")
        print(f"unseen test accuracy {ok}/{len(tests)}" + (f"  ({path})" if n else ""))

    with open(sys.argv[2], "w") as out:
        out.write(f"// Generated by train.py: {K} intents, {len(data)} examples.\n")
        out.write("#pragma once\n#include <avr/pgmspace.h>\n\n")
        out.write(f"const uint16_t CHAT_F = {F};\nconst uint8_t CHAT_H = {H};\nconst uint8_t CHAT_K = {K};\n")
        out.write("enum Intent { " + ", ".join("I_" + n.upper() for n in intents) + " };\n")
        out.write(f"const float chat_s1 = {s1:.8g}, chat_s2 = {s2:.8g};\n")
        for name, rows in (("chat_W1", qW1), ("chat_W2", qW2)):
            flat = [x for r in rows for x in r]
            out.write(f"const int8_t {name}[{len(flat)}] PROGMEM = {{")
            out.write(",".join(("\n  " if i % 32 == 0 else "") + str(x) for i, x in enumerate(flat)))
            out.write("\n};\n")
        for name, vals in (("chat_b1", b1), ("chat_b2", b2)):
            out.write(f"const float {name}[{len(vals)}] PROGMEM = {{")
            out.write(",".join(f"{v:.6g}" for v in vals))
            out.write("};\n")


if __name__ == "__main__":
    main()
