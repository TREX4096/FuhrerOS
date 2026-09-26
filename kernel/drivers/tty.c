/* Console TTY (/dev/console) + /dev/null, /dev/zero, /dev/random.
 *
 * Input comes from the keyboard (via the input sink) and the serial port;
 * output goes to the kernel console (framebuffer + serial). Canonical mode
 * does line editing and echo; raw mode passes bytes through, with special
 * keys encoded as ANSI escape sequences. Ctrl-C kills the foreground
 * process group leader registered with TTY_SETFG. */
#include "arch/x86_64/apic.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/serial.h"
#include "input.h"
#include "kernel.h"
#include "proc.h"
#include "sched.h"
#include "tty.h"
#include "vfs.h"

#define RBUF 4096
static char rbuf[RBUF];		/* bytes ready for readers */
static u32 rhead, rtail;
static char line[512];		/* canonical-mode edit buffer */
static u32 llen;
static u32 mode = TTY_ECHO | TTY_CANON;
static int fg_pid;
static struct waitqueue readers;

static void push_ready(char c)
{
	if (rhead - rtail < RBUF)
		rbuf[rhead++ % RBUF] = c;
}

static void echo(const char *s, u64 n)
{
	if (mode & TTY_ECHO)
		console_write(s, n);
}

void tty_input_char(char c)
{
	u64 f = irq_save();
	if (c == 3 && fg_pid > 0) { /* ^C */
		echo("^C\n", 3);
		proc_kill(fg_pid);
		llen = 0;
		irq_restore(f);
		return;
	}
	if (!(mode & TTY_CANON)) {
		push_ready(c);
	} else if (c == '\r' || c == '\n') {
		for (u32 i = 0; i < llen; i++)
			push_ready(line[i]);
		push_ready('\n');
		llen = 0;
		echo("\n", 1);
	} else if (c == 8 || c == 127) {
		if (llen) {
			llen--;
			echo("\b \b", 3);
		}
	} else if (c == 4) { /* ^D: flush, EOF if empty */
		for (u32 i = 0; i < llen; i++)
			push_ready(line[i]);
		if (!llen)
			push_ready(4);
		llen = 0;
	} else if (c == 21) { /* ^U: kill line */
		while (llen) {
			llen--;
			echo("\b \b", 3);
		}
	} else if ((u8)c >= 32 || c == '\t') {
		if (llen < sizeof(line) - 1) {
			line[llen++] = c;
			echo(&c, 1);
		}
	}
	wq_wake_all(&readers);
	irq_restore(f);
}

static void tty_input_str(const char *s)
{
	while (*s)
		tty_input_char(*s++);
}

/* Keyboard events while the console owns input. */
void tty_key_event(const struct input_event *ev)
{
	if (ev->type != EV_KEY || !ev->value)
		return;
	if (ev->ch) {
		tty_input_char((char)ev->ch);
		return;
	}
	if (mode & TTY_CANON)
		return;
	switch (ev->code) {
	case KEY_UP: tty_input_str("\033[A"); break;
	case KEY_DOWN: tty_input_str("\033[B"); break;
	case KEY_RIGHT: tty_input_str("\033[C"); break;
	case KEY_LEFT: tty_input_str("\033[D"); break;
	case KEY_HOME: tty_input_str("\033[H"); break;
	case KEY_END: tty_input_str("\033[F"); break;
	case KEY_DELETE: tty_input_str("\033[3~"); break;
	}
}

static void serial_irq(struct trap_frame *tf, void *ctx)
{
	int c;
	while ((c = serial_getc_nonblock()) >= 0)
		tty_input_char(c == '\r' ? '\n' : (char)c);
}

static ssize_t tty_read(struct vnode *vn, void *buf, u64 len, u64 off, int flags)
{
	u64 f = irq_save();
	while (rhead == rtail) {
		if (flags & O_NONBLOCK) {
			irq_restore(f);
			return -E_AGAIN;
		}
		wq_wait(&readers, 0);
		struct process *p = proc_current();
		if (p && p->killed) {
			irq_restore(f);
			return -E_INTR;
		}
	}
	u64 n = 0;
	char *b = buf;
	while (n < len && rhead != rtail) {
		char c = rbuf[rtail++ % RBUF];
		if (c == 4) /* EOF marker */
			break;
		b[n++] = c;
		if ((mode & TTY_CANON) && c == '\n')
			break;
	}
	irq_restore(f);
	sched_account_io();
	return (ssize_t)n;
}

static ssize_t tty_write(struct vnode *vn, const void *buf, u64 len, u64 off, int flags)
{
	console_write(buf, len);
	return (ssize_t)len;
}

static int tty_ioctl(struct vnode *vn, u64 req, u64 arg)
{
	switch (req) {
	case TTY_SETMODE: mode = (u32)arg; llen = 0; return 0;
	case TTY_GETMODE: return (int)mode;
	case TTY_SETFG: fg_pid = (int)arg; return 0;
	case TTY_GETSIZE: {
		extern int fbcon_cols(void), fbcon_rows(void);
		return (fbcon_rows() << 16) | fbcon_cols();
	}
	}
	return -E_INVAL;
}

static int tty_poll(struct vnode *vn, int events)
{
	return (rhead != rtail ? 1 : 0) | 2;
}

static const struct vnode_ops tty_ops = {
	.read = tty_read, .write = tty_write, .ioctl = tty_ioctl, .poll = tty_poll };

/* ---- /dev/null, /dev/zero, /dev/random ---- */
static ssize_t null_read(struct vnode *vn, void *b, u64 l, u64 o, int f) { return 0; }
static ssize_t null_write(struct vnode *vn, const void *b, u64 l, u64 o, int f) { return (ssize_t)l; }
static ssize_t zero_read(struct vnode *vn, void *b, u64 l, u64 o, int f)
{
	memset(b, 0, l);
	return (ssize_t)l;
}
static u64 rng_state;
static ssize_t random_read(struct vnode *vn, void *b, u64 l, u64 o, int f)
{
	u8 *p = b;
	for (u64 i = 0; i < l; i++) {
		rng_state ^= rdtsc();
		rng_state ^= rng_state << 13;
		rng_state ^= rng_state >> 7;
		rng_state ^= rng_state << 17;
		p[i] = (u8)rng_state;
	}
	return (ssize_t)l;
}
static const struct vnode_ops null_ops = { .read = null_read, .write = null_write };
static const struct vnode_ops zero_ops = { .read = zero_read, .write = null_write };
static const struct vnode_ops random_ops = { .read = random_read, .write = null_write };

static struct vnode console_vn = { .type = VT_CHAR, .ino = 2, .ops = &tty_ops, .refcnt = 1 << 20 };

struct file *tty_open_console(int flags)
{
	vnode_ref(&console_vn);
	return file_from_vnode(&console_vn, flags);
}

void tty_init(void)
{
	wq_init(&readers);
	rng_state = rdtsc() | 1;
	devfs_register("console", VT_CHAR, &tty_ops, NULL);
	devfs_register("tty", VT_CHAR, &tty_ops, NULL);
	devfs_register("null", VT_CHAR, &null_ops, NULL);
	devfs_register("zero", VT_CHAR, &zero_ops, NULL);
	devfs_register("random", VT_CHAR, &random_ops, NULL);
	input_set_sink(tty_key_event);
	irq_register(VEC_COM1, serial_irq, NULL);
	ioapic_route_isa(4, VEC_COM1);
	serial_enable_rx_irq();
	KLOG("tty", "console tty on framebuffer + COM1 (serial input enabled)");
}
