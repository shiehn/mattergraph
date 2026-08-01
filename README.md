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

## Status

- [x] Canonical timeline from ClipSpec JSON (fixed tempo, sample-exact, immutable)
- [ ] Modal voice (exciter → modal bank → radiation) + offline renderer + WAV out
- [ ] MIDI fidelity audit artifact
- [ ] SMF ingestion (integer tick arithmetic, tempo maps)
- [ ] CLI (`mattergraph-render`)

## License

MIT — see `LICENSE`.
