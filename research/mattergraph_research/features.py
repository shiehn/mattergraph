"""Acoustic features in plain numpy — no librosa/numba dependency.

Computed on the mono mix at 48 kHz. Everything is deterministic and cheap;
these feed the atlas, the held-out eval rules, and later MAP-Elites axes.
"""

from __future__ import annotations

import numpy as np

_EPS = 1e-12


def _stft_mags(x: np.ndarray, n_fft: int = 2048, hop: int = 512) -> tuple[np.ndarray, np.ndarray]:
    window = np.hanning(n_fft)
    n_frames = max(1, 1 + (len(x) - n_fft) // hop)
    mags = np.empty((n_frames, n_fft // 2 + 1))
    for i in range(n_frames):
        frame = x[i * hop:i * hop + n_fft]
        if len(frame) < n_fft:
            frame = np.pad(frame, (0, n_fft - len(frame)))
        mags[i] = np.abs(np.fft.rfft(frame * window))
    freqs = np.fft.rfftfreq(n_fft, d=1.0 / 48000.0)
    return mags, freqs


def extract_features(interleaved: np.ndarray, sample_rate: int = 48000) -> dict[str, float]:
    x = interleaved.reshape(-1, 2).mean(axis=1) if interleaved.ndim == 1 else interleaved.mean(axis=1)
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    peak = float(np.max(np.abs(x)) + _EPS)
    rms = float(np.sqrt(np.mean(x * x)) + _EPS)

    mags, freqs = _stft_mags(x)
    frame_energy = mags.sum(axis=1) + _EPS
    weights = frame_energy / frame_energy.sum()

    centroid_f = (mags * freqs).sum(axis=1) / frame_energy
    centroid_hz = float((centroid_f * weights).sum())

    cumulative = np.cumsum(mags, axis=1)
    total = cumulative[:, -1] + _EPS
    rolloff_idx = np.argmax(cumulative >= 0.85 * total[:, None], axis=1)
    rolloff_hz = float((freqs[rolloff_idx] * weights).sum())

    flatness_f = np.exp(np.mean(np.log(mags + _EPS), axis=1)) / (np.mean(mags, axis=1) + _EPS)
    flatness = float((flatness_f * weights).sum())

    hf = mags[:, freqs >= 4000].sum(axis=1)
    hf_ratio = float(((hf / (mags.sum(axis=1) + _EPS)) * weights).sum())

    # Envelope: 5 ms moving average of |x|.
    kernel = np.ones(240) / 240.0
    env = np.convolve(np.abs(x), kernel, mode="same")
    peak_idx = int(np.argmax(env))
    attack_s = peak_idx / sample_rate

    # Decay: time from envelope peak down to -40 dB relative, scaled to t60.
    env_peak = env[peak_idx] + _EPS
    below = np.nonzero(env[peak_idx:] < env_peak * 10 ** (-40 / 20))[0]
    if len(below) > 0:
        t40 = below[0] / sample_rate
        decay_t60_s = float(t40 * 1.5)
    else:
        decay_t60_s = float((n - peak_idx) / sample_rate * 1.5)

    return {
        "peak_db": 20 * np.log10(peak),
        "rms_db": 20 * np.log10(rms),
        "crest_db": 20 * np.log10(peak / rms),
        "centroid_hz": centroid_hz,
        "rolloff_hz": rolloff_hz,
        "flatness": flatness,
        "hf_ratio": hf_ratio,
        "attack_s": attack_s,
        "decay_t60_s": decay_t60_s,
    }
