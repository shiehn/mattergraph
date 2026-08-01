"""Calibrate CLAP contrastive semantic axes against NSynth's human quality labels.

NSynth notes carry quality tags applied by humans: 'bright', 'dark',
'percussive', 'fast_decay', 'long_release', etc. For each of our SoundSpec-ish
axes we score the contrastive CLAP similarity (positive-pole minus negative-
pole prompts) on notes labeled with the corresponding quality vs notes labeled
with its opposite (or lacking it), and report separation (AUC-style rank
accuracy). Output: an empirically verified axis table — polarity, separation,
and a usable/unusable verdict per axis.

Run from research/: .venv/bin/python exp0/calibrate_axes_nsynth.py
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import soundfile as sf
import torch
from scipy.signal import resample_poly
from transformers import ClapModel, ClapProcessor

NSYNTH = Path(__file__).resolve().parents[2] / "runs/nsynth/nsynth-test"
MODEL = "laion/clap-htsat-unfused"
N_PER_SIDE = 48

# NSynth quality bit order (dataset docs).
QUALITIES = ["bright", "dark", "distortion", "fast_decay", "long_release",
             "multiphonic", "nonlinear_env", "percussive", "reverb", "tempo-synced"]

AXES = [
    # (axis name, positive quality, negative quality-or-None, pos prompts, neg prompts)
    ("brightness", "bright", "dark",
     ["a bright sharp tone with strong high frequencies", "a brilliant sparkling sound"],
     ["a dark mellow tone with no high frequencies", "a dull muffled bassy sound"]),
    ("percussiveness", "percussive", None,
     ["a percussive plucked or struck sound with a sharp attack", "a hit with a hard transient"],
     ["a smooth sustained tone with a soft slow attack", "a flowing continuous sound"]),
    ("decay_speed", "fast_decay", "long_release",
     ["a short sound that decays immediately", "a quick staccato note that stops"],
     ["a long ringing sustained release", "a note that fades out very slowly"]),
]


def quality_set(ex: dict) -> set[str]:
    return {QUALITIES[i] for i, bit in enumerate(ex["qualities"]) if bit}


def main() -> None:
    print(f"loading {MODEL} ...", flush=True)
    model = ClapModel.from_pretrained(MODEL)
    model.eval()
    proc = ClapProcessor.from_pretrained(MODEL)

    def unwrap(e):  # noqa: ANN001, ANN202
        return e if torch.is_tensor(e) else e.pooler_output

    def et(texts):  # noqa: ANN001, ANN202
        i = proc(text=texts, return_tensors="pt", padding=True)
        with torch.no_grad():
            return torch.nn.functional.normalize(unwrap(model.get_text_features(**i)), dim=-1)

    def ea(waves):  # noqa: ANN001, ANN202
        out = []
        for s in range(0, len(waves), 8):
            try:
                i = proc(audio=waves[s:s + 8], sampling_rate=48000,
                         return_tensors="pt", padding=True)
            except (ValueError, TypeError):
                i = proc(audios=waves[s:s + 8], sampling_rate=48000,
                         return_tensors="pt", padding=True)
            with torch.no_grad():
                e = unwrap(model.get_audio_features(**i))
            out.append(torch.nn.functional.normalize(e, dim=-1))
        return torch.cat(out).numpy()

    meta = json.loads((NSYNTH / "examples.json").read_text())
    names_sorted = sorted(meta)

    def load(names):  # noqa: ANN001, ANN202
        waves = []
        for n in names:
            x, sr = sf.read(NSYNTH / "audio" / f"{n}.wav", dtype="float32")
            waves.append(resample_poly(x, 3, 1).astype(np.float32))
        return waves

    print(f"\n{'axis':<16} {'n+':>4} {'n-':>4} {'sep(AUC)':>9} {'polarity':>9}  verdict")
    table = []
    for axis, pos_q, neg_q, pos_p, neg_p in AXES:
        pos_names, neg_names = [], []
        for n in names_sorted:
            ex = meta[n]
            if not (36 <= ex["pitch"] <= 84):
                continue
            qs = quality_set(ex)
            if pos_q in qs and (neg_q is None or neg_q not in qs):
                if len(pos_names) < N_PER_SIDE:
                    pos_names.append(n)
            elif (neg_q in qs if neg_q else pos_q not in qs) and pos_q not in qs:
                if len(neg_names) < N_PER_SIDE:
                    neg_names.append(n)
        pv = et(pos_p).mean(dim=0).numpy()
        nv = et(neg_p).mean(dim=0).numpy()
        axis_vec = pv - nv
        pos_scores = ea(load(pos_names)) @ axis_vec
        neg_scores = ea(load(neg_names)) @ axis_vec
        # Rank accuracy (AUC): P(random positive scores above random negative).
        grid = pos_scores[:, None] > neg_scores[None, :]
        auc = float(grid.mean())
        polarity = "+" if auc >= 0.5 else "REVERSED"
        eff_auc = max(auc, 1 - auc)
        verdict = ("strong" if eff_auc >= 0.8 else
                   "usable" if eff_auc >= 0.65 else "unreliable")
        table.append({"axis": axis, "n_pos": len(pos_names), "n_neg": len(neg_names),
                      "auc": round(auc, 3), "polarity": polarity, "verdict": verdict,
                      "pos_prompts": pos_p, "neg_prompts": neg_p})
        print(f"{axis:<16} {len(pos_names):>4} {len(neg_names):>4} {auc:>9.3f} "
              f"{polarity:>9}  {verdict}")

    out_path = Path(__file__).parent / "axis_calibration.json"
    out_path.write_text(json.dumps(
        {"model": MODEL, "dataset": "nsynth-test", "n_per_side": N_PER_SIDE,
         "axes": table}, indent=1))
    print(f"\nwrote {out_path}")


if __name__ == "__main__":
    main()
