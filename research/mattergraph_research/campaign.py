"""Campaign runner: Sobol genomes -> render -> gates -> features -> CLAP -> atlas.

Usage:
    python -m mattergraph_research.campaign --n 600 --out runs/c0.sqlite

The CLAP health gate runs before any spend; a failing evaluator aborts the
campaign (the larger_clap_music lesson, research/exp0/RESULTS.md).
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path

from .atlas import Atlas
from .clap_embed import ClapEmbedder, MODEL_ID
from .features import extract_features
from .genome import sobol_genomes
from .render import render_many

REPO = Path(__file__).resolve().parents[2]
PROBES = [REPO / "fixtures/probes/diag_strike.clipspec.json",
          REPO / "fixtures/probes/diag_arp3.clipspec.json"]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=600)
    ap.add_argument("--out", type=Path, default=REPO / "runs/campaign.sqlite")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--workers", type=int, default=8)
    args = ap.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)

    print(f"[campaign] evaluator health gate ({MODEL_ID}) ...", flush=True)
    embedder = ClapEmbedder()
    embedder.health_check()
    print("[campaign] evaluator healthy", flush=True)

    genomes = sobol_genomes(args.n, base_seed=args.seed)
    atlas = Atlas(args.out)

    t0 = time.time()
    n_pass = 0
    batch_size = 64
    for start in range(0, len(genomes), batch_size):
        chunk = genomes[start:start + batch_size]
        jobs, meta = [], []
        for i, g in enumerate(chunk):
            gid = g.content_id()
            name = f"sobol_{args.seed}_{start + i}"
            skin_json = g.skin_json(name)
            atlas.add_skin(gid, name, skin_json)
            for probe in PROBES:
                jobs.append((skin_json, probe, 1000 + start + i))
                meta.append((gid, probe.stem))

        outcomes = render_many(jobs, workers=args.workers)

        waves, wave_render_ids = [], []
        for (gid, probe_name), out in zip(meta, outcomes):
            render_id = f"{gid}:{probe_name}"
            atlas.add_render(render_id, gid, probe_name, 0, out.ok, out.reason,
                             out.frames, out.peak, out.rms)
            if out.ok and out.audio is not None:
                atlas.add_features(render_id, extract_features(out.audio))
                waves.append(out.audio.reshape(-1, 2).mean(axis=1))
                wave_render_ids.append(render_id)
                n_pass += 1

        if waves:
            vecs = embedder.embed_audio(waves)
            for rid, vec in zip(wave_render_ids, vecs):
                atlas.add_embedding(rid, MODEL_ID, vec)
        atlas.commit()
        done = min(start + batch_size, len(genomes))
        rate = done / max(time.time() - t0, 1e-9)
        print(f"[campaign] {done}/{len(genomes)} genomes "
              f"({n_pass} gated renders kept, {rate:.1f} genomes/s)", flush=True)

    atlas.finalize_skin_embeddings(MODEL_ID)
    print(f"[campaign] done in {time.time() - t0:.0f}s -> {args.out}", flush=True)


if __name__ == "__main__":
    main()
