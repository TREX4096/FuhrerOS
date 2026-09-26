# System calls (FuhrerOS native ABI)

Defined in `kernel/include/uapi/fuhrer.h`, shared by kernel and user space.
Convention: `syscall`, number in `rax`, arguments `rdi, rsi, rdx, r10, r8`,
result in `rax` (negative = −errno). Not POSIX (§18), though names follow
familiar meanings.

| # | name | arguments → result |
|---|---|---|
| 0 | exit | code |
| 1 / 2 | write / read | fd, buf, len → bytes |
| 3 / 4 | open / close | path, flags → fd |
| 5 / 6 | yield / sleep | — / milliseconds |
| 7 / 8 | mmap / munmap | anonymous memory |
| 9 | spawn | path, argv, stdio[3] → pid |
| 10 | wait | pid (−1 any), &status → pid |
| 11 / 31 | getpid / getppid | |
| 12 | time | &{uptime_ns, unix_seconds} |
| 13 / 14 | stat / fstat | &struct stat |
| 15 | readdir | fd, index, &dirent → 1 / 0 at end |
| 16–19 | mkdir, unlink, rename, chdir | |
| 20 | getcwd | buf, len |
| 21 / 22 | pipe / dup2 | |
| 23 | kill | pid |
| 24 | sysinfo | &struct fu_sysinfo |
| 25 | sched_ctl | op, arg, buf (policy, priority, profiler window/hysteresis, quantum scale, task class) |
| 26 / 27 | seek / ioctl | (tty modes, foreground pid, size) |
| 28 | brk | |
| 29 / 30 | thread_create / thread_exit | |
| 32 / 33 | truncate / sync | |
| 34 | poll | fds, n, timeout → readable bitmask |
| 35–38 | port_create / lookup / send / recv | message ports |
| 40–49 | socket, connect, bind, listen, accept, send, recv, sendto, recvfrom, netinfo | AF_INET; STREAM, DGRAM, ICMP |
| 50–58 | win_create, win_surface, win_present, win_event, win_close, win_set_title, win_info, win_resize, desktop_ctl | compositor |
| 59 / 60 | blkstat / cache_ctl | block + cache statistics, cache policy/capacity/drop |

User wrappers live in `user/lib/libfu.c`, `net.c`, `gui.c` (`user/include/fu.h`, `gui.h`).
