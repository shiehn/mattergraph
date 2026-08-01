"""Pinned CLAP embedder with a mandatory known-answer health gate.

Pin: laion/clap-htsat-unfused via transformers >=4.49,<5. The health gate
exists because laion/larger_clap_music shipped silently broken through the same
API (degenerate embeddings, no warnings) — see research/exp0/RESULTS.md. A
campaign that starts without a passing health check is invalid by definition.
"""

from __future__ import annotations

import numpy as np
import torch

MODEL_ID = "laion/clap-htsat-unfused"
SAMPLE_RATE = 48000


class ClapEmbedder:
    def __init__(self) -> None:
        from transformers import ClapModel, ClapProcessor

        self.model = ClapModel.from_pretrained(MODEL_ID)
        self.model.eval()
        self.processor = ClapProcessor.from_pretrained(MODEL_ID)

    def _unwrap(self, out: object) -> torch.Tensor:
        return out if torch.is_tensor(out) else out.pooler_output  # type: ignore[union-attr]

    def embed_audio(self, waves: list[np.ndarray], batch: int = 16) -> np.ndarray:
        vecs = []
        for i in range(0, len(waves), batch):
            chunk = [np.asarray(w, dtype=np.float32) for w in waves[i:i + batch]]
            try:
                inputs = self.processor(audio=chunk, sampling_rate=SAMPLE_RATE,
                                        return_tensors="pt", padding=True)
            except (ValueError, TypeError):
                inputs = self.processor(audios=chunk, sampling_rate=SAMPLE_RATE,
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
        """Known-answer smoke test; raises RuntimeError on an unhealthy evaluator."""
        sr = SAMPLE_RATE
        t = np.arange(int(2.5 * sr)) / sr

        def tone(ratios, amps, t60s):
            y = np.zeros_like(t)
            for r, a, t60 in zip(ratios, amps, t60s):
                y += a * np.exp(-t / (t60 / 6.91)) * np.sin(2 * np.pi * 523.3 * r * t)
            return (0.9 * y / (np.abs(y).max() + 1e-9)).astype(np.float32)

        bright_long = tone([1, 2.32, 4.25, 6.63], [1, 0.8, 0.6, 0.45], [2.0, 1.6, 1.2, 0.9])
        dark_short = tone([1, 2.0], [1, 0.15], [0.15, 0.1])

        audio = self.embed_audio([bright_long, dark_short])
        text = self.embed_text([
            "a bright ringing chime with a long resonant decay",
            "a dull muted knock that stops immediately",
            "pouring water into a glass",
        ])
        sims = audio @ text.T

        # Distinctness: unrelated texts must not all collapse together.
        text_gram = text @ text.T
        off_diag = text_gram[np.triu_indices(3, k=1)]
        if float(off_diag.mean()) > 0.95:
            raise RuntimeError("CLAP health: text embeddings degenerate (collapsed)")
        # Known answers: each tone prefers its own description.
        if not (sims[0, 0] > sims[0, 1] and sims[1, 1] > sims[1, 0]):
            raise RuntimeError(f"CLAP health: known-answer ordering failed: {sims.tolist()}")
        # Cross-modal signal exists at all.
        if float(sims.max() - sims.min()) < 0.05:
            raise RuntimeError("CLAP health: audio-text similarities have no spread")
