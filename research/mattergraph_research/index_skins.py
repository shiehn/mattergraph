"""Index hand-designed and curated SoundSkins into the atlas.

The anchor-first strategy (Plan Rev4 §0 change 10) only works if retrieval can
actually see the anchors. This renders each skin JSON on the diagnostic probes,
extracts features, embeds with CLAP, and inserts alongside the searched genomes.

Run: python -m mattergraph_research.index_skins --atlas ../runs/c0.sqlite
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from .atlas import Atlas
from .clap_embed import ClapEmbedder, MODEL_ID
from .features import extract_features
from .render import render_once

REPO = Path(__file__).resolve().parents[2]
PROBES = [REPO / "fixtures/probes/diag_strike.clipspec.json",
          REPO / "fixtures/probes/diag_arp3.clipspec.json"]
DEFAULT_DIRS = [REPO / "skins/anchors", REPO / "skins/curated"]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--atlas", type=Path, required=True)
    ap.add_argument("--dirs", type=Path, nargs="*", default=DEFAULT_DIRS)
    args = ap.parse_args()

    embedder = ClapEmbedder()
    embedder.health_check()
    atlas = Atlas(args.atlas)

    for d in args.dirs:
        if not d.exists():
            continue
        for path in sorted(d.glob("*.json")):
            skin_json = path.read_text()
            name = json.loads(skin_json)["name"]
            skin_id = f"anchor_{name}"
            atlas.add_skin(skin_id, name, skin_json)
            waves = []
            rids = []
            for probe in PROBES:
                out = render_once(skin_json, probe, seed=1)
                rid = f"{skin_id}:{probe.stem}"
                atlas.add_render(rid, skin_id, probe.stem, 1, out.ok, out.reason,
                                 out.frames, out.peak, out.rms)
                if out.ok and out.audio is not None:
                    atlas.add_features(rid, extract_features(out.audio))
                    waves.append(out.audio.reshape(-1, 2).mean(axis=1))
                    rids.append(rid)
            if waves:
                for rid, vec in zip(rids, embedder.embed_audio(waves)):
                    atlas.add_embedding(rid, MODEL_ID, vec)
            print(f"indexed {skin_id} ({len(waves)} renders)")
    atlas.finalize_skin_embeddings(MODEL_ID)
    atlas.commit()
    print("done")


if __name__ == "__main__":
    main()
