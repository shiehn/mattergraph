# MatterGraph

A compiled sound renderer for Signals & Sorcery: immutable MIDI + a natural-language
sound request in, deterministic audio out. **Text determines the sound; MIDI determines
exactly what is played.**

Plan and build guide live in `~/Desktop/TheMatterGraph/` (`MatterGraph_Plan_Rev4.md`,
`MatterGraph_Build_Guide.md`, `exp0/` evaluator validation).

## Layout

- `engine/` — `mattergraph-core`, framework-free C++20. No Python, network, LLM, or UI
  dependencies, ever (plan §7).
- `tests/` — contract tests (Catch2). The MIDI authority contract is enforced here.
- `fixtures/probes/` — canonical probe phrases (ClipSpec JSON; SMF later).
- `docs/adr/` — architecture decision records.

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
