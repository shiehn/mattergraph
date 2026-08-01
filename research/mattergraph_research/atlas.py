"""SQLite atlas: genomes, gated renders, features, embeddings, skin vectors."""

from __future__ import annotations

import json
import sqlite3
from pathlib import Path

import numpy as np

SCHEMA = """
CREATE TABLE IF NOT EXISTS skins (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  genome_json TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS renders (
  id TEXT PRIMARY KEY,
  skin_id TEXT NOT NULL REFERENCES skins(id),
  probe TEXT NOT NULL,
  seed INTEGER NOT NULL,
  gate_passed INTEGER NOT NULL,
  gate_reason TEXT NOT NULL,
  frames INTEGER, peak REAL, rms REAL
);
CREATE TABLE IF NOT EXISTS features (
  render_id TEXT PRIMARY KEY REFERENCES renders(id),
  json TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS embeddings (
  render_id TEXT PRIMARY KEY REFERENCES renders(id),
  model TEXT NOT NULL, dim INTEGER NOT NULL, vec BLOB NOT NULL
);
CREATE TABLE IF NOT EXISTS skin_embeddings (
  skin_id TEXT PRIMARY KEY REFERENCES skins(id),
  model TEXT NOT NULL, dim INTEGER NOT NULL, n_renders INTEGER NOT NULL,
  vec BLOB NOT NULL
);
"""


class Atlas:
    def __init__(self, path: Path) -> None:
        # check_same_thread=False: the lab serves sync endpoints from a thread
        # pool; access is effectively single-user and writes are serialized.
        self.conn = sqlite3.connect(path, check_same_thread=False)
        self.conn.executescript(SCHEMA)

    def add_skin(self, skin_id: str, name: str, genome_json: str) -> None:
        self.conn.execute("INSERT OR IGNORE INTO skins VALUES (?,?,?)",
                          (skin_id, name, genome_json))

    def add_render(self, render_id: str, skin_id: str, probe: str, seed: int,
                   ok: bool, reason: str, frames: int, peak: float, rms: float) -> None:
        self.conn.execute("INSERT OR REPLACE INTO renders VALUES (?,?,?,?,?,?,?,?,?)",
                          (render_id, skin_id, probe, seed, int(ok), reason,
                           frames, peak, rms))

    def add_features(self, render_id: str, features: dict[str, float]) -> None:
        self.conn.execute("INSERT OR REPLACE INTO features VALUES (?,?)",
                          (render_id, json.dumps(features)))

    def add_embedding(self, render_id: str, model: str, vec: np.ndarray) -> None:
        v = np.asarray(vec, dtype=np.float32)
        self.conn.execute("INSERT OR REPLACE INTO embeddings VALUES (?,?,?,?)",
                          (render_id, model, len(v), v.tobytes()))

    def finalize_skin_embeddings(self, model: str) -> None:
        """Mean of per-render embeddings per skin, renormalized."""
        rows = self.conn.execute(
            """SELECT r.skin_id, e.vec, e.dim FROM embeddings e
               JOIN renders r ON r.id = e.render_id WHERE r.gate_passed = 1""").fetchall()
        acc: dict[str, list[np.ndarray]] = {}
        for skin_id, blob, dim in rows:
            acc.setdefault(skin_id, []).append(np.frombuffer(blob, dtype=np.float32, count=dim))
        for skin_id, vecs in acc.items():
            mean = np.mean(vecs, axis=0)
            mean = mean / (np.linalg.norm(mean) + 1e-12)
            self.conn.execute("INSERT OR REPLACE INTO skin_embeddings VALUES (?,?,?,?,?)",
                              (skin_id, model, len(mean), len(vecs),
                               mean.astype(np.float32).tobytes()))
        self.conn.commit()

    def skin_matrix(self) -> tuple[list[str], np.ndarray]:
        rows = self.conn.execute(
            "SELECT skin_id, dim, vec FROM skin_embeddings ORDER BY skin_id").fetchall()
        ids = [r[0] for r in rows]
        mat = np.stack([np.frombuffer(r[2], dtype=np.float32, count=r[1]) for r in rows]) \
            if rows else np.zeros((0, 512), dtype=np.float32)
        return ids, mat

    def skin_features(self) -> dict[str, dict[str, float]]:
        rows = self.conn.execute(
            """SELECT r.skin_id, f.json FROM features f
               JOIN renders r ON r.id = f.render_id WHERE r.gate_passed = 1""").fetchall()
        acc: dict[str, list[dict[str, float]]] = {}
        for skin_id, blob in rows:
            acc.setdefault(skin_id, []).append(json.loads(blob))
        return {
            sid: {k: float(np.mean([f[k] for f in fl])) for k in fl[0]}
            for sid, fl in acc.items()
        }

    def commit(self) -> None:
        self.conn.commit()
