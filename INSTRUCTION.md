# FuhrerOS

## Adaptive Operating-System Specialization for Personal Computing

**Project type:** Operating Systems / Systems Research / BTP
**Primary platform:** x86-64
**Primary execution environment:** QEMU/KVM virtual machine
**Secondary platform:** Bare-metal x86-64, after VM validation
**Host resources:** 64-bit CPU, 8 GB RAM, 512 GB SSD
**Initial guest allocation:** 2–4 vCPUs, 2–4 GB RAM, 20–50 GB virtual disk

---

# 1. Executive Summary

FuhrerOS is a research prototype investigating whether a general-purpose operating system can dynamically adapt its execution and I/O policies according to the workload currently running.

The project is motivated by a long line of systems research showing that general-purpose operating systems trade application-specific performance for compatibility and generality.

Examples include:

* Exokernel: move resource management toward applications.
* Dune: provide controlled userspace access to privileged hardware features.
* Arrakis: move common I/O operations out of the kernel while retaining kernel-level protection.
* IX: separate networking control-plane responsibilities from high-performance dataplane processing.
* EbbRT: make per-application library OS specialization easier.
* Unikraft: make modular unikernels practical and compatible with a useful application ecosystem.
* UKL: bring unikernel-style specialization into Linux rather than abandoning Linux compatibility.
* HongMeng: demonstrate that a production microkernel can retain Linux API/ABI compatibility.
* uCache: expose application-specific knowledge to the I/O cache.
* UnICom: adapt I/O completion to avoid the fixed polling-vs-interrupt tradeoff.
* Xkernel: make previously static Linux performance constants dynamically tunable.

These systems demonstrate that specialization can improve performance, but they generally focus on a particular application, deployment model, subsystem, or statically selected configuration.

FuhrerOS investigates the following question:

> **Can runtime workload characterization be used to dynamically select OS policies for heterogeneous personal-computing workloads, while retaining compatibility with ordinary Linux applications?**

The project will initially be implemented as a **Linux-based research prototype running in QEMU/KVM**, rather than as a completely new kernel.

This decision is deliberate.

Writing a complete kernel, filesystem, networking stack, GPU support, browser environment, and hardware drivers would turn the project into an OS-engineering exercise. Using Linux as the foundation allows the project to concentrate on the research contribution while still producing a system that can boot in a VM and eventually run useful applications.

---

# 2. Core Research Idea

The central idea is an **Adaptive OS Layer**.

Instead of forcing every workload through one fixed execution strategy:

```text
Application
     ↓
Linux
     ↓
Fixed OS policies
     ↓
Hardware
```

FuhrerOS introduces:

```text
Application
     ↓
FuhrerOS runtime / Linux interface
     ↓
Workload profiler
     ↓
Adaptive policy engine
     ↓
┌──────────────┬──────────────┬──────────────┐
│ CPU policy   │ Memory/I/O   │ Network/I/O  │
│              │ policy       │ policy       │
└──────────────┴──────────────┴──────────────┘
                     ↓
                 Linux kernel
                     ↓
                  Hardware
```

The policy engine observes workload characteristics such as:

```text
CPU utilization
system calls/sec
context switches
I/O size
I/O frequency
read/write ratio
sequential/random access
queue depth
cache behavior
network packet rate
latency sensitivity
memory pressure
```

It then chooses among available execution strategies.

For example:

```text
Low I/O rate
     ↓
normal kernel I/O


High-frequency small I/O
     ↓
specialized/direct I/O


Large sequential I/O
     ↓
read-ahead / batching


Latency-sensitive I/O
     ↓
polling / low-latency path


CPU-bound application
     ↓
CPU-oriented scheduling policy
```

The initial system should use **simple rule-based policies**.

Machine learning is optional and should only be introduced after the rule-based system has been experimentally validated.

---

# 3. What FuhrerOS Is NOT

FuhrerOS is not initially intended to be:

* a Linux replacement written entirely from scratch;
* a Chrome/Firefox replacement;
* a new filesystem designed from scratch;
* a new TCP/IP stack;
* a microkernel implementation for its own sake;
* a unikernel for one specific application;
* a collection of shell commands with no research contribution.

The project is instead:

> **A research platform for adaptive OS specialization.**

---

# 4. Why Linux Instead of a Completely New Kernel?

This is one of the most important design decisions.

A completely new OS would require:

```text
Bootloader
CPU initialization
Memory management
Virtual memory
Scheduler
Processes
Syscalls
ELF loader
Filesystem
Storage drivers
USB
Networking
TCP/IP
Graphics
Input
Audio
Applications
```

Most of this work would have little connection to the research question.

Instead:

```text
Linux
  │
  ├── mature scheduler
  ├── memory management
  ├── filesystem
  ├── networking
  ├── drivers
  ├── security
  └── application ecosystem
          │
          ▼
     FuhrerOS layer
```

This is supported by the direction of UKL: its authors explicitly pursue unikernel optimization techniques while retaining Linux's existing codebase, tools, hardware support and application ecosystem. UKL can run on bare metal and virtual servers and provides a QEMU test workflow.

Unikraft makes the same broader lesson clear from another direction: application compatibility and tooling are major barriers to unikernel adoption.

Therefore:

> **Compatibility is a feature of FuhrerOS, not an obstacle we should ignore.**

---

# 5. Literature Review

## 5.1 Exokernel

**Engler, Kaashoek, O'Toole — Exokernel: An Operating System Architecture for Application-Level Resource Management, SOSP 1995**

### Problem

Traditional OSes decide how resources such as memory and IPC should be managed.

This gives portability but restricts application-specific optimization.

### Idea

The kernel should primarily provide:

```text
protection
resource allocation
hardware multiplexing
```

while library operating systems implement higher-level policies.

### Architecture

```text
Application
    ↓
Library OS
    ↓
Exokernel
    ↓
Hardware
```

### Important lesson for FuhrerOS

Separate:

```text
resource protection
```

from:

```text
resource policy
```

This is foundational to our adaptive policy design.

### Limitation

The exokernel model requires significant application/library OS specialization and does not solve modern desktop application compatibility.

FuhrerOS retains conventional Linux applications while selectively specializing policies.

---

# 5.2 Dune

**Belay et al. — Dune: Safe User-level Access to Privileged CPU Features, OSDI 2012**

### Problem

Applications sometimes need access to hardware mechanisms normally reserved for the kernel.

### Idea

Use virtualization hardware to allow a Linux process to safely control privileged CPU features.

Dune provides userspace access to mechanisms including:

* page tables;
* protection levels;
* tagged TLB functionality;
* interrupt/exception mechanisms.

### Architecture

```text
Linux process
     ↓
Dune userspace library
     ↓
virtualization hardware
     ↓
CPU
```

The system consists of a Linux kernel module plus a userspace library.

### FuhrerOS lesson

We should investigate controlled mechanisms for giving selected applications more direct access to performance-critical facilities without abandoning Linux.

### Limitation

Dune is an enabling mechanism, not an adaptive OS policy system.

---

# 5.3 Arrakis

**Peter et al. — Arrakis: The Operating System is the Control Plane, OSDI 2014**

### Problem

Kernel-mediated I/O adds overhead.

### Idea

Applications directly access virtualized I/O devices while the kernel retains protection and resource-control responsibilities.

```text
Traditional:

Application
 ↓
Kernel
 ↓
Driver
 ↓
Device


Arrakis:

Application
 ↓
Virtual I/O
 ↓
Device

Kernel → protection/control
```

Arrakis reported 2–5× latency improvements and up to 9× throughput improvement for a persistent NoSQL workload relative to a tuned Linux implementation.

### FuhrerOS lesson

Direct I/O is a proven direction.

### Important research constraint

**Direct I/O itself is NOT our novelty claim.**

Our question is whether the system can determine *when* direct/specialized I/O is beneficial.

---

# 5.4 IX

**Belay et al. — IX: A Protected Dataplane Operating System for High Throughput and Low Latency, OSDI 2014**

### Problem

High-speed networking suffers from kernel overhead.

### Idea

Separate:

```text
control plane
```

from:

```text
network dataplane
```

and provide protected user-level networking.

IX uses hardware virtualization, dedicated hardware threads, networking queues, zero-copy APIs and bounded packet batches.

### FuhrerOS lesson

Networking can have a specialized fast path.

### Limitation

IX is primarily a high-performance networking architecture.

FuhrerOS must deal with heterogeneous desktop workloads.

---

# 5.5 EbbRT

**Schatzberg et al. — EbbRT: A Framework for Building Per-Application Library Operating Systems, OSDI 2016**

### Problem

General-purpose OSes sacrifice specialization for generality, while building a specialized OS for every application is expensive.

### Idea

Provide a framework for building per-application library OSes.

EbbRT uses:

* distributed OS architecture;
* low-overhead components;
* event-driven runtime;
* language-level primitives.

Its evaluation included a memcached prototype with substantially higher throughput than Linux and a Node.js port demonstrating broader application support.

### FuhrerOS lesson

Specialization needs infrastructure that makes it practical.

### Limitation

EbbRT focuses on building specialized environments rather than dynamically changing policies for a general-purpose personal system.

---

# 5.6 Barrelfish / Multikernel

**Baumann et al. — The Multikernel: A New OS Architecture for Scalable Multicore Systems**

### Problem

Modern multicore hardware behaves increasingly like a distributed system.

### Idea

Instead of maintaining one monolithic kernel state, treat cores as separate entities communicating through explicit mechanisms.

### FuhrerOS lesson

OS state and scheduling policy can be modular.

### Limitation

This is primarily motivated by large multicore scalability and is not necessary for the first FuhrerOS implementation.

---

# 5.7 OSv

**OSv — Operating System Designed for the Cloud**

### Problem

A VM running one application does not need all the functionality of a conventional general-purpose OS.

### Idea

Build an OS specifically around a single application.

OSv targets cloud VMs and performs aggressive specialization to minimize overhead.

### FuhrerOS lesson

The OS should not blindly provide every abstraction to every workload.

### Limitation

OSv is fundamentally application/VM oriented rather than a general personal desktop environment.

---

# 5.8 Unikraft

**Unikraft — Fast, Specialized Unikernels the Easy Way**

Unikraft modularizes OS primitives and allows applications to select only the components they need.

It addresses several historical barriers to unikernels:

* POSIX compatibility;
* tooling;
* modularity;
* security.

The project reports applications including Redis, SQLite, NGINX, HAProxy, TFLite and Memcached running with relatively small memory footprints, and provides a growing system-call compatibility layer.

### FuhrerOS lesson

Modularity and compatibility are essential.

### Limitation

Unikraft generally specializes an application at build/deployment time.

FuhrerOS asks whether specialization can happen **at runtime**.

---

# 5.9 Rump Kernels / Rump File Systems

Rump kernels reuse mature OS components such as NetBSD drivers in userspace rather than reimplementing everything.

Rump file systems demonstrate that existing kernel filesystem code can be reused as userspace components, improving testing and reducing the need to write duplicate implementations.

### FuhrerOS lesson

Do not rewrite mature subsystems merely for the sake of architectural purity.

### Design implication

Reuse Linux infrastructure wherever possible.

---

# 5.10 KylinX

**KylinX: A Dynamic Library Operating System for Simplified and Efficient Cloud Virtualization, ATC 2018**

KylinX explores a middle ground between:

```text
traditional processes
```

and:

```text
single-purpose unikernel appliances.
```

It introduces process-like VMs and supports dynamic library mapping. The reported pVM fork time was around 1.3 ms, with IPC latencies comparable to Unix IPC.

### FuhrerOS lesson

Dynamic composition is a viable design direction.

### Limitation

KylinX is focused on cloud virtualization rather than adaptive personal computing.

---

# 5.11 Unikernel Linux

**Raza et al. — Unikernel Linux, 2022**

This is one of the most directly relevant systems.

UKL allows a specially linked application to run directly with the Linux kernel at supervisor privilege, while ordinary user processes can continue running alongside it.

The authors report:

* modest gains without application modifications;
* up to 26% Redis throughput improvement with additional optimization;
* approximately 1,250 lines of Linux changes;
* multi-core support;
* bare-metal and virtual-machine execution.

### FuhrerOS lesson

**Do not throw away Linux to get specialization.**

### Limitation

The specialization is principally application-selected rather than dynamically determined by a workload-aware policy engine.

---

# 5.12 HongMeng Microkernel

**Chen et al. — Microkernel Goes General: Performance and Compatibility in the HongMeng Production Microkernel, OSDI 2024**

HongMeng addresses the challenge of making a general-purpose microkernel compatible with the Linux ecosystem.

The system preserves Linux API/ABI compatibility and explores:

* differentiated isolation;
* flexible composition;
* policy-free kernel paging;
* address-token-based access control.

The paper argues that microkernel overhead is not just about individual IPC latency; IPC frequency, duplicated state and capability handling also matter.

### FuhrerOS lesson

Compatibility and performance must be designed together.

### Limitation

HongMeng is a large production system and is not a realistic BTP-scale reproduction.

We should adopt concepts, not reproduce the architecture.

---

# 5.13 uCache — FAST 2026

**Meignan-Masson et al. — uCache: A Customizable Unikernel-based IO Cache, FAST 2026**

uCache attacks the tension between:

```text
simple OS cache
```

and:

```text
fast but complex userspace cache.
```

It uses a unikernel-based architecture to integrate application-specific knowledge into the OS cache and introduces uVFS for adapting to different I/O backends. Its evaluation reports performance comparable to kernel-bypass I/O libraries for out-of-memory workloads.

### FuhrerOS lesson

Caching policy should potentially depend on application/workload behavior.

### FuhrerOS extension

Instead of statically selecting one cache implementation:

```text
LRU
LFU
read-ahead
streaming
```

FuhrerOS could dynamically select a policy.

---

# 5.14 UnICom — FAST 2026

**Pan et al. — UnICom: A Universally High-Performant I/O Completion Mechanism, FAST 2026**

UnICom attacks the classic choice:

```text
Polling
→ low latency, CPU expensive

Interrupts
→ CPU efficient, wake-up overhead
```

It introduces:

* TagSched;
* TagPoll;
* SKIP.

The implementation is in Linux and is evaluated against ext4, BypassD and io_uring. The authors report high performance across both low- and high-CPU-utilization regimes.

### FuhrerOS lesson

A fixed I/O policy may not be optimal across workload conditions.

This is extremely close to our proposed research direction.

---

# 5.15 Xkernel — OSDI 2026

**Chen et al. — Xkernel: Principled Performance Tunability of Operating System Kernels, OSDI 2026**

Xkernel observes that Linux contains numerous performance-critical constants whose values encode assumptions about workloads and hardware.

It introduces **Scoped Indirect Execution (SIE)** to make these constants dynamically tunable at runtime without rebuilding/redeploying the kernel.

The system provides programmable policies that can incorporate application hints, hardware heuristics and isolation.

There is also an open-source implementation with tooling for building/loading runtime-tunable kernel configurations.

### FuhrerOS lesson

This is perhaps the closest recent work to the **adaptive policy engine** we want.

### Difference

Xkernel focuses on dynamically tuning kernel performance constants.

FuhrerOS proposes a broader workload-aware system that can select among:

```text
I/O strategies
cache strategies
scheduling strategies
resource policies
```

for heterogeneous personal workloads.

---

# 6. Synthesis of the Literature

The research progression can be summarized as:

```text
Exokernel
    │
    │ application-level resource management
    ▼
Dune
    │
    │ controlled hardware access from userspace
    ▼
Arrakis / IX
    │
    │ direct I/O / dataplane execution
    ▼
EbbRT / Unikraft
    │
    │ application-specific OS specialization
    ▼
UKL
    │
    │ specialization while retaining Linux
    ▼
uCache / UnICom
    │
    │ specialized I/O and adaptive mechanisms
    ▼
Xkernel
    │
    │ runtime OS performance tunability
    ▼
              FUHREROS
                 │
                 ▼
       Runtime workload-aware
       policy selection for
       heterogeneous personal
             workloads
```

This progression motivates our design, but **does not by itself establish novelty**.

The novelty claim must be refined after implementation and a more exhaustive search.

---

# 7. Proposed Research Gap

## Gap

Existing systems demonstrate:

```text
application specialization
```

or:

```text
direct I/O
```

or:

```text
runtime kernel tuning
```

but FuhrerOS will investigate their combination under a different workload model:

> **heterogeneous personal computing workloads sharing one machine.**

A laptop may simultaneously run:

```text
Browser
Compiler
Terminal
Editor
Python
File manager
Database
Media player
Background services
```

These applications have very different requirements.

A policy optimized for:

```text
database I/O
```

may be bad for:

```text
browser responsiveness.
```

A policy optimized for:

```text
maximum throughput
```

may waste CPU when:

```text
I/O load is low.
```

Therefore:

> **The research problem is not simply how to make one workload fast. It is whether an OS can identify the current workload characteristics and dynamically select an appropriate policy while preserving general-purpose application compatibility.**

---

# 8. Research Questions

### RQ1

Can runtime workload characteristics predict which OS execution/I/O policy is beneficial?

### RQ2

Can adaptive policy selection outperform a fixed policy across heterogeneous workloads?

### RQ3

What is the overhead of workload monitoring and policy switching?

### RQ4

How quickly must the system react when workload characteristics change?

### RQ5

Can adaptive policies improve performance without significantly increasing CPU or memory consumption?

### RQ6

Can this architecture preserve normal Linux application compatibility?

---

# 9. Hypotheses

### H1

A workload-aware adaptive policy will outperform at least one fixed policy for a meaningful subset of heterogeneous workloads.

### H2

The monitoring overhead can be kept sufficiently low that adaptive execution provides a net benefit.

### H3

Workload features such as I/O size, I/O frequency, queue depth, syscall rate and CPU utilization contain enough information to make useful policy decisions.

### H4

Adaptive policies can improve performance-per-resource rather than only raw throughput.

---

# 10. FuhrerOS Architecture

```text
                         APPLICATIONS
                              │
                ┌─────────────┴─────────────┐
                │                           │
          Normal Linux Apps          Fuhrer-aware Apps
                │                           │
                └─────────────┬─────────────┘
                              │
                       Linux API / Runtime
                              │
                    ┌─────────▼─────────┐
                    │ Workload Profiler │
                    └─────────┬─────────┘
                              │
                    ┌─────────▼─────────┐
                    │  Policy Engine    │
                    └─────────┬─────────┘
                              │
         ┌────────────────────┼────────────────────┐
         │                    │                    │
         ▼                    ▼                    ▼
   CPU Scheduler         Memory/Cache          I/O Policy
      Policy                Policy                 │
         │                    │              ┌──────┴──────┐
         │                    │              │             │
         │                    │           Normal         Fast
         │                    │           path           path
         │                    │              │             │
         └────────────────────┴──────────────┴─────────────┘
                                             │
                                       Linux Kernel
                                             │
                                        Virtio / VM
                                             │
                                            QEMU
```

---

# 11. Initial Adaptive Policies

Do not implement ten policies.

Start with three.

## Policy A — Normal

Use ordinary Linux interfaces.

```text
Application
 ↓
syscall
 ↓
Linux
```

This is the baseline.

---

## Policy B — Batched

Group operations to reduce overhead.

```text
Application
 ↓
batch
 ↓
kernel
 ↓
device
```

Useful for:

```text
many small operations
```

---

## Policy C — Specialized/Fast

Use an optimized path where supported.

Potential mechanisms:

```text
io_uring
shared queues
virtio queues
userspace buffers
kernel-assisted direct I/O
```

Do not jump directly to unsafe raw device access.

---

# 12. Adaptive Policy Engine

Initial implementation:

```text
if syscall_rate < T1
    normal

else if small_io_rate > T2
    specialized

else if sequential_io > T3
    batched

else
    normal
```

Every decision must be logged.

Example:

```text
[ADAPT]
PID=1823
IOPS=42180
avg_io=4096
CPU=63%
queue_depth=18
policy: NORMAL -> SPECIALIZED
reason: high_small_io
```

This is critical for research reproducibility.

---

# 13. Later: ML Policy

Do NOT start with ML.

First establish:

```text
rule-based adaptive policy
```

Then potentially collect:

```text
features
→ policy
→ performance outcome
```

Dataset:

```text
timestamp
application
CPU%
IOPS
IO size
read/write ratio
queue depth
context switches
memory pressure
network rate
selected policy
latency
throughput
CPU cost
```

Then investigate:

```text
Decision tree
Random forest
Gradient boosting
Contextual bandit
```

The ML model must be evaluated against the rule-based policy.

ML is therefore a **second-stage research extension**, not a prerequisite.

---

# 14. QEMU/KVM Requirement

This is mandatory.

## FuhrerOS must run in a VM before any bare-metal experiment.

Architecture:

```text
             YOUR COMPUTER
                  │
        ┌─────────┴─────────┐
        │                   │
   Existing OS         QEMU/KVM
                            │
                     ┌──────▼──────┐
                     │  FuhrerOS   │
                     │             │
                     │ 2–4 vCPU    │
                     │ 2–4 GB RAM  │
                     │ 20–50GB disk│
                     │ virtio NIC  │
                     └─────────────┘
```

QEMU provides x86 system emulation and can use KVM as a hardware-assisted accelerator on Linux hosts. QEMU also provides Windows Hypervisor Platform acceleration on Windows.

KVM exposes a stable API for creating VMs and virtual CPUs and devices.

---

# 15. VM Configurations

## Development VM

```text
CPU:       2 vCPU
RAM:       2 GB
Disk:      20 GB
Network:   virtio
```

## Benchmark VM

```text
CPU:       4 vCPU
RAM:       4 GB
Disk:      40 GB
Network:   virtio
```

## Stress VM

```text
CPU:       4 vCPU
RAM:       6 GB
Disk:      50 GB
```

Never allow the VM to consume all 8 GB of host memory.

---

# 16. QEMU Commands

The implementation must eventually provide:

```bash
./scripts/build.sh
./scripts/run.sh
./scripts/run-debug.sh
./scripts/reset-vm.sh
```

Conceptually:

```bash
qemu-system-x86_64 \
    -enable-kvm \
    -m 2G \
    -smp 2 \
    -drive file=fuhreros.qcow2,format=qcow2 \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0
```

On a Windows host, use the appropriate QEMU acceleration available through Windows Hypervisor Platform rather than assuming KVM. QEMU documents WHPX support for x86-64 guests.

---

# 17. VM Acceptance Criteria

Milestone 1 is NOT complete unless:

```text
[✓] VM boots
[✓] Kernel/userspace starts
[✓] Terminal works
[✓] Network interface appears
[✓] Virtual disk works
[✓] SSH/console access works
[✓] VM can be destroyed/recreated
[✓] No host disk is modified
```

Every later milestone must continue to boot in QEMU.

---

# 18. Browser Requirement

The browser is a **usability test**, not the research contribution.

Do NOT write a browser engine.

The goal is eventually:

```text
FuhrerOS VM
    ↓
Linux-compatible userspace
    ↓
browser
    ↓
Internet
```

The exact browser can be decided after compatibility testing.

The first browser milestone can be:

```bash
curl https://example.com
```

Then:

```text
text browser
```

Then eventually a graphical browser.

If full Firefox/Chromium support proves impractical inside the research prototype, that is not a failure of the core research.

The browser should be treated as an application-compatibility stress test.

---

# 19. Baselines

Every benchmark should compare:

### B0 — Native Linux

```text
Ubuntu/Fedora/etc.
```

### B1 — FuhrerOS with adaptive layer disabled

```text
Linux + Fuhrer infrastructure
fixed policy
```

### B2 — FuhrerOS adaptive

```text
Linux + profiler + policy engine
```

Potential future:

### B3 — Specialized

```text
FuhrerOS
manually selected optimal policy
```

B3 is important because it establishes the theoretical upper bound of the adaptive policy.

---

# 20. Benchmark Suite

## CPU

```text
SPEC-like microbenchmarks
sysbench CPU
compilation
```

Measure:

```text
execution time
CPU utilization
context switches
```

---

## Memory

```text
memory bandwidth
malloc/free
page faults
working-set changes
```

Measure:

```text
latency
bandwidth
RSS
page faults
```

---

## Storage

Use:

```text
fio
SQLite
file-copy benchmark
compiler workload
```

Measure:

```text
IOPS
throughput
P50 latency
P95 latency
P99 latency
CPU utilization
```

---

## Network

Use:

```text
iperf3
curl
HTTP benchmark
```

Measure:

```text
throughput
RTT
packets/sec
CPU utilization
```

---

## Desktop

Use realistic workloads:

```text
browser startup
file manager
large file copy
source compilation
Python package installation
archive extraction
media playback
```

Measure:

```text
startup latency
completion time
CPU
memory
I/O
```

---

# 21. Workload Transition Experiments

This is particularly important.

Do not only run:

```text
Workload A for 10 minutes
```

Run:

```text
0–60 sec: CPU-bound
60–120 sec: random I/O
120–180 sec: sequential I/O
180–240 sec: network-heavy
```

Then see whether FuhrerOS changes policy.

Example:

```text
Time
 │
 │ CPU       I/O          Network
 │
 ▼
 ─────────────────────────────────────
       │          │           │
       ▼          ▼           ▼
   CPU policy   I/O policy   Net policy
```

This is one of the most important experiments for the adaptive hypothesis.

---

# 22. Ablation Experiments

## A1 — No profiler

```text
Linux
vs
FuhrerOS without monitoring
```

Measures profiler overhead.

---

## A2 — Profiler but no adaptation

Collect metrics but never change policy.

Shows whether monitoring itself costs anything.

---

## A3 — Static policy

Choose one policy at boot.

Compare with adaptive.

---

## A4 — Slow adaptation

Change policy every 10 seconds.

---

## A5 — Fast adaptation

Change policy every 100 ms / 1 second.

Determine an appropriate interval experimentally.

---

## A6 — Individual features

Train/use policy with:

```text
CPU only
```

then:

```text
CPU + I/O
```

then:

```text
CPU + I/O + memory
```

Determine which features actually matter.

---

# 23. Expected Graphs

### Graph 1 — Throughput

```text
Workload
│
│          Adaptive
│        █████████
│
│ Linux  ███████
│ Static ████████
└────────────────────
```

---

### Graph 2 — P99 latency

Lower is better.

```text
Latency
│
│ Linux       ██████████
│ Static      ████████
│ Adaptive    █████
└────────────────────────
```

---

### Graph 3 — Adaptation

```text
Policy
│
│ CPU
│ ─────────────
│              \
│               \ I/O
│                ─────────────
│                            \
│                             \ Network
└────────────────────────────────────
             time
```

---

### Graph 4 — Monitoring overhead

```text
Performance
│
│
│─────────────── baseline
│───────────── adaptive
│
└────────────────────────
```

The difference should ideally be small.

---

# 24. What Counts as Success?

The project is NOT successful merely because:

```text
"FuhrerOS boots."
```

Success requires evidence.

### Minimum success

```text
✓ Runs reliably in QEMU/KVM
✓ Provides Linux userspace
✓ Collects workload statistics
✓ Changes policies dynamically
✓ Produces reproducible measurements
```

### Research success

At least one of the following should be demonstrated:

```text
adaptive policy improves throughput
```

or:

```text
adaptive policy reduces latency
```

or:

```text
adaptive policy reduces CPU cost
```

or:

```text
adaptive policy improves performance-per-resource
```

for a meaningful subset of workloads.

### Strong result

Demonstrate:

```text
Adaptive ≈ best static policy
```

across changing workloads, while:

```text
Adaptive > average fixed policy
```

and:

```text
monitoring overhead << performance benefit
```

---

# 25. What Counts as a Negative Result?

A negative result is acceptable.

For example:

```text
Adaptive policy:
+8% throughput

but

+15% CPU overhead
```

means the policy is not beneficial overall.

That is still a useful research result.

Similarly:

```text
Adaptive ≈ Static
```

may demonstrate that the selected workloads are not sufficiently heterogeneous.

Do not manipulate benchmarks to produce positive results.

---

# 26. Threats to Validity

## T1 — VM overhead

QEMU/KVM measurements may differ from bare metal.

Mitigation:

```text
VM experiments first
+
selected bare-metal validation later
```

---

## T2 — Small sample size

A few benchmarks may not represent personal computing.

Mitigation:

Use:

```text
CPU
memory
storage
network
interactive
mixed
```

workloads.

---

## T3 — Adaptive policy overfitting

A policy could perform well only on workloads used to develop it.

Mitigation:

Separate:

```text
development workloads
```

from:

```text
evaluation workloads
```

---

## T4 — Host interference

The host OS may affect VM performance.

Mitigation:

* isolate CPU cores where possible;
* repeat experiments;
* report variance;
* keep background host activity low.

---

## T5 — QEMU configuration

Different virtual hardware can produce different results.

Therefore record:

```text
QEMU version
kernel version
guest RAM
vCPU count
virtual disk type
network device
host CPU
```

for every experiment.

---

## T6 — Linux version dependence

Kernel behavior can change.

Record:

```text
Linux commit/version
compiler
configuration
```

in every benchmark report.

---

# 27. Reproducibility

Every experiment should generate:

```text
experiment/
├── config.json
├── workload.json
├── system-info.txt
├── metrics.csv
├── stdout.log
├── stderr.log
└── result.json
```

Example:

```json
{
  "kernel": "6.x.y",
  "qemu": "11.x",
  "vcpus": 4,
  "memory_mb": 4096,
  "disk_gb": 40,
  "policy": "adaptive",
  "interval_ms": 1000
}
```

Never report benchmark results without configuration metadata.

---

# 28. Project Repository

Recommended:

```text
fuhreros/
│
├── README.md
├── LICENSE
│
├── docs/
│   ├── research-design.md
│   ├── architecture.md
│   ├── literature.md
│   ├── decisions.md
│   ├── experiments.md
│   └── vm.md
│
├── kernel/
│
├── adaptive/
│   ├── profiler/
│   ├── policy/
│   ├── features/
│   └── controller/
│
├── runtime/
│
├── benchmarks/
│   ├── cpu/
│   ├── memory/
│   ├── storage/
│   ├── network/
│   └── desktop/
│
├── experiments/
│
├── scripts/
│
├── vm/
│   ├── qemu/
│   └── configs/
│
└── research-log/
    ├── decisions/
    ├── experiments/
    └── failures/
```

---

# 29. Engineering Reasoning Log

This is a mandatory part of the project.

Claude must maintain:

```text
docs/decisions.md
```

and:

```text
research-log/
```

The purpose is to preserve **engineering reasoning and decision rationale**.

Do not merely write:

```text
"Implemented profiler."
```

Write:

```text
Decision D-014

Question:
Should workload sampling happen every 10 ms, 100 ms,
1 second, or 10 seconds?

Evidence:
100 ms captures workload transitions reasonably well,
while 10 ms adds excessive measurement overhead.

Decision:
Start with 1 second.

Reason:
The initial research target is desktop-scale workload
adaptation rather than microsecond-scale packet scheduling.

Alternatives rejected:
10 ms — excessive overhead.
10 s — too slow for workload transitions.

Validation:
Compare 100 ms / 1 s / 10 s experimentally.
```

This is the kind of reasoning we want preserved.

### Important rule

The log should contain:

```text
decision
alternatives
evidence
reason
tradeoffs
experiment
result
```

It should NOT contain fabricated reasoning.

If Claude does not know why a previous decision was made, it must say:

```text
Unknown — needs verification.
```

rather than inventing a rationale.

---

# 30. Decision Record Format

Every major architectural decision gets:

```text
D-XXX

Title:

Date:

Problem:

Options considered:

Option A:
Option B:
Option C:

Decision:

Why:

Evidence:

Tradeoffs:

Expected impact:

How to validate:

Status:
```

---

# 31. Experiment Record Format

```text
E-XXX

Hypothesis:

Configuration:

Host:

Guest:

Kernel:

QEMU:

CPU:

RAM:

Workload:

Policy:

Metrics:

Expected result:

Actual result:

Variance:

Interpretation:

Threats to validity:

Next experiment:
```

---

# 32. Failure Log

Every serious failure should be documented.

```text
F-XXX

Symptom:

Command:

Expected:

Observed:

Root cause:

Fix:

Regression test:

Lesson:
```

This will prevent Claude from repeatedly making the same mistake.

---


---

# 34. Future Work

If the first prototype works, possible extensions include:

### ML policy selection

```text
features
 ↓
model
 ↓
policy
```

### Adaptive caching

Inspired by uCache.

### Adaptive I/O completion

Inspired by UnICom.

### Runtime kernel tuning

Inspired by Xkernel.

### User-level fast paths

Inspired by Arrakis/IX.

### Application-specific execution

Inspired by EbbRT/UKL.

### Stronger isolation

Inspired by Dune/HongMeng.

---

# 35. Final Architecture

The long-term FuhrerOS architecture should become:

```text
                         FUHREROS
                            │
                    ┌───────▼────────┐
                    │ Applications   │
                    └───────┬────────┘
                            │
                    Linux/POSIX API
                            │
                    ┌───────▼────────┐
                    │ Fuhrer Runtime │
                    └───────┬────────┘
                            │
                ┌───────────▼───────────┐
                │   Workload Profiler   │
                └───────────┬───────────┘
                            │
                ┌───────────▼───────────┐
                │    Policy Engine      │
                └───────────┬───────────┘
                            │
       ┌────────────────────┼────────────────────┐
       │                    │                    │
       ▼                    ▼                    ▼
   CPU Policy          Cache Policy          I/O Policy
       │                    │                    │
       │                    │            ┌───────┴───────┐
       │                    │            │               │
       │                    │          Normal          Fast
       │                    │            │               │
       └────────────────────┴────────────┴───────────────┘
                                        │
                                 Linux Kernel
                                        │
                              ┌─────────┴─────────┐
                              │                   │
                           Virtio              KVM
                              │                   │
                              └─────────┬─────────┘
                                        │
                                      QEMU
                                        │
                                     Hardware
```

---

# 36. Claude Master Implementation Prompt

The following prompt should be given to Claude as the **master project instruction**.

---

## START CLAUDE MASTER PROMPT

You are the primary systems engineer implementing **FuhrerOS**, a research prototype for adaptive operating-system specialization.

The goal is NOT to build a hobby OS merely for the sake of having a new kernel.

The research objective is:

> Investigate whether runtime workload characterization can be used to dynamically select OS execution, I/O, caching and scheduling policies for heterogeneous personal-computing workloads while retaining compatibility with normal Linux applications.

The system MUST be reproducible in a virtual machine.

The first supported execution environment is:

```text
QEMU/KVM
x86-64
2–4 vCPUs
2–4 GB RAM
20–50 GB virtual disk
virtio networking
```

Bare-metal execution is a later milestone.

---

## 1. NON-NEGOTIABLE PRINCIPLES

### Principle 1 — VM first

Never require the user's physical machine for development.

Every milestone must be testable in QEMU.

Never modify:

```text
host partitions
host bootloader
host filesystem
```

without explicit user approval.

The default workflow must be:

```text
build
→ create/reuse VM image
→ boot QEMU
→ run tests
→ collect logs
→ destroy/reset VM if necessary
```

---

### Principle 2 — Do not build a new kernel unnecessarily

Use Linux as the initial foundation.

The first prototype should be:

```text
Linux
+
FuhrerOS adaptive layer
+
runtime
+
profiler
+
policy engine
```

Only modify kernel code when the research experiment actually requires it.

Prefer:

```text
userspace
eBPF
io_uring
existing Linux interfaces
kernel modules
```

before maintaining a large kernel fork.

---

### Principle 3 — Research before optimization

Never optimize merely because something "looks slow."

Every optimization must have:

```text
hypothesis
baseline
measurement
change
measurement
comparison
```

---

### Principle 4 — Preserve reproducibility

Every benchmark must record:

```text
host
guest
kernel version
QEMU version
CPU
RAM
vCPU count
disk configuration
network configuration
policy
sampling interval
workload
commit hash
```

---

### Principle 5 — Never invent results

If a benchmark was not executed, write:

```text
NOT RUN
```

Never fabricate:

```text
throughput
latency
speedup
memory usage
CPU usage
```

---

# 2. RESEARCH MEMORY

Maintain these files continuously:

```text
docs/research-design.md
docs/architecture.md
docs/literature.md
docs/decisions.md
docs/experiments.md
research-log/
```

At the beginning of every development session:

1. inspect the repository;
2. inspect `docs/decisions.md`;
3. inspect recent research logs;
4. inspect current milestone status;
5. inspect known failures.

Do not assume previous implementation decisions from memory.

---

# 3. ENGINEERING REASONING LOG

For every significant decision create a decision record.

Use:

```text
D-XXX

Problem:

Context:

Options:

Option A:
Option B:
Option C:

Evidence:

Decision:

Reason:

Tradeoffs:

Expected effect:

Validation experiment:

Status:
```

Record engineering rationale, alternatives, evidence and tradeoffs.

Do not fabricate reasoning that was not established.

If uncertain:

```text
UNKNOWN — REQUIRES VERIFICATION
```

---

# 4. FAILURE LOG

Whenever a significant implementation failure occurs:

```text
F-XXX

Symptom:

Reproduction:

Expected:

Observed:

Root cause:

Fix:

Regression test:

Lesson:
```

Do not repeatedly retry the same failed approach without recording what changed.

---

# 5. EXPERIMENT LOG

Every experiment:

```text
E-XXX

Question:

Hypothesis:

Host:

Guest:

Kernel:

QEMU:

CPU:

RAM:

vCPUs:

Disk:

Network:

Workload:

Policy:

Configuration:

Metrics:

Result:

Variance:

Interpretation:

Threats to validity:

Next experiment:
```

---

# 6. DEVELOPMENT MILESTONES

Implement in this order.

Do NOT jump directly to advanced features.

---

## M0 — Reproducible VM

Create:

```text
scripts/build.sh
scripts/run.sh
scripts/debug.sh
scripts/reset-vm.sh
```

The system must boot Linux in QEMU.

Acceptance:

```text
./scripts/build.sh
./scripts/run.sh
```

must work.

---

## M1 — FuhrerOS Runtime

Create:

```text
fuhrer-runtime
```

running inside the Linux guest.

It should expose:

```text
fuhrer status
fuhrer profile
fuhrer policy
fuhrer benchmark
```

Example:

```text
fuhrer> status

CPU: 17%
Memory: 1.2 GB
IOPS: 120
Network: 0.4 MB/s
Policy: NORMAL
```

---

## M2 — Profiler

Collect:

```text
CPU utilization
memory utilization
context switches
system calls
I/O operations
I/O sizes
read/write ratio
queue depth
network throughput
process information
```

Do not collect unnecessary metrics.

Measure profiler overhead.

---

## M3 — Workload Classification

Start with rule-based classes:

```text
CPU_BOUND
IO_SEQUENTIAL
IO_RANDOM
NETWORK_HEAVY
INTERACTIVE
MIXED
```

Example:

```text
CPU > 80%
IOPS < threshold
→ CPU_BOUND
```

Do not use ML yet.

---

## M4 — Policy Framework

Implement:

```text
NormalPolicy
BatchedPolicy
SpecializedPolicy
```

Use a clean interface:

```text
Policy
 ├── initialize()
 ├── activate()
 ├── deactivate()
 ├── collect_metrics()
 └── name()
```

---

## M5 — Static Policy Benchmark

Before adaptation:

```text
Linux baseline
Fuhrer normal
Fuhrer batched
Fuhrer specialized
```

Determine which policy performs best for each workload.

This establishes the ground truth needed for adaptive policy research.

---

## M6 — Adaptive Controller

Architecture:

```text
Profiler
   ↓
Feature extractor
   ↓
Workload classifier
   ↓
Policy engine
   ↓
Policy selection
```

Policy changes must be logged:

```text
[ADAPT]
PID=1234
old=normal
new=specialized
reason=high_small_io
```

---

## M7 — Policy Switching Cost

Measure:

```text
switch latency
CPU overhead
memory overhead
transient performance degradation
```

Determine whether adaptation is worthwhile.

---

## M8 — Storage Fast Path

Investigate:

```text
io_uring
batched I/O
O_DIRECT where appropriate
virtio queues
shared buffers
```

Do not implement raw hardware access until necessary.

The first fast path should be safe and measurable.

---

## M9 — Adaptive Storage

Example:

```text
random small I/O
→ specialized policy

large sequential I/O
→ batching/read-ahead

low I/O load
→ normal policy
```

Compare:

```text
fixed policy
vs
adaptive
```

---

## M10 — Network Fast Path

Investigate:

```text
io_uring networking
SO_BUSY_POLL where appropriate
virtio
batching
zero-copy mechanisms
```

Do not implement a new TCP stack.

---

## M11 — Adaptive Network Policy

Potential policies:

```text
latency-oriented
throughput-oriented
CPU-efficient
```

The policy should be selected according to observed workload.

---

## M12 — Adaptive Cache Experiment

Investigate:

```text
read-ahead
cache pressure
application access patterns
mmap
page cache behavior
```

Do not replace Linux's entire page cache.

Implement a narrowly scoped experiment.

---

## M13 — Optional Kernel Integration

Only after userspace experiments work.

Possible mechanisms:

```text
eBPF
kernel module
small kernel patch
```

Keep kernel modifications as small as possible.

---

## M14 — Browser/Application Compatibility

Demonstrate:

```text
curl
Python
GCC
Git
text editor
browser
```

The browser is an application-compatibility test, not the research contribution.

---

## M15 — Full Benchmark

Run:

```text
Linux
Fuhrer static
Fuhrer adaptive
```

across:

```text
CPU
memory
storage
network
desktop
mixed workloads
```

---

# 7. QEMU REQUIREMENT

Every milestone must provide a QEMU test.

Default VM:

```text
Architecture: x86-64
vCPUs: 2
RAM: 2 GB
Disk: 20 GB
NIC: virtio
```

Benchmark VM:

```text
vCPUs: 4
RAM: 4 GB
Disk: 40 GB
NIC: virtio
```

Never assume KVM exists.

Detect:

```text
KVM available
```

and otherwise fall back to:

```text
TCG
```

when feasible.

Print:

```text
Acceleration: KVM
```

or:

```text
Acceleration: TCG
```

at startup.

---

# 8. BUILD COMMANDS

The project must eventually support:

```bash
./build.sh
./run.sh
./run-debug.sh
./reset.sh
./benchmark.sh
./test.sh
```

Also support:

```bash
make
make run
make test
make benchmark
make clean
```

if Make is used.

---

# 9. RESEARCH DASHBOARD

Create:

```text
fuhrer status
```

Output:

```text
FUHREROS STATUS
────────────────────────

Runtime:        ACTIVE
Profiler:       ACTIVE
Policy engine:  ACTIVE

CPU:            23%
Memory:         41%

I/O:
  Read:         12 MB/s
  Write:        3 MB/s
  IOPS:         421

Network:
  RX:            2.1 MB/s
  TX:            0.7 MB/s

Workload:
  IO_RANDOM

Policy:
  SPECIALIZED

Policy since:
  4.21 seconds

Switches:
  3
```

This will also make demonstrations much easier.

---

# 10. RESEARCH SAFETY

Never make adaptive decisions that can:

```text
corrupt data
disable filesystem integrity
bypass security
modify host disks
```

Performance experimentation must never compromise correctness.

Correctness has priority:

```text
Correctness
>
Safety
>
Reproducibility
>
Performance
```

---

# 11. BENCHMARKING RULES

Every benchmark:

1. warm up;
2. execute multiple repetitions;
3. report median;
4. report P95/P99 where appropriate;
5. report variance;
6. save raw measurements;
7. save configuration;
8. compare identical VM configurations.

Do not compare:

```text
Linux: 4 CPUs / 4GB
Fuhrer: 2 CPUs / 2GB
```

and call the difference an OS improvement.

---

# 12. BASELINES

Always maintain:

```text
B0 = native Linux
B1 = Fuhrer static
B2 = Fuhrer adaptive
```

Optionally:

```text
B3 = manually optimized/static-best
```

B3 tells us whether adaptive policy can approach the best known static configuration.

---

# 13. REQUIRED ABLATIONS

Run:

```text
A1: no profiler
A2: profiler, no adaptation
A3: static policy
A4: adaptive policy
A5: different sampling intervals
A6: different feature sets
A7: different policy-switch thresholds
```

Do not omit these simply because the full adaptive system looks better.

---

# 14. LITERATURE TRACKING

Maintain:

```text
docs/literature.md
```

with at least:

```text
Exokernel
Dune
Arrakis
IX
EbbRT
Barrelfish
OSv
Unikraft
Rump Kernels
KylinX
UKL
HongMeng
uCache
UnICom
Xkernel
```

For each:

```text
Problem
Idea
Architecture
Implementation
Evaluation
Limitation
Relevance to FuhrerOS
Difference from FuhrerOS
```

Never claim novelty without checking related work.

---

# 15. NOVELTY POLICY

Do NOT write:

> "FuhrerOS is the first adaptive OS."

unless a comprehensive literature search actually establishes this.

Use cautious language:

> "We investigate..."

> "We propose..."

> "Our prototype evaluates..."

> "Our literature review suggests..."

Only make a stronger novelty claim after verification.

---

# 16. DOCUMENTATION REQUIREMENT

After every milestone update:

```text
docs/architecture.md
docs/decisions.md
docs/experiments.md
README.md
```

Do not leave documentation until the end.

---

# 17. CLAUDE SESSION PROTOCOL

At the beginning of every session:

```text
1. Inspect git status.
2. Inspect current milestone.
3. Read docs/decisions.md.
4. Read recent research logs.
5. Inspect failed experiments.
6. Run existing tests.
7. Identify exactly one next objective.
```

Then state:

```text
Current milestone:
Objective:
Relevant previous decisions:
Files likely affected:
Validation plan:
```

After implementation:

```text
Files changed:
Tests:
Build:
QEMU:
Benchmark:
Research implication:
Documentation updated:
Next objective:
```

---

# 18. DO NOT MAKE LARGE UNVERIFIED CHANGES

Never modify 20 unrelated files simply to "make things work."

Prefer:

```text
small patch
→ build
→ test
→ QEMU
→ commit
```

Use Git commits after stable milestones.

Commit format:

```text
m0: add reproducible qemu environment
m1: add fuhrer runtime
m2: add workload profiler
m3: add workload classifier
...
```

---

# 19. WHEN SOMETHING BREAKS

Do NOT immediately rewrite the architecture.

First:

```text
reproduce
↓
collect logs
↓
identify layer
↓
form hypothesis
↓
test hypothesis
↓
patch
↓
regression test
```

Record the process in `research-log/`.

---

# 20. FIRST TASK

Do ONLY this.

### Task

Create the initial FuhrerOS repository and a reproducible Linux/QEMU/KVM development VM.

Deliver:

```text
fuhreros/
├── README.md
├── docs/
│   ├── architecture.md
│   ├── decisions.md
│   └── vm.md
├── scripts/
│   ├── build.sh
│   ├── run.sh
│   ├── debug.sh
│   └── reset.sh
└── vm/
```

Requirements:

```text
x86-64
QEMU
2 vCPU
2GB RAM
20GB disk
virtio network
```

The VM must boot successfully.

Do NOT implement the profiler yet.

Do NOT implement adaptive policies yet.

Do NOT modify the host OS.

After completing this task, stop and report:

```text
BUILD:
PASS/FAIL

QEMU:
PASS/FAIL

KVM:
AVAILABLE/UNAVAILABLE

VM:
BOOTED/FAILED

FILES:
...

TESTS:
...

DECISION LOG:
...

NEXT MILESTONE:
M1
```

Do not proceed to M1 until the user explicitly asks you to continue.

## END CLAUDE MASTER PROMPT

---

# 37. Final Research Position

After the literature review, I would frame FuhrerOS as:

> **not a new OS for the sake of being an OS, but a reproducible experimental platform for studying adaptive OS specialization.**

The lineage is:

```text
Exokernel
  ↓
application-level resource policy

Dune
  ↓
safe userspace hardware control

Arrakis / IX
  ↓
direct datapaths

EbbRT / Unikraft
  ↓
specialized library OSes

UKL
  ↓
specialization without abandoning Linux

uCache / UnICom
  ↓
specialized/adaptive I/O

Xkernel
  ↓
runtime kernel tunability

             ↓

        FUHREROS
             ↓
runtime workload-aware
policy selection for
personal computing
```

The **most important design choice** is that FuhrerOS starts as a **Linux-based QEMU/KVM research prototype**. UKL provides a particularly strong precedent: its implementation explicitly supports building and booting the specialized system under QEMU and can also run on bare metal.

And QEMU/KVM is a solid foundation for the experimental environment: QEMU provides x86 system emulation and KVM acceleration on Linux, while Windows hosts can use WHPX acceleration.

The recent papers are especially valuable because **UnICom and Xkernel make the adaptive-policy direction much more concrete**: UnICom shows that I/O completion strategy can benefit from adapting to CPU/I/O conditions, while Xkernel explicitly exposes runtime tunability and programmable policies in Linux.

So I would **freeze this as the research direction for now**:

> **FuhrerOS = Linux-compatible, QEMU-first, workload-aware adaptive OS research platform.**

And the very first deliverable should be a **reproducible QEMU VM**, not a kernel. That gives us a safe foundation where every subsequent research idea can be tested experimentally.

# FuhrerOS

## Adaptive Operating-System Specialization for Personal Computing

**Project type:** Operating Systems / Systems Research / BTP
**Primary platform:** x86-64
**Primary execution environment:** QEMU/KVM virtual machine
**Secondary platform:** Bare-metal x86-64, after VM validation
**Host resources:** 64-bit CPU, 8 GB RAM, 512 GB SSD
**Initial guest allocation:** 2–4 vCPUs, 2–4 GB RAM, 20–50 GB virtual disk

---

# 1. Executive Summary

FuhrerOS is a research prototype investigating whether a general-purpose operating system can dynamically adapt its execution and I/O policies according to the workload currently running.

The project is motivated by a long line of systems research showing that general-purpose operating systems trade application-specific performance for compatibility and generality.

Examples include:

* Exokernel: move resource management toward applications.
* Dune: provide controlled userspace access to privileged hardware features.
* Arrakis: move common I/O operations out of the kernel while retaining kernel-level protection.
* IX: separate networking control-plane responsibilities from high-performance dataplane processing.
* EbbRT: make per-application library OS specialization easier.
* Unikraft: make modular unikernels practical and compatible with a useful application ecosystem.
* UKL: bring unikernel-style specialization into Linux rather than abandoning Linux compatibility.
* HongMeng: demonstrate that a production microkernel can retain Linux API/ABI compatibility.
* uCache: expose application-specific knowledge to the I/O cache.
* UnICom: adapt I/O completion to avoid the fixed polling-vs-interrupt tradeoff.
* Xkernel: make previously static Linux performance constants dynamically tunable.

These systems demonstrate that specialization can improve performance, but they generally focus on a particular application, deployment model, subsystem, or statically selected configuration.

FuhrerOS investigates the following question:

> **Can runtime workload characterization be used to dynamically select OS policies for heterogeneous personal-computing workloads, while retaining compatibility with ordinary Linux applications?**

The project will initially be implemented as a **Linux-based research prototype running in QEMU/KVM**, rather than as a completely new kernel.

This decision is deliberate.

Writing a complete kernel, filesystem, networking stack, GPU support, browser environment, and hardware drivers would turn the project into an OS-engineering exercise. Using Linux as the foundation allows the project to concentrate on the research contribution while still producing a system that can boot in a VM and eventually run useful applications.

---

# 2. Core Research Idea

The central idea is an **Adaptive OS Layer**.

Instead of forcing every workload through one fixed execution strategy:

```text
Application
     ↓
Linux
     ↓
Fixed OS policies
     ↓
Hardware
```

FuhrerOS introduces:

```text
Application
     ↓
FuhrerOS runtime / Linux interface
     ↓
Workload profiler
     ↓
Adaptive policy engine
     ↓
┌──────────────┬──────────────┬──────────────┐
│ CPU policy   │ Memory/I/O   │ Network/I/O  │
│              │ policy       │ policy       │
└──────────────┴──────────────┴──────────────┘
                     ↓
                 Linux kernel
                     ↓
                  Hardware
```

The policy engine observes workload characteristics such as:

```text
CPU utilization
system calls/sec
context switches
I/O size
I/O frequency
read/write ratio
sequential/random access
queue depth
cache behavior
network packet rate
latency sensitivity
memory pressure
```

It then chooses among available execution strategies.

For example:

```text
Low I/O rate
     ↓
normal kernel I/O


High-frequency small I/O
     ↓
specialized/direct I/O


Large sequential I/O
     ↓
read-ahead / batching


Latency-sensitive I/O
     ↓
polling / low-latency path


CPU-bound application
     ↓
CPU-oriented scheduling policy
```

The initial system should use **simple rule-based policies**.

Machine learning is optional and should only be introduced after the rule-based system has been experimentally validated.

---

# 3. What FuhrerOS Is NOT

FuhrerOS is not initially intended to be:

* a Linux replacement written entirely from scratch;
* a Chrome/Firefox replacement;
* a new filesystem designed from scratch;
* a new TCP/IP stack;
* a microkernel implementation for its own sake;
* a unikernel for one specific application;
* a collection of shell commands with no research contribution.

The project is instead:

> **A research platform for adaptive OS specialization.**

---

# 4. Why Linux Instead of a Completely New Kernel?

This is one of the most important design decisions.

A completely new OS would require:

```text
Bootloader
CPU initialization
Memory management
Virtual memory
Scheduler
Processes
Syscalls
ELF loader
Filesystem
Storage drivers
USB
Networking
TCP/IP
Graphics
Input
Audio
Applications
```

Most of this work would have little connection to the research question.

Instead:

```text
Linux
  │
  ├── mature scheduler
  ├── memory management
  ├── filesystem
  ├── networking
  ├── drivers
  ├── security
  └── application ecosystem
          │
          ▼
     FuhrerOS layer
```

This is supported by the direction of UKL: its authors explicitly pursue unikernel optimization techniques while retaining Linux's existing codebase, tools, hardware support and application ecosystem. UKL can run on bare metal and virtual servers and provides a QEMU test workflow.

Unikraft makes the same broader lesson clear from another direction: application compatibility and tooling are major barriers to unikernel adoption.

Therefore:

> **Compatibility is a feature of FuhrerOS, not an obstacle we should ignore.**

---

# 5. Literature Review

## 5.1 Exokernel

**Engler, Kaashoek, O'Toole — Exokernel: An Operating System Architecture for Application-Level Resource Management, SOSP 1995**

### Problem

Traditional OSes decide how resources such as memory and IPC should be managed.

This gives portability but restricts application-specific optimization.

### Idea

The kernel should primarily provide:

```text
protection
resource allocation
hardware multiplexing
```

while library operating systems implement higher-level policies.

### Architecture

```text
Application
    ↓
Library OS
    ↓
Exokernel
    ↓
Hardware
```

### Important lesson for FuhrerOS

Separate:

```text
resource protection
```

from:

```text
resource policy
```

This is foundational to our adaptive policy design.

### Limitation

The exokernel model requires significant application/library OS specialization and does not solve modern desktop application compatibility.

FuhrerOS retains conventional Linux applications while selectively specializing policies.

---

# 5.2 Dune

**Belay et al. — Dune: Safe User-level Access to Privileged CPU Features, OSDI 2012**

### Problem

Applications sometimes need access to hardware mechanisms normally reserved for the kernel.

### Idea

Use virtualization hardware to allow a Linux process to safely control privileged CPU features.

Dune provides userspace access to mechanisms including:

* page tables;
* protection levels;
* tagged TLB functionality;
* interrupt/exception mechanisms.

### Architecture

```text
Linux process
     ↓
Dune userspace library
     ↓
virtualization hardware
     ↓
CPU
```

The system consists of a Linux kernel module plus a userspace library.

### FuhrerOS lesson

We should investigate controlled mechanisms for giving selected applications more direct access to performance-critical facilities without abandoning Linux.

### Limitation

Dune is an enabling mechanism, not an adaptive OS policy system.

---

# 5.3 Arrakis

**Peter et al. — Arrakis: The Operating System is the Control Plane, OSDI 2014**

### Problem

Kernel-mediated I/O adds overhead.

### Idea

Applications directly access virtualized I/O devices while the kernel retains protection and resource-control responsibilities.

```text
Traditional:

Application
 ↓
Kernel
 ↓
Driver
 ↓
Device


Arrakis:

Application
 ↓
Virtual I/O
 ↓
Device

Kernel → protection/control
```

Arrakis reported 2–5× latency improvements and up to 9× throughput improvement for a persistent NoSQL workload relative to a tuned Linux implementation.

### FuhrerOS lesson

Direct I/O is a proven direction.

### Important research constraint

**Direct I/O itself is NOT our novelty claim.**

Our question is whether the system can determine *when* direct/specialized I/O is beneficial.

---

# 5.4 IX

**Belay et al. — IX: A Protected Dataplane Operating System for High Throughput and Low Latency, OSDI 2014**

### Problem

High-speed networking suffers from kernel overhead.

### Idea

Separate:

```text
control plane
```

from:

```text
network dataplane
```

and provide protected user-level networking.

IX uses hardware virtualization, dedicated hardware threads, networking queues, zero-copy APIs and bounded packet batches.

### FuhrerOS lesson

Networking can have a specialized fast path.

### Limitation

IX is primarily a high-performance networking architecture.

FuhrerOS must deal with heterogeneous desktop workloads.

---

# 5.5 EbbRT

**Schatzberg et al. — EbbRT: A Framework for Building Per-Application Library Operating Systems, OSDI 2016**

### Problem

General-purpose OSes sacrifice specialization for generality, while building a specialized OS for every application is expensive.

### Idea

Provide a framework for building per-application library OSes.

EbbRT uses:

* distributed OS architecture;
* low-overhead components;
* event-driven runtime;
* language-level primitives.

Its evaluation included a memcached prototype with substantially higher throughput than Linux and a Node.js port demonstrating broader application support.

### FuhrerOS lesson

Specialization needs infrastructure that makes it practical.

### Limitation

EbbRT focuses on building specialized environments rather than dynamically changing policies for a general-purpose personal system.

---

# 5.6 Barrelfish / Multikernel

**Baumann et al. — The Multikernel: A New OS Architecture for Scalable Multicore Systems**

### Problem

Modern multicore hardware behaves increasingly like a distributed system.

### Idea

Instead of maintaining one monolithic kernel state, treat cores as separate entities communicating through explicit mechanisms.

### FuhrerOS lesson

OS state and scheduling policy can be modular.

### Limitation

This is primarily motivated by large multicore scalability and is not necessary for the first FuhrerOS implementation.

---

# 5.7 OSv

**OSv — Operating System Designed for the Cloud**

### Problem

A VM running one application does not need all the functionality of a conventional general-purpose OS.

### Idea

Build an OS specifically around a single application.

OSv targets cloud VMs and performs aggressive specialization to minimize overhead.

### FuhrerOS lesson

The OS should not blindly provide every abstraction to every workload.

### Limitation

OSv is fundamentally application/VM oriented rather than a general personal desktop environment.

---

# 5.8 Unikraft

**Unikraft — Fast, Specialized Unikernels the Easy Way**

Unikraft modularizes OS primitives and allows applications to select only the components they need.

It addresses several historical barriers to unikernels:

* POSIX compatibility;
* tooling;
* modularity;
* security.

The project reports applications including Redis, SQLite, NGINX, HAProxy, TFLite and Memcached running with relatively small memory footprints, and provides a growing system-call compatibility layer.

### FuhrerOS lesson

Modularity and compatibility are essential.

### Limitation

Unikraft generally specializes an application at build/deployment time.

FuhrerOS asks whether specialization can happen **at runtime**.

---

# 5.9 Rump Kernels / Rump File Systems

Rump kernels reuse mature OS components such as NetBSD drivers in userspace rather than reimplementing everything.

Rump file systems demonstrate that existing kernel filesystem code can be reused as userspace components, improving testing and reducing the need to write duplicate implementations.

### FuhrerOS lesson

Do not rewrite mature subsystems merely for the sake of architectural purity.

### Design implication

Reuse Linux infrastructure wherever possible.

---

# 5.10 KylinX

**KylinX: A Dynamic Library Operating System for Simplified and Efficient Cloud Virtualization, ATC 2018**

KylinX explores a middle ground between:

```text
traditional processes
```

and:

```text
single-purpose unikernel appliances.
```

It introduces process-like VMs and supports dynamic library mapping. The reported pVM fork time was around 1.3 ms, with IPC latencies comparable to Unix IPC.

### FuhrerOS lesson

Dynamic composition is a viable design direction.

### Limitation

KylinX is focused on cloud virtualization rather than adaptive personal computing.

---

# 5.11 Unikernel Linux

**Raza et al. — Unikernel Linux, 2022**

This is one of the most directly relevant systems.

UKL allows a specially linked application to run directly with the Linux kernel at supervisor privilege, while ordinary user processes can continue running alongside it.

The authors report:

* modest gains without application modifications;
* up to 26% Redis throughput improvement with additional optimization;
* approximately 1,250 lines of Linux changes;
* multi-core support;
* bare-metal and virtual-machine execution.

### FuhrerOS lesson

**Do not throw away Linux to get specialization.**

### Limitation

The specialization is principally application-selected rather than dynamically determined by a workload-aware policy engine.

---

# 5.12 HongMeng Microkernel

**Chen et al. — Microkernel Goes General: Performance and Compatibility in the HongMeng Production Microkernel, OSDI 2024**

HongMeng addresses the challenge of making a general-purpose microkernel compatible with the Linux ecosystem.

The system preserves Linux API/ABI compatibility and explores:

* differentiated isolation;
* flexible composition;
* policy-free kernel paging;
* address-token-based access control.

The paper argues that microkernel overhead is not just about individual IPC latency; IPC frequency, duplicated state and capability handling also matter.

### FuhrerOS lesson

Compatibility and performance must be designed together.

### Limitation

HongMeng is a large production system and is not a realistic BTP-scale reproduction.

We should adopt concepts, not reproduce the architecture.

---

# 5.13 uCache — FAST 2026

**Meignan-Masson et al. — uCache: A Customizable Unikernel-based IO Cache, FAST 2026**

uCache attacks the tension between:

```text
simple OS cache
```

and:

```text
fast but complex userspace cache.
```

It uses a unikernel-based architecture to integrate application-specific knowledge into the OS cache and introduces uVFS for adapting to different I/O backends. Its evaluation reports performance comparable to kernel-bypass I/O libraries for out-of-memory workloads.

### FuhrerOS lesson

Caching policy should potentially depend on application/workload behavior.

### FuhrerOS extension

Instead of statically selecting one cache implementation:

```text
LRU
LFU
read-ahead
streaming
```

FuhrerOS could dynamically select a policy.

---

# 5.14 UnICom — FAST 2026

**Pan et al. — UnICom: A Universally High-Performant I/O Completion Mechanism, FAST 2026**

UnICom attacks the classic choice:

```text
Polling
→ low latency, CPU expensive

Interrupts
→ CPU efficient, wake-up overhead
```

It introduces:

* TagSched;
* TagPoll;
* SKIP.

The implementation is in Linux and is evaluated against ext4, BypassD and io_uring. The authors report high performance across both low- and high-CPU-utilization regimes.

### FuhrerOS lesson

A fixed I/O policy may not be optimal across workload conditions.

This is extremely close to our proposed research direction.

---

# 5.15 Xkernel — OSDI 2026

**Chen et al. — Xkernel: Principled Performance Tunability of Operating System Kernels, OSDI 2026**

Xkernel observes that Linux contains numerous performance-critical constants whose values encode assumptions about workloads and hardware.

It introduces **Scoped Indirect Execution (SIE)** to make these constants dynamically tunable at runtime without rebuilding/redeploying the kernel.

The system provides programmable policies that can incorporate application hints, hardware heuristics and isolation.

There is also an open-source implementation with tooling for building/loading runtime-tunable kernel configurations.

### FuhrerOS lesson

This is perhaps the closest recent work to the **adaptive policy engine** we want.

### Difference

Xkernel focuses on dynamically tuning kernel performance constants.

FuhrerOS proposes a broader workload-aware system that can select among:

```text
I/O strategies
cache strategies
scheduling strategies
resource policies
```

for heterogeneous personal workloads.

---

# 6. Synthesis of the Literature

The research progression can be summarized as:

```text
Exokernel
    │
    │ application-level resource management
    ▼
Dune
    │
    │ controlled hardware access from userspace
    ▼
Arrakis / IX
    │
    │ direct I/O / dataplane execution
    ▼
EbbRT / Unikraft
    │
    │ application-specific OS specialization
    ▼
UKL
    │
    │ specialization while retaining Linux
    ▼
uCache / UnICom
    │
    │ specialized I/O and adaptive mechanisms
    ▼
Xkernel
    │
    │ runtime OS performance tunability
    ▼
              FUHREROS
                 │
                 ▼
       Runtime workload-aware
       policy selection for
       heterogeneous personal
             workloads
```

This progression motivates our design, but **does not by itself establish novelty**.

The novelty claim must be refined after implementation and a more exhaustive search.

---

# 7. Proposed Research Gap

## Gap

Existing systems demonstrate:

```text
application specialization
```

or:

```text
direct I/O
```

or:

```text
runtime kernel tuning
```

but FuhrerOS will investigate their combination under a different workload model:

> **heterogeneous personal computing workloads sharing one machine.**

A laptop may simultaneously run:

```text
Browser
Compiler
Terminal
Editor
Python
File manager
Database
Media player
Background services
```

These applications have very different requirements.

A policy optimized for:

```text
database I/O
```

may be bad for:

```text
browser responsiveness.
```

A policy optimized for:

```text
maximum throughput
```

may waste CPU when:

```text
I/O load is low.
```

Therefore:

> **The research problem is not simply how to make one workload fast. It is whether an OS can identify the current workload characteristics and dynamically select an appropriate policy while preserving general-purpose application compatibility.**

---

# 8. Research Questions

### RQ1

Can runtime workload characteristics predict which OS execution/I/O policy is beneficial?

### RQ2

Can adaptive policy selection outperform a fixed policy across heterogeneous workloads?

### RQ3

What is the overhead of workload monitoring and policy switching?

### RQ4

How quickly must the system react when workload characteristics change?

### RQ5

Can adaptive policies improve performance without significantly increasing CPU or memory consumption?

### RQ6

Can this architecture preserve normal Linux application compatibility?

---

# 9. Hypotheses

### H1

A workload-aware adaptive policy will outperform at least one fixed policy for a meaningful subset of heterogeneous workloads.

### H2

The monitoring overhead can be kept sufficiently low that adaptive execution provides a net benefit.

### H3

Workload features such as I/O size, I/O frequency, queue depth, syscall rate and CPU utilization contain enough information to make useful policy decisions.

### H4

Adaptive policies can improve performance-per-resource rather than only raw throughput.

---

# 10. FuhrerOS Architecture

```text
                         APPLICATIONS
                              │
                ┌─────────────┴─────────────┐
                │                           │
          Normal Linux Apps          Fuhrer-aware Apps
                │                           │
                └─────────────┬─────────────┘
                              │
                       Linux API / Runtime
                              │
                    ┌─────────▼─────────┐
                    │ Workload Profiler │
                    └─────────┬─────────┘
                              │
                    ┌─────────▼─────────┐
                    │  Policy Engine    │
                    └─────────┬─────────┘
                              │
         ┌────────────────────┼────────────────────┐
         │                    │                    │
         ▼                    ▼                    ▼
   CPU Scheduler         Memory/Cache          I/O Policy
      Policy                Policy                 │
         │                    │              ┌──────┴──────┐
         │                    │              │             │
         │                    │           Normal         Fast
         │                    │           path           path
         │                    │              │             │
         └────────────────────┴──────────────┴─────────────┘
                                             │
                                       Linux Kernel
                                             │
                                        Virtio / VM
                                             │
                                            QEMU
```

---

# 11. Initial Adaptive Policies

Do not implement ten policies.

Start with three.

## Policy A — Normal

Use ordinary Linux interfaces.

```text
Application
 ↓
syscall
 ↓
Linux
```

This is the baseline.

---

## Policy B — Batched

Group operations to reduce overhead.

```text
Application
 ↓
batch
 ↓
kernel
 ↓
device
```

Useful for:

```text
many small operations
```

---

## Policy C — Specialized/Fast

Use an optimized path where supported.

Potential mechanisms:

```text
io_uring
shared queues
virtio queues
userspace buffers
kernel-assisted direct I/O
```

Do not jump directly to unsafe raw device access.

---

# 12. Adaptive Policy Engine

Initial implementation:

```text
if syscall_rate < T1
    normal

else if small_io_rate > T2
    specialized

else if sequential_io > T3
    batched

else
    normal
```

Every decision must be logged.

Example:

```text
[ADAPT]
PID=1823
IOPS=42180
avg_io=4096
CPU=63%
queue_depth=18
policy: NORMAL -> SPECIALIZED
reason: high_small_io
```

This is critical for research reproducibility.

---

# 13. Later: ML Policy

Do NOT start with ML.

First establish:

```text
rule-based adaptive policy
```

Then potentially collect:

```text
features
→ policy
→ performance outcome
```

Dataset:

```text
timestamp
application
CPU%
IOPS
IO size
read/write ratio
queue depth
context switches
memory pressure
network rate
selected policy
latency
throughput
CPU cost
```

Then investigate:

```text
Decision tree
Random forest
Gradient boosting
Contextual bandit
```

The ML model must be evaluated against the rule-based policy.

ML is therefore a **second-stage research extension**, not a prerequisite.

---

# 14. QEMU/KVM Requirement

This is mandatory.

## FuhrerOS must run in a VM before any bare-metal experiment.

Architecture:

```text
             YOUR COMPUTER
                  │
        ┌─────────┴─────────┐
        │                   │
   Existing OS         QEMU/KVM
                            │
                     ┌──────▼──────┐
                     │  FuhrerOS   │
                     │             │
                     │ 2–4 vCPU    │
                     │ 2–4 GB RAM  │
                     │ 20–50GB disk│
                     │ virtio NIC  │
                     └─────────────┘
```

QEMU provides x86 system emulation and can use KVM as a hardware-assisted accelerator on Linux hosts. QEMU also provides Windows Hypervisor Platform acceleration on Windows.

KVM exposes a stable API for creating VMs and virtual CPUs and devices.

---

# 15. VM Configurations

## Development VM

```text
CPU:       2 vCPU
RAM:       2 GB
Disk:      20 GB
Network:   virtio
```

## Benchmark VM

```text
CPU:       4 vCPU
RAM:       4 GB
Disk:      40 GB
Network:   virtio
```

## Stress VM

```text
CPU:       4 vCPU
RAM:       6 GB
Disk:      50 GB
```

Never allow the VM to consume all 8 GB of host memory.

---

# 16. QEMU Commands

The implementation must eventually provide:

```bash
./scripts/build.sh
./scripts/run.sh
./scripts/run-debug.sh
./scripts/reset-vm.sh
```

Conceptually:

```bash
qemu-system-x86_64 \
    -enable-kvm \
    -m 2G \
    -smp 2 \
    -drive file=fuhreros.qcow2,format=qcow2 \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0
```

On a Windows host, use the appropriate QEMU acceleration available through Windows Hypervisor Platform rather than assuming KVM. QEMU documents WHPX support for x86-64 guests.

---

# 17. VM Acceptance Criteria

Milestone 1 is NOT complete unless:

```text
[✓] VM boots
[✓] Kernel/userspace starts
[✓] Terminal works
[✓] Network interface appears
[✓] Virtual disk works
[✓] SSH/console access works
[✓] VM can be destroyed/recreated
[✓] No host disk is modified
```

Every later milestone must continue to boot in QEMU.

---

# 18. Browser Requirement

The browser is a **usability test**, not the research contribution.

Do NOT write a browser engine.

The goal is eventually:

```text
FuhrerOS VM
    ↓
Linux-compatible userspace
    ↓
browser
    ↓
Internet
```

The exact browser can be decided after compatibility testing.

The first browser milestone can be:

```bash
curl https://example.com
```

Then:

```text
text browser
```

Then eventually a graphical browser.

If full Firefox/Chromium support proves impractical inside the research prototype, that is not a failure of the core research.

The browser should be treated as an application-compatibility stress test.

---

# 19. Baselines

Every benchmark should compare:

### B0 — Native Linux

```text
Ubuntu/Fedora/etc.
```

### B1 — FuhrerOS with adaptive layer disabled

```text
Linux + Fuhrer infrastructure
fixed policy
```

### B2 — FuhrerOS adaptive

```text
Linux + profiler + policy engine
```

Potential future:

### B3 — Specialized

```text
FuhrerOS
manually selected optimal policy
```

B3 is important because it establishes the theoretical upper bound of the adaptive policy.

---

# 20. Benchmark Suite

## CPU

```text
SPEC-like microbenchmarks
sysbench CPU
compilation
```

Measure:

```text
execution time
CPU utilization
context switches
```

---

## Memory

```text
memory bandwidth
malloc/free
page faults
working-set changes
```

Measure:

```text
latency
bandwidth
RSS
page faults
```

---

## Storage

Use:

```text
fio
SQLite
file-copy benchmark
compiler workload
```

Measure:

```text
IOPS
throughput
P50 latency
P95 latency
P99 latency
CPU utilization
```

---

## Network

Use:

```text
iperf3
curl
HTTP benchmark
```

Measure:

```text
throughput
RTT
packets/sec
CPU utilization
```

---

## Desktop

Use realistic workloads:

```text
browser startup
file manager
large file copy
source compilation
Python package installation
archive extraction
media playback
```

Measure:

```text
startup latency
completion time
CPU
memory
I/O
```

---

# 21. Workload Transition Experiments

This is particularly important.

Do not only run:

```text
Workload A for 10 minutes
```

Run:

```text
0–60 sec: CPU-bound
60–120 sec: random I/O
120–180 sec: sequential I/O
180–240 sec: network-heavy
```

Then see whether FuhrerOS changes policy.

Example:

```text
Time
 │
 │ CPU       I/O          Network
 │
 ▼
 ─────────────────────────────────────
       │          │           │
       ▼          ▼           ▼
   CPU policy   I/O policy   Net policy
```

This is one of the most important experiments for the adaptive hypothesis.

---

# 22. Ablation Experiments

## A1 — No profiler

```text
Linux
vs
FuhrerOS without monitoring
```

Measures profiler overhead.

---

## A2 — Profiler but no adaptation

Collect metrics but never change policy.

Shows whether monitoring itself costs anything.

---

## A3 — Static policy

Choose one policy at boot.

Compare with adaptive.

---

## A4 — Slow adaptation

Change policy every 10 seconds.

---

## A5 — Fast adaptation

Change policy every 100 ms / 1 second.

Determine an appropriate interval experimentally.

---

## A6 — Individual features

Train/use policy with:

```text
CPU only
```

then:

```text
CPU + I/O
```

then:

```text
CPU + I/O + memory
```

Determine which features actually matter.

---

# 23. Expected Graphs

### Graph 1 — Throughput

```text
Workload
│
│          Adaptive
│        █████████
│
│ Linux  ███████
│ Static ████████
└────────────────────
```

---

### Graph 2 — P99 latency

Lower is better.

```text
Latency
│
│ Linux       ██████████
│ Static      ████████
│ Adaptive    █████
└────────────────────────
```

---

### Graph 3 — Adaptation

```text
Policy
│
│ CPU
│ ─────────────
│              \
│               \ I/O
│                ─────────────
│                            \
│                             \ Network
└────────────────────────────────────
             time
```

---

### Graph 4 — Monitoring overhead

```text
Performance
│
│
│─────────────── baseline
│───────────── adaptive
│
└────────────────────────
```

The difference should ideally be small.

---

# 24. What Counts as Success?

The project is NOT successful merely because:

```text
"FuhrerOS boots."
```

Success requires evidence.

### Minimum success

```text
✓ Runs reliably in QEMU/KVM
✓ Provides Linux userspace
✓ Collects workload statistics
✓ Changes policies dynamically
✓ Produces reproducible measurements
```

### Research success

At least one of the following should be demonstrated:

```text
adaptive policy improves throughput
```

or:

```text
adaptive policy reduces latency
```

or:

```text
adaptive policy reduces CPU cost
```

or:

```text
adaptive policy improves performance-per-resource
```

for a meaningful subset of workloads.

### Strong result

Demonstrate:

```text
Adaptive ≈ best static policy
```

across changing workloads, while:

```text
Adaptive > average fixed policy
```

and:

```text
monitoring overhead << performance benefit
```

---

# 25. What Counts as a Negative Result?

A negative result is acceptable.

For example:

```text
Adaptive policy:
+8% throughput

but

+15% CPU overhead
```

means the policy is not beneficial overall.

That is still a useful research result.

Similarly:

```text
Adaptive ≈ Static
```

may demonstrate that the selected workloads are not sufficiently heterogeneous.

Do not manipulate benchmarks to produce positive results.

---

# 26. Threats to Validity

## T1 — VM overhead

QEMU/KVM measurements may differ from bare metal.

Mitigation:

```text
VM experiments first
+
selected bare-metal validation later
```

---

## T2 — Small sample size

A few benchmarks may not represent personal computing.

Mitigation:

Use:

```text
CPU
memory
storage
network
interactive
mixed
```

workloads.

---

## T3 — Adaptive policy overfitting

A policy could perform well only on workloads used to develop it.

Mitigation:

Separate:

```text
development workloads
```

from:

```text
evaluation workloads
```

---

## T4 — Host interference

The host OS may affect VM performance.

Mitigation:

* isolate CPU cores where possible;
* repeat experiments;
* report variance;
* keep background host activity low.

---

## T5 — QEMU configuration

Different virtual hardware can produce different results.

Therefore record:

```text
QEMU version
kernel version
guest RAM
vCPU count
virtual disk type
network device
host CPU
```

for every experiment.

---

## T6 — Linux version dependence

Kernel behavior can change.

Record:

```text
Linux commit/version
compiler
configuration
```

in every benchmark report.

---

# 27. Reproducibility

Every experiment should generate:

```text
experiment/
├── config.json
├── workload.json
├── system-info.txt
├── metrics.csv
├── stdout.log
├── stderr.log
└── result.json
```

Example:

```json
{
  "kernel": "6.x.y",
  "qemu": "11.x",
  "vcpus": 4,
  "memory_mb": 4096,
  "disk_gb": 40,
  "policy": "adaptive",
  "interval_ms": 1000
}
```

Never report benchmark results without configuration metadata.

---

# 28. Project Repository

Recommended:

```text
fuhreros/
│
├── README.md
├── LICENSE
│
├── docs/
│   ├── research-design.md
│   ├── architecture.md
│   ├── literature.md
│   ├── decisions.md
│   ├── experiments.md
│   └── vm.md
│
├── kernel/
│
├── adaptive/
│   ├── profiler/
│   ├── policy/
│   ├── features/
│   └── controller/
│
├── runtime/
│
├── benchmarks/
│   ├── cpu/
│   ├── memory/
│   ├── storage/
│   ├── network/
│   └── desktop/
│
├── experiments/
│
├── scripts/
│
├── vm/
│   ├── qemu/
│   └── configs/
│
└── research-log/
    ├── decisions/
    ├── experiments/
    └── failures/
```

---

# 29. Engineering Reasoning Log

This is a mandatory part of the project.

Claude must maintain:

```text
docs/decisions.md
```

and:

```text
research-log/
```

The purpose is to preserve **engineering reasoning and decision rationale**.

Do not merely write:

```text
"Implemented profiler."
```

Write:

```text
Decision D-014

Question:
Should workload sampling happen every 10 ms, 100 ms,
1 second, or 10 seconds?

Evidence:
100 ms captures workload transitions reasonably well,
while 10 ms adds excessive measurement overhead.

Decision:
Start with 1 second.

Reason:
The initial research target is desktop-scale workload
adaptation rather than microsecond-scale packet scheduling.

Alternatives rejected:
10 ms — excessive overhead.
10 s — too slow for workload transitions.

Validation:
Compare 100 ms / 1 s / 10 s experimentally.
```

This is the kind of reasoning we want preserved.

### Important rule

The log should contain:

```text
decision
alternatives
evidence
reason
tradeoffs
experiment
result
```

It should NOT contain fabricated reasoning.

If Claude does not know why a previous decision was made, it must say:

```text
Unknown — needs verification.
```

rather than inventing a rationale.

---

# 30. Decision Record Format

Every major architectural decision gets:

```text
D-XXX

Title:

Date:

Problem:

Options considered:

Option A:
Option B:
Option C:

Decision:

Why:

Evidence:

Tradeoffs:

Expected impact:

How to validate:

Status:
```

---

# 31. Experiment Record Format

```text
E-XXX

Hypothesis:

Configuration:

Host:

Guest:

Kernel:

QEMU:

CPU:

RAM:

Workload:

Policy:

Metrics:

Expected result:

Actual result:

Variance:

Interpretation:

Threats to validity:

Next experiment:
```

---

# 32. Failure Log

Every serious failure should be documented.

```text
F-XXX

Symptom:

Command:

Expected:

Observed:

Root cause:

Fix:

Regression test:

Lesson:
```

This will prevent Claude from repeatedly making the same mistake.

---

# 33. BTP Timeline

Assuming approximately one semester.

## Weeks 1–2

Literature review:

```text
Exokernel
Dune
Arrakis
IX
EbbRT
Unikraft
UKL
HongMeng
uCache
UnICom
Xkernel
```

Produce:

```text
literature matrix
research gap
hypotheses
```

---

## Weeks 3–4

QEMU/KVM prototype:

```text
Linux guest
Fuhrer userspace
metrics collector
benchmark harness
```

Deliverable:

```text
bootable/reproducible VM
```

---

## Weeks 5–6

Profiler:

```text
CPU
memory
I/O
syscalls
network
```

Deliverable:

```text
workload trace
```

---

## Weeks 7–8

Static policies:

```text
normal
batched
specialized
```

Deliverable:

```text
policy framework
```

---

## Weeks 9–10

Adaptive controller:

```text
profiler
 ↓
classifier/rules
 ↓
policy switch
```

---

## Weeks 11–12

Benchmarking:

```text
CPU
storage
network
desktop
mixed workload
```

---

## Weeks 13–14

Ablations:

```text
sampling interval
features
switching cost
policy choices
```

---

## Weeks 15–16

Analysis:

```text
graphs
statistics
discussion
limitations
```

---

## Weeks 17–18

Final report:

```text
architecture
implementation
evaluation
results
related work
future work
```

---

# 34. Future Work

If the first prototype works, possible extensions include:

### ML policy selection

```text
features
 ↓
model
 ↓
policy
```

### Adaptive caching

Inspired by uCache.

### Adaptive I/O completion

Inspired by UnICom.

### Runtime kernel tuning

Inspired by Xkernel.

### User-level fast paths

Inspired by Arrakis/IX.

### Application-specific execution

Inspired by EbbRT/UKL.

### Stronger isolation

Inspired by Dune/HongMeng.

---

# 35. Final Architecture

The long-term FuhrerOS architecture should become:

```text
                         FUHREROS
                            │
                    ┌───────▼────────┐
                    │ Applications   │
                    └───────┬────────┘
                            │
                    Linux/POSIX API
                            │
                    ┌───────▼────────┐
                    │ Fuhrer Runtime │
                    └───────┬────────┘
                            │
                ┌───────────▼───────────┐
                │   Workload Profiler   │
                └───────────┬───────────┘
                            │
                ┌───────────▼───────────┐
                │    Policy Engine      │
                └───────────┬───────────┘
                            │
       ┌────────────────────┼────────────────────┐
       │                    │                    │
       ▼                    ▼                    ▼
   CPU Policy          Cache Policy          I/O Policy
       │                    │                    │
       │                    │            ┌───────┴───────┐
       │                    │            │               │
       │                    │          Normal          Fast
       │                    │            │               │
       └────────────────────┴────────────┴───────────────┘
                                        │
                                 Linux Kernel
                                        │
                              ┌─────────┴─────────┐
                              │                   │
                           Virtio              KVM
                              │                   │
                              └─────────┬─────────┘
                                        │
                                      QEMU
                                        │
                                     Hardware
```

---

# 36. Claude Master Implementation Prompt

The following prompt should be given to Claude as the **master project instruction**.

---

## START CLAUDE MASTER PROMPT

You are the primary systems engineer implementing **FuhrerOS**, a research prototype for adaptive operating-system specialization.

The goal is NOT to build a hobby OS merely for the sake of having a new kernel.

The research objective is:

> Investigate whether runtime workload characterization can be used to dynamically select OS execution, I/O, caching and scheduling policies for heterogeneous personal-computing workloads while retaining compatibility with normal Linux applications.

The system MUST be reproducible in a virtual machine.

The first supported execution environment is:

```text
QEMU/KVM
x86-64
2–4 vCPUs
2–4 GB RAM
20–50 GB virtual disk
virtio networking
```

Bare-metal execution is a later milestone.

---

## 1. NON-NEGOTIABLE PRINCIPLES

### Principle 1 — VM first

Never require the user's physical machine for development.

Every milestone must be testable in QEMU.

Never modify:

```text
host partitions
host bootloader
host filesystem
```

without explicit user approval.

The default workflow must be:

```text
build
→ create/reuse VM image
→ boot QEMU
→ run tests
→ collect logs
→ destroy/reset VM if necessary
```

---

### Principle 2 — Do not build a new kernel unnecessarily

Use Linux as the initial foundation.

The first prototype should be:

```text
Linux
+
FuhrerOS adaptive layer
+
runtime
+
profiler
+
policy engine
```

Only modify kernel code when the research experiment actually requires it.

Prefer:

```text
userspace
eBPF
io_uring
existing Linux interfaces
kernel modules
```

before maintaining a large kernel fork.

---

### Principle 3 — Research before optimization

Never optimize merely because something "looks slow."

Every optimization must have:

```text
hypothesis
baseline
measurement
change
measurement
comparison
```

---

### Principle 4 — Preserve reproducibility

Every benchmark must record:

```text
host
guest
kernel version
QEMU version
CPU
RAM
vCPU count
disk configuration
network configuration
policy
sampling interval
workload
commit hash
```

---

### Principle 5 — Never invent results

If a benchmark was not executed, write:

```text
NOT RUN
```

Never fabricate:

```text
throughput
latency
speedup
memory usage
CPU usage
```

---

# 2. RESEARCH MEMORY

Maintain these files continuously:

```text
docs/research-design.md
docs/architecture.md
docs/literature.md
docs/decisions.md
docs/experiments.md
research-log/
```

At the beginning of every development session:

1. inspect the repository;
2. inspect `docs/decisions.md`;
3. inspect recent research logs;
4. inspect current milestone status;
5. inspect known failures.

Do not assume previous implementation decisions from memory.

---

# 3. ENGINEERING REASONING LOG

For every significant decision create a decision record.

Use:

```text
D-XXX

Problem:

Context:

Options:

Option A:
Option B:
Option C:

Evidence:

Decision:

Reason:

Tradeoffs:

Expected effect:

Validation experiment:

Status:
```

Record engineering rationale, alternatives, evidence and tradeoffs.

Do not fabricate reasoning that was not established.

If uncertain:

```text
UNKNOWN — REQUIRES VERIFICATION
```

---

# 4. FAILURE LOG

Whenever a significant implementation failure occurs:

```text
F-XXX

Symptom:

Reproduction:

Expected:

Observed:

Root cause:

Fix:

Regression test:

Lesson:
```

Do not repeatedly retry the same failed approach without recording what changed.

---

# 5. EXPERIMENT LOG

Every experiment:

```text
E-XXX

Question:

Hypothesis:

Host:

Guest:

Kernel:

QEMU:

CPU:

RAM:

vCPUs:

Disk:

Network:

Workload:

Policy:

Configuration:

Metrics:

Result:

Variance:

Interpretation:

Threats to validity:

Next experiment:
```

---

# 6. DEVELOPMENT MILESTONES

Implement in this order.

Do NOT jump directly to advanced features.

---

## M0 — Reproducible VM

Create:

```text
scripts/build.sh
scripts/run.sh
scripts/debug.sh
scripts/reset-vm.sh
```

The system must boot Linux in QEMU.

Acceptance:

```text
./scripts/build.sh
./scripts/run.sh
```

must work.

---

## M1 — FuhrerOS Runtime

Create:

```text
fuhrer-runtime
```

running inside the Linux guest.

It should expose:

```text
fuhrer status
fuhrer profile
fuhrer policy
fuhrer benchmark
```

Example:

```text
fuhrer> status

CPU: 17%
Memory: 1.2 GB
IOPS: 120
Network: 0.4 MB/s
Policy: NORMAL
```

---

## M2 — Profiler

Collect:

```text
CPU utilization
memory utilization
context switches
system calls
I/O operations
I/O sizes
read/write ratio
queue depth
network throughput
process information
```

Do not collect unnecessary metrics.

Measure profiler overhead.

---

## M3 — Workload Classification

Start with rule-based classes:

```text
CPU_BOUND
IO_SEQUENTIAL
IO_RANDOM
NETWORK_HEAVY
INTERACTIVE
MIXED
```

Example:

```text
CPU > 80%
IOPS < threshold
→ CPU_BOUND
```

Do not use ML yet.

---

## M4 — Policy Framework

Implement:

```text
NormalPolicy
BatchedPolicy
SpecializedPolicy
```

Use a clean interface:

```text
Policy
 ├── initialize()
 ├── activate()
 ├── deactivate()
 ├── collect_metrics()
 └── name()
```

---

## M5 — Static Policy Benchmark

Before adaptation:

```text
Linux baseline
Fuhrer normal
Fuhrer batched
Fuhrer specialized
```

Determine which policy performs best for each workload.

This establishes the ground truth needed for adaptive policy research.

---

## M6 — Adaptive Controller

Architecture:

```text
Profiler
   ↓
Feature extractor
   ↓
Workload classifier
   ↓
Policy engine
   ↓
Policy selection
```

Policy changes must be logged:

```text
[ADAPT]
PID=1234
old=normal
new=specialized
reason=high_small_io
```

---

## M7 — Policy Switching Cost

Measure:

```text
switch latency
CPU overhead
memory overhead
transient performance degradation
```

Determine whether adaptation is worthwhile.

---

## M8 — Storage Fast Path

Investigate:

```text
io_uring
batched I/O
O_DIRECT where appropriate
virtio queues
shared buffers
```

Do not implement raw hardware access until necessary.

The first fast path should be safe and measurable.

---

## M9 — Adaptive Storage

Example:

```text
random small I/O
→ specialized policy

large sequential I/O
→ batching/read-ahead

low I/O load
→ normal policy
```

Compare:

```text
fixed policy
vs
adaptive
```

---

## M10 — Network Fast Path

Investigate:

```text
io_uring networking
SO_BUSY_POLL where appropriate
virtio
batching
zero-copy mechanisms
```

Do not implement a new TCP stack.

---

## M11 — Adaptive Network Policy

Potential policies:

```text
latency-oriented
throughput-oriented
CPU-efficient
```

The policy should be selected according to observed workload.

---

## M12 — Adaptive Cache Experiment

Investigate:

```text
read-ahead
cache pressure
application access patterns
mmap
page cache behavior
```

Do not replace Linux's entire page cache.

Implement a narrowly scoped experiment.

---

## M13 — Optional Kernel Integration

Only after userspace experiments work.

Possible mechanisms:

```text
eBPF
kernel module
small kernel patch
```

Keep kernel modifications as small as possible.

---

## M14 — Browser/Application Compatibility

Demonstrate:

```text
curl
Python
GCC
Git
text editor
browser
```

The browser is an application-compatibility test, not the research contribution.

---

## M15 — Full Benchmark

Run:

```text
Linux
Fuhrer static
Fuhrer adaptive
```

across:

```text
CPU
memory
storage
network
desktop
mixed workloads
```

---

# 7. QEMU REQUIREMENT

Every milestone must provide a QEMU test.

Default VM:

```text
Architecture: x86-64
vCPUs: 2
RAM: 2 GB
Disk: 20 GB
NIC: virtio
```

Benchmark VM:

```text
vCPUs: 4
RAM: 4 GB
Disk: 40 GB
NIC: virtio
```

Never assume KVM exists.

Detect:

```text
KVM available
```

and otherwise fall back to:

```text
TCG
```

when feasible.

Print:

```text
Acceleration: KVM
```

or:

```text
Acceleration: TCG
```

at startup.

---

# 8. BUILD COMMANDS

The project must eventually support:

```bash
./build.sh
./run.sh
./run-debug.sh
./reset.sh
./benchmark.sh
./test.sh
```

Also support:

```bash
make
make run
make test
make benchmark
make clean
```

if Make is used.

---

# 9. RESEARCH DASHBOARD

Create:

```text
fuhrer status
```

Output:

```text
FUHREROS STATUS
────────────────────────

Runtime:        ACTIVE
Profiler:       ACTIVE
Policy engine:  ACTIVE

CPU:            23%
Memory:         41%

I/O:
  Read:         12 MB/s
  Write:        3 MB/s
  IOPS:         421

Network:
  RX:            2.1 MB/s
  TX:            0.7 MB/s

Workload:
  IO_RANDOM

Policy:
  SPECIALIZED

Policy since:
  4.21 seconds

Switches:
  3
```

This will also make demonstrations much easier.

---

# 10. RESEARCH SAFETY

Never make adaptive decisions that can:

```text
corrupt data
disable filesystem integrity
bypass security
modify host disks
```

Performance experimentation must never compromise correctness.

Correctness has priority:

```text
Correctness
>
Safety
>
Reproducibility
>
Performance
```

---

# 11. BENCHMARKING RULES

Every benchmark:

1. warm up;
2. execute multiple repetitions;
3. report median;
4. report P95/P99 where appropriate;
5. report variance;
6. save raw measurements;
7. save configuration;
8. compare identical VM configurations.

Do not compare:

```text
Linux: 4 CPUs / 4GB
Fuhrer: 2 CPUs / 2GB
```

and call the difference an OS improvement.

---

# 12. BASELINES

Always maintain:

```text
B0 = native Linux
B1 = Fuhrer static
B2 = Fuhrer adaptive
```

Optionally:

```text
B3 = manually optimized/static-best
```

B3 tells us whether adaptive policy can approach the best known static configuration.

---

# 13. REQUIRED ABLATIONS

Run:

```text
A1: no profiler
A2: profiler, no adaptation
A3: static policy
A4: adaptive policy
A5: different sampling intervals
A6: different feature sets
A7: different policy-switch thresholds
```

Do not omit these simply because the full adaptive system looks better.

---

# 14. LITERATURE TRACKING

Maintain:

```text
docs/literature.md
```

with at least:

```text
Exokernel
Dune
Arrakis
IX
EbbRT
Barrelfish
OSv
Unikraft
Rump Kernels
KylinX
UKL
HongMeng
uCache
UnICom
Xkernel
```

For each:

```text
Problem
Idea
Architecture
Implementation
Evaluation
Limitation
Relevance to FuhrerOS
Difference from FuhrerOS
```

Never claim novelty without checking related work.

---

# 15. NOVELTY POLICY

Do NOT write:

> "FuhrerOS is the first adaptive OS."

unless a comprehensive literature search actually establishes this.

Use cautious language:

> "We investigate..."

> "We propose..."

> "Our prototype evaluates..."

> "Our literature review suggests..."

Only make a stronger novelty claim after verification.

---

# 16. DOCUMENTATION REQUIREMENT

After every milestone update:

```text
docs/architecture.md
docs/decisions.md
docs/experiments.md
README.md
```

Do not leave documentation until the end.

---

# 17. CLAUDE SESSION PROTOCOL

At the beginning of every session:

```text
1. Inspect git status.
2. Inspect current milestone.
3. Read docs/decisions.md.
4. Read recent research logs.
5. Inspect failed experiments.
6. Run existing tests.
7. Identify exactly one next objective.
```

Then state:

```text
Current milestone:
Objective:
Relevant previous decisions:
Files likely affected:
Validation plan:
```

After implementation:

```text
Files changed:
Tests:
Build:
QEMU:
Benchmark:
Research implication:
Documentation updated:
Next objective:
```

---

# 18. DO NOT MAKE LARGE UNVERIFIED CHANGES

Never modify 20 unrelated files simply to "make things work."

Prefer:

```text
small patch
→ build
→ test
→ QEMU
→ commit
```

Use Git commits after stable milestones.

Commit format:

```text
m0: add reproducible qemu environment
m1: add fuhrer runtime
m2: add workload profiler
m3: add workload classifier
...
```

---

# 19. WHEN SOMETHING BREAKS

Do NOT immediately rewrite the architecture.

First:

```text
reproduce
↓
collect logs
↓
identify layer
↓
form hypothesis
↓
test hypothesis
↓
patch
↓
regression test
```

Record the process in `research-log/`.

---

# 20. FIRST TASK

Do ONLY this.

### Task

Create the initial FuhrerOS repository and a reproducible Linux/QEMU/KVM development VM.

Deliver:

```text
fuhreros/
├── README.md
├── docs/
│   ├── architecture.md
│   ├── decisions.md
│   └── vm.md
├── scripts/
│   ├── build.sh
│   ├── run.sh
│   ├── debug.sh
│   └── reset.sh
└── vm/
```

Requirements:

```text
x86-64
QEMU
2 vCPU
2GB RAM
20GB disk
virtio network
```

The VM must boot successfully.

Do NOT implement the profiler yet.

Do NOT implement adaptive policies yet.

Do NOT modify the host OS.

After completing this task, stop and report:

```text
BUILD:
PASS/FAIL

QEMU:
PASS/FAIL

KVM:
AVAILABLE/UNAVAILABLE

VM:
BOOTED/FAILED

FILES:
...

TESTS:
...

DECISION LOG:
...

NEXT MILESTONE:
M1
```

Do not proceed to M1 until the user explicitly asks you to continue.

## END CLAUDE MASTER PROMPT

---

# 37. Final Research Position

After the literature review, I would frame FuhrerOS as:

> **not a new OS for the sake of being an OS, but a reproducible experimental platform for studying adaptive OS specialization.**

The lineage is:

```text
Exokernel
  ↓
application-level resource policy

Dune
  ↓
safe userspace hardware control

Arrakis / IX
  ↓
direct datapaths

EbbRT / Unikraft
  ↓
specialized library OSes

UKL
  ↓
specialization without abandoning Linux

uCache / UnICom
  ↓
specialized/adaptive I/O

Xkernel
  ↓
runtime kernel tunability

             ↓

        FUHREROS
             ↓
runtime workload-aware
policy selection for
personal computing
```

The **most important design choice** is that FuhrerOS starts as a **Linux-based QEMU/KVM research prototype**. UKL provides a particularly strong precedent: its implementation explicitly supports building and booting the specialized system under QEMU and can also run on bare metal.

And QEMU/KVM is a solid foundation for the experimental environment: QEMU provides x86 system emulation and KVM acceleration on Linux, while Windows hosts can use WHPX acceleration.

The recent papers are especially valuable because **UnICom and Xkernel make the adaptive-policy direction much more concrete**: UnICom shows that I/O completion strategy can benefit from adapting to CPU/I/O conditions, while Xkernel explicitly exposes runtime tunability and programmable policies in Linux.

So I would **freeze this as the research direction for now**:

> **FuhrerOS = Linux-compatible, QEMU-first, workload-aware adaptive OS research platform.**

And the very first deliverable should be a **reproducible QEMU VM**, not a kernel. That gives us a safe foundation where every subsequent research idea can be tested experimentally.


### 22. DEVELOPMENT ENVIRONMENT — WINDOWS + WSL 2

The host development machine is Windows.

Use **WSL 2 as the primary Linux development environment**.

WSL is the development environment, NOT the final FuhrerOS execution environment.

FuhrerOS must continue to boot and run independently inside QEMU.

The development architecture is:

```text
Windows
  │
  ├── VS Code / Terminal / Browser / File Explorer
  │
  └── WSL 2
        │
        ├── Git
        ├── GCC / Clang
        ├── Make / CMake
        ├── QEMU
        ├── debugging tools
        ├── benchmarking tools
        └── FuhrerOS source tree
                  │
                  ↓
             FuhrerOS VM
                  │
                QEMU
                  │
          WHPX acceleration
                  │
                CPU
```

### WSL distribution

Prefer a current Ubuntu LTS distribution under WSL 2 unless an explicit technical reason requires another distribution.

First inspect the environment rather than assuming it.

Run and record:

```bash
wsl.exe --version
wsl.exe --status
uname -a
cat /etc/os-release
nproc
free -h
df -h
```

Also inspect:

```bash
qemu-system-x86_64 --version
git --version
gcc --version
clang --version
make --version
```

If a required tool is missing, install it through the normal package manager.

Do NOT blindly reinstall the entire development environment.

### Project location

Store the FuhrerOS repository inside the WSL Linux filesystem:

```text
~/projects/fuhreros
```

Do NOT put the primary repository under:

```text
/mnt/c/Users/...
```

unless there is a specific reason.

Linux build systems, Git repositories, compiler workloads, and large numbers of small files should remain inside the WSL filesystem for performance.

Windows can still access the project through:

```text
\\wsl$\...
```

or:

```bash
explorer.exe .
```

when necessary.

Do not maintain two independent copies of the repository.

The WSL copy is the canonical development repository.

### Windows interoperability

WSL may use Windows programs when useful.

Examples include:

```bash
explorer.exe .
powershell.exe
cmd.exe
```

Use Windows tools when they provide functionality unavailable or significantly better on Linux.

However, prefer Linux-native tools for:

* compilation
* Git
* QEMU
* kernel development
* benchmarking
* scripting
* filesystem experiments
* profiling

Do not unnecessarily mix Windows and Linux versions of the same tool.

### HOST ACCESS POLICY

Claude should have broad operational access to the FuhrerOS development environment.

Claude is permitted to:

* create/edit/delete files inside the FuhrerOS repository;
* create build directories;
* compile software;
* install required development packages inside WSL;
* configure QEMU;
* create/delete FuhrerOS VM disk images;
* start/stop/restart FuhrerOS VMs;
* inspect CPU/RAM/disk/network configuration;
* run benchmarks;
* run Git commands;
* create scripts;
* create documentation;
* inspect WSL configuration;
* inspect Windows/WSL interoperability where required;
* invoke Windows tools through WSL when necessary;
* use the project filesystem freely.

Claude should not require confirmation for routine operations inside:

```text
~/projects/fuhreros/
```

or for disposable FuhrerOS VM artifacts.

### HOST SAFETY BOUNDARY

"Full access to the computer" does NOT mean unrestricted destructive access to the Windows host.

Before executing potentially destructive host operations, stop and explicitly report the command and its effect.

This includes:

* deleting Windows system directories;
* formatting physical disks;
* changing partition tables;
* modifying the Windows bootloader;
* modifying EFI partitions;
* disabling Windows security mechanisms;
* changing firewall rules globally;
* deleting user data outside the FuhrerOS workspace;
* recursively deleting broad Windows paths;
* modifying BIOS/UEFI settings;
* installing unknown kernel drivers;
* changing critical Windows networking configuration.

Never execute commands such as:

```text
rm -rf /
rm -rf /mnt/c/...
format ...
diskpart ...
bcdedit ...
```

or equivalent destructive commands unless the user has explicitly requested that exact host-level operation.

Do not disable Windows Defender or other security mechanisms merely for performance.

### WSL CONFIGURATION

Inspect the existing WSL configuration before modifying it.

Check:

```text
%USERPROFILE%\.wslconfig
```

and:

```text
/etc/wsl.conf
```

If systemd is useful, it may be enabled.

Do not arbitrarily allocate all host RAM or CPU cores to WSL.

The host has limited resources and FuhrerOS itself needs RAM for QEMU.

Maintain a resource budget.

Initial target:

```text
Windows host
        ↓
WSL 2
        ↓
QEMU FuhrerOS

WSL/QEMU combined usage must remain practical on an 8 GB host.
```

Do not configure a 6–8 GB FuhrerOS VM on an 8 GB host.

### QEMU REQUIREMENT

QEMU is mandatory.

The first FuhrerOS VM should use:

```text
Architecture: x86-64
vCPU: 2
RAM: 2 GB
Disk: 20 GB
Network: virtio
```

Benchmark configuration:

```text
vCPU: 4
RAM: 4 GB
Disk: 40 GB
Network: virtio
```

Attempt hardware acceleration first.

On Windows:

```text
WHPX
```

On Linux:

```text
KVM
```

If hardware acceleration is unavailable, fall back to QEMU TCG where practical.

The run script must clearly report:

```text
Acceleration: WHPX
```

or:

```text
Acceleration: KVM
```

or:

```text
Acceleration: TCG
```

Never silently fall back.

### QEMU COMMAND DISCOVERY

Do not blindly assume that WHPX is installed.

Detect capabilities first.

Check:

```bash
qemu-system-x86_64 -accel help
```

Then select the best supported accelerator.

Create:

```text
scripts/detect-acceleration.sh
```

which reports available acceleration mechanisms.

### VM isolation

FuhrerOS development should remain VM-first.

Do NOT require:

* modifying the Windows bootloader;
* repartitioning disks;
* installing FuhrerOS onto the physical disk;
* replacing Windows;
* modifying EFI;
* dual-boot configuration.

All early development must remain inside:

```text
WSL → QEMU → FuhrerOS
```

This is a hard requirement.

### GUI DEVELOPMENT

WSL may be used to develop and test GUI components.

Use WSL GUI capabilities where useful during development.

However, distinguish:

1. GUI application running inside WSL;
2. FuhrerOS GUI running inside QEMU.

The second is the actual product.

Do not mistake a WSL GUI application for the FuhrerOS desktop.

### DISPLAY ARCHITECTURE

The intended final architecture is:

```text
FuhrerOS
    │
    ├── compositor
    ├── window manager
    ├── desktop shell
    ├── input subsystem
    └── applications
             │
             ↓
          QEMU display
             │
             ↓
          Windows
```

WSL GUI support is a development convenience, not the final display architecture.

### TOUCHPAD DEVELOPMENT

The physical Windows Precision Touchpad is part of the target hardware.

Investigate how touchpad input is exposed through:

```text
Windows
   ↓
WSL / virtualization boundary
   ↓
QEMU
   ↓
FuhrerOS input subsystem
```

Do not assume that raw Precision Touchpad events will automatically be visible inside the guest.

For VM development, provide an equivalent virtual input device where necessary.

Later investigate direct/near-direct input paths if required.

The final FuhrerOS UX must support familiar Windows-style touchpad gestures.

### REPRODUCIBILITY

Create:

```text
scripts/bootstrap-wsl.sh
scripts/check-host.sh
scripts/check-qemu.sh
scripts/detect-acceleration.sh
scripts/build.sh
scripts/run.sh
scripts/run-debug.sh
scripts/reset-vm.sh
```

The intended workflow should eventually be:

```bash
cd ~/projects/fuhreros

./scripts/check-host.sh
./scripts/check-qemu.sh
./scripts/build.sh
./scripts/run.sh
```

A new developer should be able to reproduce the environment from documentation.

### HOST INFORMATION

Create:

```text
docs/development-environment.md
```

Record:

* Windows version
* WSL version
* WSL distribution
* Linux kernel version
* CPU model
* physical CPU cores/threads
* RAM
* GPU
* storage
* QEMU version
* accelerator
* compiler versions
* Git version
* relevant environment variables
* relevant WSL configuration

Never record passwords, authentication tokens, API keys, or other secrets.

### RESOURCE MONITORING

Because the host has limited RAM, monitor:

* Windows memory usage
* WSL memory usage
* QEMU memory usage
* guest memory usage
* CPU utilization
* swap usage
* disk I/O

Do not interpret host-level resource contention as a FuhrerOS performance result without controlling for it.

### EXPERIMENTAL VALIDITY

For research experiments distinguish:

```text
Host overhead
```

from:

```text
Guest/FuhrerOS overhead
```

Record:

```text
host configuration
guest configuration
QEMU configuration
accelerator
workload
policy
measurement interval
```

in every experiment.

If an experiment is performed under WHPX, record that explicitly.

Do not compare WHPX and KVM results as though they were identical execution environments.

### DEVELOPMENT LOOP

Every implementation cycle should follow:

```text
Inspect
  ↓
Plan
  ↓
Modify
  ↓
Build
  ↓
Boot QEMU
  ↓
Test
  ↓
Measure
  ↓
Document
  ↓
Commit
```

Never make a large collection of changes and only test at the end.

### FIRST WSL TASK

Before implementing FuhrerOS functionality:

1. Inspect Windows.
2. Inspect WSL.
3. Inspect QEMU.
4. Inspect available acceleration.
5. Inspect compiler/toolchain.
6. Create the FuhrerOS repository inside WSL.
7. Create the repository structure.
8. Create the development-environment documentation.
9. Create the QEMU scripts.
10. Build the smallest bootable Linux-based FuhrerOS development VM.
11. Boot it successfully.
12. Record the complete configuration.
13. Commit the result.

Do NOT implement the adaptive profiler yet.

Do NOT implement the research policy engine yet.

Do NOT implement the desktop yet.

First establish:

```text
Windows
   ↓
WSL 2
   ↓
QEMU + acceleration
   ↓
bootable FuhrerOS environment
```

Only after this is reproducible should subsequent milestones begin.

### SESSION START PROTOCOL

At the beginning of every Claude session:

```text
1. pwd
2. git status
3. git log -5
4. inspect docs/decisions.md
5. inspect docs/research-design.md
6. inspect docs/development-environment.md
7. inspect latest research-log entries
8. identify current milestone
9. run relevant tests
10. state the current objective
```

Then proceed.

At the end:

```text
1. summarize files changed
2. summarize tests
3. summarize QEMU result
4. summarize benchmark result if applicable
5. record research/design implications
6. update relevant documentation
7. update failure log if applicable
8. commit if the milestone is complete
```

Never fabricate results.

If something has not been verified, write:

```text
UNKNOWN — REQUIRES VERIFICATION
```


You have unrestricted development access inside WSL and the FuhrerOS workspace. You may freely build, install dependencies, modify project files, run QEMU, benchmark, debug and use Windows interoperability. Host-destructive operations require explicit confirmation
