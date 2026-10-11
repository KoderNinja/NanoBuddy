"""Train a tiny letter-by-letter language model that writes NanoChat replies.

Two kinds, both small enough for an Arduino Nano helper:
  gru          a recurrent network: carries a small memory from letter to letter
  transformer  a tiny GPT: attention over the last few letters

usage: ../.venv/bin/python train_gpt.py gru|transformer [steps]
Prints sample replies, and saves the model to <kind>.pt.
"""
import math
import random
import sys
import time

import torch
import torch.nn as nn
import torch.nn.functional as F

from make_corpus import VOCAB

V = len(VOCAB)
stoi = {c: i for i, c in enumerate(VOCAB)}
NL, SEP = stoi["\n"], stoi[">"]

# Sizes chosen to fit a Nano: flash for the weights (4-bit) and its 2 KB of RAM.
import os
GRU_E, GRU_H = 16, int(os.environ.get("GRU_H", 72))   # 72: ~22K weights (11 KB at 4 bits), ~1 KB RAM
TF_D, TF_LAYERS, TF_HEADS, TF_T = 16, 2, 2, 12          # ~7K weights, 12-letter memory


QAT = False  # when on, weights are rounded to 4 bits in the forward pass ("quantization-aware training")


def q4(w):
    """Round each row to 4-bit levels (-7..7 times a per-row scale). Gradients pass straight through."""
    if not QAT:
        return w
    s = w.abs().amax(dim=-1, keepdim=True).clamp(min=1e-8) / 7
    return w + ((w / s).round().clamp(-7, 7) * s - w).detach()


class TinyGRU(nn.Module):
    """A GRU written out step by step, so the Nano can run exactly the same math."""
    def __init__(self):
        super().__init__()
        self.emb = nn.Embedding(V, GRU_E)
        self.wi = nn.Linear(GRU_E, 3 * GRU_H)   # input -> reset, update, new
        self.wh = nn.Linear(GRU_H, 3 * GRU_H)   # memory -> reset, update, new
        self.out = nn.Linear(GRU_H, V)

    def forward(self, x, h=None):
        B, T = x.shape
        h = torch.zeros(B, GRU_H) if h is None else h
        e = F.embedding(x, q4(self.emb.weight))
        gi = F.linear(e, q4(self.wi.weight), self.wi.bias)
        wh = q4(self.wh.weight)
        ys = []
        for t in range(T):
            gh = F.linear(h, wh, self.wh.bias)
            ir, iz, inn = gi[:, t].chunk(3, dim=1)
            hr, hz, hn = gh.chunk(3, dim=1)
            r, z = torch.sigmoid(ir + hr), torch.sigmoid(iz + hz)
            n = torch.tanh(inn + r * hn)
            h = (1 - z) * n + z * h
            ys.append(h)
        y = torch.stack(ys, dim=1)
        return F.linear(y, q4(self.out.weight), self.out.bias), h


class Block(nn.Module):
    def __init__(self):
        super().__init__()
        self.ln1, self.ln2 = nn.LayerNorm(TF_D), nn.LayerNorm(TF_D)
        self.qkv = nn.Linear(TF_D, 3 * TF_D)
        self.proj = nn.Linear(TF_D, TF_D)
        self.mlp = nn.Sequential(nn.Linear(TF_D, 4 * TF_D), nn.GELU(), nn.Linear(4 * TF_D, TF_D))

    def forward(self, x):
        B, T, D = x.shape
        q, k, v = self.qkv(self.ln1(x)).split(D, dim=2)
        hd = D // TF_HEADS
        q, k, v = (t.view(B, T, TF_HEADS, hd).transpose(1, 2) for t in (q, k, v))
        att = F.scaled_dot_product_attention(q, k, v, is_causal=True)
        x = x + self.proj(att.transpose(1, 2).reshape(B, T, D))
        return x + self.mlp(self.ln2(x))


class TinyGPT(nn.Module):
    def __init__(self):
        super().__init__()
        self.tok = nn.Embedding(V, TF_D)
        self.pos = nn.Embedding(TF_T, TF_D)
        self.blocks = nn.ModuleList(Block() for _ in range(TF_LAYERS))
        self.ln = nn.LayerNorm(TF_D)
        self.head = nn.Linear(TF_D, V)

    def forward(self, x, h=None):
        x = self.tok(x) + self.pos(torch.arange(x.shape[1]))
        for b in self.blocks:
            x = b(x)
        return self.head(self.ln(x)), None


def batches(data, length, size):
    while True:
        idx = torch.randint(len(data) - length - 1, (size,))
        x = torch.stack([data[i:i + length] for i in idx])
        y = torch.stack([data[i + 1:i + length + 1] for i in idx])
        yield x, y


@torch.no_grad()
def reply(model, kind, message, temp=0.5):
    ids = [stoi[c] for c in message.lower() if c in stoi and c not in "\n>"] + [SEP]
    out, h = "", None
    if kind == "gru":
        logits, h = model(torch.tensor([[NL] + ids]))
        last = logits[0, -1]
    for _ in range(80):
        if kind == "transformer":
            logits, _ = model(torch.tensor([ids[-TF_T:]]))
            last = logits[0, -1]
        c = torch.multinomial(F.softmax(last / temp, dim=0), 1).item()
        if c == NL:
            break
        out += VOCAB[c]
        ids.append(c)
        if kind == "gru":
            logits, h = model(torch.tensor([[c]]), h)
            last = logits[0, -1]
    return out


PROMPTS = ["hi nano", "how are you", "tell me a joke", "i feel sad today", "what do you eat",
           "you are so cool", "who made you", "i have a hamster", "i like dinosaurs", "can you help me",
           "do you like music", "goodbye"]


def main():
    kind = sys.argv[1]
    steps = int(sys.argv[2]) if len(sys.argv) > 2 else 6000
    torch.manual_seed(1)
    random.seed(1)
    text = open("corpus.txt").read()
    data = torch.tensor([stoi[c] for c in text])
    split = int(len(data) * 0.95)
    train, val = data[:split], data[split:]
    model = TinyGRU() if kind == "gru" else TinyGPT()
    n = sum(p.numel() for p in model.parameters())
    print(f"{kind}: {n} weights (~{n // 2 / 1024:.1f} KB at 4 bits)")
    length = 96 if kind == "gru" else TF_T
    opt = torch.optim.AdamW(model.parameters(), lr=3e-3, weight_decay=0.01)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=3e-3, total_steps=steps)
    t0 = time.time()
    global QAT
    for step, (x, y) in zip(range(steps), batches(train, length, 64)):
        QAT = step >= steps // 2  # second half: practice with 4-bit weights
        logits, _ = model(x)
        loss = F.cross_entropy(logits.reshape(-1, V), y.reshape(-1))
        opt.zero_grad()
        loss.backward()
        nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        opt.step()
        sched.step()
        if step % 1000 == 0 or step == steps - 1:
            with torch.no_grad():
                vx, vy = next(batches(val, length, 256))
                vl = F.cross_entropy(model(vx)[0].reshape(-1, V), vy.reshape(-1)).item()
            print(f"step {step:5d}  train {loss.item():.3f}  val {vl:.3f}  ({vl / math.log(2):.2f} bits/letter)  {time.time() - t0:.0f}s")
    torch.save(model.state_dict(), f"{kind}.pt")
    model.eval()
    print("sample replies at full precision:")
    for p in PROMPTS[:6]:
        print(f"  you: {p:<20} nano: {reply(model, kind, p)}")
    with torch.no_grad():  # the Nano's 4-bit weights (one scale per row)
        for name, w in model.named_parameters():
            if w.dim() == 2:
                s = w.abs().amax(dim=1, keepdim=True).clamp(min=1e-8) / 7
                w.copy_((w / s).round().clamp(-7, 7) * s)
        vx, vy = next(batches(val, length, 256))
        vl = F.cross_entropy(model(vx)[0].reshape(-1, V), vy.reshape(-1)).item()
    print(f"after 4-bit squeeze: val {vl:.3f} ({vl / math.log(2):.2f} bits/letter)")
    model.eval()
    for p in PROMPTS:
        print(f"  you: {p:<20} nano: {reply(model, kind, p)}")


if __name__ == "__main__":
    main()
