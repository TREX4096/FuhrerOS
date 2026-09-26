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

Failures F-001..F-006 of the Linux-based prototype are in linux-prototype/research-log/failures/.
