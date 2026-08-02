# qd1 → qd2: the coordinate system was the bottleneck (2026-08-01, late night)

## qd1 (144-cell strike-only map, percentile fitness): the null result that taught

Coverage 48 → 72/144 over c2 — only +2 final cells vs the two-gesture qd0 despite
periodic and sample gestures existing. Diagnosis: **cell descriptors computed from
diag_strike features only** — a pad or drone files by what its strike render looks
like, so sustained identity was invisible to the map. Plan §13.2's "descriptor choice
is the search" warning, observed empirically.

## qd2 (288-cell gesture-aware map): the fix, verified

4th axis = MEASURED sustain class (diag_sustain render's decay > 2.5 s — phenotype,
not exciter-type label); timbre axes now from per-probe (strike) features rather than
probe-averaged. Same engine, same 2000-eval budget:

| | qd1 (strike-blind) | qd2 (gesture-aware) |
|---|---|---|
| new cells found by evolution | +24 | **+55** |
| final coverage | 72/144 | **122/288** |
| elite improvements | 226 | 286 |

More than double the niche discovery from measuring what matters. Fitness is now
contrast-margin percentile (un-gameable ceiling), vocab includes bass/lead prompts.

## Retrieval progression (held-out 30, hit-rate@3 vs random)

| atlas / path | hit-rate | lift |
|---|---|---|
| c1, legacy mean-pooled | 0.694 | 2.41× |
| qd0, legacy | 0.750 | 2.59× |
| **qd2, production path (--v2: best-probe + constraints + gesture gating)** | **0.806** | **2.80×** |

30/30 prompts positive semantic lift throughout. The batch eval now has a --v2 flag
scoring the exact path the lab and product use (legacy path retained for continuity).

## Incidental fix

Probe-name inconsistency ("diag_strike" vs "diag_strike.clipspec" from Path.stem on
double extensions) emptied a probe-filtered query; prefix matching + a loud
zero-seed guard now prevent the silent variant of this class.

## State at close

Machine queue: empty. Human-gated: marimba re-vote (bar-law render delivered),
in-app A/B of the integration branch, curation toward 25 (6 kept). Next machine
epochs when summoned: brass/string-body gestures, SoundSpec parser via gateway,
RunPod scale-out when local throughput binds.
