# Experiment 0 — Can CLAP judge modal-synthesis timbre? (2026-08-01)

Run on Apple M4, CPU only. `uv venv`, `numpy soundfile torch "transformers>=4.49,<5"`.
Tones synthesized by `synth_modal.py` (pure decaying-sinusoid modal model — exactly
MatterGraph's proposed v0 primitive), scored by `score_clap.py`.

## Verdict: PASS with caveats — the research loop's evaluator is viable.

## Critical operational finding

`laion/larger_clap_music` **is broken via HuggingFace transformers** (tested on both
v4.57.6 and v5.14.1): every text and every audio input collapses to near-identical
embeddings (all pairwise sims ≈ 0.999 within modality, ≈ 0.007 across). No warning is
emitted. Any research campaign scored with it would have been silent garbage.

`laion/clap-htsat-unfused` works correctly (structured similarities, sane behavior).

**Consequence: the evaluator must have a known-answer smoke-test gate that runs before
every campaign. Pinning versions is not enough — this suite is that gate's seed.**

## Test A — material identification (4 materials × 4 prompt-sets, 2 paraphrases each)

| audio \ text | wood | glass | metal | drum | top-1 |
|---|---|---|---|---|---|
| wood  | 0.320 | 0.264 | 0.190 | **0.339** | drum ✗ (margin 0.019) |
| glass | 0.259 | **0.459** | 0.323 | 0.171 | glass ✓ |
| metal | 0.262 | 0.283 | **0.299** | 0.266 | metal ✓ |
| drum  | 0.226 | 0.083 | 0.041 | **0.487** | drum ✓ |

3/4 top-1 on first attempt, zero tuning. The wood miss is a near-tie against "drum" —
acoustically adjacent categories (a dark short C4 marimba strike *is* tom-like), and the
wood tone was the least carefully designed. Distinctive materials (glass, drum) separate
with wide margins.

## Test B — brightness monotonicity (spectral tilt sweep on glass tone)

bright-sim: 0.434 → 0.450 → 0.466 → 0.476 → 0.492 across tilt −1.0 … +1.0.
**Spearman(tilt, bright-sim) = +1.00.** A continuous engine parameter maps monotonically
onto CLAP's semantic axis — exactly what search needs. Caveat: "dark" prompts were flat
(~0.13) rather than anti-correlated; use contrastive scoring (bright − dark) and calibrate
per-axis direction rather than assuming both poles are informative.

## Test C — decay semantics (same spectrum, t60 0.1s vs 4s)

short audio: short-prompt 0.472 vs long-prompt 0.231 ✓
long audio: short-prompt 0.180 vs long-prompt 0.443 ✓
Clean, wide margins in both directions.

## Costs / speeds measured (M4, CPU)

- clap-htsat-unfused load: 1.7 s; audio embedding: **~75 ms/clip** (4 clips in 0.3 s).
  Runtime candidate scoring on CPU is affordable at this checkpoint size.
- Full experiment wall time including model download (~1.8 GB): ~10 min.

## Addendum (same day): blast radius narrowed

Probed the other checkpoints already cached on this machine (from prior experiments,
July 2026 and March 2026): `laion/larger_clap_general` and
`laion/larger_clap_music_and_speech` are **healthy** via transformers (structured sims,
audio-text spread 0.33–0.52). Only `laion/larger_clap_music` is degenerate. Prior work is
suspect only where that specific checkpoint was used. `music_and_speech` is a working
music-oriented alternative if the small general checkpoint proves too coarse.

## Follow-ups for the real evaluator health suite

1. Extend to 8–10 material archetypes and velocity ladders.
2. Cross-check the original `laion_clap` package checkpoints (`music_speech_audioset_...`)
   against the HF ports; also add MS-CLAP as a second opinion.
3. Verify which checkpoint `sas-patch-service` used for its 512-d index — if it was a
   `larger_clap_*` HF port, audit that index for the same degeneracy.
4. Add NSynth-labeled samples as an external known-answer calibration set.
