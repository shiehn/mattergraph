"""Export the skin-index pack for in-app retrieval.

The sas-gateway embeds text with `laion/larger_clap_music_and_speech` (its
patch-retrieval space), so the pack's AUDIO embeddings must live in that same
space — not the research loop's clap-htsat-unfused. Audio is re-rendered from
genomes (determinism makes the atlas's deleted audio free to reproduce) across
five register/gesture probes, embedded with the gateway-matched model, and
written as one JSON pack the app loads at runtime:

    { version, model, dim,
      skins: [ { id, name, anchor, genome, features{...}, vecs_b64[...] } ] }

Health gate first, as always — larger_clap_music (sibling checkpoint) shipped
broken, and this pack IS the product's retrieval brain.

Run: python -m mattergraph_research.export_pack --atlas ../runs/qd2.sqlite \
         --out ../runs/mattergraph-skin-index.json
"""

from __future__ import annotations

import argparse
import base64
import json
import time
from pathlib import Path

import numpy as np
import torch

from .atlas import Atlas
from .render import render_many

BAND_EDGES = np.array([60, 120, 250, 500, 1000, 2000, 4000, 8000, 16000], dtype=float)


def band_profile(x: np.ndarray, sr: int = 48000) -> np.ndarray:
    """Energy distribution across 8 log bands (sums to 1) — complement scoring."""
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    freqs = np.fft.rfftfreq(len(x), 1 / sr)
    out = np.zeros(len(BAND_EDGES) - 1)
    for i in range(len(out)):
        m = (freqs >= BAND_EDGES[i]) & (freqs < BAND_EDGES[i + 1])
        out[i] = spec[m].sum()
    s = out.sum()
    return out / s if s > 0 else out


def transientness(x: np.ndarray) -> float:
    """0 sustained .. 1 transient (envelope crest heuristic; mirror in TS)."""
    env = np.convolve(np.abs(x), np.ones(240) / 240, mode="same")
    peak = env.max() + 1e-12
    return float(np.clip(1.0 - (env.mean() / peak) * 3.0, 0, 1))

REPO = Path(__file__).resolve().parents[2]
PROBES = ["diag_strike", "diag_arp3", "diag_bass", "diag_sustain", "high_arp"]
PACK_MODEL = "laion/larger_clap_music_and_speech"


class GatewaySpaceEmbedder:
    """Audio embeddings in the gateway's text-embedding space."""

    def __init__(self) -> None:
        from transformers import ClapModel, ClapProcessor

        self.model = ClapModel.from_pretrained(PACK_MODEL)
        self.model.eval()
        self.processor = ClapProcessor.from_pretrained(PACK_MODEL)

    def _unwrap(self, out: object) -> torch.Tensor:
        return out if torch.is_tensor(out) else out.pooler_output  # type: ignore[union-attr]

    def embed_audio(self, waves: list[np.ndarray], batch: int = 8) -> np.ndarray:
        vecs = []
        for i in range(0, len(waves), batch):
            chunk = [np.asarray(w, dtype=np.float32) for w in waves[i:i + batch]]
            try:
                inputs = self.processor(audio=chunk, sampling_rate=48000,
                                        return_tensors="pt", padding=True)
            except (ValueError, TypeError):
                inputs = self.processor(audios=chunk, sampling_rate=48000,
                                        return_tensors="pt", padding=True)
            with torch.no_grad():
                e = self._unwrap(self.model.get_audio_features(**inputs))
            vecs.append(torch.nn.functional.normalize(e, dim=-1).numpy())
        return np.concatenate(vecs, axis=0)

    def embed_text(self, prompts: list[str]) -> np.ndarray:
        inputs = self.processor(text=prompts, return_tensors="pt", padding=True)
        with torch.no_grad():
            e = self._unwrap(self.model.get_text_features(**inputs))
        return torch.nn.functional.normalize(e, dim=-1).numpy()

    def health_check(self) -> None:
        texts = self.embed_text(["a wooden drum hit", "a glass chime ringing",
                                 "pouring water into a cup"])
        gram = texts @ texts.T
        off = gram[np.triu_indices(3, k=1)]
        if float(off.mean()) > 0.95:
            raise RuntimeError(f"{PACK_MODEL}: text embeddings degenerate")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--atlas", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--limit", type=int, default=0, help="debug: cap skin count")
    args = ap.parse_args()

    embedder = GatewaySpaceEmbedder()
    embedder.health_check()
    print(f"[pack] {PACK_MODEL} healthy", flush=True)

    atlas = Atlas(args.atlas)
    strike_feats = atlas.features_for_probe("diag_strike")
    rows = atlas.conn.execute("SELECT id, name, genome_json FROM skins").fetchall()
    rows = [r for r in rows if r[0] in strike_feats]  # gate-passed, featured
    if args.limit:
        rows = rows[: args.limit]
    print(f"[pack] exporting {len(rows)} skins x {len(PROBES)} probes", flush=True)

    probe_paths = {p: REPO / f"fixtures/probes/{p}.clipspec.json" for p in PROBES}
    t0 = time.time()
    skins_out: list[dict] = []
    batch_size = 32
    dim = 512
    for start in range(0, len(rows), batch_size):
        chunk = rows[start:start + batch_size]
        jobs, meta = [], []
        for sid, name, gj in chunk:
            for p in PROBES:
                jobs.append((gj, probe_paths[p], 9000))
                meta.append((sid, p))
        outcomes = render_many(jobs, workers=8)
        waves, keys = [], []
        for (sid, p), out in zip(meta, outcomes):
            if out.ok and out.audio is not None:
                waves.append(out.audio.reshape(-1, 2).mean(axis=1))
                keys.append((sid, p))
        embeds: dict[tuple[str, str], np.ndarray] = {}
        mono_by_key: dict[tuple[str, str], np.ndarray] = {}
        if waves:
            for key, w, vec in zip(keys, waves, embedder.embed_audio(waves)):
                embeds[key] = vec.astype(np.float32)
                mono_by_key[key] = w
        for sid, name, gj in chunk:
            vecs = [embeds[(sid, p)] for p in PROBES if (sid, p) in embeds]
            if not vecs:
                continue
            f = strike_feats[sid]
            strike_mono = mono_by_key.get((sid, "diag_strike"))
            sus_mono = mono_by_key.get((sid, "diag_sustain"))
            comp_src = np.concatenate([m for m in (strike_mono, sus_mono) if m is not None]) \
                if (strike_mono is not None or sus_mono is not None) else np.zeros(2)
            skins_out.append({
                "id": sid,
                "name": name,
                "anchor": sid.startswith("anchor_"),
                "genome": json.loads(gj),
                "features": {
                    "decay_t60_s": round(f["decay_t60_s"], 4),
                    "centroid_hz": round(f["centroid_hz"], 1),
                    "flatness": round(f["flatness"], 5),
                    "bands": [round(float(b), 5) for b in band_profile(comp_src)],
                    "trans": round(transientness(strike_mono)
                                   if strike_mono is not None else 0.5, 4),
                },
                "vecs_b64": [base64.b64encode(v.tobytes()).decode() for v in vecs],
            })
        done = min(start + batch_size, len(rows))
        rate = done / max(time.time() - t0, 1e-9)
        print(f"[pack] {done}/{len(rows)} skins ({rate:.1f}/s)", flush=True)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({
        "version": 1,
        "model": PACK_MODEL,
        "dim": dim,
        "built_from": str(args.atlas.name),
        "skins": skins_out,
    }))
    size_mb = args.out.stat().st_size / 1e6
    print(f"[pack] wrote {len(skins_out)} skins -> {args.out} ({size_mb:.1f} MB) "
          f"in {time.time() - t0:.0f}s")


if __name__ == "__main__":
    main()
