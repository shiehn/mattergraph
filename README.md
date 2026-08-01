# MatterGraph

A compiled sound renderer for Signals & Sorcery: immutable MIDI + a natural-language
sound request in, deterministic audio out. **Text determines the sound; MIDI determines
exactly what is played.**

## Layout

- `engine/` — `mattergraph-core`, framework-free C++20. No Python, network, LLM, or UI
  dependencies, ever (plan §7).
- `tests/` — contract tests (Catch2). The MIDI authority contract is enforced here.
- `fixtures/probes/` — canonical probe phrases (ClipSpec JSON; SMF later).
- `docs/plan/` — the working plan (`MatterGraph_Plan_Rev4.md`), the build guide, and the
  archived rev 3.0 spec whose contracts rev 4 carries forward by reference.
- `docs/adr/` — architecture decision records.
- `research/exp0/` — Experiment 0: empirical validation that CLAP can judge modal timbre
  (see `RESULTS.md`; audio regenerates deterministically via `synth_modal.py`).

## Build & test

```bash
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Render something

```bash
./build/mattergraph-render \
  --midi fixtures/probes/bass_groove.clipspec.json \
  --skin skins/anchors/glass.json \
  --seed 48291 --sample-rate 48000 --normalize -1.0 --out /tmp/mg-out
```

Writes `audio.wav`, `render_result.json`, and `midi_fidelity_audit.json`.
Anchor skins: `skins/anchors/{glass, wood_bar, metal_bell, membrane}.json`.

## Status

- [x] Canonical timeline from ClipSpec JSON (fixed tempo, sample-exact, immutable)
- [x] Modal voice (exciter → modal bank → radiation) + offline renderer + WAV out
- [x] MIDI fidelity audit artifact (0-sample tolerance, enforced in tests and CLI)
- [x] CLI (`mattergraph-render`) with exit-code taxonomy
- [x] First anchor skins (glass, wood bar, metal bell, membrane)
- [x] SMF ingestion (formats 0/1, exact 128-bit tick arithmetic — zero tempo-map drift by construction)
- [x] Probe phrases: bass groove, velocity ladder, pluck arpeggio, pad chords, range sweep
- [x] CI: Linux + macOS build/test + ASan/UBSan job
- [ ] Expressive-lead probe (needs pitch bend/pressure in the timeline — reserved)
- [ ] Research loop v0 (Sobol → render → gates → CLAP/features → atlas)
- [ ] More anchor skins via lab curation

## License

MIT — see `LICENSE`.
