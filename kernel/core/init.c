/* Late kernel initialisation, run once the timer is live: scheduler, then
 * (as milestones land) devices, filesystems, networking and user space.
 * The first kernel thread, "kinit", finishes bring-up so that everything
 * after the scheduler runs as a normal, preemptible task. */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "sched.h"

void test_scheduler(void);
void selftest_summary(void);
int selftest_failures(void);

/* Subsystems register here as they are implemented (weak = not yet built). */
__attribute__((weak)) void devices_init(void) {}
__attribute__((weak)) void storage_init(void) {}
__attribute__((weak)) void fs_init(void) {}
__attribute__((weak)) void net_init(void) {}
__attribute__((weak)) void run_late_selftests(void) {}
__attribute__((weak)) void start_userspace(void)
{
	KLOG("init", "no user space yet (M5)");
}

static void kinit(void *arg)
{
	KLOG("init", "kinit running as task %d under policy '%s'", sched_current()->tid,
	     sched_policy_name());
	devices_init();
	storage_init();
	fs_init();
	net_init();
	if (selftest_mode()) {
		test_scheduler();
		run_late_selftests();
		if (!boot_cmdline_has("usertest")) {
			selftest_summary();
			qemu_exit(selftest_failures() ? 2 : 1);
		}
		/* continue into user space: /bin/init runs /bin/usertest and
		 * ends the run through SYS_DESKTOP_CTL (see proc/syscall.c) */
	}
	start_userspace();
}

void kernel_late_init(void)
{
	sched_init();
	task_create_kernel("kinit", kinit, NULL);
	sched_start();
}
