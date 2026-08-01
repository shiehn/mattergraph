"""Experiment 0: Can CLAP rank material semantics on cheap modal-synthesis tones?

Synthesizes strike tones for four material archetypes using nothing but
exponentially decaying sinusoids (the modal model MatterGraph v0 proposes),
then measures LAION-CLAP text-audio cosine similarities:

  Test A (material identification): 4 materials x 4 material prompts.
     Pass = each audio's best-matching prompt is its own material (diagonal top-1).
  Test B (brightness monotonicity): one glass tone with 5 spectral tilts.
     Pass = similarity to "bright" prompts rises with tilt, "dark" falls (Spearman).
  Test C (decay semantics): short vs long ring of same spectrum.
     Pass = "short muted" vs "long ringing" prompts rank correctly.
"""

import numpy as np
import soundfile as sf
from pathlib import Path

SR = 48000
OUT = Path(__file__).parent / "audio"
OUT.mkdir(exist_ok=True)
rng = np.random.default_rng(48291)


def modal_tone(f0, ratios, amps, t60s, dur, strike_noise=0.0, strike_bright=0.5,
               shimmer_hz=0.0, tilt=0.0):
    """Sum of decaying sinusoids + optional strike transient. Returns mono float32."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    y = np.zeros(n)
    for r, a, t60 in zip(ratios, amps, t60s):
        f = f0 * r
        if f > SR * 0.45:
            continue
        a = a * (f / f0) ** tilt  # spectral tilt: >0 brighter, <0 darker
        tau = t60 / 6.91  # t60 = time to -60 dB
        partial = a * np.exp(-t / tau) * np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi))
        if shimmer_hz > 0:  # beating partner a few Hz off, like real glass/bells
            partial += 0.6 * a * np.exp(-t / tau) * np.sin(
                2 * np.pi * (f + rng.uniform(-shimmer_hz, shimmer_hz)) * t)
        y += partial
    # 3 ms raised-cosine attack so onset isn't a click artifact
    atk = int(0.003 * SR)
    y[:atk] *= 0.5 * (1 - np.cos(np.pi * np.arange(atk) / atk))
    if strike_noise > 0:  # short filtered noise burst = mallet/impact
        m = int(0.02 * SR)
        burst = rng.standard_normal(m) * np.exp(-np.arange(m) / (0.004 * SR))
        # crude brightness control: one-pole lowpass, darker = heavier smoothing
        alpha = 0.02 + 0.9 * strike_bright
        for i in range(1, m):
            burst[i] = alpha * burst[i] + (1 - alpha) * burst[i - 1]
        y[:m] += strike_noise * burst / (np.abs(burst).max() + 1e-9)
    return (0.9 * y / (np.abs(y).max() + 1e-9)).astype(np.float32)


def save(name, y):
    sf.write(OUT / f"{name}.wav", y, SR)
    print(f"wrote {name}.wav  ({len(y)/SR:.1f}s)")


# ---- Test A: four material archetypes, single strike, C4-ish ----
save("wood", modal_tone(
    261.6, ratios=[1, 3.9, 9.2, 14.0], amps=[1, 0.35, 0.12, 0.05],
    t60s=[0.35, 0.22, 0.12, 0.08], dur=3.0, strike_noise=0.35, strike_bright=0.15))

save("glass", modal_tone(
    523.3, ratios=[1, 2.32, 4.25, 6.63, 9.38], amps=[1, 0.7, 0.45, 0.3, 0.2],
    t60s=[2.2, 1.8, 1.4, 1.0, 0.7], dur=4.0, strike_noise=0.15, strike_bright=0.9,
    shimmer_hz=2.5))

save("metal", modal_tone(
    220.0, ratios=[0.56, 0.92, 1.0, 1.19, 1.71, 2.0, 2.74, 3.0, 3.76, 4.07],
    amps=[0.6, 0.8, 1, 0.75, 0.65, 0.55, 0.45, 0.4, 0.3, 0.25],
    t60s=[7.0, 6.0, 5.5, 5.0, 4.2, 3.6, 3.0, 2.6, 2.1, 1.8], dur=8.0,
    strike_noise=0.25, strike_bright=0.7, shimmer_hz=1.5))

save("drum", modal_tone(
    110.0, ratios=[1, 1.59, 2.14, 2.30, 2.65, 2.92], amps=[1, 0.6, 0.45, 0.4, 0.3, 0.25],
    t60s=[0.6, 0.45, 0.35, 0.3, 0.25, 0.2], dur=2.5, strike_noise=0.6, strike_bright=0.3))

# ---- Test B: brightness sweep on the glass archetype ----
for i, tilt in enumerate([-1.0, -0.5, 0.0, 0.5, 1.0]):
    save(f"glass_tilt_{i}", modal_tone(
        523.3, ratios=[1, 2.32, 4.25, 6.63, 9.38], amps=[1, 0.7, 0.45, 0.3, 0.2],
        t60s=[2.2, 1.8, 1.4, 1.0, 0.7], dur=4.0, strike_noise=0.15, strike_bright=0.9,
        shimmer_hz=2.5, tilt=tilt))

# ---- Test C: decay semantics, same spectrum short vs long ----
save("ring_short", modal_tone(
    329.6, ratios=[1, 2.32, 4.25, 6.63], amps=[1, 0.6, 0.4, 0.25],
    t60s=[0.12, 0.1, 0.08, 0.06], dur=2.0, strike_noise=0.3, strike_bright=0.6))
save("ring_long", modal_tone(
    329.6, ratios=[1, 2.32, 4.25, 6.63], amps=[1, 0.6, 0.4, 0.25],
    t60s=[4.5, 3.8, 3.0, 2.2], dur=7.0, strike_noise=0.3, strike_bright=0.6))

print("done")
