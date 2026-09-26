# Processes, threads and IPC

- **Process** (`include/proc.h`): pid, address space, up to 64 file
  descriptors, working directory, parent, threads, exit code, heap (`brk`)
  and mmap regions, `killed` flag.
- **Creation:** `spawn(path, argv, stdio[3])` (D-113). The ELF64 loader
  (`proc/proc.c`) maps `PT_LOAD` segments into a fresh address space (user,
  writable, executable only where `PF_X`), maps a 256 KiB stack and an argv
  page, and starts the first thread with `iretq` into ring 3
  (`enter_user`). `_start` (user/lib/crt0.S) calls `main(argc, argv)`.
- **Threads:** `thread_create(entry, stack, arg)` adds a task sharing the
  address space.
- **Exit and reaping:** the last thread closes descriptors, releases window
  surfaces and ports, and becomes a zombie; `wait()` reaps it only after
  every thread has switched away (its kernel stack is freed then), avoiding
  a use-after-free (F-112). Orphans are reaped by the kernel's `kinit`.
- **Kill:** sets `killed`, wakes blocked threads (their syscalls return),
  and threads running in ring 3 are stopped at their next interrupt
  (`proc_check_killed`, F-106). Ctrl-C on the console kills the foreground
  process registered by the shell.
- **Faults:** any exception in ring 3 prints a one-line report and
  terminates the process with status 128 + vector; the kernel keeps running.
  Tested: `cli` in ring 3 → 141 (#GP), reading a kernel address → 142 (#PF).

## IPC
- **Pipes** (`proc/ipc.c`): 16 KiB ring, blocking read/write, EOF when all
  writers close, `EPIPE` when all readers close. Used by shell pipelines and
  by the desktop terminal ↔ shell link.
- **Message ports:** named mailboxes (`port_create/lookup/send/recv`), up to
  32 queued messages of ≤ 256 bytes, sender pid delivered, receive timeout.
- **Shared memory:** window surfaces are frames mapped into both the kernel
  and the owning process.
