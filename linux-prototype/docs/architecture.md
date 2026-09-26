# FuhrerOS Architecture

FuhrerOS = **Linux 6.12 (Alpine `linux-virt`) + the FuhrerOS adaptive layer**,
packaged as a bootable VM image. The adaptive layer observes the running
workload, classifies it, and switches the system between a small set of
reversible OS policies. Unmodified Linux programs are affected through kernel
tunables; Fuhrer-aware programs additionally get a policy-following I/O path
via `libfuhrer`.

```text
                          APPLICATIONS
               ┌───────────────┴────────────────┐
        Normal Linux apps               Fuhrer-aware apps
     (python, gcc, firefox, ...)     (fuhrer-bench, libfuhrer users)
               │                                │ libfuhrer: NORMAL=pread/pwrite
               │                                │ BATCHED=io_uring batch, SPECIALIZED=io_uring+O_DIRECT
               │                                │   ▲ reads policy lock-free from
               │                                │   │ /run/fuhrer/state.shm (seqlock)
               └───────────────┬────────────────┘   │
                         Linux syscalls              │
                               │                     │
   ┌───────────────────────────┴─────────────────────┴──────────┐
   │ fuhrerd  (adaptive/controller/fuhrerd.c)                    │
   │                                                             │
   │  Profiler ──► Feature extractor ──► Classifier ──► Engine ──┼──► Policy framework
   │  /proc/stat    rates, avg I/O size   IDLE INTERACTIVE        │      NormalPolicy
   │  /proc/diskstats merge ratio, QD    CPU_BOUND IO_SEQUENTIAL │      BatchedPolicy
   │  /proc/net/dev  pps, pkt size       IO_RANDOM NETWORK_HEAVY │      SpecializedPolicy
   │  /proc/<pid>/io top PID/COMM        MIXED                    │      (knob manager:
   │                                     hysteresis + dwell ──────┘       save/apply/restore)
   │  logs: adapt.log ([ADAPT]), metrics.csv, status, state.shm   │
   └──────────────────────────────┬──────────────────────────────┘
                                  │ sysfs / procfs / debugfs writes
                          Linux kernel 6.12
          block (virtio-blk mq) · page cache/writeback · NAPI/sockets · EEVDF
                                  │
                         virtio-blk · virtio-net · virtio-rng
                                  │
                     QEMU 10.2 (KVM, or TCG fallback)
                                  │
            WSL2 Ubuntu 26.04  →  Windows 11 (Hyper-V)  →  hardware
```

## Components

| Path | Component | Milestone |
|---|---|---|
| `adaptive/profiler/profiler.c` | counter snapshots, per-process attribution (hash table of previous values) | M2 |
| `adaptive/features/features.c` | rates + rule-based classifier | M3 |
| `adaptive/policy/` | `struct fu_policy` interface (`initialize/activate/deactivate/collect_metrics/name`), knob manager | M4 |
| `adaptive/controller/engine.c` | class→policy map, hysteresis, dwell (pure logic, unit-tested) | M6 |
| `adaptive/controller/fuhrerd.c` | daemon loop, logging, modes, signals | M1/M6 |
| `adaptive/controller/config.c` | `/etc/fuhrer/fuhrer.conf` parser | M4 |
| `runtime/cli/fuhrer.c` | `fuhrer status/profile/policy/classify/log/benchmark`, `fuhrer>` shell | M1 |
| `runtime/libfuhrer/` | policy-following I/O library (liburing) | M8 |
| `benchmarks/fuhrer-bench.c` | storage/cpu/memory/net/transition benchmarks with raw-sample percentiles | M5/M15 |
| `vm/image/` | Dockerfile + `mkimage.sh` + overlay (services, config, branding) | M0 |
| `scripts/` | build/run/debug/reset/test/benchmark/analyze | M0/M15 |

## Control loop

Every `interval_ms` (default 1 s, D-004):

1. **sample** — ~5 procfs reads + 2 files per process (`scan_procs=1`);
2. **features** — deltas → CPU %, IOPS, MB/s, avg request KB, merge ratio,
   queue depth (`time_in_queue`/wall), disk util, pps, packet size,
   I/O syscalls/s, top CPU and top I/O process;
3. **classify** — first matching rule (disk activity → network → cached I/O →
   CPU → interactive → idle), with a reason string (`high_small_io`, …);
4. **decide** — the engine wants `map[class]`; it switches only after
   `hysteresis` agreeing samples and `min_dwell_ms` since the last switch;
5. **switch** — `old.deactivate()` restores that policy's knobs, then
   `new.activate()` applies the new column; latency is measured;
6. **publish/log** — `[ADAPT]` record, `metrics.csv` row, `status`,
   `state.shm`.

## Modes (baselines and ablations)

| mode | meaning | used for |
|---|---|---|
| `fuhrer.disable=1` (kernel cmdline) | daemon never starts | B0 native Linux |
| `off` | daemon idle, no profiling, stock knobs | A1 |
| `observe` | profile + classify + log, never switch | A2 |
| `static:<policy>` | fixed policy | B1 (normal), B3 candidates, A3 |
| `adaptive` | full loop | B2, A4 |

Set at boot (`fuhrer.mode=...`), in `fuhrer.conf`, or live
(`fuhrer policy auto|observe|off|normal|batched|specialized`).

## Safety properties

- Every modified tunable is restored on `deactivate`, on daemon exit, and by
  a successor after a crash (originals persisted in `/run/fuhrer/knobs.orig`).
- No tunable alters durability semantics (see D-007).
- libfuhrer finishes short writes synchronously and falls back from O_DIRECT
  for unaligned requests; any backend reads back what any other wrote
  (unit test covers all 4×3 combinations).
- The guest never writes the base image; each VM has a CoW overlay.

## Image contents

Alpine 3.22 base, OpenRC, `linux-virt` 6.12, mdev (base) or eudev (desktop),
OpenSSH, chrony, Python 3.12, GCC 14, Git, Vim/Nano, tmux, Lynx/w3m, fio,
sysstat, strace, iperf3, liburing; desktop profile adds Xorg, Openbox, tint2,
xterm, PCManFM, Firefox ESR and NetSurf.
