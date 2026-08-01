"""MAP-Elites quality-diversity search over the MatterGraph genome space.

Plan §13: instead of re-rolling Sobol dice, maintain an ARCHIVE holding the
best skin found in every perceptual niche. Niches are interpretable descriptor
bins (plan §13.2 — never raw CLAP dimensions):

    decay_t60_s   6 log bins   0.05 … 12 s
    centroid_hz   6 log bins   150 … 8000 Hz
    flatness      4 bins       tonal … noisy

Fitness inside a cell (what "best" means):
    0.5 * playability  — velocity-contract quality (RMS rho, centroid rho)
    0.5 * nameability  — best CLAP match against a fixed vocabulary bank:
                         a skin text can FIND beats an equally-pretty orphan.

Seeded from an existing campaign atlas (its skins/features/embeddings/behavior
are copied in, so the output atlas is self-contained). Each iteration mutates
random elites (plus Sobol immigrants), renders through the real binary, gates,
and places winners. Archive checkpoints every batch — kill it any time.

Run: python -m mattergraph_research.qd --seed-atlas ../runs/c1.sqlite \
         --out ../runs/qd0.sqlite --evals 1500
"""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import numpy as np

from .atlas import Atlas
from .clap_embed import ClapEmbedder, MODEL_ID
from .contracts import score_skin as contract_score
from .features import extract_features
from .genome import Genome, mutate, sobol_genomes
from .render import render_many

REPO = Path(__file__).resolve().parents[2]
PROBES = {
    "diag_strike": REPO / "fixtures/probes/diag_strike.clipspec.json",
    "diag_bass": REPO / "fixtures/probes/diag_bass.clipspec.json",
    "diag_sustain": REPO / "fixtures/probes/diag_sustain.clipspec.json",
}

DECAY_EDGES = np.array([0.12, 0.3, 0.8, 2.0, 5.0])       # 6 bins over 0.05..12s
CENTROID_EDGES = np.array([300, 600, 1100, 2000, 3800])  # 6 bins over 150..8000
FLATNESS_EDGES = np.array([0.05, 0.12, 0.25])            # 4 bins

VOCAB = [
    "a delicate glass chime", "a wooden marimba hit", "a metal bell ringing",
    "a deep drum thump", "a low tom hit", "a kalimba pluck", "a tiny music box note",
    "a plucked string", "a bass stab", "a short dry click", "a rimshot",
    "a gong wash", "a resonant singing bowl", "icy shimmering percussion",
    "a dark muffled thud", "bright sparkling percussion", "a hollow tube knock",
    "a bowed string drone", "scraped metal texture", "a harsh noisy scrape",
    "a soft sustained drone", "a breathy evolving texture",
]


def cell_of(features: dict[str, float]) -> tuple[int, int, int]:
    d = int(np.searchsorted(DECAY_EDGES, features["decay_t60_s"]))
    c = int(np.searchsorted(CENTROID_EDGES, features["centroid_hz"]))
    f = int(np.searchsorted(FLATNESS_EDGES, features["flatness"]))
    return d, c, f


def playability(rms_rho: float, cent_rho: float) -> float:
    return 0.7 * float(np.clip(rms_rho, 0, 1)) + 0.3 * float(np.clip(cent_rho, 0, 1))


class Archive:
    def __init__(self) -> None:
        self.cells: dict[tuple[int, int, int], dict] = {}

    def place(self, cell: tuple[int, int, int], skin_id: str, fitness: float) -> bool:
        cur = self.cells.get(cell)
        if cur is None or fitness > cur["fitness"]:
            self.cells[cell] = {"skin_id": skin_id, "fitness": fitness}
            return True
        return False

    def qd_score(self) -> float:
        return float(sum(c["fitness"] for c in self.cells.values()))

    def checkpoint(self, path: Path, evals: int) -> None:
        path.write_text(json.dumps({
            "evals": evals,
            "coverage": len(self.cells),
            "qd_score": round(self.qd_score(), 3),
            "cells": {"/".join(map(str, k)): v for k, v in self.cells.items()},
        }, indent=1))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed-atlas", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--evals", type=int, default=1500)
    ap.add_argument("--batch", type=int, default=32)
    ap.add_argument("--rng-seed", type=int, default=17)
    args = ap.parse_args()

    embedder = ClapEmbedder()
    embedder.health_check()
    vocab_mat = embedder.embed_text(VOCAB)

    # Self-contained output atlas: copy the seed campaign in wholesale.
    args.out.parent.mkdir(parents=True, exist_ok=True)
    atlas = Atlas(args.out)
    atlas.conn.execute("ATTACH DATABASE ? AS seed", (str(args.seed_atlas),))
    for table in ("skins", "renders", "features", "embeddings"):
        atlas.conn.execute(f"INSERT OR IGNORE INTO main.{table} SELECT * FROM seed.{table}")
    atlas.conn.executescript(
        "CREATE TABLE IF NOT EXISTS behavior (skin_id TEXT PRIMARY KEY, "
        "vel_rms_rho REAL, vel_centroid_rho REAL, ok INTEGER);")
    try:
        atlas.conn.execute("INSERT OR IGNORE INTO main.behavior SELECT * FROM seed.behavior")
    except Exception:
        pass  # seed atlas may predate behavior scoring
    atlas.conn.executescript(
        "CREATE TABLE IF NOT EXISTS qd (skin_id TEXT PRIMARY KEY, cell TEXT, "
        "fitness REAL, playability REAL, nameability REAL, origin TEXT);")
    atlas.conn.execute("DETACH DATABASE seed")
    atlas.commit()

    rng = np.random.default_rng(args.rng_seed)
    archive = Archive()
    genome_of: dict[str, Genome] = {}

    # ---- Seed the archive from existing scored skins (no new renders). ----
    feats = atlas.skin_features()
    behavior = {sid: (r, c) for sid, r, c, ok in atlas.conn.execute(
        "SELECT skin_id, vel_rms_rho, vel_centroid_rho, ok FROM behavior").fetchall() if ok}
    render_ids, mat = atlas.render_matrix()
    name_by_skin: dict[str, float] = {}
    sims = mat @ vocab_mat.T
    for rid_skin, row in zip(render_ids, sims):
        best = float(row.max())
        if best > name_by_skin.get(rid_skin, -2.0):
            name_by_skin[rid_skin] = best
    seeded = 0
    for (sid, gj) in atlas.conn.execute("SELECT id, genome_json FROM skins").fetchall():
        f = feats.get(sid)
        if f is None or sid not in behavior or sid not in name_by_skin:
            continue
        try:
            genome_of[sid] = Genome.from_skin_json(gj)
        except (KeyError, ValueError, TypeError):
            continue  # anchors with non-genome extras still parse; malformed skip
        play = playability(*behavior[sid])
        name = float(np.clip(name_by_skin[sid] / 0.6, 0, 1))
        fit = 0.5 * play + 0.5 * name
        if archive.place(cell_of(f), sid, fit):
            atlas.conn.execute("INSERT OR REPLACE INTO qd VALUES (?,?,?,?,?,?)",
                               (sid, "/".join(map(str, cell_of(f))), fit, play, name, "seed"))
        seeded += 1
    atlas.commit()
    print(f"[qd] seeded {seeded} skins -> coverage {len(archive.cells)}/144, "
          f"QD-score {archive.qd_score():.1f}", flush=True)

    checkpoint_path = args.out.with_suffix(".archive.json")
    t0 = time.time()
    evals = 0
    new_cells = 0
    improved = 0
    while evals < args.evals:
        # ---- Propose a batch: elite mutations + Sobol immigrants. ----
        batch: list[Genome] = []
        elites = list(archive.cells.values())
        n_immigrants = max(2, args.batch // 6)
        for _ in range(args.batch - n_immigrants):
            parent_id = elites[rng.integers(len(elites))]["skin_id"]
            parent = genome_of.get(parent_id)
            if parent is None:
                continue
            batch.append(mutate(parent, rng))
        batch.extend(sobol_genomes(n_immigrants, base_seed=1000 + evals))

        # ---- Render: strike+bass+sustain per candidate (features+embeds). ----
        jobs, meta = [], []
        for i, g in enumerate(batch):
            for probe_name, probe in PROBES.items():
                jobs.append((g.skin_json(f"qd_{evals + i}"), probe, 5000 + evals + i))
                meta.append((i, probe_name))
        outcomes = render_many(jobs, workers=8)

        per_candidate: dict[int, dict] = {i: {} for i in range(len(batch))}
        waves, wave_keys = [], []
        for (i, probe_name), out in zip(meta, outcomes):
            if out.ok and out.audio is not None:
                per_candidate[i][probe_name] = out
                waves.append(out.audio.reshape(-1, 2).mean(axis=1))
                wave_keys.append((i, probe_name))
        embeds: dict[tuple[int, str], np.ndarray] = {}
        if waves:
            for key, vec in zip(wave_keys, embedder.embed_audio(waves)):
                embeds[key] = vec

        # ---- Score and place. ----
        for i, g in enumerate(batch):
            evals += 1
            strike = per_candidate[i].get("diag_strike")
            if strike is None:
                continue  # can't place without the canonical descriptor render
            f = extract_features(strike.audio)
            r_rms, r_cent, ok = contract_score(g.skin_json("qd_ladder"))
            if not ok:
                continue
            vecs = [embeds[k] for k in embeds if k[0] == i]
            if not vecs:
                continue
            name_raw = max(float((v @ vocab_mat.T).max()) for v in vecs)
            name = float(np.clip(name_raw / 0.6, 0, 1))
            play = playability(r_rms, r_cent)
            fit = 0.5 * play + 0.5 * name
            sid = g.content_id()
            cell = cell_of(f)
            was_empty = cell not in archive.cells
            if archive.place(cell, sid, fit):
                new_cells += int(was_empty)
                improved += int(not was_empty)
                genome_of[sid] = g
                atlas.add_skin(sid, f"qd_{evals}", g.skin_json(f"qd_{evals}"))
                rid = f"{sid}:diag_strike"
                atlas.add_render(rid, sid, "diag_strike", 0, True, "ok",
                                 strike.frames, strike.peak, strike.rms)
                atlas.add_features(rid, f)
                for (ci, probe_name), vec in embeds.items():
                    if ci == i:
                        rid_p = f"{sid}:{probe_name}"
                        out = per_candidate[i][probe_name]
                        atlas.add_render(rid_p, sid, probe_name, 0, True, "ok",
                                         out.frames, out.peak, out.rms)
                        atlas.add_embedding(rid_p, MODEL_ID, vec)
                        if probe_name != "diag_strike":
                            atlas.add_features(rid_p, extract_features(out.audio))
                atlas.conn.execute(
                    "INSERT OR REPLACE INTO behavior VALUES (?,?,?,1)", (sid, r_rms, r_cent))
                atlas.conn.execute(
                    "INSERT OR REPLACE INTO qd VALUES (?,?,?,?,?,?)",
                    (sid, "/".join(map(str, cell)), fit, play, name, "search"))
        atlas.commit()
        archive.checkpoint(checkpoint_path, evals)
        rate = evals / max(time.time() - t0, 1e-9)
        print(f"[qd] {evals}/{args.evals} evals  coverage {len(archive.cells)}/144  "
              f"QD {archive.qd_score():.1f}  (+{new_cells} new cells, {improved} improved, "
              f"{rate:.1f} evals/s)", flush=True)

    atlas.finalize_skin_embeddings(MODEL_ID)
    atlas.commit()
    print(f"[qd] done in {time.time() - t0:.0f}s -> {args.out}")


if __name__ == "__main__":
    main()
