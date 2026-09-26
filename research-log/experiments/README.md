# Experiment records (E-1xx, from-scratch kernel)

Each record follows the NEW_EXPLANATION template (question, hypothesis, host,
guest, QEMU, kernel, workload, configuration, metrics, result, variance,
interpretation, threats, next experiment) and links to its raw data in
`experiments/`. The records are produced by `tools/gen_exp_records.py` from
each run's `config.json` plus a hand-written result and interpretation.
Every number in them is copied from that run's `summary.md`.

**Final results: E-129..E-133 and E-135** (commit `4768a1f`, E-135 `389c7f5`).
Earlier runs are kept, not deleted, because each one exposed a bug or was a
diagnostic; the "interpretation" field says which.

| record | suite | status | exposed / confirmed |
|---|---|---|---|
| [E-101](E-101.md) | sched | superseded | F-109 |
| [E-102](E-102.md) | cache | superseded | F-113, F-114 |
| [E-103](E-103.md) | transition | superseded | (F-115, found later) |
| [E-104](E-104.md) | ablation | superseded | — |
| [E-105](E-105.md) | sched | superseded | F-117 |
| [E-106](E-106.md) | cache | superseded | F-116 |
| [E-107](E-107.md) | transition | superseded | F-115 |
| [E-108](E-108.md) | ablation | superseded | F-117 |
| [E-109](E-109.md) | sched | superseded | first dispatch-latency run |
| [E-110](E-110.md) | cache | superseded | F-118, iobench overflow |
| [E-111](E-111.md) | transition | superseded | confirms F-115 fix |
| [E-112](E-112.md) | ablation | superseded | missing A5 |
| [E-113](E-113.md) | sched | superseded | confirms F-118 fix |
| [E-114](E-114.md) | cache | superseded | F-119 |
| [E-115](E-115.md) | transition | superseded | — |
| [E-116](E-116.md) | ablation | superseded | old layout |
| [E-117](E-117.md) | sched | superseded | (F-122, F-123 found later) |
| [E-118](E-118.md) | cache | superseded | confirms F-119 fix |
| [E-119](E-119.md) | transition | superseded | — |
| [E-120](E-120.md) | ablation | superseded | A1–A7 first complete |
| [E-121](E-121.md) | micro | superseded | F-123, unpaced UDP |
| [E-122](E-122.md) | stress | superseded | F-122 |
| [E-123](E-123.md)–[E-128](E-128.md) | wdebug | diagnostic | F-123 diagnosis and fix |
| [E-129](E-129.md) | sched | **final** | |
| [E-130](E-130.md) | cache | **final** | |
| [E-131](E-131.md) | transition | **final** | |
| [E-132](E-132.md) | ablation | **final** | |
| [E-133](E-133.md) | micro | **final** | §35 |
| [E-134](E-134.md) | stress | superseded | confirms F-122 fix; HTTP port artifact |
| [E-135](E-135.md) | stress | **final** | M17 |

One run was aborted by hand before it produced any result: the first attempt
at E-109, stopped when F-117 was found. Its directory was deleted.

Summary and discussion: [docs/experiments.md](../../docs/experiments.md).
