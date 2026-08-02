"""mattergraph-lab v0: browser front end for retrieval, audition, and curation.

Run:  python -m mattergraph_research.lab --atlas ../runs/c0.sqlite
Then open http://127.0.0.1:8321

Flow: type a prompt -> top-k skins from the atlas (CLAP text retrieval) ->
render any candidate on any probe -> audition/A-B in the browser -> save keepers
to skins/curated/ with tags. Renders happen live through the compiled binary,
so what you hear is exactly what the engine ships.
"""

from __future__ import annotations

import argparse
import io
import json
import re
import tempfile
from pathlib import Path

import numpy as np
import soundfile as sf
import uvicorn
from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse, Response
from pydantic import BaseModel

from .atlas import Atlas
from .clap_embed import ClapEmbedder
from .render import render_once

REPO = Path(__file__).resolve().parents[2]
PROBE_DIR = REPO / "fixtures/probes"
CURATED_DIR = REPO / "skins/curated"
ANCHOR_DIR = REPO / "skins/anchors"
STATIC = Path(__file__).parent / "lab_static"

app = FastAPI(title="mattergraph-lab")
state: dict = {}


def _embedder() -> ClapEmbedder:
    if "embedder" not in state:
        emb = ClapEmbedder()
        emb.health_check()
        state["embedder"] = emb
    return state["embedder"]


@app.get("/")
def index() -> FileResponse:
    return FileResponse(STATIC / "index.html")


@app.get("/api/probes")
def probes() -> list[str]:
    return sorted(p.name.removesuffix(".clipspec.json")
                  for p in PROBE_DIR.glob("*.clipspec.json"))


@app.get("/api/anchors")
def anchors() -> list[dict]:
    out = []
    for d, kind in ((ANCHOR_DIR, "anchor"), (CURATED_DIR, "curated")):
        if d.exists():
            for p in sorted(d.glob("*.json")):
                out.append({"name": p.stem, "kind": kind})
    return out


class SearchReq(BaseModel):
    prompt: str
    topk: int = 6


@app.post("/api/search")
def search(req: SearchReq) -> list[dict]:
    from .query import search_skins

    atlas: Atlas = state["atlas"]
    results = search_skins(atlas, _embedder(), req.prompt, topk=req.topk)
    if not results:
        raise HTTPException(400, "atlas has no skin embeddings")
    return results


@app.get("/api/render_wav")
def render_wav(skin_id: str = "", anchor: str = "", probe: str = "pluck_arpeggio",
               seed: int = 7) -> Response:
    probe_path = PROBE_DIR / f"{probe}.clipspec.json"
    if not probe_path.exists():
        raise HTTPException(404, f"no probe {probe}")
    if anchor:
        for d in (ANCHOR_DIR, CURATED_DIR):
            p = d / f"{anchor}.json"
            if p.exists():
                skin_json = p.read_text()
                break
        else:
            raise HTTPException(404, f"no anchor {anchor}")
    else:
        atlas: Atlas = state["atlas"]
        row = atlas.conn.execute("SELECT genome_json FROM skins WHERE id=?",
                                 (skin_id,)).fetchone()
        if row is None:
            raise HTTPException(404, f"no skin {skin_id}")
        skin_json = row[0]

    out = render_once(skin_json, probe_path, seed)
    if not out.ok:
        raise HTTPException(500, f"render failed: {out.reason}")
    audio = out.audio.reshape(-1, 2)
    peak = np.abs(audio).max() + 1e-9
    audio = audio * (10 ** (-1.0 / 20) / peak)  # normalize -1 dBFS for audition
    buf = io.BytesIO()
    sf.write(buf, audio, out.sample_rate, format="WAV", subtype="FLOAT")
    return Response(buf.getvalue(), media_type="audio/wav")


GATE_PROMPTS = [
    "a delicate glass chime with a clear ring",
    "a deep drum thump",
    "a metal bell with a long sustained ring",
    "a warm wooden knock, like a marimba",
    "short staccato click that stops immediately",
    "brilliant shimmering high-frequency strike",
    "resonant singing bowl",
    "a tiny music box note",
]


@app.get("/api/gate_prompts")
def gate_prompts() -> list[str]:
    return GATE_PROMPTS


class GateReq(BaseModel):
    prompt: str
    skin_id: str
    verdict: str  # "yes" | "no"


@app.post("/api/gate")
def gate(req: GateReq) -> dict:
    import time as _time
    path = REPO / "runs/gate_results.jsonl"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a") as f:
        f.write(json.dumps({"t": _time.time(), "prompt": req.prompt,
                            "skin_id": req.skin_id, "verdict": req.verdict}) + "\n")
    return gate_summary()


@app.get("/api/gate_summary")
def gate_summary() -> dict:
    """The formal gate covers the preset prompts only; custom prompts are
    scope-exploration and reported separately (lumping them painted the gate
    as failing while presets sat at ~80%)."""
    path = REPO / "runs/gate_results.jsonl"
    if not path.exists():
        return {"preset_yes": 0, "preset_total": 0, "custom_yes": 0,
                "custom_total": 0, "pass_rate": 0.0}
    latest: dict[str, str] = {}
    for line in path.read_text().splitlines():
        row = json.loads(line)
        latest[row["prompt"]] = row["verdict"]  # last verdict per prompt wins
    preset = {p: v for p, v in latest.items() if p in GATE_PROMPTS}
    custom = {p: v for p, v in latest.items() if p not in GATE_PROMPTS}
    p_yes = sum(1 for v in preset.values() if v == "yes")
    return {
        "preset_yes": p_yes,
        "preset_total": len(preset),
        "custom_yes": sum(1 for v in custom.values() if v == "yes"),
        "custom_total": len(custom),
        "pass_rate": round(p_yes / len(preset), 3) if preset else 0.0,
    }


class CurateReq(BaseModel):
    skin_id: str
    name: str
    tags: str = ""


@app.post("/api/curate")
def curate(req: CurateReq) -> dict:
    atlas: Atlas = state["atlas"]
    row = atlas.conn.execute("SELECT genome_json FROM skins WHERE id=?",
                             (req.skin_id,)).fetchone()
    if row is None:
        raise HTTPException(404, f"no skin {req.skin_id}")
    name = re.sub(r"[^a-zA-Z0-9_\-]", "_", req.name.strip()) or req.skin_id
    CURATED_DIR.mkdir(parents=True, exist_ok=True)
    skin = json.loads(row[0])
    skin["name"] = name
    if req.tags:
        skin["_tags"] = req.tags
    path = CURATED_DIR / f"{name}.json"
    path.write_text(json.dumps(skin, indent=1))
    return {"saved": str(path.relative_to(REPO))}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--atlas", type=Path, default=REPO / "runs/c0.sqlite")
    ap.add_argument("--port", type=int, default=8321)
    args = ap.parse_args()
    state["atlas"] = Atlas(args.atlas)
    uvicorn.run(app, host="127.0.0.1", port=args.port, log_level="warning")


if __name__ == "__main__":
    main()
