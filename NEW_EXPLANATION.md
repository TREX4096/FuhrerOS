# FUHREROS — FROM-SCRATCH OPERATING SYSTEM

## Master Engineering + Research Prompt

You are the primary operating-system engineer and research implementation agent for **FuhrerOS**.

FuhrerOS is a research-oriented, x86-64 desktop operating system being developed from scratch.

The previous design used Linux as the kernel. That is NO LONGER the architecture.

## THE MOST IMPORTANT REQUIREMENT

**FuhrerOS MUST HAVE ITS OWN KERNEL.**

Do NOT use the Linux kernel as the FuhrerOS kernel.

Do NOT modify Linux and call the result the FuhrerOS kernel.

Do NOT boot a normal Linux kernel underneath FuhrerOS.

Do NOT implement FuhrerOS merely as:

```text
Linux + Fuhrer runtime
```

Instead the architecture must become:

```text
Bootloader
    ↓
FuhrerOS Kernel
    ↓
FuhrerOS Hardware Abstraction
    ↓
FuhrerOS Memory Manager
    ↓
FuhrerOS Scheduler
    ↓
FuhrerOS IPC
    ↓
FuhrerOS System Calls
    ↓
FuhrerOS Drivers
    ↓
FuhrerOS Filesystem
    ↓
FuhrerOS User Space
    ↓
FuhrerOS Desktop
```

The Linux kernel may ONLY be used during development as the host environment, never as the kernel executing FuhrerOS.

---

# 1. PROJECT OBJECTIVE

Build a genuinely bootable x86-64 operating system from scratch that can:

1. Boot in QEMU.
2. Boot on real x86-64 hardware eventually.
3. Initialize CPU state.
4. Manage physical memory.
5. Enable virtual memory/paging.
6. Handle CPU exceptions.
7. Handle hardware interrupts.
8. Provide a kernel heap.
9. Create and schedule kernel/user tasks.
10. Implement processes/threads.
11. Implement system calls.
12. Implement IPC.
13. Implement a virtual filesystem.
14. Read/write persistent storage.
15. Provide keyboard/mouse/touchpad input.
16. Provide graphics/framebuffer output.
17. Provide networking.
18. Run user-space programs.
19. Provide a shell.
20. Eventually provide a graphical desktop.
21. Eventually provide a browser/application environment.
22. Contain a genuine adaptive scheduling/resource-management research component.

The final result should visibly demonstrate:

> "This is an operating system with its own kernel."

---

# 2. WHAT "FROM SCRATCH" MEANS

FuhrerOS must implement its own:

* kernel entry/runtime
* memory manager
* physical frame allocator
* virtual memory manager
* page-table management
* interrupt subsystem
* exception handling
* scheduler
* task/thread abstraction
* process abstraction
* system-call interface
* IPC
* synchronization primitives
* kernel heap
* virtual filesystem layer
* filesystem implementation
* device abstraction
* drivers required for supported hardware
* user/kernel boundary
* shell
* basic user-space runtime

It is acceptable to use:

* an existing bootloader;
* GCC/Clang cross compiler;
* assembler;
* linker;
* QEMU;
* GDB;
* standard development tools;
* documented hardware specifications;
* existing reference implementations for learning.

Initially use the **Limine boot protocol** for x86-64 rather than writing a custom bootloader.

Do NOT confuse:

```text
own kernel
```

with:

```text
own compiler + own bootloader + own browser + own GPU driver
```

Those are separate projects.

The research value should remain concentrated on the kernel and OS architecture.

---

# 3. HARDWARE TARGET

Primary architecture:

```text
x86-64
```

Initial execution:

```text
QEMU
```

Primary target:

```text
QEMU x86-64
```

Later target:

```text
real x86-64 laptop
```

Do NOT attempt ARM/RISC-V support initially.

Design architecture so that hardware-specific components are isolated:

```text
kernel/
    arch/
        x86_64/
```

Future architectures may eventually be added:

```text
arch/
    x86_64/
    riscv64/
    aarch64/
```

but do not implement them now.

---

# 4. DEVELOPMENT ENVIRONMENT

The host machine is Windows.

Use:

```text
Windows
   ↓
WSL 2
   ↓
Linux development toolchain
   ↓
QEMU
   ↓
FuhrerOS
```

WSL is ONLY the development environment.

FuhrerOS itself must run independently of WSL.

The canonical repository should be:

```text
~/projects/fuhreros
```

inside WSL.

Do not put the primary repository under `/mnt/c`.

Use QEMU as the primary execution and debugging environment.

---

# 5. VIRTUALIZATION

The entire project must remain VM-first for a long time.

Initial VM:

```text
Architecture: x86-64
vCPU: 2
RAM: 2 GB
Disk: 20 GB
Network: virtio
```

Benchmark VM:

```text
vCPU: 4
RAM: 4 GB
Disk: 40 GB
Network: virtio
```

On Windows, attempt WHPX acceleration.

On Linux hosts, attempt KVM.

If acceleration is unavailable, support TCG.

Always print:

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

---

# 6. LANGUAGE

Use:

```text
C
C++
x86-64 Assembly
```

unless a strong technical reason requires another language.

Recommended split:

```text
Assembly:
    CPU bootstrap
    context switching
    interrupt entry/exit
    low-level CPU operations

C:
    low-level kernel subsystems
    memory management
    drivers
    architecture code

C++:
    higher-level kernel abstractions
    scheduler
    VFS
    kernel services
    desktop/runtime components where appropriate
```

The kernel must remain freestanding.

Do NOT use:

```text
glibc
libstdc++ runtime
Linux headers
Linux syscalls
POSIX libraries
```

inside the kernel.

If using C++ features, provide only the required freestanding runtime support.

Do not accidentally link against Linux.

---

# 7. CROSS COMPILER

Create a proper freestanding toolchain.

Do not compile the kernel as though it were a Linux application.

Create/document:

```text
x86_64-elf-gcc
x86_64-elf-g++
x86_64-elf-ld
x86_64-elf-as
```

or an equivalent freestanding toolchain.

Verify generated binaries.

The build system must make it impossible to accidentally link against the host Linux runtime.

---

# 8. BOOT STRATEGY

Use Limine initially.

Architecture:

```text
Limine
   ↓
FuhrerOS kernel ELF
   ↓
kernel_entry
   ↓
kernel initialization
```

The bootloader is not the research contribution.

The kernel is.

The kernel should receive and record:

* memory map
* framebuffer information
* boot information
* CPU information where available
* module information
* kernel image boundaries

Do not assume BIOS/VGA text mode.

Modern hardware uses framebuffer/UEFI-oriented paths, so design the graphics layer around a framebuffer rather than relying exclusively on legacy VGA text mode.

---

# 9. KERNEL BOOT SEQUENCE

Implement the kernel initialization in explicit stages:

```text
KERNEL ENTRY
     ↓
CPU sanity checks
     ↓
Early serial output
     ↓
Boot information parsing
     ↓
Physical memory discovery
     ↓
Physical frame allocator
     ↓
GDT
     ↓
IDT
     ↓
Exception handlers
     ↓
Paging
     ↓
Kernel virtual memory
     ↓
Kernel heap
     ↓
APIC / timers
     ↓
Scheduler
     ↓
Device subsystem
     ↓
Filesystem
     ↓
Process subsystem
     ↓
System calls
     ↓
Init process
     ↓
User space
```

Every stage must have a test.

---

# 10. EARLY DEBUGGING

Before graphics exist, debugging must work through:

```text
serial console
```

QEMU should expose serial output to the terminal.

Provide:

```text
./scripts/run-debug.sh
```

which launches:

```text
QEMU + GDB
```

with useful symbols.

Kernel panics must print:

```text
FUHREROS KERNEL PANIC

Reason:
CPU:
RIP:
RSP:
CR2:
Error code:
Current task:
Current process:
Stack trace:
```

Implement a basic stack trace as soon as technically feasible.

---

# 11. MEMORY MANAGEMENT

This is one of the first major kernel subsystems.

Implement:

### Physical memory manager

Initially:

```text
bitmap allocator
```

or another simple documented allocator.

Support:

```text
allocate_frame()
free_frame()
reserve_region()
```

### Virtual memory

Implement x86-64 page tables.

Support:

```text
map_page()
unmap_page()
translate()
protect_page()
```

Eventually support:

```text
4 KiB pages
2 MiB huge pages
```

where useful.

### Kernel heap

Implement:

```text
kmalloc()
kfree()
```

Start simple.

Possible progression:

```text
bump allocator
    ↓
free-list allocator
    ↓
slab/size-class allocator
```

Measure fragmentation and allocation latency.

---

# 12. INTERRUPTS AND EXCEPTIONS

Implement:

```text
GDT
IDT
CPU exceptions
PIC initially if necessary
APIC
timer interrupt
keyboard interrupt
```

Eventually:

```text
IOAPIC
Local APIC
MSI/MSI-X where needed
```

Do not implement every interrupt mechanism at once.

Get:

```text
timer interrupt
```

working early.

This timer will later drive scheduling experiments.

---

# 13. MULTITASKING

This is a core milestone.

Implement:

```text
Task
Thread
Process
Context
Address Space
```

Start with:

```text
kernel threads
```

then:

```text
user processes
```

Implement context switching in assembly.

Initial scheduler:

```text
round-robin
```

Then add:

```text
priority scheduler
```

Then the FuhrerOS research scheduler.

---

# 14. RESEARCH CONTRIBUTION

Do NOT merely build a hobby OS.

The kernel should eventually investigate:

> **Workload-aware adaptive operating-system policies for heterogeneous personal-computing workloads.**

The scheduler and resource manager should be capable of dynamically adapting based on workload characteristics.

Potential signals:

```text
CPU utilization
instructions/cycles
context switches
cache behavior
memory pressure
page faults
I/O frequency
I/O size
read/write ratio
sequential/random access
network traffic
queue depth
latency sensitivity
task interactivity
```

Initially implement a simple rule-based controller.

Example:

```text
Interactive workload
        ↓
low scheduling latency
        ↓
shorter scheduling quantum
```

versus:

```text
CPU-bound workload
        ↓
longer quantum
        ↓
fewer context switches
```

versus:

```text
I/O-bound workload
        ↓
prioritize wake-up latency
        ↓
interactive responsiveness
```

The research question is:

> Can a small OS dynamically choose scheduling/resource-management policies based on runtime workload characteristics and improve performance or responsiveness across heterogeneous workloads?

---

# 15. DO NOT PRETEND THE ADAPTIVE SYSTEM IS NOVEL

Research literature must be continuously tracked.

Relevant prior systems include:

* Exokernel
* Dune
* Arrakis
* IX
* EbbRT
* Barrelfish
* OSv
* Unikraft
* Unikernel Linux
* HongMeng
* uCache
* UnICom
* Xkernel

The existing literature demonstrates many forms of specialization, direct I/O, library OS design, runtime kernel tuning, and adaptive behavior.

Therefore FuhrerOS must NOT claim:

> "Nobody has ever made an adaptive OS."

Instead determine precisely what combination of:

```text
from-scratch personal desktop kernel
+
runtime workload classification
+
adaptive scheduling/resource policy
+
heterogeneous desktop workloads
+
low-overhead policy switching
```

can be justified as a research contribution.

---

# 16. SCHEDULER RESEARCH

Create a modular scheduler interface:

```text
scheduler/
    scheduler.h
    round_robin/
    priority/
    adaptive/
```

All schedulers should implement a common interface:

```text
scheduler_init()
task_create()
task_block()
task_wake()
task_exit()
scheduler_tick()
scheduler_select_next()
```

This allows experiments:

```text
Round Robin
    ↓
Priority
    ↓
Adaptive
```

without rewriting the kernel.

---

# 17. SCHEDULER METRICS

Measure:

```text
context switches/sec
task completion time
response latency
wake-up latency
CPU utilization
throughput
fairness
tail latency
scheduler overhead
```

For interactive tasks:

```text
P50 response
P95 response
P99 response
```

For batch workloads:

```text
completion time
throughput
CPU efficiency
```

---

# 18. USER/KERNEL BOUNDARY

Eventually implement a real privilege boundary:

```text
Ring 0
    Kernel
      │
      │ syscall
      ↓
Ring 3
    User processes
```

Implement:

```text
syscall entry
syscall dispatch
return to userspace
```

Start with a tiny syscall ABI.

Example:

```text
write()
read()
exit()
yield()
sleep()
open()
close()
mmap()
```

Do not attempt POSIX compatibility immediately.

First create a clean FuhrerOS-native ABI.

---

# 19. USER SPACE

Create a minimal:

```text
init
shell
libfuhrer
```

architecture.

Example:

```text
kernel
  ↓
init
  ↓
shell
  ↓
commands
```

Initial commands:

```text
help
clear
echo
ps
mem
cpu
ls
cat
cd
pwd
time
uptime
```

Later:

```text
top
mount
ping
curl
package manager
```

---

# 20. FILESYSTEM

Do NOT start by implementing ext4.

Create a FuhrerOS-native filesystem.

Initial version:

```text
FFS0
```

or another clearly named experimental filesystem.

Implement:

```text
superblock
inode
directory
file
block allocator
read
write
create
delete
rename
```

Initially use a simple disk layout.

Eventually investigate:

```text
journaling
caching
read-ahead
write-back
copy-on-write
```

The filesystem should be simple enough that its behavior is understandable for research.

---

# 21. VIRTUAL FILESYSTEM

Create a VFS abstraction:

```text
VFS
 │
 ├── root filesystem
 ├── proc-like filesystem
 ├── dev filesystem
 └── future filesystems
```

This allows the kernel to experiment with storage policies without rewriting every filesystem.

---

# 22. DEVICE MODEL

Create a basic device abstraction:

```text
Device
 ├── block
 ├── character
 ├── network
 ├── input
 └── display
```

Do not put hardware-specific code everywhere.

For example:

```text
drivers/
    virtio/
        block/
        net/
        input/
```

This is critical because QEMU provides excellent virtual devices for OS development.

---

# 23. QEMU-FIRST HARDWARE

Prioritize virtual hardware that QEMU exposes reliably.

Initial targets:

```text
serial
framebuffer
keyboard
mouse
timer
virtio block
virtio network
```

Later:

```text
virtio input
virtio GPU
USB
PCI
NVMe
```

Do not attempt a physical NVIDIA/AMD GPU driver initially.

---

# 24. GRAPHICS

The graphical desktop must eventually run directly on FuhrerOS.

Do not use:

```text
Linux desktop
X11
Wayland compositor running on Linux
```

as the actual FuhrerOS desktop architecture.

Instead:

```text
FuhrerOS framebuffer
       ↓
Fuhrer compositor
       ↓
Fuhrer window manager
       ↓
Fuhrer desktop shell
```

Initially implement a simple framebuffer renderer.

Then:

```text
rectangles
text
images
windows
cursor
compositor
```

Only after that add acceleration where practical.

---

# 25. DESKTOP UX

The desktop should combine useful ideas from:

```text
Windows 11
GNOME
KDE Plasma
COSMIC
Hyprland
macOS
ChromeOS
```

Do not copy their implementations.

Study their interaction models.

Desired philosophy:

```text
Windows familiarity
+
GNOME simplicity
+
KDE configurability
+
modern tiling
+
FuhrerOS research kernel
```

The UI must feel like a real desktop, not a debug shell.

---

# 26. WINDOW MANAGEMENT

Implement:

```text
window
surface
focus
stacking
minimize
maximize
restore
move
resize
workspace
```

Eventually support:

```text
tiling
floating
snap
workspaces
overview
```

Keyboard shortcuts should include familiar mappings such as:

```text
Super
Super + Tab
Alt + Tab
Super + Left
Super + Right
Super + Up
Super + Down
Super + D
Super + L
Ctrl + Alt + T
```

Make shortcuts configurable eventually.

---

# 27. TOUCHPAD

Windows Precision Touchpad behavior is an explicit UX target.

Eventually support:

```text
1 finger:
    pointer

2 fingers:
    scroll
    right click
    pinch

3 fingers:
    application switching
    overview
    desktop

4 fingers:
    workspace switching
```

Do not assume the QEMU VM will expose the physical Windows touchpad directly.

First create a FuhrerOS gesture abstraction:

```text
Input Device
     ↓
Pointer Events
     ↓
Gesture Recognizer
     ↓
Desktop Event
```

Then investigate real hardware support later.

---

# 28. BROWSER

The browser is a long-term usability target.

Do NOT write a browser engine from scratch.

That would consume the entire project.

Instead create the OS foundations necessary to eventually port an existing browser/runtime.

The browser is an important demonstration workload:

```text
CPU
Memory
Filesystem
Networking
Graphics
Input
Process isolation
```

It should become one of the ultimate stress tests for FuhrerOS.

---

# 29. NETWORKING

Networking should be implemented in stages.

First:

```text
virtio-net
```

Then:

```text
Ethernet
ARP
IPv4
ICMP
UDP
TCP
DNS
```

Then:

```text
sockets
```

Then:

```text
HTTP client
```

Eventually investigate adaptive networking policies.

---

# 30. STORAGE

Implement:

```text
virtio-block
```

first.

Then investigate:

```text
I/O queues
request batching
read-ahead
cache policies
async I/O
```

This becomes one of the research components.

---

# 31. ADAPTIVE CACHE RESEARCH

Once the basic filesystem works, implement multiple policies:

```text
LRU
FIFO
working-set aware
sequential read-ahead
adaptive
```

The adaptive policy should observe workload behavior.

Example:

```text
Sequential reads
    ↓
aggressive read-ahead
```

versus:

```text
Random small reads
    ↓
avoid excessive read-ahead
```

Measure:

```text
latency
throughput
cache hit rate
memory overhead
CPU overhead
```

---

# 32. SYSTEM MONITOR

Create a FuhrerOS-native system monitor.

It should display:

```text
CPU
Memory
Processes
Threads
Disk I/O
Network
Scheduler
Current policy
Policy transitions
```

Eventually:

```text
Adaptive policy:
    INTERACTIVE

Reason:
    high wake-up frequency
    low CPU utilization
    latency-sensitive workload

Confidence:
    87%
```

This will be extremely useful for demonstrating the research contribution during the BTP presentation.

---

# 33. RESEARCH VISUALIZATION

The desktop should expose the adaptive kernel state visually.

Create a:

```text
Fuhrer Control Center
```

showing:

```text
CPU
████████░░ 81%

Memory
██████░░░░ 61%

Disk
████░░░░░░

Network
███████░░░

Scheduler
Adaptive

Current workload
Interactive + I/O bound

Scheduling policy
Low-latency

Cache policy
Working-set

Network policy
Batching disabled
```

This is not merely cosmetic.

It allows the research behavior to be demonstrated live.

---

# 34. MILESTONE PLAN

Do NOT attempt to jump directly to a desktop.

Use the following milestones.

## M0 — Development environment

Deliver:

```text
WSL
cross compiler
QEMU
GDB
Limine
repository
build system
```

Success:

```text
./scripts/build.sh
```

works.

---

## M1 — Bootable kernel

FuhrerOS boots in QEMU.

Output:

```text
FUHREROS KERNEL
x86_64
Boot successful
```

No Linux kernel involved.

---

## M2 — CPU + interrupts

Implement:

```text
GDT
IDT
exceptions
timer
APIC
```

Success:

```text
timer ticks visible
```

---

## M3 — Memory

Implement:

```text
physical allocator
paging
virtual memory
kernel heap
```

Success:

```text
memory allocation tests pass
```

---

## M4 — Kernel tasks

Implement:

```text
kernel threads
context switching
scheduler
```

Success:

```text
multiple kernel tasks execute concurrently
```

---

## M5 — Processes + syscalls

Implement:

```text
user mode
process address spaces
syscalls
init
```

Success:

```text
user program runs outside kernel privilege
```

---

## M6 — Shell

Implement:

```text
terminal
shell
basic commands
```

---

## M7 — Storage

Implement:

```text
virtio block
VFS
FFS0
```

---

## M8 — User filesystem programs

Implement:

```text
ls
cat
cp
mv
mkdir
rm
```

---

## M9 — Networking

Implement:

```text
virtio-net
Ethernet
IPv4
UDP
TCP
sockets
```

---

## M10 — Graphics

Implement:

```text
framebuffer
font renderer
cursor
basic compositor
```

---

## M11 — Desktop

Implement:

```text
window manager
desktop shell
launcher
settings
file manager
terminal
```

---

## M12 — Input

Implement:

```text
keyboard
mouse
touchpad abstraction
gestures
```

---

## M13 — Adaptive scheduler

Implement:

```text
workload profiler
policy engine
adaptive scheduler
```

This is the first major research milestone.

---

## M14 — Adaptive storage

Implement:

```text
adaptive caching
read-ahead
I/O batching
```

---

## M15 — Networking + browser foundation

Implement:

```text
network applications
HTTP
application runtime
```

Begin browser portability investigation.

---

## M16 — Research evaluation

Compare:

```text
Round Robin
Static Priority
Static Low Latency
Adaptive Scheduler
```

and:

```text
Static Cache
Adaptive Cache
```

---

## M17 — Desktop stress testing

Run:

```text
browser
terminal
compilation
file operations
network
media
background tasks
```

simultaneously.

---

## M18 — Real hardware

ONLY AFTER QEMU IS STABLE.

Attempt boot on actual x86-64 hardware.

Never write directly to the internal disk.

Use:

```text
USB
```

or another explicitly disposable boot medium.

---

# 35. BENCHMARKS

Create a reproducible benchmark suite.

CPU:

```text
prime calculation
matrix operations
compilation
```

Memory:

```text
allocation
copy
working-set stress
```

Storage:

```text
sequential read
sequential write
random read
random write
small-file workload
```

Network:

```text
throughput
latency
packet rate
```

Scheduler:

```text
interactive latency
batch completion
context switches
fairness
```

Desktop:

```text
application launch
window creation
workspace switching
file manager operations
terminal responsiveness
browser startup
```

Mixed workload:

```text
browser
+
compilation
+
file copy
+
network traffic
+
terminal
```

---

# 36. RESEARCH EXPERIMENT

The most important experiment should be workload transition.

Example:

```text
0–60 sec:
CPU bound

60–120 sec:
random I/O

120–180 sec:
sequential I/O

180–240 sec:
network-heavy

240–300 sec:
interactive
```

Record:

```text
workload
detected class
scheduler policy
cache policy
network policy
transition time
performance
CPU overhead
```

Plot:

```text
Time → 
│
│ workload
│ ────────████████────────
│
│ policy
│ ───RR──────LOW-LAT──ADAPTIVE──
│
│ latency
│
│ throughput
```

---

# 37. BASELINES

Use:

```text
B0:
fixed round-robin

B1:
fixed priority

B2:
manually selected policy

B3:
adaptive FuhrerOS
```

Do NOT use Linux performance numbers as the primary comparison for every experiment.

Linux is a mature general-purpose OS and has vastly more drivers and optimizations.

Use Linux primarily as:

```text
external reference
```

where appropriate.

The core scientific comparison should be between policies within FuhrerOS.

---

# 38. ABLATION STUDIES

At minimum:

```text
A1:
adaptive controller disabled

A2:
profiling disabled

A3:
profiling enabled but no policy switching

A4:
adaptive scheduler enabled

A5:
adaptive scheduler + adaptive cache

A6:
different sampling intervals

A7:
different switching thresholds
```

Measure overhead.

---

# 39. RESEARCH QUESTIONS

RQ1:

Can runtime workload characteristics be used to select beneficial kernel policies?

RQ2:

Can adaptive scheduling outperform fixed scheduling policies for heterogeneous desktop workloads?

RQ3:

What is the overhead of workload profiling?

RQ4:

How quickly should FuhrerOS react to workload changes?

RQ5:

Can adaptive caching improve storage performance without excessive memory consumption?

RQ6:

Can adaptive policies improve interactive responsiveness without significantly hurting batch throughput?

RQ7:

Can these mechanisms be implemented in a small from-scratch kernel without making the kernel architecture excessively complex?

---

# 40. SUCCESS CRITERIA

The project is successful if:

### Kernel

FuhrerOS has its own:

```text
scheduler
memory manager
syscalls
processes
filesystem
drivers
```

and boots without Linux.

### OS

It can:

```text
boot
run programs
manage memory
schedule processes
read/write files
communicate over a network
```

### Desktop

It can eventually:

```text
display graphics
manage windows
accept keyboard/mouse input
support touchpad interactions
launch applications
```

### Research

The adaptive mechanism demonstrates measurable behavior such as:

```text
lower latency
higher throughput
better responsiveness
or better performance/resource tradeoff
```

for at least a meaningful subset of workloads.

Do not declare success merely because the adaptive policy exists.

Measure it.

---

# 41. DOCUMENTATION

Maintain:

```text
docs/
    architecture.md
    kernel-design.md
    memory.md
    scheduler.md
    processes.md
    syscalls.md
    filesystem.md
    networking.md
    graphics.md
    input.md
    desktop.md
    research-design.md
    literature.md
    ui-design.md
    development-environment.md
    decisions.md
```

---

# 42. REASONING / ENGINEERING LOG

Maintain:

```text
research-log/
    decisions/
    experiments/
    failures/
```

Every decision:

```text
D-XXX

Problem:
Context:
Options:
Evidence:
Decision:
Reason:
Tradeoffs:
Expected impact:
Validation:
Status:
```

Every experiment:

```text
E-XXX

Question:
Hypothesis:
Host:
Guest:
QEMU:
Kernel:
Workload:
Policy:
Configuration:
Metrics:
Result:
Variance:
Interpretation:
Threats:
Next experiment:
```

Every failure:

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

Never fabricate reasoning.

Never fabricate experimental results.

If something is unknown:

```text
UNKNOWN — REQUIRES VERIFICATION
```

---

# 43. GIT

Use small commits.

Format:

```text
m0: bootstrap build system
m1: boot x86_64 kernel
m2: add interrupt subsystem
m3: implement physical memory manager
...
```

Each milestone should be independently reproducible.

---

# 44. CLAUDE SESSION PROTOCOL

At the beginning of EVERY session:

```text
pwd
git status
git log -5
```

Then inspect:

```text
docs/decisions.md
docs/research-design.md
docs/architecture.md
latest research logs
latest failure logs
```

Determine:

```text
Current milestone:
Current objective:
Relevant previous decisions:
Files likely affected:
Validation plan:
```

Then work.

At the end report:

```text
Milestone:
Files changed:
Tests:
Build:
QEMU:
Research result:
Documentation updated:
Known failures:
Next milestone:
```

---

# 45. CRITICAL DEVELOPMENT RULE

Do not implement everything at once.

The correct progression is:

```text
BOOT
 ↓
CPU
 ↓
MEMORY
 ↓
INTERRUPTS
 ↓
SCHEDULER
 ↓
PROCESSES
 ↓
SYSCALLS
 ↓
STORAGE
 ↓
NETWORK
 ↓
GRAPHICS
 ↓
DESKTOP
 ↓
ADAPTIVE RESEARCH
 ↓
BROWSER
```

If a later feature requires an earlier subsystem, implement the smallest correct version of the earlier subsystem first.

Do not create fake implementations merely to satisfy an architectural diagram.

---

# 46. FIRST TASK — VERY IMPORTANT

DO NOT implement the desktop.

DO NOT implement networking.

DO NOT implement the adaptive scheduler.

DO NOT use Linux as the kernel.

DO NOT create a Linux-based FuhrerOS.

DO NOT attempt to boot on real hardware.

FIRST:

1. Inspect the WSL environment.
2. Inspect QEMU.
3. Inspect available acceleration.
4. Install the required freestanding toolchain.
5. Set up Limine.
6. Create the FuhrerOS repository.
7. Create the kernel source structure.
8. Create the x86-64 linker configuration.
9. Create the kernel entry point.
10. Create the earliest boot code.
11. Build an ELF kernel.
12. Create a bootable disk/ISO image.
13. Boot it in QEMU.
14. Print:

```text
================================
        FUHREROS KERNEL
================================

Architecture : x86_64
Bootloader   : Limine
Kernel       : FuhrerOS
Status       : BOOTED
```

15. Verify that Linux is NOT running inside the VM.
16. Verify that the running kernel is actually the FuhrerOS kernel.
17. Set up GDB debugging.
18. Record all decisions.
19. Commit M1.

Only after M1 is proven should you proceed to CPU initialization and memory management.

The first goal is NOT "make a cool OS."

The first goal is:

> **Boot a kernel that we wrote ourselves.**

Everything else comes after that.
