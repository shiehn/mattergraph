"""Score the modal tones against material/brightness/decay prompts with LAION-CLAP."""

import numpy as np
import soundfile as sf
import torch
from pathlib import Path
from transformers import ClapModel, ClapProcessor

AUDIO = Path(__file__).parent / "audio"
MODEL = "laion/clap-htsat-unfused"

print(f"loading {MODEL} ...", flush=True)
model = ClapModel.from_pretrained(MODEL)
processor = ClapProcessor.from_pretrained(MODEL)
model.eval()

def _unwrap(e):
    return e if torch.is_tensor(e) else e.pooler_output  # v4 returns tensor, v5 returns ModelOutput

def embed_audio(names):
    waves = []
    for n in names:
        y, sr = sf.read(AUDIO / f"{n}.wav", dtype="float32")
        assert sr == 48000
        waves.append(y)
    try:
        inputs = processor(audio=waves, sampling_rate=48000, return_tensors="pt", padding=True)
    except (ValueError, TypeError):
        inputs = processor(audios=waves, sampling_rate=48000, return_tensors="pt", padding=True)
    with torch.no_grad():
        e = _unwrap(model.get_audio_features(**inputs))
    return torch.nn.functional.normalize(e, dim=-1)

def embed_text(prompts):
    inputs = processor(text=prompts, return_tensors="pt", padding=True)
    with torch.no_grad():
        e = _unwrap(model.get_text_features(**inputs))
    return torch.nn.functional.normalize(e, dim=-1)

def spearman(x, y):
    rx = np.argsort(np.argsort(x)).astype(float)
    ry = np.argsort(np.argsort(y)).astype(float)
    rx -= rx.mean(); ry -= ry.mean()
    return float((rx * ry).sum() / np.sqrt((rx**2).sum() * (ry**2).sum()))

# ---------- Test A: material identification ----------
materials = ["wood", "glass", "metal", "drum"]
# Two paraphrases per material, averaged — the plan's frozen-paraphrase idea in miniature
prompt_sets = {
    "wood":  ["a wooden percussion hit, like a marimba or woodblock",
              "warm woody knock with a soft attack"],
    "glass": ["a glass chime being struck, bright and delicate",
              "glassy crystalline percussion with a clear ring"],
    "metal": ["a metal bell ringing with long sustain",
              "metallic clang with inharmonic overtones"],
    "drum":  ["a drum hit, deep membrane percussion",
              "a low tom drum thump"],
}
A = embed_audio(materials)
sims = {}
for m, prompts in prompt_sets.items():
    T = embed_text(prompts)
    sims[m] = (A @ T.T).mean(dim=1).numpy()  # mean over paraphrases

print("\n=== Test A: material identification (rows=audio, cols=text) ===")
header = "audio\\text " + "".join(f"{m:>8}" for m in materials)
print(header)
correct = 0
for i, m_audio in enumerate(materials):
    row = np.array([sims[m_text][i] for m_text in materials])
    best = materials[int(row.argmax())]
    mark = "OK " if best == m_audio else "MISS"
    correct += best == m_audio
    print(f"{m_audio:>10} " + "".join(f"{v:8.3f}" for v in row) + f"   top-1: {best} {mark}")
print(f"Test A result: {correct}/4 diagonal top-1")

# ---------- Test B: brightness monotonicity ----------
tilt_names = [f"glass_tilt_{i}" for i in range(5)]
B = embed_audio(tilt_names)
bright_T = embed_text(["a bright shimmering glassy tone", "brilliant sparkling high-frequency chime"])
dark_T = embed_text(["a dark muffled dull tone", "a muted lowpassed thud with no highs"])
bright_s = (B @ bright_T.T).mean(dim=1).numpy()
dark_s = (B @ dark_T.T).mean(dim=1).numpy()
tilts = np.array([-1.0, -0.5, 0.0, 0.5, 1.0])
print("\n=== Test B: brightness monotonicity (tilt -> similarity) ===")
for t, bs, ds in zip(tilts, bright_s, dark_s):
    print(f"tilt {t:+.1f}   bright-sim {bs:.3f}   dark-sim {ds:.3f}")
rb, rd = spearman(tilts, bright_s), spearman(tilts, dark_s)
print(f"Spearman(tilt, bright)={rb:+.2f} (want strongly +), Spearman(tilt, dark)={rd:+.2f} (want strongly -)")

# ---------- Test C: decay semantics ----------
C = embed_audio(["ring_short", "ring_long"])
short_T = embed_text(["a short muted staccato percussive hit that stops immediately"])
long_T = embed_text(["a long ringing sustained resonant tone that decays slowly"])
cs = (C @ short_T.T).squeeze(-1).numpy()
cl = (C @ long_T.T).squeeze(-1).numpy()
print("\n=== Test C: decay semantics ===")
print(f"short audio: short-prompt {cs[0]:.3f} vs long-prompt {cl[0]:.3f}  -> {'OK' if cs[0] > cl[0] else 'MISS'}")
print(f"long audio:  short-prompt {cs[1]:.3f} vs long-prompt {cl[1]:.3f}  -> {'OK' if cl[1] > cs[1] else 'MISS'}")
