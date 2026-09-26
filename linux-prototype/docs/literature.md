# FuhrerOS — Literature Tracking

Template per system (INSTRUCTION M-§14): Problem · Idea · Architecture ·
Implementation · Evaluation · Limitation · Relevance · Difference.

**Source note.** Entries summarise the project's literature review in
`INSTRUCTION.md` §5. Where that review does not state a detail, the field says
*not yet reviewed from the primary paper* instead of guessing. Numbers quoted
are the papers' reported results, not reproduced by us. No novelty claim is
made on the basis of this list (Novelty policy, M-§15).

---

## Exokernel — Engler, Kaashoek, O'Toole, SOSP 1995
- **Problem:** OS-imposed resource abstractions restrict application-specific optimisation.
- **Idea:** kernel provides only protection, allocation and multiplexing; library OSes implement policy.
- **Architecture:** application → library OS → exokernel → hardware.
- **Implementation / Evaluation:** not yet reviewed from the primary paper.
- **Limitation:** requires library-OS specialisation; no desktop compatibility story.
- **Relevance:** the protection/policy split is the basis of FuhrerOS's policy framework (policies change *settings*, never protection).
- **Difference:** FuhrerOS keeps Linux's abstractions and switches policies at runtime.

## Dune — Belay et al., OSDI 2012
- **Problem:** applications sometimes need privileged CPU features.
- **Idea:** use virtualisation hardware so a Linux process can safely use page tables, rings, tagged TLB, exceptions.
- **Architecture:** process → Dune library → VT-x → CPU; a Linux kernel module plus a userspace library.
- **Evaluation:** not yet reviewed from the primary paper.
- **Limitation:** a mechanism, not a policy system.
- **Relevance:** controlled direct access without abandoning Linux (cf. libfuhrer's io_uring/O_DIRECT path).
- **Difference:** FuhrerOS decides *when* a fast path is used.

## Arrakis — Peter et al., OSDI 2014
- **Problem:** kernel-mediated I/O overhead.
- **Idea:** applications access virtualised devices directly; the kernel is the control plane.
- **Evaluation (reported):** 2–5× latency, up to 9× throughput for a persistent NoSQL workload vs tuned Linux.
- **Limitation:** requires SR-IOV-style hardware support; static choice of path.
- **Relevance:** direct I/O is a proven direction — **not our novelty claim**.
- **Difference:** FuhrerOS asks when specialised I/O is beneficial and switches accordingly.

## IX — Belay et al., OSDI 2014
- **Problem:** kernel overhead for high-speed networking.
- **Idea:** separate control plane and protected dataplane; dedicated hardware threads, queues, zero-copy, bounded batches.
- **Limitation:** networking-specific, dedicated resources.
- **Relevance:** a network fast path can exist beside a general stack (M10: busy-polling knobs).
- **Difference:** heterogeneous desktop workloads, no dedicated cores.

## EbbRT — Schatzberg et al., OSDI 2016
- **Problem:** generality vs cost of per-application OS specialisation.
- **Idea:** framework for per-application library OSes; event-driven runtime, low-overhead components.
- **Evaluation (reported):** memcached prototype with substantially higher throughput than Linux; Node.js port.
- **Limitation:** builds specialised environments rather than changing policy at runtime.
- **Relevance:** specialisation needs infrastructure (our policy framework + libfuhrer).

## Barrelfish / Multikernel — Baumann et al.
- **Problem:** multicore machines behave like distributed systems.
- **Idea:** per-core kernels with explicit message passing, replicated state.
- **Limitation:** motivated by large-scale multicore; unnecessary for the first prototype.
- **Relevance:** OS state and policy can be modular.

## OSv
- **Problem:** single-application VMs don't need a full general-purpose OS.
- **Idea:** OS built around one application for cloud VMs.
- **Limitation:** application/VM oriented, not a desktop environment.
- **Relevance:** don't give every abstraction to every workload.

## Unikraft
- **Idea:** modular OS primitives; applications select only needed components; POSIX compatibility layer.
- **Evaluation (reported):** Redis, SQLite, NGINX, HAProxy, TFLite, Memcached with small footprints.
- **Limitation:** specialisation at build/deploy time.
- **Difference:** FuhrerOS specialises **at runtime**.

## Rump kernels / rump file systems
- **Idea:** reuse mature NetBSD kernel components (drivers, filesystems) in userspace.
- **Relevance / design implication:** reuse Linux subsystems instead of rewriting them (D-001).

## KylinX — ATC 2018
- **Idea:** process-like VMs (pVMs) with dynamic library mapping, between processes and unikernels.
- **Evaluation (reported):** pVM fork ≈ 1.3 ms; IPC latency comparable to Unix IPC.
- **Limitation:** cloud virtualisation focus.
- **Relevance:** dynamic composition is viable.

## Unikernel Linux (UKL) — Raza et al., 2022
- **Idea:** link an application with the Linux kernel to run at supervisor level while normal processes continue alongside.
- **Implementation (reported):** ≈ 1,250 lines of Linux changes; multicore; bare metal and VMs; QEMU test flow.
- **Evaluation (reported):** modest gains unmodified; up to 26 % Redis throughput with further optimisation.
- **Limitation:** specialisation is application-selected.
- **Relevance:** **do not throw away Linux to get specialisation** (D-001, D-002).
- **Difference:** FuhrerOS selects policies from observed workload.

## HongMeng — Chen et al., OSDI 2024
- **Idea:** production microkernel with Linux API/ABI compatibility; differentiated isolation, flexible composition, policy-free kernel paging, address-token access control.
- **Insight:** microkernel overhead also comes from IPC frequency, duplicated state and capability handling.
- **Limitation:** not reproducible at BTP scale.
- **Relevance:** compatibility and performance designed together (M14 selftest).

## uCache — Meignan-Masson et al., FAST 2026
- **Problem:** simple OS cache vs fast but complex userspace cache.
- **Idea:** unikernel-based customisable I/O cache with application knowledge; uVFS for multiple backends.
- **Evaluation (reported):** comparable to kernel-bypass I/O libraries for out-of-memory workloads.
- **Relevance:** cache policy should depend on workload → M12 (read-ahead is the first, narrow cache knob FuhrerOS switches).
- **Difference:** static selection vs runtime selection.

## UnICom — Pan et al., FAST 2026
- **Problem:** polling (low latency, CPU-hungry) vs interrupts (efficient, wake-up cost).
- **Idea:** TagSched, TagPoll, SKIP; implemented in Linux; compared against ext4, BypassD, io_uring.
- **Evaluation (reported):** high performance at both low and high CPU utilisation.
- **Relevance:** a fixed I/O completion policy is not optimal across conditions — **very close** to FuhrerOS's direction.
- **Difference:** UnICom adapts one mechanism inside the kernel; FuhrerOS selects multi-subsystem policies from workload classes, from userspace.

## Xkernel — Chen et al., OSDI 2026
- **Problem:** Linux hard-codes performance constants that encode workload/hardware assumptions.
- **Idea:** Scoped Indirect Execution makes constants tunable at runtime without rebuilding; programmable policies with application hints and isolation; open-source tooling.
- **Relevance:** the closest recent work to the FuhrerOS policy engine.
- **Difference:** Xkernel tunes kernel constants (mechanism for tunability); FuhrerOS classifies workloads and selects among I/O / cache / scheduling strategies for heterogeneous personal workloads, using only already-exposed tunables so far. A natural extension (M13) is to use Xkernel-style tunables as additional policy actuators.

---

## Positioning (cautious)

Our literature review suggests prior systems demonstrate application
specialisation, direct I/O, or runtime kernel tunability. We investigate
their combination for heterogeneous personal workloads on one machine. A
comprehensive search (e.g. auto-tuning work such as machine-learning-based
kernel parameter tuners, and adaptive I/O schedulers) has **not** yet been
done; until it is, FuhrerOS is described as "a prototype that evaluates
runtime workload-aware policy selection", not as the first adaptive OS.
