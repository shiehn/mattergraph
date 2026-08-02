"""Parallel render harness driving the compiled mattergraph-render binary.

Hard gates (plan §12.1) run here: audit failure, silence, uncontrolled energy,
and non-success exit codes all mark the render as gate-failed. Audio is
ephemeral by policy — determinism means any render is reproducible from
(engine, genome, seed) — so WAVs live in a temp dir and are deleted after
feature extraction unless keep_audio is set.
"""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf

DEFAULT_BIN = Path(os.environ.get(
    "MG_RENDER_BIN",
    Path(__file__).resolve().parents[2] / "build" / "mattergraph-render",
))
EXCITER_DIR = Path(__file__).resolve().parents[2] / "assets/exciters"
WAVETABLE_DIR = Path(__file__).resolve().parents[2] / "assets/wavetables"

RMS_SILENCE_GATE = 10 ** (-60 / 20)  # -60 dBFS


@dataclass
class RenderOutcome:
    ok: bool
    reason: str
    frames: int = 0
    peak: float = 0.0
    rms: float = 0.0
    audio: np.ndarray | None = None  # interleaved float32, only when ok
    sample_rate: int = 48000


def render_once(skin_json: str, midi_path: Path, seed: int,
                sample_rate: int = 48000, keep_audio: bool = True) -> RenderOutcome:
    with tempfile.TemporaryDirectory(prefix="mgr_") as td:
        tdir = Path(td)
        skin_path = tdir / "skin.json"
        skin_path.write_text(skin_json)
        out_dir = tdir / "out"
        proc = subprocess.run(
            [str(DEFAULT_BIN), "--midi", str(midi_path), "--skin", str(skin_path),
             "--seed", str(seed), "--sample-rate", str(sample_rate),
             "--exciter-dir", str(EXCITER_DIR),
             "--wavetable-dir", str(WAVETABLE_DIR), "--out", str(out_dir)],
            capture_output=True, text=True, timeout=120,
        )
        if proc.returncode != 0:
            return RenderOutcome(False, f"exit_{proc.returncode}:{proc.stderr.strip()[:120]}")

        result = json.loads((out_dir / "render_result.json").read_text())
        audit = json.loads((out_dir / "midi_fidelity_audit.json").read_text())
        if audit.get("status") != "passed":
            return RenderOutcome(False, "audit_failed")
        if result.get("peak_limited"):
            return RenderOutcome(False, "uncontrolled_energy")
        if result.get("rms", 0.0) < RMS_SILENCE_GATE:
            return RenderOutcome(False, "silent")

        audio = None
        if keep_audio:
            data, sr = sf.read(out_dir / "audio.wav", dtype="float32")
            audio = np.asarray(data)
            assert sr == sample_rate
        return RenderOutcome(True, "ok", int(result["frames"]), float(result["peak"]),
                             float(result["rms"]), audio, sample_rate)


def render_many(jobs: list[tuple[str, Path, int]], workers: int = 8,
                sample_rate: int = 48000) -> list[RenderOutcome]:
    """jobs: (skin_json, midi_path, seed) -> outcomes in the same order."""
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(render_once, s, m, seed, sample_rate) for s, m, seed in jobs]
        return [f.result() for f in futures]
