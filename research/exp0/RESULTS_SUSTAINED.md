# Experiment 0-S — CLAP on sustained material (2026-08-01)

**Purpose:** sustain-track step 0 (Plan Rev4 §Phase 4 cloud note): validate the evaluator
on sustained archetypes *before* renting any compute for the sustain research track.

**Verdict: GATE NOT PASSED. The sustain track stays local until this is resolved.**
The percussion loop is unaffected (Experiment 0 passed decisively there).

## Results (`exp0s_sustained.py`, laion/clap-htsat-unfused)

**S-A, archetype identification: 1/4.** Pad, bowed-drone, and flute archetypes were all
classified as "organ"; only organ itself was correct. Every naive archetype is built from
steady additive sines, so a serious confound exists: the stimuli may genuinely be
organ-adjacent, implicating the synthesis rather than the evaluator.

**S-B, brightness on pads: raw poles unreliable, contrastive works.** Raw "bright" similarity
*decreased* with rising cutoff (0.330 → 0.268) while "dark" correctly decreased too
(0.286 → 0.156). The contrastive score (bright − dark) is perfectly monotonic:
0.044 → 0.087 → 0.112. Same family of lesson as Experiment 0's flat dark-pole:
**never score a semantic axis with a single pole; always use contrastive prompt pairs.**

**S-C, static vs evolving: 1/2.** Static organ detected cleanly; the evolving pad tied
(0.079 vs 0.085). The evolution axis is not usable as-is.

## Interpretation

1. CLAP + naive sine-based sustained stimuli do not produce a usable identification or
   evolution signal, unlike percussion where naive modal tones worked immediately.
2. Two competing explanations, currently unresolved:
   a. CLAP is weak on sustained-timbre distinctions (evaluator limit);
   b. the stimuli are too crude — all four archetypes truly sound organ-like (stimulus limit).
3. The brightness axis IS usable on sustained material — contrastively.

## Required follow-up (still local, still $0) before any sustain-track pod rental

1. **Real-sample discrimination test:** score CLAP on genuine sustained instruments with
   known labels (NSynth family labels: flute/organ/string/synth-pad, or renders through
   Surge XT via sas-app) — separates evaluator-limit from stimulus-limit cleanly.
2. If real samples separate well → the fix is better sustained *synthesis* (driven
   excitation, the actual research question) and step 0 re-runs against early engine output.
3. If real samples also confuse → the sustain track needs a different/augmented evaluator
   (e.g., music_speech checkpoint, MS-CLAP second opinion, acoustic-feature-forward scoring)
   before search can be trusted.

## Standing rule adopted project-wide

All semantic-axis scoring (SoundSpec identity fields included) uses **contrastive prompt
pairs with empirically verified polarity**, recorded per axis in the evaluator config.

---

## Addendum (same day): NSynth real-sample test — confound RESOLVED

`exp0s_nsynth.py` scored CLAP family identification on real NSynth test-split notes.

**First pass (naive taxonomy): macro 0.365 (2.2× chance)** — but the "failures" were
substantially artifacts: the prompt set included `synth_lead`, which has **zero examples
in the test split** (pure distractor); NSynth flutes are 69% *synthetic* and organs 100%
*electronic*, so "electronic synth" answers were semantically defensible.

**Refined pass (source-filtered, honest prompts): macro 0.500 (3.0× chance).**
Per-family: brass **1.00**, string **0.94**, organ **0.88** (was 0.00 — fixed *entirely*
by prompting "electric organ" instead of "church organ"), reed 0.19 (drifts to brass —
sax/clarinet vs brass is a genuinely adjacent timbre pair), vocal 0.00 (n=7, reads as
organ — steady sung vowels vs pipe organ, a classic acoustic adjacency), flute n=0
(filter artifact: acoustic flutes need relaxed velocity/pitch constraints; TODO).

**Resolved verdict for the sustain track:**
1. The Exp 0-S archetype failure was **substantially stimulus- and taxonomy-limited**,
   not a blanket evaluator failure — CLAP hears real sustained timbre well at the
   coarse-family level.
2. CLAP is **usable for sustained-material search** with the same discipline the
   percussion loop already adopted: contrastive pairs, verified polarity, paraphrase
   sets — plus acoustic-feature co-scoring for adjacent-timbre pairs (reed/brass,
   vocal/organ) where CLAP alone is unreliable.
3. Prompt wording is part of the evaluator and must be validated per family
   (the organ 0.00 → 0.88 swing came from two words).
4. Sustain-track step 0 status: **conditionally passed** — search may proceed once the
   driven-excitation prototype produces stimuli, scored coarse-family + contrastive
   axes; pod rental unblocks when local campaign throughput becomes the bottleneck.
