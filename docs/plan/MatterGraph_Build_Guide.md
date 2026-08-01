# MatterGraph Build Guide — hardware, provisioning, and first-month steps

**Date:** 2026-08-01 · Companion to `MatterGraph_Plan_Rev4.md`. Every step has a gate; stop and reassess at any failed gate — that's the point.

---

## 1. Hardware: what to provision, when, and what it costs

### 1.1 Now (Phases 0–5): your Mac is enough — with one fix

Measured on this machine: Apple M4, 10 cores (4P+6E), 16 GB RAM. Modal rendering is ~0.6 GFLOPS for 8 voices × 200 modes — >20× realtime on one core. CLAP (`clap-htsat-unfused`) embeds at ~75 ms/clip on CPU. A 10k-render local mini-campaign is an overnight job, not a cloud job.

**The one real constraint: ~31 GB free disk.** Do this before anything else:
- `~/sas-platform/sas-app/` is carrying ~110 MB of stale `output.log.*` files plus other cruft — prune.
- Hugging Face cache (`~/.cache/huggingface`) already holds ~4 GB from Experiment 0 (both CLAP checkpoints; the broken `larger_clap_music` one can be deleted).
- Budget ~10 GB for toolchains/builds, ~5 GB for HF cache, ~10 GB working audio. If you dip under ~15 GB free, buy a 1 TB external NVMe SSD (~$80–120) and point `HF_HOME`, the artifact store, and campaign dirs at it. This is the only local hardware purchase to consider.

Do **not** buy a new Mac or a local GPU for this project. 16 GB RAM is adequate for everything local (CLAP small checkpoint ~600 MB resident; C++ builds are small; the app itself is the biggest RAM user).

### 1.2 Phase 6 (research campaigns): one RunPod pod, rented, disposable

Rendering is CPU-bound; embedding and (later) proxy training want GPU. Current RunPod pricing: RTX 4090 ≈ **$0.34/hr community / $0.69/hr secure**; A100 80GB ≈ $1.39/hr; L40S ≈ $0.39–0.79/hr.

**Provision exactly this to start:**
1. **Pod:** RTX 4090, *community cloud*, filtered to offers with **≥ 16 vCPU and ≥ 60 GB RAM** (offers vary per host — the vCPU count matters more than the GPU; reject low-CPU offers). ~$0.34/hr, billed per second. Stop it when not campaigning.
2. **Network volume:** 150 GB attached persistent volume (~$0.07/GB/mo ≈ **$10/mo**) for the SQLite atlas, checkpoints, elite audio, and the HF cache.
3. **Off-pod backup:** Backblaze B2 or Cloudflare R2 bucket (~$1–3/mo at this scale). Nightly `rclone sync` of the atlas DB + curated skins + campaign configs. The pod and even the volume are disposable; the bucket is not.
4. **Image:** RunPod PyTorch template (CUDA 12.x) + `apt install cmake ninja-build g++-13` + `uv`. Your renderer is a static-ish Linux x86_64 binary built in CI — don't hand-compile on the pod.

**Costs at realistic usage:** 8 h/day campaigning for 2 weeks ≈ 112 h × $0.34 ≈ **$38 GPU + $10 volume + $2 backup ≈ $50/mo** during active research. Upgrade to A100/L40S only when proxy-model training epochs measurably dominate wall time — not before.

**Skip entirely for now:** Kubernetes, serverless endpoints, multi-node, spot fleets, Postgres server, OTel stack. Single pod + SQLite + rclone until a real bottleneck appears.

### 1.2b Cloud-first variant (owner is cloud-ready — noted 2026-08-01)

Since provisioning a VM/pod is not a constraint, Phases 2+ may go cloud-first instead of waiting for Phase 6:
- Campaigns, atlas, HF cache, and artifact store live on the pod + network volume from day one — the local 31 GB disk constraint mostly evaporates (sync only curated elites down for listening).
- The **sustain experiment** runs as a parallel track on its own pod once the Phase-2 loop exists (see Plan §Phase 4 cloud note). Its step 0 is local and free: CLAP validation on sustained archetypes before any engine work. A CPU-only pod is a legitimate cheaper choice for it — rendering is CPU-bound and small-CLAP embeds fine on CPU; take the 4090 only when embedding throughput is the measured bottleneck.
- What stays on the Mac regardless: the macOS arm64 renderer build (sas-app integration requires it; CI builds both platforms), the lab UI, and listening/curation.
- If the *autonomous build loop* itself moves to a VM later: use a ~$20–40/mo CPU VM (Hetzner/EC2-class), not a GPU pod; tmux + git-push-per-sprint as checkpoints; hard API spend cap set before the first unattended run. Isolation solves blast radius, not the harness-lite ROI ordering.

### 1.3 Production serving (Phase 5+)

The renderer is CPU-only by design (rev 3.0 §26 — correct, keep). It ships inside sas-app on the user's Mac; server-side rendering, if ever needed, runs on any x86/arm CPU box. GPU never enters the product path for precomputed skins.

### 1.4 LLM/API budget (the other "hardware")

- Composition/SoundSpec parsing: already flows through sas-gateway → Gemini; no new keys or infra.
- Agent-built code (harness-lite): Claude Code on your existing plan for interactive/sprint work. If you later run headless Agent-SDK loops (Phase 6), set a **hard $25/day cap** in the API console before the first unattended run.

---

## 2. Software provisioning (local, one-time, ~30 min)

```bash
# Toolchain
xcode-select --install                          # clang, if not present
brew install cmake ninja                        # build system
brew install libsndfile                         # or vendor dr_wav.h — zero-dep option
# Catch2 via CMake FetchContent (no brew needed)

# Python side (already proven in Experiment 0)
# uv is installed; per-project:
uv venv .venv && uv pip install numpy soundfile librosa "transformers>=4.49,<5" torch

# Repo
mkdir ~/mattergraph && cd ~/mattergraph && git init
```

**Pinned decisions (from Experiment 0 — do not relitigate silently):**
- CLAP checkpoint: `laion/clap-htsat-unfused`, transformers pinned `>=4.49,<5`.
- `laion/larger_clap_music` via HF transformers is **broken** (degenerate embeddings, no warning). Banned until re-verified through the original `laion_clap` package.
- Every campaign starts by running the evaluator health suite (seeded from `exp0/score_clap.py`); a failing suite aborts the campaign.

**No JUCE, no Tracktion, no VST3 SDK in mattergraph** — the C++ core needs only the standard library + a WAV writer. This keeps the license surface clean (rev 3.0 §1.2 honored structurally) and builds in seconds.

---

## 3. Step-by-step: the first four weeks

### Week 0 (done — 2026-08-01)
- ✅ Experiment 0: CLAP validated as timbre judge on modal tones (3/4 material ID, +1.00 brightness monotonicity, 2/2 decay). Broken checkpoint caught. Artifacts in `exp0/`.

### Week 1 — C++ vertical slice (Plan §Phase 1)

**Day 1–2: repo + canonical timeline.**
- CMake skeleton: `engine/` (library), `renderer/` (CLI), `tests/`. CI: GitHub Actions macOS arm64 + Linux x86_64, debug+release, ASan/UBSan job.
- Implement ClipSpec-JSON ingestion first (sas-app's wire format: `{pitch, start_qn, dur_qn, vel, chan}` + tempo/timesig header), then SMF type 0/1. Rational-arithmetic tick→sample conversion; sustain pedal; stable event ordering.
- Commit the six probe phrases (bass groove w/ velocity, pad chords, pluck arpeggio, expressive lead, velocity ladder, C1–C7 sweep) as both ClipSpec JSON and SMF fixtures.
- **Gate:** timeline round-trip tests green; tempo-drift test < 1 sample at end-of-file.

**Day 3–4: modal voice + renderer.**
- Fixed topology: NoiseBurst/Impulse exciter (hardness, color, level) → ModalBank (macros: size, rigidity, inharmonicity, mode density, damping curve, irregularity; velocity→brightness/hardness mapping) → Radiation (width, safety limiter). Per-voice RNG: counter-based (Philox-style), keyed by (note id, seed) so one extra note never reseeds others.
- Offline block renderer, voice budget check before render (no silent stealing), f32/24-bit WAV out.
- **Gate:** `mattergraph-render --midi probe.json --skin glass.json --seed 1 -o out/` produces audio + `midi_fidelity_audit.json`; repeat run byte-identical; ≥ 20× realtime.

**Day 5: audits + hand-designed anchors.**
- Fidelity audit comparing input events ↔ engine event trace (counts, identities, sample-exact times).
- Hand-tune the first 6 anchor skins by ear in a tight loop (render probe → listen → tweak JSON): glass, wood bar, metal bell, membrane, pluck, bass stab. *You* are the sound designer this week; search comes later.
- **Gate (week):** all Phase-1 gates green; you can *listen* to six materially-distinct skins playing the same immutable MIDI.

### Week 2 — Research loop v0 (Plan §Phase 2)

**Day 1–2:** genome schema (JSON Schema, versioned); Sobol sampler over macro space; subprocess render harness (parallel across 8 cores); hard gates (NaN/silence/clip/fidelity).
**Day 3:** feature extractor (librosa: loudness, crest, centroid, rolloff, flatness, attack/decay, harmonicity) + pinned CLAP embedder + **evaluator health suite wired as the campaign preflight**.
**Day 4:** SQLite atlas (`genomes`, `renders`, `features`, `embeddings`, `scores`, `provenance`); content-addressed artifact dir; delete-losing-audio policy (keep metadata; elites keep audio).
**Day 5:** retrieval CLI: `mg-search "warm glass percussion" --topk 5 --probe pluck_arp` → renders top-k on the probe, opens a compare page. Overnight: first 10k-render local campaign.
- **Gate:** on 30 held-out prompts, top-3 beats random baseline; ≥ 50% of top-1s humanly "plausible." **Kill/pivot rule:** if search ≤ anchors after ~50k renders, stop scaling and fix evaluator/search first (Plan §Phase 2).

### Week 3 — Lab + curation (Plan §Phase 3)
- FastAPI + one static page: prompt box, probe picker, MIDI upload, seed/profile, render buttons, waveform + A/B audition, save-as-skin with tags, export reproducibility bundle. Playwright smoke test.
- Curate ≥ 25 skins through the lab (budget ~3–4 focused listening hours).
- **Gate:** prompt → audition < 2 min; 25 tagged skins in the atlas.

### Week 4 — Behavior contracts + first integration spike (Plan §Phases 4–5)
- Behavior contract renders (velocity ladder → monotonic brightness check — Experiment 0's Test B generalized); CMA-ES local refinement of retrieval winners; measure and record profile budgets on the M4.
- Integration spike in a sas-app worktree: stage binary under `resources/mattergraph/arm64/`, one ToolRegistry tool (`mattergraph_render_track`), respect render lock + `renders` cache + −14 dBFS RMS / −0.3 dBFS peak conventions, `SAS_MATTERGRAPH=0` kill switch, `build.files` entry.
- **Gate:** one real project renders a percussion track through MatterGraph inside the app, A/B against the Surge path; worker kill mid-render → clean error, app unaffected.

### Then: decision point
With Phases 1–3 gates green you have evidence, sounds, and an integration path. Only now decide: scale research on RunPod (§1.2), start the Phase-4 sustain experiment, or ship the percussion-first alpha. All three are justified by data you'll actually have.

---

## 4. Fail-fast tripwires (paste into the task ledger)

1. Evaluator health suite fails → abort campaign, fix evaluator, never "just rerun."
2. Repeat-render not byte-identical on same platform → stop feature work until fixed.
3. Any MIDI-audit failure on a committed render → the commit is invalid, full stop (rev 3.0 §3.6).
4. Search not beating anchors after 50k renders → pivot per Plan §Phase 2.
5. A week with no new audio you can *listen to* → you're building infrastructure, not the product; re-read Plan §0 change 1.
6. Local free disk < 15 GB → prune or move artifact store to external SSD before the next campaign.
7. Unattended anything without a hard cost cap → don't.
