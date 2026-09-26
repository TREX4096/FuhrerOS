# Experiments (M16, M17, §35)

The question behind these experiments: **does a small kernel that classifies
what is running, and adapts its scheduler and buffer cache to that class, do
better than fixed policies?** Every number below is copied from a run's
`summary.md` in `experiments/`, and every run can be reproduced with one
command. Values that were not measured are written as NOT RUN.

**Final runs:**

| run | suite | commit |
|---|---|---|
| E-129 | sched | `4768a1f` |
| E-130 | cache | `4768a1f` |
| E-131 | transition | `4768a1f` |
| E-132 | ablation | `4768a1f` |
| E-133 | micro (§35) | `4768a1f` |
| E-135 | desktop stress (M17) | `389c7f5`, which only changes the benchmark's HTTP port |

All earlier runs are superseded, and each of them exposed a bug; see the
history below and
[research-log/experiments](../research-log/experiments/README.md). Hashes
quoted by records written before 2026-09-26 18:00 are mapped in
[commit-map.md](../research-log/commit-map.md).

## Setup

| | |
|---|---|
| host | AMD Ryzen 5 7520U, Windows 11 → WSL 2 (Linux 6.6.87.2) → QEMU 10.2.1 |
| acceleration | **KVM** (nested inside WSL 2), printed as `Acceleration: KVM` by every script |
| guest | FuhrerOS 0.1.0; 4 vCPUs offered, **1 used** (uniprocessor kernel), 4096 MB |
| devices | virtio-blk (FFS0 raw image, host cache=writeback, device write cache + FLUSH), virtio-net (slirp) |
| clock | TSC, calibrated at boot (PIT channel 2); scheduler tick 1000 Hz |

`./scripts/experiment.sh SUITE` builds an ISO with `bench=SUITE`, boots it
headless, and waits for the guest to power itself off. Init runs
`rootfs/etc/bench/SUITE.sh`. The script then turns the serial log into
`results.jsonl`, `summary.md` and SVG graphs (`scripts/analyze.py`). The
configuration, the suite script and the raw log are kept with every run.

## Metrics

- **Dispatch latency:** the kernel-measured time from the interactive probe
  becoming runnable to it running. This is the part a scheduling policy
  controls (D-119).
- **Wake latency:** additionally contains the 1 ms timer granularity. Its
  phase depends on the per-boot calibration (F-117), so compare it only
  within one run.
- **Policy decision time:** requeue + select in the scheduler, excluding
  the context switch and the profiler.

Baselines (§37):
- B0 round-robin;
- B1 priority;
- B2 low-latency (the manually selected policy);
- B3 adaptive.

## Results

### Scheduler, E-129 (median of 5; CV in parentheses)

Mixed scenario: 3 CPU workers + 1 random-read I/O worker + a probe.

| metric | B0 round-robin | B1 priority | B2 low-latency | B3 adaptive |
|---|---|---|---|---|
| dispatch p50 (µs) | 29,063 | 29,063 | 8 | 7 |
| dispatch p99 (µs) | 29,281 | 29,213 | 107 | **70** |
| batch jobs/s (x100) | 984,733 | 978,096 | 956,852 | 968,289 |
| I/O ops/s | 22 | 21 | 511 | 501 |
| context switches/s | 188 | 188 | 1,956 | 1,755 |
| policy decision time (ns/s) | 27,981 | 27,894 | 97,693 | 119,186 |

Batch scenario: 4 CPU workers + the probe.
- Dispatch p99: B2 38 µs, B3 116 µs (CV 87 %).
- Batch jobs/s ranged from 971,569 to 992,008 (x100) across all four
  policies.

### Buffer cache, E-130 (median of 2; MB/s; 64 MiB file, 4 MiB cache)

| workload | LRU | FIFO | CLOCK | read-ahead | adaptive |
|---|---|---|---|---|---|
| seq | 21.18 | 22.42 | 21.53 | 342.97 | **301.04** |
| random | 24.18 | 23.28 | 23.15 | 23.49 | **25.24** |
| hotset | 100.91 | 64.10 | 95.20 | 101.23 | **93.47** |
| scan | 35.14 | 31.84 | 33.88 | 27.56 | **26.37** |

### Workload transition, E-131 (8 s phases)

| run | cpu | random-io | sequential-io | network | interactive |
|---|---|---|---|---|---|
| adaptive (sched + cache) | 284 ms | 286 ms | 1,516 ms | 289 ms | 407 ms |
| round_robin + LRU | 303 ms | 289 ms | 287 ms | 304 ms | 299 ms |

The 1,516 ms is a labelling delay, not slow I/O. With read-ahead, the reads
hit the cache from the first sample (25,000–31,000 reads per 250 ms), so
the readers hardly block and look INTERACTIVE for about 1.2 s before they
are classed IO_BOUND.

### Ablations, E-132 (mixed scenario; median of 3; cache LRU except A5)

| variant | dispatch p50 (µs) | dispatch p99 (µs) | batch jobs/s (x100) |
|---|---|---|---|
| A1 controller off (priority) | 29,067 | 29,261 | 969,827 |
| A2 profiling off (all UNKNOWN) | 11,095 | 18,019 | 975,074 |
| A3 profiling on, no switching | 29,066 | 29,378 | 969,435 |
| A4 adaptive scheduler | 7 | 92 | 959,788 |
| A5 + adaptive cache | 7 | 272 | 914,110 |
| A6 window 25 ms | 7 | 100 | 949,204 |
| A6 window 500 ms | 7 | 13,091 | 969,621 |
| A7 hysteresis 1 | 7 | 76 | 942,914 |
| A7 hysteresis 5 | 7 | 10,966 | 956,978 |

The profiler used 8.85 ms in total during that boot, which ran at least
27 × 10 s of benchmarks.

### Micro-benchmarks (§35), E-133 (median of 3)

| group | results |
|---|---|
| CPU | primes < 300,000: 62 ms · 4 × 160² int matrix: 10 ms · **compilation: NOT RUN** (no compiler) |
| memory | malloc+free 19 ns · memcpy 4,579 MB/s · dependent loads 1.2 / 3.3 / 69.0 / 154.0 ns (16 KiB / 256 KiB / 4 MiB / 32 MiB) |
| storage | seq write 25.21 MB/s · seq read 370.34 MB/s (cold, read-ahead) · random read 5,739 ops/s · random write 4,487 ops/s · small files 1,402 created/s, 12,835 deleted/s |
| network (own stack, loopback path) | TCP 153.53 MB/s · 1-byte RTT 2 µs (p99 3 µs) · UDP 87,913 delivered packets/s |
| desktop | window create 1,666 µs · app launch → window 3–4 ms (terminal, files, editor, browser, settings, Control Center) · workspace switch 1,314 µs · composition avg 1,716 µs, max 7,894 µs · **terminal keystroke latency: NOT RUN** (needs injected input) |

### Desktop stress (M17), E-135 (median of 3)

Everything below runs at the same time:
- an HTTP client and server;
- two CPU-bound "build" workers (a proxy: no compiler has been ported);
- an 8 MiB file-copy loop;
- a TCP stream;
- an interactive probe.

| metric | B0 round-robin | B1 priority | B2 low-latency | B3 adaptive |
|---|---|---|---|---|
| probe dispatch p50 (µs) | 19,528 | 19,447 | 9 | 14 |
| probe dispatch p99 (µs) | 28,355 | 32,736 | **245** | 3,019 |
| build-proxy jobs/s | 554 | 557 | 197 | 42 |
| file copy MB/s | 0.20 | 0.19 | 6.44 | 5.51 |
| HTTP requests/s | 0.09 | 0.09 | 2.09 | 0.19 |
| TCP stream MB/s | 2.06 | 2.04 | 38.70 | **99.24** |
| context switches/s | 332 | 324 | 5,095 | 8,340 |

Media playback: NOT RUN (FuhrerOS has no audio or video path).

Thread classes under B3 at mid-run:
- copy, stream and web threads: IO_BOUND (priority 8);
- probe: INTERACTIVE (priority 4);
- build workers: MIXED (priority 12).

## Answers to the research questions

- **RQ1 — Can runtime characteristics select beneficial policies?** Yes, for
  the workloads they were designed for.
  - In the mixed scenario the classes are right: the I/O worker is IO_BOUND,
    the probe INTERACTIVE, the CPU workers CPU_BOUND.
  - Both profiling and class-based parameters are needed (E-132: A2, A3
    vs A4).
  - In the desktop-stress mix the classes are **wrong in a costly way**
    (below).
- **RQ2 — Does adaptive beat fixed scheduling on heterogeneous workloads?**
  - It clearly beats B0/B1 on latency and I/O. In the mixed scenario:
    dispatch p50 7 µs vs 29 ms; I/O throughput about 23× higher; batch
    throughput 1.7 % lower.
  - Against the hand-picked B2 it is mixed:
    - better tail in the mixed scenario (70 vs 107 µs);
    - worse in the pure-CPU scenario (116 vs 38 µs);
    - clearly worse under the desktop stress mix (p99 3.0 ms vs 0.25 ms).
- **RQ3 — Overhead.**
  - The profiler used under 0.004 % of CPU time (8.85 ms over at least
    270 s).
  - Adaptive policy decisions cost about 0.12–0.14 ms per second.
  - The adaptive policy causes about 9× the context switches of
    round-robin (E-129). Their CPU cost is **UNKNOWN - REQUIRES
    VERIFICATION**.
- **RQ4 — How fast should it react?**
  - Phase changes are detected in 284–407 ms, except sequential reads
    under read-ahead (1,516 ms, a labelling delay).
  - Slow reaction hurts: a 500 ms window or hysteresis 5 raises dispatch
    p99 about 120–140×.
  - A 25 ms window gives no measurable gain over 100 ms.
- **RQ5 — Adaptive caching without more memory?** Partly.
  - Sequential: 88 % of the dedicated read-ahead policy and 14.2× LRU;
    best on random reads.
  - Below the best fixed policy on hotset (about 8 %) and scan (25 %).
- **RQ6 — Responsiveness without hurting batch work?**
  - In the mixed scenario: yes (−1.7 % batch).
  - In the desktop-stress mix: **no**. The build proxy does 92 % fewer jobs
    than under round-robin and 79 % fewer than under low-latency, because
    busy streaming threads are classified IO_BOUND (many I/O calls, short
    bursts) and outrank the MIXED build workers. The IO_BOUND rule ignores
    CPU share. This is the next thing to fix.
- **RQ7 — Small kernel?** The adaptive policy is 135 lines of C++, and the
  profiler is one C file behind the same interface as the fixed policies.
  The kernel plus user space is about 21,000 lines (without the generated logo data).

## How the results changed, and why that matters

Every run before the final campaign exposed a bug. Each one has a record
under [research-log/failures](../research-log/failures/README.md):

| found in | failure | effect on earlier numbers |
|---|---|---|
| E-101 | F-109 system class IDLE for blocking workloads | class column wrong |
| E-102 | F-114 metadata hid sequential streams | read-ahead never ran |
| E-103, E-107 | F-115 aging did not lift IDLE-class tasks | "4.4–5.7 s detection" was a starved sampler |
| E-105, E-108 | F-117 wake latency follows the per-boot tick phase | 44 µs vs 975 µs for one configuration |
| E-106 | F-116 read-ahead evicted its own prefetches | 110k prefetches, 6k used |
| E-110 | F-118 a wake-up on an idle CPU waited for the tick | every disk read about 1 ms |
| E-114 | F-119 evict-behind evicted metadata | adaptive cache no better than LRU |
| E-121 | F-123 virtio-blk ran write-through | writes 1.82 MB/s (now 25.21) |
| E-122 | F-122 the aging boost never expired | adaptive dispatch p50 49 ms under stress |

The F-122 bug was only visible under the heavier M17 mix. The F-115 fix had
turned a latent defect harmful, and the E-117 scenario had too few threads
to show it.

## Threats to validity

- **Virtualisation.** The stack is nested (Windows → WSL 2 → KVM). Absolute
  times are not bare-metal times, and comparisons are within one boot.
- **One CPU**, and synthetic workloads. Whether the results transfer to real
  desktop use is UNKNOWN - REQUIRES VERIFICATION.
- **Few repetitions** (5 sched, 2 cache, 3 ablation/micro/stress, 1
  transition). Several tail cells have a CV of 40–120 %.
- **Shorter phases.** Transition phases are 8 s, not the 60 s of §36.
- **Hit rates include metadata.**
- **Network benchmarks** use the stack's loopback path, not a NIC.
- **Proxies.** The "build" workload is a CPU proxy, and media is not run.

## Reproduce

```bash
./scripts/build.sh
./scripts/experiment.sh sched    # also: cache | transition | ablation | micro | stress
```
