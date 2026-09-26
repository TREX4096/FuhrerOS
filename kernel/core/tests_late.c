/* Late self tests (run by kinit after devices and filesystems are up):
 * gesture recognizer with synthetic touch frames (QEMU has no multi-touch
 * device, D-117) and the buffer cache policy switch. */
#include "blk.h"
#include "input.h"
#include "kernel.h"
#include "sched.h"

static struct input_event got[64];
static int ngot;

static void capture(const struct input_event *ev)
{
	if (ngot < 64)
		got[ngot++] = *ev;
}

static bool saw(u16 type, u16 code)
{
	for (int i = 0; i < ngot; i++)
		if (got[i].type == type && got[i].code == code)
			return true;
	return false;
}

/* Feed a gesture: `fingers` contacts moving by (dx,dy) over `steps` frames. */
static void swipe(int fingers, int dx, int dy, int steps, int spread_delta)
{
	struct touch_contact c[4];
	for (int s = 0; s <= steps; s++) {
		for (int f = 0; f < fingers; f++) {
			int spread = 200 + (spread_delta * s) / (steps ? steps : 1);
			c[f].id = (u16)f;
			c[f].x = (u16)(1500 + f * spread + dx * s / (steps ? steps : 1));
			c[f].y = (u16)(2000 + dy * s / (steps ? steps : 1));
			c[f].down = true;
		}
		input_touch_frame(c, fingers);
	}
	for (int f = 0; f < fingers; f++)
		c[f].down = false;
	input_touch_frame(c, fingers);
}

static void test_gestures(void)
{
	input_set_sink(capture);

	ngot = 0;
	swipe(3, 0, -800, 8, 0);
	selftest_report("gesture.three_swipe_up", saw(EV_GESTURE, GESTURE_THREE_SWIPE_UP),
			"%d events", ngot);
	ngot = 0;
	swipe(4, 900, 0, 8, 0);
	selftest_report("gesture.four_swipe_right", saw(EV_GESTURE, GESTURE_FOUR_SWIPE_RIGHT),
			"%d events", ngot);
	ngot = 0;
	swipe(2, 0, 600, 8, 0);
	selftest_report("gesture.two_finger_scroll",
			saw(EV_GESTURE, GESTURE_SCROLL) && saw(EV_SCROLL, 0), "%d events", ngot);
	ngot = 0;
	swipe(2, 0, 0, 8, 400);
	selftest_report("gesture.pinch", saw(EV_GESTURE, GESTURE_PINCH), "%d events", ngot);
	ngot = 0;
	swipe(2, 0, 0, 1, 0); /* quick, no motion */
	selftest_report("gesture.two_finger_tap_right_click",
			saw(EV_GESTURE, GESTURE_TWO_FINGER_TAP) && saw(EV_BUTTON, 2), "%d events", ngot);
	ngot = 0;
	swipe(1, 200, 120, 6, 0);
	selftest_report("gesture.one_finger_pointer", saw(EV_POINTER, 0), "%d events", ngot);

	extern void tty_key_event(const struct input_event *ev);
	input_set_sink(tty_key_event); /* give input back to the console */
}

static void test_cache_policies(void)
{
	const char *names[] = { "lru", "fifo", "clock", "readahead", "adaptive" };
	bool ok = true;
	for (int i = 0; i < 5; i++)
		ok &= bcache_set_policy(names[i]) == 0 && !strcmp(bcache_policy_name(), names[i]);
	ok &= bcache_set_policy("nonsense") < 0;
	selftest_report("bcache.policies", ok, "5 policies selectable, unknown rejected");
}

void run_late_selftests(void)
{
	test_gestures();
	if (blk_get("vda"))
		test_cache_policies();
}
