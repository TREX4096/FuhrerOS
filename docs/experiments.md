# FuhrerOS — Experiments

All numbers below were measured by `scripts/benchmark.sh` and are reproduced
from the per-experiment `summary.md` files. Each experiment directory holds
`config.json` (host, QEMU, accel, VM size, image, commit), `system-info.txt`,
raw per-run JSON, fuhrerd's `metrics.csv` and `adapt-*.log`.

**Platform for every run:** Ryzen 5 7520U laptop with 8 GB, running Windows 11
→ WSL2 → QEMU 10.2.1 with **KVM (nested)**. Bench VM: 4 vCPU, 4 GB, 40 GB qcow2
overlay (cache=none), virtio. Guest kernel 6.12.111-0-virt (Alpine 3.22). Only
relative comparisons within one experiment are meaningful.

| id | suite | reps | purpose | record |
|---|---|---|---|---|
| E-001 | main (quick) | 1 | pipeline validation | [log](../research-log/experiments/E-001.md) |
| E-002 | main + transition + ablation | 3 | M5 static ground truth, RQ1–RQ4 | [log](../research-log/experiments/E-002.md) |
| E-003 | main | 2 (aborted in rep 3) | validate revised policy map | [log](../research-log/experiments/E-003.md) |
| E-004 | transition | 3 | adaptation with the revised map | [log](../research-log/experiments/E-004.md) |

---

## RQ1/M5 — which static policy is best? (E-002)

SPECIALIZED was the best static policy for **all four storage workloads**:

| | normal | batched | specialized |
|---|---|---|---|
| randread (IOPS) | 842 | 7,721 | **14,860** |
| randwrite (IOPS) | 8,818 | 9,372 | **11,653** |
| seqread (MB/s) | 90 | 96 | **775** |
| seqwrite (MB/s) | 71 | 66 | **876** |

The original hypothesis "sequential → BATCHED" (H2) was **refuted** on this
platform, and the class→policy map was revised (D-006 rev. 2).

**Where the gain comes from (A6).** This is the most important caveat of
the project. Relative to static NORMAL:

| | kernel knobs only | libfuhrer backend only |
|---|---|---|
| randread | +10…+17 % | **+913…+1,359 %** |
| seqread | −6…−14 % (noise) | **+760 %** (O_DIRECT path) |
| seqwrite | +5…+8 % | **+856 %** |

The kernel tunables alone contribute little. Nearly all of the benefit is
the policy-selected **application I/O path**: io_uring with 32 requests in
flight, and O_DIRECT bypassing a page-cache path that is very slow under
nested virtualisation. Unmodified applications therefore benefit far less
than Fuhrer-aware ones.

**Tail-latency trade-off:** SPECIALIZED raises per-request p99 for random
I/O. Randread goes from 2.9 ms to 3.9 ms (the figure includes queueing inside the
32-deep batch), and randwrite from 0.6 ms to 9.8 ms (buffered writes return from
cache). It lowers p99 for sequential reads, from 32 ms to 9 ms.

## RQ2/H3 — does adaptive reach the best static policy?

| workload | B2 vs B3, original map (E-002) | B2 vs B3, revised map (E-003, n=2) |
|---|---|---|
| randread | −10.3 % | +0.9 % |
| randwrite | +7.1 % | +27.8 % |
| seqread | −87.0 % | +6.3 % |
| seqwrite | −91.6 % | +11.6 % |

With the revised map, adaptive **matches B3 within noise** on every
storage workload, while no single *fixed* choice is right everywhere unless
the platform happens to favour one policy uniformly (as this one does). The
experiment that would show the value of adaptation over "always
SPECIALIZED" is therefore a mixed workload with an unaware application,
where SPECIALIZED's latency cost matters. See Next steps.

## RQ3/H4 — overhead (E-002 ablation, Graph 4)

| profiler | fuhrerd CPU (share of 4 vCPUs) | CPU benchmark vs profiler off |
|---|---|---|
| off (A1) | 0 | — |
| observe @ 1000 ms (A2) | **0.087 %** | −2.0 % (within noise) |
| observe @ 100 ms | 0.69 % | −1.2 % |
| observe @ 10 ms | 5.1 % | −9.0 % |

Switch latency is 23–120 ms, dominated by changing the block elevator (D-014).
With a 3 s minimum dwell, switching costs < 4 % of the time even at the
maximum switching rate.

## RQ4 — reaction time (E-002 transition + ablations, Graph 3)

- Default (1 s interval, hysteresis 3): the policy is reached **3.0–4.1 s** after
  each phase change, with 4–5 switches per 50 s run.
- Hysteresis 1 and interval 100 ms: **1 s**. Hysteresis 5: **5 s**.
  Interval 5000 ms: **never** within 10 s phases; random-read throughput in
  that phase falls to 4.6 MB/s, against 40–57 for the faster settings.
- Faster reaction bought more random-phase throughput here (57 vs 40 MB/s),
  at 8× the monitoring cost (0.69 % vs 0.087 %).

**E-004 (revised map, 3 reps):** the policy was reached 3.0–4.0 s after the random
phase began. The sequential phase ran at 523–585 MB/s, equal to static SPECIALIZED
(564–587) and 3–5× static NORMAL (104–167). The old map gave 400–424. There were 2–3
switches per run, down from 4–5. The reaction delay costs about 25–35 % of a 10 s random phase
against a static policy that is already right.

## RQ6 — compatibility

`fuhrer-selftest` passes 25/25 applicable checks: kernel, userspace, disk,
virtio-blk/net, IPv4, sshd, fuhrerd, dashboard as non-root, profile, shared
memory state, unit tests in the guest, io_uring, Python 3.12 (with ssl and
sqlite3), GCC, Git, Vim, Lynx, DNS, `curl https://example.com` and Lynx over
the web. The desktop image runs **Firefox ESR 140** rendering
https://example.com (docs/images/desktop-firefox.png). The self-booting
disk boots through GRUB under **SeaBIOS and OVMF UEFI**.

## Threats to validity

1. **Nested virtualisation**. CPU benchmarks vary by up to 94 % CV between
   configurations that cannot affect them (A6 "lib-only" on the CPU
   workload). Only effects of about 2× or more are robust.
2. Storage stack: qcow2 → ext4 in the WSL2 vhdx → NTFS → NVMe. The page-cache
   path is disproportionately slow, which likely explains why O_DIRECT wins
   sequential I/O. **On bare metal the map may differ**: measure it (M5) per
   platform.
3. Network tests use loopback, so virtio-net and the busy-poll knobs are barely
   exercised.
4. Small n (3, and 2 in E-003); one host; one kernel.
5. B0 runs in a separate boot from the FuhrerOS configurations.

## Next steps

- Mixed and unaware workloads, such as a compile running beside a random-read database and
  a browser, to test adaptation where no single policy is best.
- Bare-metal run from the bootable image (`--kernel lts`), to re-derive the map.
- Per-cgroup policies (D-009); network experiments against a real peer.
