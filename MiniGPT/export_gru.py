"""Pack a trained GRU (gru.pt) into gpt_model.h for the Brain helper Nano.

Weights: 4 bits each, two per byte (low nibble first), one scale per row.
Scales are stored as bfloat16 (the top 16 bits of a float) to save flash.
Biases: int8 with one scale per vector.
usage: ../.venv/bin/python export_gru.py [gru.pt] [../NanoNetHelper/gpt_model.h]
"""
import struct
import sys

import torch

import train_gpt as T


def bf16(x):
    return struct.unpack(">I", struct.pack(">f", float(x)))[0] >> 16


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "gru.pt"
    dst = sys.argv[2] if len(sys.argv) > 2 else "../NanoNetHelper/gpt_model.h"
    model = T.TinyGRU()
    model.load_state_dict(torch.load(src))
    sd = {k: v.detach() for k, v in model.state_dict().items()}
    out = [f"// Made by MiniGPT/export_gru.py from {src}: a tiny letter-by-letter reply writer (GRU).",
           "#pragma once", "#include <avr/pgmspace.h>", "",
           f"#define GPT_V {T.V}", f"#define GPT_E {T.GRU_E}", f"#define GPT_H {T.GRU_H}",
           'const char GPT_VOCAB[] PROGMEM = "' + T.VOCAB.replace("\n", "\\n").replace("'", "\\'") + '";']
    total = 0
    for name, w in (("emb", sd["emb.weight"]), ("wi", sd["wi.weight"]), ("wh", sd["wh.weight"]),
                    ("out", sd["out.weight"])):
        s = w.abs().amax(dim=1).clamp(min=1e-8) / 7
        q = (w / s[:, None]).round().clamp(-7, 7).to(torch.int8).flatten().tolist()
        q += [0] * (len(q) % 2)
        packed = [(a & 15) | ((b & 15) << 4) for a, b in zip(q[::2], q[1::2])]
        out.append(f"const uint8_t gpt_{name}[{len(packed)}] PROGMEM = {{" + ",".join(map(str, packed)) + "};")
        out.append(f"const uint16_t gpt_{name}_s[{len(s)}] PROGMEM = {{" + ",".join(str(bf16(x)) for x in s) + "};")
        total += len(packed) + 2 * len(s)
    for name, b in (("bi", sd["wi.bias"]), ("bh", sd["wh.bias"]), ("bo", sd["out.bias"])):
        sc = float(b.abs().max()) / 127 or 1e-8
        q = (b / sc).round().clamp(-127, 127).to(torch.int8).tolist()
        out.append(f"const int8_t gpt_{name}[{len(q)}] PROGMEM = {{" + ",".join(map(str, q)) + "};")
        out.append(f"const float gpt_{name}_scale = {sc:.8g};")
        total += len(q) + 4
    open(dst, "w").write("\n".join(out) + "\n")
    print(f"wrote {dst}: about {total} bytes of flash")


if __name__ == "__main__":
    main()
