# Failure log (from-scratch kernel)

| id | title |
|---|---|
| [F-101](F-101-limine-protocol-moved.md) | Limine protocol header missing |
| [F-102](F-102-libgcc-red-zone.md) | libgcc built with the red zone |
| [F-103](F-103-disk-not-sparse.md) | 20 GiB disk image fully allocated on NTFS |
| [F-104](F-104-fbcon-vram-reads.md) | Printing stalled the whole machine |
| [F-105](F-105-stdout-race.md) | Duplicated output lines |
| [F-106](F-106-exit-leaves-threads.md) | A multi-threaded program never finished exiting |
| [F-107](F-107-tcp-zero-window-deadlock.md) | TCP connection stalled forever |
| [F-108](F-108-tcp-ignores-zero-window.md) | 20x lower TCP throughput under the adaptive scheduler |
| [F-109](F-109-system-class-idle.md) | Interactive and network phases detected as IDLE |
| [F-110](F-110-ps2-mouse-bytes.md) | Keyboard IRQ would consume mouse bytes |
| [F-111](F-111-launcher-on-super-press.md) | Super+T opened the launcher |
| [F-112](F-112-exit-reap-races.md) | Two task-exit races |
| [F-113](F-113-cache-cpu-includes-io.md) | Cache CPU time included disk waits |
| [F-114](F-114-readahead-metadata-interleave.md) | Read-ahead never triggered on sequential file reads |
| [F-115](F-115-adaptive-starvation.md) | Adaptive scheduler starved a sleeping thread for seconds |
| [F-116](F-116-readahead-self-eviction.md) | Read-ahead evicted its own prefetched blocks |
| [F-117](F-117-wake-latency-tick-phase.md) | Wake-up latency depended on the boot, not the policy |
| [F-118](F-118-idle-wake-waits-for-tick.md) | A task woken on an idle CPU waited for the next timer tick |
| [F-119](F-119-evict-behind-metadata.md) | Adaptive cache evict-behind evicted the file's own metadata |
| [F-120](F-120-system-class-flicker.md) | The system-level class flickered on every profiler window |
| [F-121](F-121-launcher-blocks-in-irq.md) | Launching an app from the launcher froze the desktop |
| [F-122](F-122-aging-boost-never-expires.md) | The adaptive scheduler's aging boost never expired |
| [F-123](F-123-virtio-blk-write-through.md) | Every disk write waited for a host flush (virtio-blk write-through) |
| [F-124](F-124-fairness-test-window.md) | Scheduler fairness self-test too short for 30 ms quanta |
| [F-125](F-125-delay-test-host-preemption.md) | Timer self-test failed when the host descheduled the vCPU |

Failures F-001..F-006 of the Linux-based prototype are in linux-prototype/research-log/failures/.
