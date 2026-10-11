"""Find training examples that are probably mislabeled or confusing.

Trains 5 times, each time holding out a different fifth of the examples, and
lists every held-out example the model got wrong. Examples that keep showing
up here are worth checking: wrong label, duplicate in two intents, or a
phrasing that needs more examples like it.

usage: python3 audit.py intents-more-training.txt
"""
import random
import sys
from collections import Counter

import train

intents, data = train.load(sys.argv[1])
K = len(intents)
random.seed(1)
order = list(range(len(data)))
random.shuffle(order)
folds = 5
wrong = []
for f in range(folds):
    held = set(order[f::folds])
    random.seed(7)
    *_, forward = train.fit([d for i, d in enumerate(data) if i not in held], K, log=lambda *a: None)
    for i in held:
        words, y = data[i]
        p = forward(train.features(words))[1]
        k = max(range(K), key=p.__getitem__)
        if k != y:
            wrong.append((intents[y], intents[k], p[k], " ".join(words)))
    print(f"fold {f + 1}/{folds} done", file=sys.stderr)

print(f"cross-validated accuracy {1 - len(wrong) / len(data):.1%}  ({len(wrong)} wrong of {len(data)})")
print("most confused pairs:", Counter((a, b) for a, b, _, _ in wrong).most_common(8))
for want, got, conf, text in sorted(wrong):
    print(f"{want:<14} -> {got:<14} {conf:.2f}  {text}")
