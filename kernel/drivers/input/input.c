/* Input core: event routing and the touchpad gesture recognizer.
 *
 * Pipeline (NEW_EXPLANATION §27):  device -> contacts -> recognizer -> events
 *   1 finger  : pointer motion; quick tap = left click
 *   2 fingers : scroll (parallel motion), pinch (distance change),
 *               quick tap = right click
 *   3 fingers : swipe up/down/left/right, tap
 *   4 fingers : swipe up/down/left/right, tap
 * What 3/4-finger gestures do is decided by the desktop (Settings ->
 * Touchpad). Options: tap-to-click, natural scrolling, pointer/scroll speed,
 * and ignoring the touchpad for a moment after typing (accidental palm
 * contact while typing; the contacts carry no size, so real palm detection
 * is not possible with this interface).
 * QEMU exposes no multi-touch device, so the recognizer is exercised by the
 * self-test with synthetic contact frames; PS/2 mice bypass it. */
#include "arch/x86_64/cpu.h"
#include "input.h"
#include "kernel.h"

static input_sink_t sink;
static u64 events_total;
static u64 last_key_ns;
static struct input_options opt = { .tap_to_click = true, .natural_scroll = false,
				    .typing_block_ms = 250, .pointer_speed = 4, .scroll_speed = 4 };

void input_set_sink(input_sink_t s) { sink = s; }
void input_set_options(const struct input_options *o) { opt = *o; }

void input_report(const struct input_event *ev)
{
	events_total++;
	if (ev->type == EV_KEY && ev->value)
		last_key_ns = ev->time_ns ? ev->time_ns : time_ns();
	if (sink)
		sink(ev);
}

static const char *gnames[] = { "none", "scroll", "pinch", "two-finger-tap", "3-swipe-up",
				"3-swipe-down", "3-swipe-left", "3-swipe-right", "4-swipe-left",
				"4-swipe-right", "4-swipe-up", "4-swipe-down", "3-tap", "4-tap" };
const char *gesture_name(int g) { return g >= 0 && g < (int)ARRAY_LEN(gnames) ? gnames[g] : "?"; }

/* ---- gesture recognizer ---- */
static struct {
	int fingers;		/* max contacts in the current touch sequence */
	int cur;		/* contacts in the last frame */
	i32 cx, cy;		/* last centroid */
	i32 sx, sy;		/* centroid at sequence start */
	i32 spread0, spread;	/* two-finger distance (L1) at start / now */
	u64 t0;
	bool moved;
	bool decided;		/* swipe already emitted for this sequence */
	int mode;		/* 0 undecided, 1 scroll, 2 pinch */
} g;

static void emit(u16 type, u16 code, i32 value, i32 dx, i32 dy)
{
	struct input_event ev = { .type = type, .code = code, .value = value, .dx = dx, .dy = dy,
				  .mods = input_modifiers(), .time_ns = time_ns() };
	input_report(&ev);
}

static i32 iabs(i32 v) { return v < 0 ? -v : v; }

void input_touch_frame(const struct touch_contact *c, int n)
{
	int down = 0;
	i32 x = 0, y = 0;
	for (int i = 0; i < n; i++)
		if (c[i].down) {
			x += c[i].x;
			y += c[i].y;
			down++;
		}
	u64 now = time_ns();
	/* disable-while-typing: drop whole sequences that start just after a key */
	if (opt.typing_block_ms && g.fingers == 0 && down &&
	    now - last_key_ns < (u64)opt.typing_block_ms * 1000000ULL)
		return;
	if (down == 0) {
		/* sequence ended: taps */
		if (g.fingers && !g.moved && now - g.t0 < 250000000ULL) {
			if (g.fingers == 1 && opt.tap_to_click)
				emit(EV_BUTTON, 1, 1, 0, 0), emit(EV_BUTTON, 1, 0, 0, 0);
			else if (g.fingers == 2 && opt.tap_to_click) {
				emit(EV_GESTURE, GESTURE_TWO_FINGER_TAP, 1, 0, 0);
				emit(EV_BUTTON, 2, 1, 0, 0), emit(EV_BUTTON, 2, 0, 0, 0);
			} else if (g.fingers == 3) {
				emit(EV_GESTURE, GESTURE_THREE_TAP, 1, 0, 0);
			} else if (g.fingers >= 4) {
				emit(EV_GESTURE, GESTURE_FOUR_TAP, 1, 0, 0);
			}
		}
		memset(&g, 0, sizeof(g));
		return;
	}
	x /= down;
	y /= down;
	i32 spread = 0;
	if (down >= 2) {
		int a = -1, b = -1;
		for (int i = 0; i < n; i++)
			if (c[i].down) {
				if (a < 0) a = i; else if (b < 0) b = i;
			}
		spread = iabs((i32)c[a].x - c[b].x) + iabs((i32)c[a].y - c[b].y);
	}
	if (g.fingers == 0 || down != g.cur) {
		/* new sequence or finger count changed: restart tracking */
		if (g.fingers == 0)
			g.t0 = now;
		g.cx = g.sx = x;
		g.cy = g.sy = y;
		g.spread0 = g.spread = spread;
		g.mode = 0;
		g.cur = down;
		if (down > g.fingers)
			g.fingers = down;
		return;
	}
	i32 dx = x - g.cx, dy = y - g.cy;
	g.cx = x;
	g.cy = y;
	if (iabs(x - g.sx) + iabs(y - g.sy) > 40)
		g.moved = true;
	switch (down) {
	case 1:
		if (dx || dy)
			emit(EV_POINTER, 0, 0, dx * opt.pointer_speed / 16, dy * opt.pointer_speed / 16);
		break;
	case 2: {
		i32 ds = spread - g.spread;
		g.spread = spread;
		if (g.mode == 0 && (iabs(spread - g.spread0) > 120 || iabs(y - g.sy) + iabs(x - g.sx) > 60))
			g.mode = iabs(spread - g.spread0) > 120 ? 2 : 1;
		if (g.mode == 1 && (dx || dy)) {
			/* natural scrolling: content follows the fingers */
			i32 sdx = opt.natural_scroll ? -dx : dx, sdy = opt.natural_scroll ? -dy : dy;
			emit(EV_GESTURE, GESTURE_SCROLL, 0, sdx, sdy);
			emit(EV_SCROLL, 0, 0, sdx * opt.scroll_speed / 64, sdy * opt.scroll_speed / 64);
		} else if (g.mode == 2 && ds) {
			emit(EV_GESTURE, GESTURE_PINCH, 0, 0, ds);
		}
		g.moved |= g.mode != 0;
		break;
	}
	case 3:
	case 4: {
		if (g.decided)
			break;
		i32 tx = x - g.sx, ty = y - g.sy;
		if (iabs(tx) < 400 && iabs(ty) < 400)
			break;
		int gest;
		if (down == 3)
			gest = iabs(ty) > iabs(tx) ? (ty < 0 ? GESTURE_THREE_SWIPE_UP : GESTURE_THREE_SWIPE_DOWN)
						   : (tx < 0 ? GESTURE_THREE_SWIPE_LEFT : GESTURE_THREE_SWIPE_RIGHT);
		else
			gest = iabs(ty) > iabs(tx) ? (ty < 0 ? GESTURE_FOUR_SWIPE_UP : GESTURE_FOUR_SWIPE_DOWN)
						   : (tx < 0 ? GESTURE_FOUR_SWIPE_LEFT : GESTURE_FOUR_SWIPE_RIGHT);
		emit(EV_GESTURE, (u16)gest, 1, tx, ty);
		g.decided = true;
		break;
	}
	}
}

void input_init(void)
{
	memset(&g, 0, sizeof(g));
}
