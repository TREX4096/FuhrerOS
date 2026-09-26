# FuhrerOS — Research Design

## Question

> Can runtime workload characterisation be used to dynamically select OS
> policies for heterogeneous personal-computing workloads, while retaining
> compatibility with ordinary Linux applications?

## Research questions → how the prototype answers them

| RQ | question | instrument | where |
|---|---|---|---|
| RQ1 | Do runtime characteristics predict the beneficial policy? | classifier decisions vs **B3** (best static per workload) | main suite: does `map[class]` equal the B3 column? |
| RQ2 | Does adaptive beat a fixed policy across workloads? | B2 vs B1 and vs B3, per workload and aggregate | main suite |
| RQ3 | Monitoring and switching overhead? | CPU benchmark under off / observe@10/100/1000 ms; `overhead_pct`; `switch_ms` | ablation suite (Graph 4), adapt.log |
| RQ4 | How fast must it react? | time-to-policy per phase; interval 100/1000/5000 ms; hysteresis 1/3/5 | transition + ablation suites (Graph 3) |
| RQ5 | Gains without more CPU/memory? | `cpu_per_op_us`, `cpu_user_s/sys_s` in storage JSON; daemon RSS | main suite raw JSON |
| RQ6 | Linux application compatibility preserved? | `fuhrer-selftest` (python, gcc, git, vim, curl, lynx, firefox) | `test.sh`; desktop screenshot |

## Hypotheses

- **H1** Random small I/O benefits from the SPECIALIZED policy (minimal
  read-ahead, `none` scheduler, asynchronous O_DIRECT path).
- **H2** Large sequential I/O benefits from BATCHED (read-ahead, deadline,
  batching). *E-001 (1 rep) already contradicts H2 for Fuhrer-aware apps:
  O_DIRECT + io_uring (SPECIALIZED) read 658 MB/s vs 50 MB/s for BATCHED.
  E-002 tests it properly and separates kernel-knob effects (A6).*
- **H3** Adaptive (B2) approaches the best static policy (B3) on each
  workload and beats any single static policy across the mix.
- **H4** Monitoring at 1 s costs < 1 % CPU; switching costs < 5 % of the dwell time.

## Independent variables

policy (NORMAL / BATCHED / SPECIALIZED / adaptive / none), workload,
sampling interval, hysteresis, component (knobs-only vs library-only, A6).

## Dependent variables

throughput (IOPS, MB/s, Mops/s, GB/s), latency (p50/p95/p99 from raw
samples), CPU time per operation, time-to-policy, number of switches,
switch latency, daemon CPU share.

## Controls

identical VM configuration for every configuration (bench: 4 vCPU/4 GB/40 GB);
same image (B0 differs only by `fuhrer.disable=1`); caches dropped; data file
1.5 × RAM; shuffled configuration order per repetition; configuration
metadata stored with every result (config.json, system-info.txt, build-info).

## Success criteria (INSTRUCTION §24)

- *Minimum:* boots in QEMU, profiler + policy engine + logging work, at least
  one workload where adaptive beats a fixed default, overhead quantified.
- *Research:* adaptive ≥ B1 on the mix, close to B3 per workload, measured
  reaction time, ablations explain the contribution of each component.
- *Negative results are results* (§25): if B3 ≫ B2 or adaptation never pays
  for itself, that is reported with the same care.

## Known threats

see `docs/experiments.md` and each `summary.md`: nested virtualisation,
qcow2-on-vhdx storage stack, loopback-only network workloads, small N,
single host, kernel 6.12 only.
