# MatterGraph for Signals & Sorcery
## Autonomous Build and Sound-Engine Research Handoff Plan

**Document status:** implementation specification  
**Revision:** 3.0  
**Date:** 2026-08-01  
**Primary product:** a compiled native sound renderer embedded in Signals & Sorcery.  
**Primary input:** immutable MIDI plus a natural-language sound request.  
**Primary output:** deterministic high-quality PCM/WAV/FLAC generated within a configured seconds-scale budget.  
**Optional mode:** immediate low-cost preview; VST3 is explicitly not a primary requirement.  

---

## 1. Executive handoff

Build a checkpointable autonomous engineering and research platform that creates, evaluates, maps, and improves a new graph-based sound engine for **Signals & Sorcery**.

The production path is:

```text
User request
    -> Gemini or another composition model produces MIDI
    -> prompt is parsed into a versioned SoundSpec
    -> native MatterGraph renderer receives immutable MIDI + SoundSpec
    -> renderer retrieves or constructs a compatible SoundSkin
    -> renderer produces deterministic audio
    -> Signals & Sorcery returns and stores the audio plus provenance
```

The engine is not a VST3 wrapper and does not depend on a DAW or third-party synthesizer. It is a compiled C++ component exposed through a command-line worker and a stable C ABI. A standalone laboratory is required for testing. A VST3 adapter may be added later, but it must not constrain the initial architecture.

Two distinct autonomous loops must be implemented:

1. **Autonomous build loop:** uses a Claude-first agent harness to construct and maintain the software repository incrementally, with sprint contracts, isolated worktrees, deterministic tests, independent review, budgets, checkpoints, and resumable sessions.
2. **Autonomous sound-engine research loop:** uses numerical search, CLAP, acoustic analysis, proxy models, and tightly governed architecture experiments to discover SoundSkins and improve the engine after evidence of capacity failure.

The LLM is never the high-frequency optimizer. It does not audition millions of candidates and does not decide success by intuition. Numerical systems produce evidence; agents plan, implement, review, diagnose, and propose controlled changes.

### 1.1 Non-negotiable product rule

> **Text determines the sound. MIDI determines exactly what is played.**

No language model, retrieval system, optimizer, SoundSkin, or offline renderer may add, delete, transpose, reorder, quantize, reharmonize, or retime MIDI events. Composition is complete before MatterGraph receives the MIDI.

### 1.2 Mandatory constraints

- Do not use Surge, Serum, Vital, or another fixed conventional synthesizer as the core engine.
- Ordinary oscillators may exist as optional graph nodes, but oscillator-filter-amplifier topology cannot be required.
- The production renderer must be compiled; C++20 is the default implementation language.
- Python may own research orchestration, CLAP, proxy training, analysis, and agent harnesses, but Python is not required in the distributed renderer.
- Engine topology, parameters, mappings, SoundSpecs, SoundSkins, and render requests must be schema-versioned and serializable.
- All unattended work must be checkpointable, resumable, budget-limited, auditable, and safe to interrupt.
- CLAP is a semantic evaluator and retrieval coordinate system, not the sole quality judge and not the renderer.
- Engine redesign must be triggered by evidence of expressive-capacity failure, not simply elapsed time.
- Agents may propose and implement changes, but only deterministic gates may merge or promote them.
- Runtime rendering may take seconds if that materially improves quality; exact MIDI fidelity remains mandatory.

## 2. Product definition

The production renderer accepts:

```text
Input A: Standard MIDI File bytes or canonical MIDI event timeline
Input B: original natural-language sound prompt
Input C: structured SoundSpec derived from the prompt
Input D: render profile, seed, sample rate, output format, engine compatibility policy
Output: deterministic PCM/WAV/FLAC plus complete provenance and a MIDI-fidelity audit
```

Example request:

```json
{
  "request_id": "rnd_01J8...",
  "midi_uri": "sha256:...",
  "sound_prompt": "warm glass percussion with a soft wooden attack",
  "sound_spec": {
    "schema_version": "1.0.0",
    "identity": {
      "family": "pitched_percussion",
      "materials": {"glass": 0.72, "wood": 0.28},
      "brightness": 0.61,
      "roughness": 0.14,
      "harmonicity": 0.53
    },
    "articulation": {
      "attack": "soft_impact",
      "sustain": "short_resonant_decay",
      "release_seconds": 1.8
    },
    "performance": {
      "velocity_to_brightness": 0.62,
      "velocity_to_hardness": 0.47,
      "pitch_tracking": "strict"
    }
  },
  "render_profile": "high_quality",
  "seed": 48291,
  "sample_rate": 48000,
  "channels": 2,
  "output_format": "wav_f32"
}
```

Example result:

```json
{
  "request_id": "rnd_01J8...",
  "status": "succeeded",
  "audio_uri": "sha256:...",
  "sound_skin_id": "skin_81b9...",
  "engine_version": "mattergraph-0.4.2",
  "renderer_binary_sha256": "...",
  "seed": 48291,
  "render_time_ms": 4386,
  "midi_fidelity": {
    "status": "passed",
    "event_count_input": 318,
    "event_count_render_timeline": 318,
    "added_notes": 0,
    "removed_notes": 0,
    "transposed_notes": 0,
    "max_event_time_error_samples": 0
  }
}
```

### 2.1 SoundSkin definition

A SoundSkin is a playable, searchable realization contract:

```text
SoundSkin = EngineGenome
          + valid pitch and velocity ranges
          + MIDI and expression mappings
          + semantic identity descriptors
          + behavior contracts
          + render-profile compatibility
          + quality, latency, and provenance metadata
```

It is not one WAV file and not one CLAP vector. It describes a sound family and how that family reacts to an immutable performance.

### 2.2 Product modes

**Preview** targets a first audible result in under 500 ms on reference hardware. It may retrieve one existing skin and use reduced rendering complexity.

**Fast** targets a final result in under 3 seconds for a 10-second MIDI passage. It may evaluate a small candidate set.

**High quality** targets under 15 seconds. It may retrieve and adapt several skins, fast-render candidates, score them, and render the winner with oversampling and more resonant detail.

**Studio** targets under 60 seconds. It may use a larger candidate pool, costly spectral stages, learned residuals, stem rendering, and deeper quality analysis.

Targets are budgets, not promises until benchmarked. Every profile is versioned and records the reference machine used to define its budget.

## 3. Exact MIDI authority contract

### 3.1 Immutable performance timeline

The MIDI parser must produce an immutable canonical timeline before sound generation begins. The timeline contains:

- exact note number or tuned frequency;
- note-on sample position;
- note-off sample position;
- attack velocity and release velocity;
- MIDI channel, track, note identity, and MPE zone identity;
- pitch bend and per-note pitch curves;
- channel pressure, polyphonic pressure, CC values, sustain, sostenuto, soft pedal, expression, modulation, and RPN/NRPN data;
- tempo map, time-signature metadata, SMPTE/tick basis, and event ordering;
- tuning table and A4 reference;
- original byte offsets or event indices for auditability.

Once canonicalized, the timeline is read-only. Sound generation consumes it but cannot rewrite it.

### 3.2 Exact pitch

For ordinary equal temperament, the base frequency is:

```text
f = A4_reference * 2^((midi_note - 69) / 12)
```

Pitch bend, MPE per-note pitch, tuning tables, and MIDI Tuning Standard data are applied deterministically. The synthesis graph receives the resulting pitch trajectory.

A SoundSkin may create inharmonic partials or material resonances, but its perceived pitch anchor must track the supplied trajectory. It may not silently choose a nearby pitch because the semantic score is higher.

### 3.3 Exact timing and duration

- Note-on and controller events are applied at the exact sample implied by the MIDI timeline.
- No quantization is permitted.
- Note-off occurs at the exact sample. A release tail may continue afterward according to the SoundSkin, but the release state must begin exactly at note-off.
- Tempo changes are converted with deterministic rational or high-precision arithmetic so accumulated drift remains below one sample at the end of the file.
- Same-tick event ordering follows a documented policy and preserves source ordering where the format provides it.

### 3.4 Polyphony and voice allocation

Offline rendering must not silently discard notes because of a low voice limit. Before rendering, calculate maximum simultaneous notes, pedal-held notes, and release tails. The renderer must then:

1. allocate enough voices within the profile limit;
2. choose a compatible SoundSkin with sufficient polyphony;
3. increase the offline voice budget if allowed; or
4. reject the request with a clear incompatibility error.

Silent voice stealing is prohibited in final offline profiles. Preview mode may use declared voice limits, but the result metadata must disclose any dropped voice and the UI must not present it as final.

### 3.5 Randomness boundaries

Randomness may change microtexture, initial phases, modal irregularity, particle positions, or noise realization. It may not change note identity, pitch trajectory, event timing, duration, velocity, or controller data. Random streams must be seeded by stable identifiers so adding one node does not unpredictably reseed unrelated voices.

### 3.6 MIDI fidelity audit

Every render writes a machine-readable audit comparing input events with the canonical performance timeline and the engine event trace. Required fields include event counts, note identity correspondence, timing error, pitch trajectory error, controller application, voice allocation, and violations.

A render with a failed MIDI audit cannot be committed as successful, regardless of semantic or audio quality scores.

## 4. SoundSpec and prompt separation

The original prompt is preserved for semantic retrieval and CLAP evaluation. The runtime also receives a versioned SoundSpec so an LLM is not required inside the compiled renderer.

The SoundSpec separates:

- **identity:** material, family, brightness, harmonicity, noisiness, mass, size, warmth, organic/synthetic character;
- **articulation:** attack mechanism, sustain mechanism, release character, transient hardness;
- **behavior:** velocity, register, duration, pressure, modulation, and repetition responses;
- **motion:** evolution, periodicity, randomness, instability, spatial movement;
- **constraints:** strict pitch tracking, minimum polyphony, maximum render time, prohibited transformations;
- **uncertainty:** fields the parser could not confidently infer.

The parser may use Gemini or another model upstream, but must return schema-valid structured output. Missing fields use explicit defaults; invented notes or performance edits are outside this schema.

## 5. Runtime architecture for Signals & Sorcery

```text
Signals & Sorcery application
  |-- composition service: prompt -> MIDI
  |-- SoundSpec service: prompt -> structured timbre request
  |-- sound-atlas service: text/SoundSpec -> candidate SoundSkins
  |-- render coordinator
       |-- validates MIDI and SoundSpec
       |-- selects render profile
       |-- launches crash-isolated native worker
       |-- stores audio and provenance
       '-- returns progress and result

Native worker process
  |-- canonical MIDI parser
  |-- MatterGraphCore C++ engine
  |-- SoundSkin loader/adaptor
  |-- optional bounded candidate refinement
  |-- offline renderer
  '-- MIDI-fidelity and quality audit

Research platform
  |-- candidate generation and quality-diversity search
  |-- CLAP and acoustic evaluation
  |-- proxy training and active learning
  |-- atlas construction
  '-- governed engine redesign
```

### 5.1 Production targets

1. `mattergraph-core`: framework-independent C++20 library.
2. `mattergraph-render`: native deterministic CLI executable.
3. `mattergraph-worker`: crash-isolated job protocol and Signals & Sorcery adapter.
4. `mattergraph-lab`: interactive standalone tester with prompt, MIDI upload, canned phrases, waveform/audio audition, and metadata.
5. `mattergraph-research`: Python research and agent environment; not part of end-user distribution.
6. Optional future adapters: shared library, C ABI, Rust/Node/Python bindings, VST3.

## 6. Language and distribution decisions

### 6.1 C++ production boundary

C++20 owns:

- MIDI parsing/canonical timeline;
- graph compiler and DSP;
- voice state and performance mappings;
- SoundGenome and SoundSkin loading;
- deterministic offline rendering;
- render profiles and safety limits;
- C ABI and CLI;
- production quality and MIDI audits.

The distributed renderer must not require Python, CUDA, an LLM SDK, or a database client unless a specific deployment package declares those dependencies. A CPU-only final-render path must exist.

### 6.2 Python research boundary

Python owns:

- campaign orchestration;
- CLAP and other embeddings;
- acoustic feature extraction;
- numerical search and MAP-Elites;
- proxy models and GPU training;
- autonomous coding/research agent harness;
- experiment database and console backend;
- analysis and report generation.

### 6.3 Stable C ABI

Expose a narrow C ABI even though implementation is C++:

```c
typedef struct MG_RenderRequest {
  const uint8_t* midi_data;
  size_t midi_size;
  const char* sound_spec_json;
  const char* sound_prompt_utf8;
  const char* render_profile;
  uint64_t seed;
  uint32_t sample_rate;
  uint32_t channels;
} MG_RenderRequest;

typedef struct MG_RenderResult {
  float* interleaved_audio;
  uint64_t frame_count;
  uint32_t sample_rate;
  uint32_t channels;
  char* metadata_json;
  char* error_message;
} MG_RenderResult;

int mg_render(const MG_RenderRequest*, MG_RenderResult*);
void mg_free_render_result(MG_RenderResult*);
```

The first production integration should use a separate worker process, not FFI inside the web server. A malformed or experimental graph can crash the worker without crashing Signals & Sorcery.

## 7. Repository layout

```text
mattergraph/
|-- AGENTS.md
|-- CLAUDE.md
|-- CMakeLists.txt
|-- pyproject.toml
|-- docs/
|   |-- adr/
|   |-- architecture/
|   |-- operations/
|   '-- licenses/
|-- engine/
|   |-- include/mattergraph/
|   |   |-- core/
|   |   |-- graph/
|   |   |-- midi/
|   |   |-- nodes/
|   |   |-- parameters/
|   |   |-- rendering/
|   |   |-- serialization/
|   |   '-- safety/
|   |-- src/
|   '-- tests/
|-- renderer/
|   |-- app/
|   |-- worker/
|   '-- tests/
|-- research/mattergraph/
|   |-- agents/
|   |-- atlas/
|   |-- checkpointing/
|   |-- console/
|   |-- database/
|   |-- evaluation/
|   |-- experiments/
|   |-- proxy/
|   |-- search/
|   |-- supervisor/
|   '-- workers/
|-- agent_harness/
|   |-- orchestrator/
|   |-- prompts/
|   |-- hooks/
|   |-- schemas/
|   '-- tests/
|-- schemas/
|-- migrations/
|-- midi/
|-- prompts/
|-- apps/lab/
|-- bindings/c/
|-- integrations/signals_and_sorcery/
|-- third_party/
|-- tools/
|-- tests/integration/
|-- tests/recovery/
'-- deploy/runpod/
```

`engine/` cannot link Python, network, database, LLM, or UI libraries. Research code invokes the renderer but may not duplicate DSP behavior.

## 8. MatterGraph engine specification

### 8.1 Conceptual model

The engine models:

```text
event -> energy -> material/body -> interaction -> radiation
```

Oscillation may occur inside a resonator, but the graph does not require a conventional oscillator as the primitive. Topology and parameters are both searchable.

### 8.2 Typed graph

Port types:

- `Event`: note-on, note-off, collision, trigger;
- `Pitch`: exact Hz or normalized pitch trajectory;
- `Energy`: nonnegative excitation signal;
- `Audio`: signed sample stream;
- `Control`: bounded modulation;
- `Envelope`: phase-aware control;
- `Spectrum`: block/frame-domain data;
- `State`: bounded persistent state exchange.

The graph compiler rejects type mismatches. Feedback requires explicit delayed edges and a stability contract.

### 8.3 Node interface

```cpp
class AudioNode {
public:
  virtual ~AudioNode() = default;
  virtual NodeDescriptor descriptor() const = 0;
  virtual PrepareResult prepare(const PrepareSpec&) = 0;
  virtual void reset(std::uint64_t seed) noexcept = 0;
  virtual void process(ProcessContext&) noexcept = 0;
  virtual NodeState saveState() const = 0;
  virtual LoadResult loadState(const NodeState&) = 0;
};
```

Even though rendering is offline, node code must remain bounded and deterministic. No unbounded loops, nondeterministic global state, hidden network access, or unmanaged allocation inside the inner sample/block loop.

### 8.4 Version-zero nodes

**Exciters:** impulse, colored noise burst, particle stream, friction/stick-slip, granular microtexture, optional band-limited periodic exciter.

**Bodies:** modal resonator bank, dispersive waveguide, membrane/plate approximation, formant cavity, coupled resonator network.

**Interactions:** energy coupling, collision, ring/amplitude interaction, waveshaping, saturation, spectral tilt, bounded feedback, delay, diffusion.

**Controllers:** envelopes, monotonic curves, random walks, sample-and-hold, bounded chaos, note-history state.

**Radiation:** stereo/mono output, mode-dependent radiation, room/body coupling, conditioning and emergency safety guard.

### 8.5 Parameter domains

Every parameter declares normalized range, physical/perceptual mapping, safe distribution, mutation distribution, dependencies, validity constraints, smoothing, update rate, cost estimate, and semantic tags.

Expose coherent high-level parameters rather than raw arrays. Example:

```text
material, size, rigidity, inharmonicity, mode density, damping, irregularity
```

These generate internally consistent modal frequencies and decays. A feedback `stability` parameter maps to a verified bounded feedback structure rather than raw gain.

### 8.6 Performance mappings

DSP state is a function of base genome plus immutable performance state:

```text
state = mapping(genome, note, velocity, note_age, pressure, CC, pitch_trajectory)
```

Mappings are bounded linear curves, monotonic splines, piecewise curves, envelopes, or safe expression graphs. They may affect timbre and articulation but cannot rewrite performance events.

### 8.7 Determinism

The same engine binary, genome, SoundSkin, MIDI bytes, SoundSpec, profile, seed, sample rate, block policy, floating-point policy, and platform compatibility class must produce the same result or pass declared numerical tolerances. Record all of these in provenance.

## 9. Render profiles and candidate refinement

The runtime search must be bounded. It may not redesign the engine while a user waits.

### Preview

- retrieve one compatible SoundSkin;
- reduced mode count and oversampling;
- no neural residual required;
- prioritize first-audio latency.

### Fast

- retrieve 4-8 candidate skins;
- adapt bounded semantic macros;
- fast-render a short diagnostic passage derived from the actual MIDI;
- score and render winner.

### High quality

- retrieve 8-32 candidates;
- run bounded local parameter optimization;
- fast-render full or representative phrase candidates;
- rank using semantic, behavior, fidelity, and quality ensemble;
- final render with more modes, oversampling, high-quality resampling, and optional learned residual.

### Studio

- wider candidate pool;
- multi-stage refinement;
- optional stems and spatial passes;
- multiple seeds with automatic and optional human selection;
- expensive neural or spectral stages.

Each profile has hard CPU/GPU, memory, candidate, model-call, and wall-clock budgets. If the budget is exceeded, return the best verified candidate and mark the result as budget-truncated; never continue indefinitely.

## 10. Renderer and worker protocol

Canonical CLI:

```bash
mattergraph-render \
  --midi /path/performance.mid \
  --sound-spec /path/sound_spec.json \
  --sound-prompt "dark rubber bass with wooden attack" \
  --profile high_quality \
  --seed 12345 \
  --sample-rate 48000 \
  --channels 2 \
  --output-dir /tmp/job-00001234
```

A research-only command may specify a genome or SoundSkin directly:

```bash
mattergraph-render \
  --sound-skin /path/skin.json \
  --midi /path/probe.mid \
  --profile research_probe \
  --seed 12345 \
  --output-dir /tmp/job-00001234
```

The worker writes into a temporary directory:

```text
audio.wav
render_result.json
midi_fidelity_audit.json
engine_event_trace.json.zst
quality_metrics.json
stderr.log
artifact_manifest.json
```

After validation, the coordinator content-hashes, fsyncs, atomically renames, and commits the artifact set.

Exit codes distinguish invalid MIDI, invalid schema, incompatible engine, invalid graph, pitch contract failure, timing contract failure, polyphony incompatibility, safety rejection, timeout, resource exhaustion, model failure, and internal crash.

## 11. Interactive laboratory

`mattergraph-lab` is the easiest test surface and must exist before production integration. It provides:

- natural-language prompt input;
- SoundSpec view/edit/validation;
- MIDI upload and six bundled phrases;
- event-timeline and pitch-range inspection;
- render-profile selector;
- seed control;
- start, cancel, checkpoint, and resume;
- candidate A/B audition;
- waveform/spectrogram and MIDI-fidelity status;
- SoundSkin and engine metadata;
- export of audio, request, result, and reproducibility bundle.

Bundled phrases:

1. bass groove with velocity variation;
2. sustained pad chords;
3. pluck arpeggio across register;
4. expressive lead with bend/pressure/modulation;
5. velocity ladder;
6. C1-C7 range sweep.

## 12. Evaluation architecture

### 12.1 Hard gates before semantic scoring

Reject or mark failure for:

- MIDI fidelity violation;
- non-finite samples;
- silence where notes exist;
- clipping or uncontrolled energy;
- unsupported pitch range;
- missing voices;
- excessive DC or ultrasonic energy;
- render timeout or memory breach;
- nondeterminism beyond declared tolerance.

### 12.2 Semantic evaluation

CLAP provides shared text/audio embeddings and supports retrieval and semantic similarity. Pin model, weights, preprocessing, sample rate, normalization, windowing, and text templates. Evaluate attack, sustain, release, phrase, and velocity/register slices separately.

Use frozen paraphrase sets and contrast prompts. Never optimize one prompt string only.

### 12.3 Acoustic and musical evaluation

Measure loudness, crest factor, spectral distribution, harmonicity, pitch confidence, inharmonicity, transient sharpness, attack/decay/release, modulation, noise ratio, stereo behavior, continuity, and tail stability.

Pitch tests must combine internal contract checks with output analysis. Output pitch detection alone is unreliable for highly inharmonic sounds, so the engine must also expose exact pitch-control traces and mode anchoring metadata.

### 12.4 Behavior contracts

For a request such as “dark when soft, metallic when hard,” render controlled velocity conditions and evaluate intended perceptual direction, monotonicity, smoothness, valid-range coverage, and unintended pitch/loudness changes.

### 12.5 Anti-gaming

CLAP is one objective in a Pareto or gated ensemble. Use acoustic metrics, negative prompts, paraphrases, multiple embedding models where justified, human calibration, and adversarial fixtures. Evaluators are versioned; an engine experiment cannot modify its evaluator.

## 13. Search, atlas, and proxy learning

### 13.1 Search portfolio

- schema-aware Sobol initialization;
- safe graph motifs;
- MAP-Elites quality-diversity archive;
- CMA-ES/local optimization for continuous parameters;
- topology mutation and motif crossover;
- novelty search over semantic, acoustic, behavioral, and topology distance;
- anchor-family exploration around curated skins;
- active learning using proxy score and uncertainty.

### 13.2 Atlas descriptors

Use interpretable axes: attack, duration, brightness, harmonicity, noisiness, evolution, pitch stability, stereo width, and cost class. Do not use raw CLAP dimensions as MAP-Elites axes.

### 13.3 Proxy models

Predict semantic embedding, acoustic features, MIDI fidelity risk, playability, behavior, safety, and render cost from genome + conditions. Use uncertainty. True-render high-scoring, high-uncertainty, and random control samples. The proxy never becomes the final judge.

## 14. Autonomous sound-engine research loop

The research loop operates after the base platform is working:

```text
EXPLORE
  -> render candidates
  -> hard MIDI/safety gates
  -> semantic/acoustic/behavior scoring
  -> atlas update
  -> proxy retraining when justified
  -> stall diagnosis
  -> optimizer or mapping intervention
  -> engine-capacity experiment only with evidence
```

A stall requires multiple consecutive windows and minimum sample counts. Diagnose optimizer failure, mapping failure, evaluator failure, or engine-capacity failure before changing architecture.

Engine experiments use one falsifiable hypothesis and one controlled change. Baseline and experiment receive the same prompts, seeds, probes, candidate counts, and compute budget. Promotion requires repeatable held-out gains, no MIDI regressions, safety, performance, and non-regression across existing sound families.

## 15. Autonomous software-build harness

The software-build harness is Claude-first but provider-adaptable. Use the Claude Agent SDK from Python rather than an unbounded shell loop. The harness—not the model—owns state transitions, turn limits, retries, checkpoints, worktrees, cost limits, and completion decisions.

Current long-running-agent practice favors a durable harness over one enormous prompt. The design must include:

- a specialized initializer session;
- incremental one-task sprints;
- sprint contracts negotiated before coding;
- generator and independent evaluator roles;
- fresh context for review;
- persistent progress artifacts and task ledger;
- deterministic hooks and test oracles;
- isolated worktrees;
- structured agent output;
- external session storage;
- watchdogs, max turns, retries, and hard budgets;
- explicit context retrieval rather than stuffing the full repository into each prompt.

### 15.1 Why not one continuous “keep coding” loop

Long sessions can overreach, lose context, declare completion early, or repeatedly revisit failed approaches. The harness should prefer bounded sprints with a clean repository state and explicit handoff. Continuous sessions may be used for a tightly scoped task when compaction is stable, but the durable source of truth is Git, the task ledger, test results, and decision artifacts—not conversational memory.

### 15.2 Initializer agent

The first session:

1. validates the bundle and dependency availability;
2. creates the repository skeleton and development container;
3. installs deterministic build/test commands;
4. creates `CLAUDE.md`, `AGENTS.md`, progress ledger, ADR index, and task database;
5. establishes the first clean Git commit;
6. runs a no-op checkpoint/resume drill;
7. does not begin broad feature implementation.

### 15.3 Sprint contract

Before code is written, a planner proposes a machine-readable contract containing:

- task ID and dependencies;
- exact scope and prohibited scope;
- files expected to change;
- interfaces and schemas involved;
- deterministic acceptance commands;
- performance and compatibility budgets;
- rollback plan;
- maximum turns, cost, and wall time.

A fresh evaluator approves, rejects, or narrows the contract. Implementation cannot begin without approval.

### 15.4 Implementer

The implementer receives only the approved contract, relevant just-in-time context, current tests, and prior failure notes. It works in an isolated worktree. After meaningful edits, hooks run format, lint, targeted unit tests, schema validation, and protected-path checks.

At the end, it returns structured output containing changed files, tests, unresolved risks, cost, commit, and evidence IDs. Narrative confidence is not an acceptance signal.

### 15.5 Independent reviewer and verifier

A fresh-context reviewer examines the diff and contract without sharing the implementer’s rationale unless needed. It looks for requirement drift, weak tests, unsafe assumptions, license issues, and overengineering.

A deterministic verifier runs the acceptance commands in a clean environment. For UI or laboratory features, an interactive evaluator may use browser automation. For DSP, the verifier uses compiled tests, sanitizers, golden/tolerance renders, MIDI audits, and benchmarks.

### 15.6 Fix loop and merge

If review or verification fails, return a bounded issue list to the implementer. Maximum default: three fix iterations. After that, mark the task blocked and request diagnosis rather than looping indefinitely.

Only the integration controller may merge. It rebases, reruns required gates, records provenance, updates the task ledger, writes the progress summary, and creates a build checkpoint.

## 16. Agent roles and authority

**Build coordinator:** deterministic state machine; leases tasks; creates worktrees; invokes agents; enforces budgets. It does not write product code.

**Planner:** translates one backlog item into a sprint contract. It cannot change product goals or held-out tests.

**Implementer:** writes code in one worktree. It cannot merge or alter protected specifications.

**Reviewer:** fresh-context code and architecture review. It cannot approve its own code.

**Verifier:** deterministic commands and optional interactive tests. Prefer scripts over model judgment.

**Integration controller:** merges only after gates pass and creates checkpoints.

**Research diagnostician:** analyzes empirical engine-search evidence and classifies stalls.

**Engine architect:** proposes one falsifiable engine experiment after capacity diagnosis.

**Human owner:** approves product-definition changes, unclear licenses, security-boundary changes, evaluator changes, and release candidates.

## 17. Agent parallelism

Parallel work is allowed only for independent tasks with explicit ownership.

- Use subagents for read-only research, codebase exploration, and independent review.
- Use separate worktrees for agents that edit files.
- Do not let multiple agents edit the same files concurrently.
- Agent teams are optional and experimental; the default is an external coordinator with isolated sessions because deterministic completion and collision handling are easier to audit.
- Background subagents must emit explicit completion records. The coordinator must wait for all required children and must not treat the first top-level result as proof that background work completed.

## 18. Context and memory strategy

Use just-in-time context:

- task contract and dependency summaries;
- file paths and exact line ranges;
- relevant ADRs;
- failing test output;
- latest progress ledger and failure memory;
- compact API/schema excerpts.

Do not inject the entire master plan on every turn. Keep canonical documents in the repository and let agents retrieve them as needed.

Persistent sources of truth:

```text
Git commits and tags
PostgreSQL/SQLite task ledger
ADR files
progress.ndjson
failure_memory.md
structured agent outputs
external session transcript store
artifact/checkpoint manifests
```

Claude session persistence stores conversation, not the complete filesystem. Git and artifact checkpoints remain mandatory.

## 19. Hooks, permissions, and sandboxing

Use hooks for deterministic control:

- before tool use: block protected paths, dangerous shell patterns, secret access, network use outside allowlist;
- after file edits: format, compile affected targets, run targeted tests;
- after tool failure: capture logs and classify retryability;
- before stop: require clean status or explicit documented dirty state, required tests, task result schema, and checkpoint;
- session end: flush transcript pointer, cost, task state, and worktree status.

Use a development container or sandbox. Agents have least privilege and no production secrets. Generated DSP binaries and untrusted graphs run in restricted subprocesses.

Required skills must be invoked explicitly in headless prompts and harness configuration. Do not rely solely on automatic skill triggering.

## 20. Session, checkpoint, and crash recovery for the build loop

A coding checkpoint contains:

- repository remote, base commit, worktree commit and diff hash;
- task and sprint-contract IDs;
- agent provider/model/version and session ID;
- external session-store pointer;
- structured outputs and test evidence;
- active child sessions;
- cost and turn counters;
- environment/container digest;
- pending integration action;
- retry count and last heartbeat.

Checkpoint triggers:

- after initializer;
- before and after every sprint;
- after approved contract;
- after implementation commit;
- after review and verification;
- before merge;
- on safe pause;
- every fixed time interval for long sessions.

A watchdog detects missing heartbeats, stalled tool activity, hung commands, and expired leases. It terminates the process, preserves logs, reclaims the lease, and resumes from the last stable state. Provider retries are bounded and classified. A retry cannot repeat a non-idempotent external action without an idempotency key.

## 21. Build-loop state machine

```text
BOOTSTRAP
  -> PLAN_TASK
  -> REVIEW_CONTRACT
  -> CREATE_WORKTREE
  -> IMPLEMENT
  -> STATIC_REVIEW
  -> VERIFY
       -> FIX (bounded) -> STATIC_REVIEW
       -> BLOCKED_DIAGNOSIS
       -> INTEGRATE
  -> CHECKPOINT
  -> SELECT_NEXT_TASK
  -> HALT_WHEN_RELEASE_GATE_PASSES
```

Completion is decided by the external verifier and release gate, not by the implementer saying “done.” Claude `/goal` or stop evaluators may assist local sessions, but production orchestration must have deterministic stop conditions and maximum turns.

## 22. Build-loop observability and cost controls

Record per session and task:

- input/output tokens and estimated cost;
- model/provider and prompt-cache use;
- tool calls, tool durations, and failures;
- files read/edited;
- test commands and results;
- number of review/fix iterations;
- wall time and idle time;
- worktree and commit;
- checkpoint and session-store IDs.

Export metrics, events, and traces through OpenTelemetry. Hard budgets exist at task, role, day, milestone, and campaign levels. When telemetry is missing, the loop stops rather than spending blindly.

Model allocation is configurable. Use the strongest model for architecture and difficult implementation, a strong independent model for review, and cheaper models only for bounded classification or summarization. Do not hard-code marketing model names into architectural contracts.

## 23. Checkpointing and recovery for the sound research loop

Research checkpoints include campaign config, engine binary/source hashes, database snapshot or WAL position, search population/archive, RNG states, proxy weights and optimizer, model/evaluator versions, job leases, pending render queue, committed artifacts, budget ledger, and agent experiment state.

Commit rule:

> A render result is either fully committed and content-addressed or safely replayable. There is no partially successful state.

Use temporary write, validate, hash, fsync, atomic rename, then database commit. Workers use leases, heartbeat, expiration, and idempotency keys.

## 24. Database and artifact model

Minimum tables:

- `engine_versions`, `build_manifests`, `node_types`;
- `sound_genomes`, `sound_specs`, `sound_skins`;
- `midi_assets`, `midi_events`, `render_requests`, `render_jobs`, `render_results`;
- `midi_fidelity_audits`, `quality_metrics`, `embeddings`, `atlas_entries`;
- `search_campaigns`, `search_state`, `proxy_models`, `engine_experiments`;
- `agent_tasks`, `agent_sprint_contracts`, `agent_sessions`, `agent_reviews`, `agent_checkpoints`;
- `artifacts`, `budgets`, `cost_events`, `human_reviews`.

Large immutable artifacts use content-addressed storage. Database rows reference hashes and storage URIs.

## 25. Signals & Sorcery integration contract

The platform submits a render job with MIDI bytes/hash, original prompt, validated SoundSpec, profile, seed, and output settings. The coordinator returns a job ID and progress events.

Recommended status model:

```text
queued -> validating -> retrieving_skin -> candidate_search -> final_render
       -> auditing -> committing -> succeeded
       -> failed | cancelled | budget_truncated
```

Cancellation is cooperative at candidate/render boundaries. Completed audio remains immutable. A project stores engine version, SoundSkin, prompt, SoundSpec, MIDI hash, seed, profile, and output hash so it can reproduce or intentionally migrate a sound.

## 26. Runpod and hardware

Use Runpod or equivalent for research GPU workloads. The sound renderer is mostly CPU DSP; CLAP embedding and proxy training use GPU. Start with one 24 GB GPU and at least 16 vCPUs/64 GB RAM. Prefer CPU-rich configurations over the fastest GPU with few cores.

Use a persistent network volume and external object-store backups. The supervisor and database should run on reliable on-demand compute until recovery is proven. Disposable render/embed workers may later use interruptible capacity.

The production native renderer can run on Signals & Sorcery servers without a GPU for precomputed skins. GPU becomes optional for runtime CLAP, neural residuals, or high-quality candidate selection.

## 27. Security and unattended-operation rules

- least-privilege tools and filesystem scopes;
- no production secrets available to coding agents;
- sandbox all generated code and untrusted genomes;
- no unlimited recursion or self-spawning;
- max turns, wall time, child count, and cost on every session;
- protected product contracts, held-out tests, evaluator weights, baselines, and license manifests;
- no automatic dependency import without license approval;
- no merge to main without deterministic gates;
- stop on missing telemetry, integrity failure, MIDI regression, or repeated nondeterminism;
- validate prompt length, MIDI size/events/duration, SoundSpec, and graph complexity.

## 28. Test strategy

### 28.1 MIDI contract tests

- parse and reproduce every event from fixtures;
- sample-accurate note/controller scheduling;
- tempo-map drift below one sample;
- pitch formula and tuning fixtures;
- pitch-bend and MPE curves;
- pedal semantics;
- dense polyphony without dropped notes in final profiles;
- no added/deleted/transposed/reordered notes;
- adversarial prompt cannot alter performance;
- event trace and audit reproducibility.

### 28.2 DSP tests

Unit tests for node bounds, parameter transforms, graph validation, state serialization, deterministic RNG, and safety. Property/fuzz tests for random graphs, parameter extremes, feedback, invalid schemas, and timeouts. ASan/UBSan on nightly campaigns.

### 28.3 Render tests

Golden or tolerance renders for deterministic components. Profile-budget benchmarks. Candidate search under fixed budgets. Crash-isolated worker tests. Audio artifact validation and content hashing.

### 28.4 Research validity

Equal-compute A/B, held-out prompts, multiple seeds, lineage-aware data splits, evaluator version pinning, proxy calibration, and blind human review.

### 28.5 Agent harness tests

- crash after every state transition;
- session-store resume on another host;
- worktree collision prevention;
- protected-path hook enforcement;
- hung agent watchdog;
- background child completion tracking;
- missing structured output fails closed;
- cost cap stops before overspend;
- implementer cannot approve own work;
- fresh checkout reproduces verifier results;
- deterministic release gate prevents premature completion.

## 29. CI/CD

Required pipelines:

- Linux C++ debug/release and unit tests;
- ASan/UBSan graph fuzz;
- renderer benchmark and MIDI contract suite;
- Python lint, types, schemas, database migrations;
- recovery chaos tests;
- agent-harness simulated-provider tests;
- dependency/license manifest;
- container reproducibility and binary manifest;
- optional Windows/macOS native builds for future distribution.

Research artifacts and release binaries cannot originate from a dirty tree. Engine versions are immutable once referenced by committed results.

## 30. Milestones and exit criteria

### M0 - Autonomous repository foundation

Initializer creates repo, dev container, task ledger, hooks, CI, external session storage adapter, cost telemetry, checkpoint/resume, and clean Git history.

**Exit:** kill the coordinator during a dummy task and resume on another process without losing task or session state.

### M1 - Exact MIDI vertical slice

C++ parser creates immutable timeline. Minimal graph renders `MIDI -> ImpulseExciter -> ModalBody -> RadiationOutput -> WAV`.

**Exit:** all MIDI authority tests pass; no CLAP required.

### M2 - Native renderer and laboratory

CLI/worker protocols, profiles, artifact commit, lab UI, prompt/SoundSpec input, MIDI upload and canned phrases.

**Exit:** non-developer can render, audition, export, cancel, checkpoint, and reproduce a result.

### M3 - Sound exploration loop

CLAP, acoustic features, queue, atlas, Sobol and MAP-Elites, SoundSkin schema and retrieval.

**Exit:** text + immutable MIDI returns ranked candidate skins above random baseline and passes exact MIDI audit.

### M4 - Behavior and high-quality rendering

Velocity/register/duration contracts, mapping evolution, candidate refinement, profile budgets, human review.

**Exit:** at least 50 curated SoundSkins satisfy prompt, playability, and MIDI-fidelity criteria.

### M5 - Signals & Sorcery integration alpha

Production render coordinator and worker integrated with project storage/provenance.

**Exit:** end-to-end prompt -> Gemini MIDI -> SoundSpec -> MatterGraph audio operates reliably with retries and cancellation.

### M6 - Autonomous engine redesign demonstration

Evidence-based stall diagnosis triggers one module/grammar experiment. Agent implements, A/B tests, promotes or rejects, and checkpoints without bypassing gates.

**Exit:** complete audit trail with equal-compute evidence and no MIDI regression.

### M7 - Research product alpha

Hundreds of curated skins, robust prompt families, stable high-quality profile, observability, cost controls, backups, and human evaluation report.

## 31. First autonomous coding engagement

The first engagement is complete only when:

1. repository, CI, dev container, task ledger, and harness exist;
2. schemas validate and examples round-trip;
3. initializer and one sprint execute through planning, implementation, review, verification, merge, and checkpoint;
4. six MIDI probes are stored with hashes;
5. C++ timeline parser and minimal graph render one probe;
6. event trace proves exact note/timing/pitch handling;
7. worker writes a validated artifact bundle;
8. coordinator can be killed and resumed;
9. no conventional synth or VST3 dependency is introduced;
10. all work is committed with test and cost evidence.

## 32. Risk register

**Agent builds the wrong product:** protect product contract and exact MIDI specification; sprint contracts cite acceptance tests.  
**Agent loops forever:** external maxTurns, watchdog, bounded fix loops, cost limits, blocked state.  
**Agent declares done early:** independent verifier and release gate.  
**Parallel agents collide:** isolated worktrees and file ownership.  
**Context loss:** persistent ledger, ADRs, Git, session store, just-in-time retrieval.  
**Background child work is lost:** explicit child lifecycle records and barrier before completion.  
**CLAP gaming:** hard MIDI/safety gates, evaluator ensemble, human calibration.  
**Random graph garbage:** safe motifs, semantic parameter domains, curriculum, quality-diversity archive.  
**Engine too narrow:** evidence-based capacity diagnosis and controlled module experiments.  
**Python prototype cannot ship:** C++ core from M1.  
**Render cost too high:** profile budgets, proxy pruning, cached embeddings, CPU/GPU metrics.  
**License contamination:** per-file provenance and CI license gate.  
**Cloud data loss:** content-addressed artifacts, persistent volume, external checkpoints.  

## 33. Coding-agent reading order

1. `README.md`
2. `AGENTS.md`
3. `CODING_AGENT_BOOTSTRAP_PROMPT.md`
4. `MASTER_PLAN.md`
5. `architecture/exact_midi_contract.md`
6. `signals_and_sorcery/integration_contract.md`
7. `agent_harness/README.md`
8. `schemas/*.json`
9. `config/*.yaml`
10. `db/schema.sql`
11. `tests/acceptance_matrix.md`
12. `backlog/implementation_backlog.csv`

Then select only the first unblocked P0 task. Do not skip to CLAP, engine evolution, or UI polish before the exact MIDI vertical slice and build-loop recovery are proven.

## 34. References

References and current agent-harness research are listed in `references/sources.md`. Critical design influences include Anthropic's long-running-agent initializer/incremental-session pattern, generator-evaluator sprint loops, deterministic test oracles, worktree isolation, hooks, Agent SDK session storage, structured outputs, cost tracking, and OpenTelemetry.

## Appendix A - Gemini review disposition

Accepted:

- make the headless renderer an explicit deterministic file-producing target;
- use the SoundSkin composition of genome, valid ranges, mappings, semantic descriptors, and behavior contracts;
- treat offline rendering as a first-class product path;
- retain a standalone text/MIDI laboratory.

Modified:

- the renderer is embedded in Signals & Sorcery rather than defined primarily as a VST3;
- output is not “creative MIDI interpretation”; MIDI is immutable and authoritative;
- a low-latency preview is optional, while seconds-scale high-quality rendering is primary;
- production distribution is compiled C++, while Python remains research infrastructure.

## Appendix B - Non-negotiable reminder

This project is not an agent turning knobs on an existing synth and not a text-to-audio model allowed to reinterpret a composition. It is an autonomous laboratory and native renderer for a new sound engine. **The text selects and shapes timbre; the MIDI remains exact.**
