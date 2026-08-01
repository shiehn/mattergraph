# MatterGraph for Signals & Sorcery — Revised Plan

**Revision:** 4.0 (supersedes rev 3.0 sequencing and scope; rev 3.0 contracts carry forward where cited)
**Date:** 2026-08-01
**Basis:** rev 3.0 review + sas-platform architecture audit + Experiment 0 (empirical CLAP validation, see `exp0/RESULTS.md`) + prior-art review (CTAG/ICML 2024, CLAP timbre-semantics research)

---

## 0. What changed from rev 3.0 and why

| # | Change | Reason |
|---|--------|--------|
| 1 | **Re-sequenced for fail-fast.** Rev 3.0 built the autonomous harness first (M0) and tested the riskiest bet — "CLAP-guided search finds prompt-matching sounds" — at M3. Rev 4.0 tested it on day 0 (done, passed) and puts sound-in-ears before infrastructure. | Fail-fast principle. The existential risk was the evaluator, not the plumbing. |
| 2 | **Evaluator health gate is now a hard, mandatory, known-answer test suite** run before every campaign, seeded from `exp0/`. | Experiment 0 found `laion/larger_clap_music` silently broken via HF transformers — degenerate embeddings, no warnings. Version pinning alone would not have caught it. |
| 3 | **Autonomous build harness descoped to "harness-lite"** (Claude Code + worktrees + CI gates + task ledger + hooks + budget caps). The full rev 3.0 §15–§22 orchestration platform is deferred to Phase 6, built only when unattended overnight campaigns are actually blocked without it. | Rev 3.0 M0 is weeks of meta-engineering before any product risk is retired. 80% of its value ships free with existing tooling. |
| 4 | **v0 sound-family target narrowed to struck/plucked/percussive/bell/mallet + short bass stabs.** Pads and sustained leads are an explicit Phase-4+ research question, not a v0 promise. | Exciter→resonator engines are percussion-native. Sustained harmonic tones need driven/nonlinear excitation — genuinely harder. CTAG (the closest prior art) produced "abstract, sketch-like" results; calibrate promises accordingly. |
| 5 | **MatterGraph is routed per-role, not positioned as a Surge replacement.** sas-app's role taxonomy (kicks/bass/pads/…) routes percussion/pluck/bell roles to MatterGraph first; Surge XT path remains for roles MatterGraph can't serve yet. | De-risks the product: partial success is shippable. sas-app already A/Bs sound sources per track. |
| 6 | **MIDI ingestion accepts two formats: SMF and sas-app's ClipSpec-style JSON** (`{pitch, start_qn, dur_qn, vel, chan}` quarter-note wire format). MPE, MTS, sostenuto deferred (schema reserves fields). | sas-app's Gemini pipeline emits ClipSpec, not MPE-rich SMF. Rev 3.0 §3 parsed far more MIDI than the producer produces. Exactness contract unchanged — scope of *what* is exact is right-sized. |
| 7 | **Runtime CLAP scoring is allowed on CPU** with the small checkpoint (~75 ms/clip measured on M4); precomputed-embedding retrieval remains the primary runtime path. | Measured in Experiment 0. Rev 3.0 assumed GPU needed for runtime semantics. |
| 8 | **SQLite + content-addressed artifact dir + ndjson journals until multi-node.** Postgres/OTel/leases arrive with the RunPod fleet, not before. Losing candidates' audio is deleted — determinism means any render is reproducible from (engine hash, genome, seed); keep embeddings/features/metadata only. | Storage math: 100k renders ≈ 380 GB if audio kept, ≈ 300 MB as metadata. Local disk has 31 GB free. |
| 9 | **Search space v0 is the macro-parameter space of a fixed modal topology** (materials/damping/inharmonicity/exciter macros), not free graph topology. Topology mutation and motif crossover move to Phase 6. | Quality-diversity over a well-conditioned 20–40-dim macro space is where CTAG-class methods demonstrably work. Free topology search multiplies failure modes before the basics are proven. |
| 10 | **Anchor-first search.** 15–25 hand-designed anchor skins are Phase-2 deliverables; search explores around and between anchors. Search is a candidate factory; humans curate. | Rev 3.0 had anchors as one bullet among many. Prior art says uncurated search output is sketch-grade; anchors set the quality floor. |
| 11 | Added **integration conventions from the sas-app audit** (§7 below): loudness normalization, stems/cache contracts, binary staging, kill switches. | The Explore audit found exact seams; matching them makes Phase 5 a small diff instead of a project. |

**Unchanged from rev 3.0 (carried forward by reference):**
- §1.1 non-negotiable rule: **text determines the sound; MIDI determines exactly what is played.**
- §3 exact-MIDI authority contract and fidelity audit (minus MPE/MTS/sostenuto scope, per change 6).
- §2.1 SoundSkin definition; §4 SoundSpec/prompt separation; §8.5–8.7 parameter domains, mappings, determinism.
- §10 renderer/worker protocol and artifact commit discipline; §12 evaluation architecture (hard gates → semantic → acoustic → behavior; anti-gaming ensemble).
- §27 security rules; §28 test strategy.

---

## 1. Evidence base (what we now know, not believe)

1. **CLAP can judge modal timbre** (Experiment 0, 2026-08-01, `exp0/RESULTS.md`): 3/4 material top-1 identification, +1.00 Spearman brightness monotonicity, 2/2 decay semantics — on first-attempt naive tones with `laion/clap-htsat-unfused`. Weaknesses found: wood/drum adjacency confusion; "dark" pole weakly informative → use contrastive scoring and per-axis calibration.
2. **The exact concept has been published**: CTAG (Cherep & Singh, ICML 2024, MIT-licensed code) — evolutionary search over a 78-param interpretable synth guided by LAION-CLAP. It works and runs on CPU; output quality is "abstract, sketch-like." MatterGraph's deltas: physically-grounded modal primitives (materials are *native* parameters, not emergent), SoundSpec constraints, behavior contracts, and hard MIDI-fidelity gates.
3. **LAION-CLAP is the best available timbre judge**: 2025 study (arXiv:2510.14249) found it aligns best with human timbre perception among MS-CLAP/LAION-CLAP/MuQ-MuLan, specifically on brightness/roughness/warmth — the SoundSpec identity axes. Checkpoints Apache-2.0.
4. **sas-app already ships the retrieval half of this product**: `sas-patch-service` does NL → CLAP text embedding → cosine retrieval over a patch index with probe-phrase reranking (bass-riff/low-sustain/mid-phrase/pad-chord), default-on, via `preset-retrieval-service.ts` / `patch-index-service.ts`. MatterGraph = the same retrieval pattern over an *owned, unbounded, deterministic* sound space instead of a fixed Surge preset library. Team knowledge transfers 1:1.
5. **Modal rendering cost is a non-issue**: ~8 FLOPs/mode/sample ⇒ 8 voices × 200 modes × 48 kHz ≈ 0.6 GFLOPS — >20× realtime single-threaded on one M4 core. Candidate fan-outs are CPU-cheap; the expensive step is embedding, at ~75 ms/clip (small checkpoint, CPU).
6. **Integration seams exist and are proven** (sas-app audit): per-track prompt already stored and CLAP-consumed; MIDI already in clean quarter-note wire form; per-layer render abstraction with content-hash cache (`renders`, `layer_stems`); precedent for bundled native binaries (`resources/<name>/<arch>/`, spawned+health-checked) and for non-Tracktion audio sources (Lyria textures).

---

## 2. Phase plan (each phase has a gate and a kill/pivot rule)

### Phase 0 — Evaluator validity — **DONE (2026-08-01)**
Result above. Residual work folded into Phase 2: extend known-answer suite to 8–10 archetypes + velocity ladders; second-opinion checkpoint (MS-CLAP or original `laion_clap` weights); audit which checkpoint built the sas patch index.

### Phase 1 — Exact-MIDI C++ vertical slice (goal: ~1–2 weeks)
Build `mattergraph-core` + `mattergraph-render` (CLI): canonical timeline (SMF type 0/1 + ClipSpec JSON; rational-arithmetic tempo map; sustain pedal; velocity; channels), one hardcoded modal SoundSkin, voice allocator with no silent stealing, seeded per-voice RNG keyed by stable IDs, f32 WAV out, `midi_fidelity_audit.json` + `render_result.json`, exit-code taxonomy (rev 3.0 §10).
**Nodes (fixed topology v0):** NoiseBurst/Impulse exciter (hardness, color, level) → ModalBank body (material macros: size, rigidity, inharmonicity, mode density, damping curve, irregularity; velocity→brightness/hardness mappings) → Radiation (stereo spread, safety limiter).
**Tests:** MIDI contract fixtures (rev 3.0 §28.1 minus deferred scope), golden/tolerance renders, byte-identical repeat-run determinism test, ASan/UBSan job, render benchmark. Six bundled probe phrases committed as fixtures (rev 3.0 §11 list).
**Gate:** all contract tests green; 10 s / 8-voice phrase renders ≥ 20× realtime; two runs byte-identical; audits pass on all six probes.
**Kill/pivot:** none — this is known engineering. If it takes > 3 weeks, the problem is process, not feasibility.

### Phase 2 — Research loop v0 against the real binary (goal: ~1–2 weeks)
Python (`mattergraph-research`): genome schema v0 = macro params of the fixed topology; Sobol init; render via CLI; hard gates (silence/clipping/NaN/fidelity); acoustic features (loudness, crest, centroid, rolloff, flatness, attack/decay times, harmonicity); pinned CLAP embed with **evaluator health suite as a pre-campaign gate**; SQLite atlas (genomes, features, embeddings, scores, provenance); retrieval = prompt → text embed → cosine top-k → probe-phrase rerank. Delete losing audio; keep metadata (change 8).
**Also:** 15–25 hand-designed anchor skins (glass, wood bar, metal bell, membrane, pluck, kalimba, bass stab, …) — these are product assets *and* search seeds.
**Gate (falsifiable):** on ≥ 30 held-out prompts, top-3 retrieval beats random-skin baseline on the blended score, and human audit rates ≥ 50% of top-1 results "plausibly what I asked for."
**Kill/pivot:** if 50k+ renders of search add nothing over hand-designed anchors, pivot the product to curated-library-plus-interpolation (still a real product over an owned engine) and investigate evaluator/search upgrades before scaling compute.

### Phase 3 — Laboratory + curation (goal: ~1 week)
`mattergraph-lab` as a local FastAPI + static-page web app (fastest to build; Playwright-testable like the rest of sas): prompt box, SoundSpec view/edit, MIDI upload + six probes, profile/seed controls, render, waveform + audio A/B, fidelity status, skin metadata, tag/save/export reproducibility bundle (rev 3.0 §11 feature list).
**Gate:** non-developer path prompt → audition < 2 min; ≥ 25 curated skins saved through the lab.

### Phase 4 — Behavior contracts, profiles, and the sustain experiment (goal: ~2 weeks)
Velocity/register/duration behavior contracts rendered and scored (rev 3.0 §12.4); bounded CMA-ES refinement of top retrieval candidates; preview/fast/high-quality profiles with budgets *measured on the M4 reference machine* and recorded in provenance.
**Sustain experiment (explicit research question):** can driven excitation (banded-noise bow, stick-slip friction, optional band-limited periodic exciter — permitted by rev 3.0 §1.2) produce pad/lead-class sustains that pass behavior contracts? Timeboxed; outcome decides whether pads/leads enter the v1 roadmap or stay routed to Surge.
**Cloud note (2026-08-01):** owner is prepared to fund VM/RunPod compute for this track. It therefore need not be serialized at Phase 4 — it can start as a **parallel research track on its own pod** as soon as the Phase-2 loop exists. Its own step 0 (local, ~1 day, $0, before renting anything): an Experiment-0-style CLAP validation on *sustained* material — pad/bowed/blown archetypes, movement/evolution axes — because Experiment 0 only proved the evaluator hears percussion. Compute buys campaign scale here (sustained candidates cost ~5–10× percussion candidates: 10 s renders, larger genomes); it does not substitute for the excitation-design and evaluator-fit questions, which stay on the critical path.
**Gate:** ≥ 50 curated skins passing prompt-match + behavior + fidelity criteria (rev 3.0 M4 exit).

### Phase 5 — Signals & Sorcery integration alpha (goal: ~1 week)
Ship the renderer exactly like existing native tools and match app conventions (§7 below). Per-track opt-in (`engine: "mattergraph"`); percussion/pluck/bell roles first (change 5).
**Gate:** 10 real projects rendered end-to-end (prompt → Gemini MIDI → SoundSpec → MatterGraph audio) with retries, cancellation, cache hits, and crash isolation demonstrated (kill worker mid-render → clean error, app unaffected).

### Phase 6 — Scale: RunPod campaigns, topology search, proxy models, harness hardening
Move campaigns to RunPod (see Build Guide); introduce MAP-Elites over interpretable descriptors (rev 3.0 §13.2 axes), then topology motifs; train proxy models when >100k labeled renders exist; adopt the rev 3.0 §15–§23 orchestration/checkpoint machinery *incrementally, as unattended scale demands it* — each piece justified by an actual observed failure (lost run, collided worktree, blown budget), not built speculatively.

### Phase 7 — Governed engine evolution
Rev 3.0 §14/§16/M6 unchanged: evidence-based capacity-stall diagnosis, one falsifiable controlled experiment, equal-compute A/B, deterministic promotion gates. Deferred until the atlas has enough statistics for "stall" to be measurable.

---

## 3. Quality expectations (honest calibration)

- **Where MatterGraph should win immediately:** prompt-specific struck/plucked/resonant timbres ("warm glass percussion with a soft wooden attack") — modal synthesis is *native* here and Surge presets are not; deterministic re-render; per-note behavior contracts; zero third-party licensing in the sound path.
- **Where it will lose for a while:** lush pads, classic subtractive leads, anything whose identity is oscillator-culture. Route those roles to the existing Surge path until the Phase-4 sustain experiment says otherwise.
- **Uncurated search output will sound sketch-like** (CTAG's finding). The product ladder is: hand-designed anchors (good now) → search-around-anchors (better, curated) → free search (novelty mine, curated hardest).

## 4. SoundSpec notes (delta to rev 3.0 §4)

- Parser runs through the existing sas-gateway Gemini structured-output path (`generateWithLLMTools` pattern) — no new LLM plumbing, keys stay server-side.
- Calibrate per-axis polarity empirically (Experiment 0: "bright" informative, "dark" flat) — the SoundSpec→score mapping uses contrastive prompt pairs with measured directionality, never single adjectives.
- Keep raw prompt alongside SoundSpec for open-vocabulary retrieval (rev 3.0 already requires this; Experiment 0 confirms text-embedding retrieval carries real signal).

## 5. Determinism scope (delta to rev 3.0 §8.7)

- Bit-exact: same binary + same platform class (single-threaded render path, FTZ/DAZ set, no libm in inner loops or pinned approximations).
- Cross-platform: declared tolerances only; never promise bit-exactness across OS/arch.
- CI enforces: repeat-run byte-identity, and a golden-tolerance suite that runs on every merge.

## 6. Harness-lite (replaces rev 3.0 §15–§22 for Phases 1–5)

- **Claude Code** sessions per task; fresh session for review (generator/evaluator separation preserved); git worktrees for parallel tasks; `tasks.md`/SQLite ledger; hooks: format+targeted tests on edit, protected paths (`schemas/`, golden fixtures, evaluator config) read-only to implementers.
- **Deterministic gates:** CI (GitHub Actions) = build + unit + contract + sanitizer + golden-tolerance + license manifest. Nothing merges red. Release binaries only from clean tags.
- **Budgets:** hard monthly API cap; per-session turn caps; stop-on-missing-telemetry not required yet — a daily cost glance is enough at this scale.
- The full state-machine harness (sprint contracts, watchdogs, cross-host resume, OTel) is Phase 6 work, adopted piecewise when real unattended failures justify each piece.

## 7. Integration contract with sas-app (from the architecture audit)

- **Ship shape:** `resources/mattergraph/{arm64}/mattergraph-render`, staged like `sas-audio-tool` / `sas-stem-splitter`; spawned + health-checked like `audio-engine-manager.ts` does; **must be added to electron-builder `build.files`/`extraResources`** (release-only failure mode, CLAUDE.md invariant #6).
- **Inputs:** track MIDI via `engine.getTrackMidiNotes` wire form or ClipSpec JSON; prompt from `tracks.prompt` / scene `plugin_data` (already the CLAP-retrieval input today, `preset-tools.ts:320-350`).
- **Outputs:** 24-bit (or f32) stereo WAV, **RMS-normalized to −14 dBFS, peak-limited −0.3 dBFS** to match `SceneRenderer` conventions; written into the `renders`/`layer_stems` content-hash cache contracts (cache key must include engine version + skin id + seed).
- **Surface:** one ToolRegistry tool (auto-becomes a `sas` CLI verb via `/api/v1/actions`); coverage-ledger + parity tests apply; renders take the global `transition-render-lock` like every other long render.
- **Ops:** kill switch env (`SAS_MATTERGRAPH=0` pattern, like `SAS_SEMANTIC_PATCHES` / `SAS_PLUGIN_SANDBOX`); `project_id` scoping on any new tables; crash-isolated worker (rev 3.0 §6.3's process-not-FFI decision — reaffirmed, it matches the app's existing architecture).

## 8. Risk register (delta view)

| Risk | Rev 4.0 status |
|---|---|
| CLAP can't judge timbre | **Retired empirically** (Exp 0) for percussion-class; monitor per-axis polarity |
| Evaluator silently broken | **Materialized once already**; mitigated by mandatory known-answer gate |
| Search output quality ceiling | Open; mitigated by anchor-first + curation ladder + per-role routing fallback |
| Sustained tones (pads/leads) | Open; explicit timeboxed Phase-4 experiment; product fallback = Surge routing |
| Harness over-engineering | Mitigated by harness-lite + adopt-on-failure rule |
| Storage/disk blowout | Mitigated by delete-losing-audio (determinism) + metadata-only atlas |
| Single-human curation bottleneck | Bounded: 50 skins ≈ 3–4 focused listening hours; revisit at hundreds |
| License contamination | Same as rev 3.0; CLAP checkpoints Apache-2.0; CTAG code MIT if consulted; no GPL in the C++ core |
