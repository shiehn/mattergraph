"""Experiment 0-S: can CLAP judge SUSTAINED material? (sustain-track step 0)

Experiment 0 validated the evaluator on percussion. The sustain research track
(Plan Rev4 §Phase 4 cloud note) must not rent compute until CLAP demonstrates
it can hear sustained archetypes: pad, bowed string, breathy flute, organ —
plus brightness and static-vs-evolving axes on sustained material.

Run: python exp0s_sustained.py   (from research/, using research/.venv)
"""

from __future__ import annotations

import numpy as np
import torch
from transformers import ClapModel, ClapProcessor

SR = 48000
DUR = 6.0
MODEL = "laion/clap-htsat-unfused"
rng = np.random.default_rng(48291)


def _t() -> np.ndarray:
    return np.arange(int(DUR * SR)) / SR


def _norm(y: np.ndarray) -> np.ndarray:
    return (0.9 * y / (np.abs(y).max() + 1e-9)).astype(np.float32)


def _attack(y: np.ndarray, seconds: float) -> np.ndarray:
    n = int(seconds * SR)
    env = np.ones(len(y))
    env[:n] = 0.5 * (1 - np.cos(np.pi * np.arange(n) / n))
    return y * env


def warm_pad(cutoff_k: float = 5.0, evolve: bool = False) -> np.ndarray:
    """Three detuned saw-ish voices; cutoff_k shapes harmonic rolloff."""
    t = _t()
    y = np.zeros_like(t)
    for cents in (-7.0, 0.0, 6.0):
        f0 = 220.0 * 2 ** (cents / 1200)
        for k in range(1, 24):
            if f0 * k > 9000:
                break
            kc = cutoff_k + (8.0 * t / DUR if evolve else 0.0)
            amp = (1.0 / k) * np.exp(-k / kc)
            y += amp * np.sin(2 * np.pi * f0 * k * t + rng.uniform(0, 2 * np.pi))
    drift = 1 + (0.25 if evolve else 0.08) * np.sin(2 * np.pi * 0.3 * t + 1.0)
    return _norm(_attack(y * drift, 1.2))


def bowed_drone() -> np.ndarray:
    t = _t()
    f0 = 146.83
    y = np.zeros_like(t)
    for k in range(1, 13):
        wobble_f = rng.uniform(0.5, 2.0)
        wobble = 1 + 0.3 * np.sin(2 * np.pi * wobble_f * t + rng.uniform(0, 2 * np.pi))
        y += (1.0 / k**0.8) * wobble * np.sin(2 * np.pi * f0 * k * t + rng.uniform(0, 2 * np.pi))
    scratch = rng.standard_normal(len(t))
    lp = np.zeros_like(scratch)
    a = 0.12
    for i in range(1, len(scratch)):
        lp[i] = lp[i - 1] + a * (scratch[i] - lp[i - 1])
    return _norm(_attack(y + 0.06 * lp * np.abs(y).max(), 0.5))


def breathy_flute() -> np.ndarray:
    t = _t()
    f0 = 587.33
    vib = f0 * (1 + 0.004 * np.sin(2 * np.pi * 5.0 * t))
    phase = 2 * np.pi * np.cumsum(vib) / SR
    tone = np.sin(phase) + 0.25 * np.sin(2 * phase)
    breath = rng.standard_normal(len(t))
    lp = np.zeros_like(breath)
    a = 0.25
    for i in range(1, len(breath)):
        lp[i] = lp[i - 1] + a * (breath[i] - lp[i - 1])
    return _norm(_attack(tone + 0.18 * lp, 0.3))


def organ_tone() -> np.ndarray:
    t = _t()
    f0 = 261.63
    partials = [(1, 1.0), (2, 0.6), (3, 0.3), (4, 0.35), (6, 0.2), (8, 0.12)]
    y = sum(a * np.sin(2 * np.pi * f0 * k * t) for k, a in partials)
    return _norm(_attack(y, 0.03))


def main() -> None:
    print(f"loading {MODEL} ...", flush=True)
    model = ClapModel.from_pretrained(MODEL)
    model.eval()
    proc = ClapProcessor.from_pretrained(MODEL)

    def unwrap(e):  # noqa: ANN001, ANN202
        return e if torch.is_tensor(e) else e.pooler_output

    def ea(waves):  # noqa: ANN001, ANN202
        try:
            i = proc(audio=waves, sampling_rate=SR, return_tensors="pt", padding=True)
        except (ValueError, TypeError):
            i = proc(audios=waves, sampling_rate=SR, return_tensors="pt", padding=True)
        with torch.no_grad():
            return torch.nn.functional.normalize(unwrap(model.get_audio_features(**i)), dim=-1)

    def et(texts):  # noqa: ANN001, ANN202
        i = proc(text=texts, return_tensors="pt", padding=True)
        with torch.no_grad():
            return torch.nn.functional.normalize(unwrap(model.get_text_features(**i)), dim=-1)

    # --- Test S-A: sustained archetype identification ---
    names = ["pad", "bowed", "flute", "organ"]
    audio = ea([warm_pad(), bowed_drone(), breathy_flute(), organ_tone()])
    prompts = {
        "pad": ["a warm analog synth pad", "soft lush sustained pad chord"],
        "bowed": ["a bowed string drone, like a cello bowing one note",
                  "sustained bowed strings with bow noise"],
        "flute": ["a breathy airy flute note", "soft blown flute tone with breath noise"],
        "organ": ["a steady church pipe organ tone", "sustained pipe organ note"],
    }
    sims = {m: (audio @ et(p).T).mean(dim=1).numpy() for m, p in prompts.items()}
    print("\n=== Test S-A: sustained archetype identification ===")
    print("audio\\text " + "".join(f"{m:>8}" for m in names))
    correct = 0
    for i, m_audio in enumerate(names):
        row = np.array([sims[m][i] for m in names])
        best = names[int(row.argmax())]
        correct += best == m_audio
        print(f"{m_audio:>10} " + "".join(f"{v:8.3f}" for v in row) +
              f"   top-1: {best} {'OK' if best == m_audio else 'MISS'}")
    print(f"Test S-A result: {correct}/4 diagonal top-1")

    # --- Test S-B: brightness on sustained material ---
    pads = ea([warm_pad(cutoff_k=2.0), warm_pad(cutoff_k=5.0), warm_pad(cutoff_k=10.0)])
    bright = et(["a bright shimmering synth pad", "brilliant airy pad with sparkling highs"])
    dark = et(["a dark mellow muffled pad", "warm dull bassy pad with no highs"])
    bs = (pads @ bright.T).mean(dim=1).numpy()
    ds = (pads @ dark.T).mean(dim=1).numpy()
    print("\n=== Test S-B: brightness monotonicity (cutoff 2 -> 5 -> 10) ===")
    for c, b, d in zip([2, 5, 10], bs, ds):
        print(f"cutoff {c:>2}   bright-sim {b:.3f}   dark-sim {d:.3f}")
    mono_up = bool(bs[0] < bs[1] < bs[2])
    mono_dn = bool(ds[0] > ds[2])
    print(f"bright ascending: {'OK' if mono_up else 'MISS'}   dark descending: {'OK' if mono_dn else 'MISS'}")

    # --- Test S-C: static vs evolving ---
    pair = ea([organ_tone(), warm_pad(evolve=True)])
    evolving = et(["a slowly evolving shifting ambient texture"])
    static = et(["a perfectly steady unchanging constant tone"])
    ev = (pair @ evolving.T).squeeze(-1).numpy()
    st = (pair @ static.T).squeeze(-1).numpy()
    print("\n=== Test S-C: static vs evolving ===")
    print(f"static organ:  evolving {ev[0]:.3f} vs static {st[0]:.3f} -> "
          f"{'OK' if st[0] > ev[0] else 'MISS'}")
    print(f"evolving pad:  evolving {ev[1]:.3f} vs static {st[1]:.3f} -> "
          f"{'OK' if ev[1] > st[1] else 'MISS'}")


if __name__ == "__main__":
    main()
