/* Subsystem bring-up in dependency order (called from kinit, core/init.c):
 *   devices  -> PCI, PS/2, TTY, (virtio-blk / virtio-net register themselves)
 *   fs       -> VFS mounts: / (FFS0 or initrd), /initrd, /dev, /proc, /tmp
 *   user     -> /bin/init with the console as stdin/stdout/stderr */
#include "input.h"
#include "kernel.h"
#include "proc.h"
#include "sched.h"
#include "tty.h"
#include "vfs.h"

void syscall_init(void);
void proc_reap_orphans(void);
__attribute__((weak)) void pci_init(void) {}
__attribute__((weak)) int blk_root_device_present(void) { return 0; }
__attribute__((weak)) void blk_init_devices(void) {}
__attribute__((weak)) void net_init_devices(void) {}
__attribute__((weak)) void compositor_init(void) {}

void devices_init(void)
{
	input_init();
	ps2_init();
	proc_init();
	syscall_init();
	pci_init();		/* discovers and binds virtio devices */
	blk_init_devices();
	net_init_devices();
	compositor_init();
}

static bool root_on_disk;

void fs_init(void)
{
	vfs_init();
	tty_init();		/* registers /dev nodes before devfs is mounted */
	int r = -1;
	if (blk_root_device_present() && !boot_cmdline_has("root=initrd")) {
		r = vfs_mount("ffs0", "vda", "/");
		if (r < 0)
			KLOG("vfs", "FFS0 root on vda failed (%d); falling back to the initrd", r);
	}
	root_on_disk = r == 0;
	if (!root_on_disk && vfs_mount("tarfs", "initrd", "/") < 0)
		panic("no root filesystem: neither FFS0 on vda nor an initrd module");
	if (root_on_disk)
		vfs_mount("tarfs", "initrd", "/initrd");
	vfs_mount("devfs", NULL, "/dev");
	vfs_mount("procfs", NULL, "/proc");
	vfs_mount("ramfs", NULL, "/tmp");
	KLOG("vfs", "root filesystem: %s", root_on_disk ? "FFS0 (virtio-blk vda)" : "initrd (read-only)");
}

bool fs_root_on_disk(void) { return root_on_disk; }

void start_userspace(void)
{
	static const char *argv[] = { "/bin/init", NULL };
	int pid = proc_spawn("/bin/init", argv, NULL, "/");
	if (pid < 0) {
		printk("\033[1;31mkinit: cannot start /bin/init (%d)\033[0m\n", pid);
		return;
	}
	KLOG("init", "started /bin/init as pid %d - kernel handing over to user space", pid);
	/* kinit becomes the reaper of orphaned processes. */
	for (;;) {
		sleep_ms(250);
		proc_reap_orphans(); /* zombies without a parent, including init */
		if (!proc_by_pid(pid)) {
			printk("\033[1;33mkinit: init exited; restarting it\033[0m\n");
			pid = proc_spawn("/bin/init", argv, NULL, "/");
		}
	}
}
