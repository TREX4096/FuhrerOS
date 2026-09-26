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
	ngot = 0;
	swipe(4, 0, -900, 8, 0);
	selftest_report("gesture.four_swipe_up", saw(EV_GESTURE, GESTURE_FOUR_SWIPE_UP), "%d events", ngot);
	ngot = 0;
	swipe(3, 0, 0, 1, 0);
	bool t3 = saw(EV_GESTURE, GESTURE_THREE_TAP);
	ngot = 0;
	swipe(4, 0, 0, 1, 0);
	selftest_report("gesture.three_and_four_finger_tap", t3 && saw(EV_GESTURE, GESTURE_FOUR_TAP),
			"%d events", ngot);

	/* options: natural scrolling inverts, tap-to-click off suppresses clicks,
	 * a key press blocks the touchpad for typing_block_ms */
	struct input_options o = { .tap_to_click = true, .natural_scroll = false, .typing_block_ms = 0,
				   .pointer_speed = 4, .scroll_speed = 4 };
	input_set_options(&o);
	ngot = 0;
	swipe(2, 0, 600, 8, 0);
	i32 plain = 0;
	for (int i = 0; i < ngot; i++)
		if (got[i].type == EV_GESTURE && got[i].code == GESTURE_SCROLL)
			plain += got[i].dy;
	o.natural_scroll = true;
	input_set_options(&o);
	ngot = 0;
	swipe(2, 0, 600, 8, 0);
	i32 natural = 0;
	for (int i = 0; i < ngot; i++)
		if (got[i].type == EV_GESTURE && got[i].code == GESTURE_SCROLL)
			natural += got[i].dy;
	selftest_report("gesture.natural_scroll", plain > 0 && natural == -plain, "dy %d vs %d", plain,
			natural);
	o.natural_scroll = false;
	o.tap_to_click = false;
	input_set_options(&o);
	ngot = 0;
	swipe(1, 0, 0, 1, 0);
	bool no_click = !saw(EV_BUTTON, 1);
	o.tap_to_click = true;
	o.typing_block_ms = 250;
	input_set_options(&o);
	struct input_event key = { .type = EV_KEY, .code = 'a', .value = 1, .time_ns = time_ns() };
	input_report(&key);
	ngot = 0;
	swipe(1, 200, 120, 6, 0);
	selftest_report("gesture.tap_off_and_typing_block", no_click && !saw(EV_POINTER, 0),
			"tap-to-click off: no click; touch right after a key ignored");
	o.typing_block_ms = 250;
	input_set_options(&o); /* defaults */

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
