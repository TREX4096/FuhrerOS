/* i8042 PS/2 controller: keyboard (scancode set 1 via translation) and
 * mouse (IntelliMouse protocol with wheel). Both feed the input subsystem. */
#include "arch/x86_64/apic.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/idt.h"
#include "input.h"
#include "kernel.h"

#define DATA 0x60
#define STATUS 0x64
#define CMD 0x64

static bool e0, shift_l, shift_r, ctrl, alt, super, caps;
static u8 mpacket[4];
static int mpos, mpacket_len = 3;
static u8 mbuttons;
static bool mouse_ok;

static const u8 set1_ascii[128] = {
	0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8, 9,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 10, 0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0,
};
static const char shifted[128] = {
	['1'] = '!', ['2'] = '@', ['3'] = '#', ['4'] = '$', ['5'] = '%', ['6'] = '^',
	['7'] = '&', ['8'] = '*', ['9'] = '(', ['0'] = ')', ['-'] = '_', ['='] = '+',
	['['] = '{', [']'] = '}', [';'] = ':', ['\''] = '"', ['`'] = '~', ['\\'] = '|',
	[','] = '<', ['.'] = '>', ['/'] = '?',
};

static void wait_write(void)
{
	for (int i = 0; i < 100000 && (inb(STATUS) & 2); i++)
		cpu_pause();
}
static bool wait_read(void)
{
	for (int i = 0; i < 100000; i++) {
		if (inb(STATUS) & 1)
			return true;
		cpu_pause();
	}
	return false;
}
static void ctl_cmd(u8 c)
{
	wait_write();
	outb(CMD, c);
}
static void ctl_data(u8 d)
{
	wait_write();
	outb(DATA, d);
}
static int mouse_cmd(u8 c)
{
	ctl_cmd(0xD4);
	ctl_data(c);
	if (!wait_read())
		return -1;
	return inb(DATA);
}

u16 input_modifiers(void)
{
	return (u16)(((shift_l || shift_r) ? MOD_SHIFT : 0) | (ctrl ? MOD_CTRL : 0) |
		     (alt ? MOD_ALT : 0) | (super ? MOD_SUPER : 0));
}

static void mouse_byte(u8 b);

static void kbd_byte(u8 sc)
{
	{
		if (sc == 0xE0) {
			e0 = true;
			return;
		}
		bool rel = sc & 0x80;
		u8 code = sc & 0x7F;
		u16 key = 0;
		if (e0) {
			e0 = false;
			switch (code) {
			case 0x48: key = KEY_UP; break;
			case 0x50: key = KEY_DOWN; break;
			case 0x4B: key = KEY_LEFT; break;
			case 0x4D: key = KEY_RIGHT; break;
			case 0x47: key = KEY_HOME; break;
			case 0x4F: key = KEY_END; break;
			case 0x49: key = KEY_PGUP; break;
			case 0x51: key = KEY_PGDN; break;
			case 0x52: key = KEY_INSERT; break;
			case 0x53: key = KEY_DELETE; break;
			case 0x1D: key = KEY_RCTRL; ctrl = !rel; break;
			case 0x38: key = KEY_RALT; alt = !rel; break;
			case 0x5B: case 0x5C: key = KEY_SUPER; super = !rel; break;
			case 0x5D: key = KEY_MENU; break;
			case 0x1C: key = KEY_ENTER; break;
			case 0x35: key = '/'; break;
			default: return;
			}
		} else {
			switch (code) {
			case 0x2A: key = KEY_LSHIFT; shift_l = !rel; break;
			case 0x36: key = KEY_RSHIFT; shift_r = !rel; break;
			case 0x1D: key = KEY_LCTRL; ctrl = !rel; break;
			case 0x38: key = KEY_LALT; alt = !rel; break;
			case 0x3A: key = KEY_CAPS; if (!rel) caps = !caps; break;
			default:
				if (code >= 0x3B && code <= 0x44)
					key = (u16)(KEY_F1 + code - 0x3B);
				else if (code == 0x57)
					key = KEY_F11;
				else if (code == 0x58)
					key = KEY_F12;
				else if (code < 128 && set1_ascii[code])
					key = set1_ascii[code];
				else
					return;
			}
		}
		struct input_event ev = { .type = EV_KEY, .code = key, .value = rel ? 0 : 1,
					  .mods = input_modifiers(), .time_ns = time_ns() };
		if (!rel && key < 128) {
			u32 ch = key;
			bool sh = shift_l || shift_r;
			if (ch >= 'a' && ch <= 'z') {
				if (sh ^ caps)
					ch -= 32;
			} else if (sh && shifted[ch]) {
				ch = (u32)shifted[ch];
			}
			if (ctrl && ((ch | 0x20) >= 'a' && (ch | 0x20) <= 'z'))
				ch = (ch | 0x20) - 'a' + 1; /* ^C = 3 etc. */
			ev.ch = ch;
		}
		input_report(&ev);
	}
}

/* Both IRQ lines drain the shared output buffer; bit 5 of the status
 * register says which device each byte came from. */
static void ps2_drain(struct trap_frame *tf, void *ctx)
{
	for (int guard = 0; guard < 64; guard++) {
		u8 st = inb(STATUS);
		if (!(st & 1))
			break;
		u8 b = inb(DATA);
		if (st & 0x20)
			mouse_byte(b);
		else
			kbd_byte(b);
	}
}

static void mouse_byte(u8 b)
{
	{
		if (mpos == 0 && !(b & 0x08))
			return; /* resynchronise on the always-set bit */
		mpacket[mpos++] = b;
		if (mpos < mpacket_len)
			return;
		mpos = 0;
		i32 dx = (i32)mpacket[1] - ((mpacket[0] & 0x10) ? 256 : 0);
		i32 dy = (i32)mpacket[2] - ((mpacket[0] & 0x20) ? 256 : 0);
		u64 now = time_ns();
		if (dx || dy) {
			struct input_event ev = { .type = EV_POINTER, .dx = dx, .dy = -dy,
						  .mods = input_modifiers(), .time_ns = now };
			input_report(&ev);
		}
		u8 btn = mpacket[0] & 7;
		for (int i = 0; i < 3; i++) {
			u8 bit = (u8)(1 << i);
			if ((btn ^ mbuttons) & bit) {
				static const u16 map[3] = { 1, 2, 3 };
				struct input_event ev = { .type = EV_BUTTON, .code = map[i],
							  .value = (btn & bit) ? 1 : 0,
							  .mods = input_modifiers(), .time_ns = now };
				input_report(&ev);
			}
		}
		mbuttons = btn;
		if (mpacket_len == 4 && mpacket[3]) {
			i32 w = (i8)(mpacket[3] << 4) >> 4;
			struct input_event ev = { .type = EV_SCROLL, .dy = -w, .time_ns = now };
			input_report(&ev);
		}
	}
}

void ps2_init(void)
{
	ctl_cmd(0xAD); /* disable keyboard */
	ctl_cmd(0xA7); /* disable mouse */
	while (inb(STATUS) & 1)
		inb(DATA);
	ctl_cmd(0x20);
	u8 cfg = wait_read() ? inb(DATA) : 0;
	cfg |= 0x01 | 0x02 | 0x40; /* IRQ1, IRQ12, set-1 translation */
	cfg &= (u8)~0x30;	    /* clocks enabled */
	ctl_cmd(0x60);
	ctl_data(cfg);
	ctl_cmd(0xAE);
	ctl_cmd(0xA8);

	/* Mouse: defaults, try the IntelliMouse wheel sequence, enable reporting. */
	if (mouse_cmd(0xF6) == 0xFA) {
		mouse_cmd(0xF3); mouse_cmd(200);
		mouse_cmd(0xF3); mouse_cmd(100);
		mouse_cmd(0xF3); mouse_cmd(80);
		mouse_cmd(0xF2);
		u8 id = wait_read() ? inb(DATA) : 0;
		mpacket_len = id == 3 ? 4 : 3;
		mouse_ok = mouse_cmd(0xF4) == 0xFA;
	}
	while (inb(STATUS) & 1)
		inb(DATA);
	irq_register(VEC_KEYBOARD, ps2_drain, NULL);
	irq_register(VEC_MOUSE, ps2_drain, NULL);
	ioapic_route_isa(1, VEC_KEYBOARD);
	ioapic_route_isa(12, VEC_MOUSE);
	KLOG("ps2", "keyboard ready, mouse %s (%d-byte packets%s)", mouse_ok ? "ready" : "absent",
	     mpacket_len, mpacket_len == 4 ? ", wheel" : "");
}
