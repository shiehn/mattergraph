"""Experiment 0-S follow-up: CLAP family identification on REAL sustained instruments.

Resolves the Exp 0-S confound (RESULTS_SUSTAINED.md): were the sustained-archetype
failures the evaluator's fault, or the fault of naive additive-sine stimuli?

Uses the NSynth test split (real 4 s instrument notes, labeled by family).
High accuracy here => evaluator is fine on real sustained timbre; the sustain
track's problem is stimulus quality (which is the research task itself).

Run from research/: .venv/bin/python exp0/exp0s_nsynth.py
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
PER_FAMILY = 16

FAMILY_PROMPTS = {
    "flute": ["a flute playing a sustained note", "a soft woodwind flute tone"],
    "organ": ["a sustained organ note", "a church or electric organ tone"],
    "string": ["a bowed violin sustained note", "bowed orchestral string instrument"],
    "brass": ["a brass instrument playing a sustained note", "a trumpet or trombone tone"],
    "reed": ["a clarinet or saxophone sustained note", "a reed woodwind instrument tone"],
    "synth_lead": ["a synthesizer lead tone", "an electronic synth lead note"],
}


def pick_examples() -> dict[str, list[str]]:
    meta = json.loads((NSYNTH / "examples.json").read_text())
    buckets: dict[str, list[str]] = {f: [] for f in FAMILY_PROMPTS}
    for name in sorted(meta):
        ex = meta[name]
        fam = ex["instrument_family_str"]
        if fam in buckets and 48 <= ex["pitch"] <= 72 and ex["velocity"] in (75, 100):
            buckets[fam].append(name)
    return {f: names[:PER_FAMILY] for f, names in buckets.items()}


def main() -> None:
    print(f"loading {MODEL} ...", flush=True)
    model = ClapModel.from_pretrained(MODEL)
    model.eval()
    proc = ClapProcessor.from_pretrained(MODEL)

    def unwrap(e):  # noqa: ANN001, ANN202
        return e if torch.is_tensor(e) else e.pooler_output

    families = list(FAMILY_PROMPTS)
    text_vecs = []
    for fam in families:
        inputs = proc(text=FAMILY_PROMPTS[fam], return_tensors="pt", padding=True)
        with torch.no_grad():
            e = unwrap(model.get_text_features(**inputs))
        text_vecs.append(torch.nn.functional.normalize(e, dim=-1).mean(dim=0))
    text_mat = torch.nn.functional.normalize(torch.stack(text_vecs), dim=-1).numpy()

    picked = pick_examples()
    print({f: len(v) for f, v in picked.items()})

    confusion = np.zeros((len(families), len(families)), dtype=int)
    for fi, fam in enumerate(families):
        waves = []
        for name in picked[fam]:
            x, sr = sf.read(NSYNTH / "audio" / f"{name}.wav", dtype="float32")
            assert sr == 16000
            waves.append(resample_poly(x, 3, 1).astype(np.float32))
        for start in range(0, len(waves), 8):
            chunk = waves[start:start + 8]
            try:
                inputs = proc(audio=chunk, sampling_rate=48000,
                              return_tensors="pt", padding=True)
            except (ValueError, TypeError):
                inputs = proc(audios=chunk, sampling_rate=48000,
                              return_tensors="pt", padding=True)
            with torch.no_grad():
                e = unwrap(model.get_audio_features(**inputs))
            av = torch.nn.functional.normalize(e, dim=-1).numpy()
            for row in av @ text_mat.T:
                confusion[fi, int(np.argmax(row))] += 1

    print("\n=== NSynth real-sample family identification (rows=true) ===")
    print("true\\pred " + "".join(f"{f[:8]:>10}" for f in families))
    accs = []
    for fi, fam in enumerate(families):
        total = confusion[fi].sum()
        acc = confusion[fi, fi] / max(total, 1)
        accs.append(acc)
        print(f"{fam:>9} " + "".join(f"{c:>10}" for c in confusion[fi]) + f"   acc {acc:.2f}")
    macro = float(np.mean(accs))
    chance = 1.0 / len(families)
    print(f"\nmacro accuracy {macro:.3f} vs chance {chance:.3f} "
          f"({macro / chance:.1f}x chance)")
    verdict = ("EVALUATOR OK on real sustained timbre -> Exp0-S failure was "
               "stimulus-limited; sustain synthesis research proceeds with CLAP"
               if macro >= 3 * chance else
               "EVALUATOR WEAK on real sustained timbre -> augment evaluator "
               "before sustain-track search")
    print("VERDICT:", verdict)


if __name__ == "__main__":
    main()
