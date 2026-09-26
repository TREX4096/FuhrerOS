/* Input subsystem: devices -> events -> (tty | compositor), with a gesture
 * recognizer in between for pointer devices (NEW_EXPLANATION §27). */
#ifndef FUHRER_INPUT_H
#define FUHRER_INPUT_H

#include "types.h"

enum input_type {
	EV_KEY = 1,		/* code = keycode, value = 1 press / 0 release */
	EV_POINTER = 2,		/* dx, dy relative motion */
	EV_BUTTON = 3,		/* code = button (1 left, 2 right, 3 middle), value */
	EV_SCROLL = 4,		/* dy = wheel / two-finger scroll */
	EV_GESTURE = 5,		/* code = gesture id, dx/dy = direction */
	EV_TOUCH = 6,		/* multi-touch frame: code = contact count, dx/dy = centroid delta */
};

/* Keycodes: printable keys use their unshifted ASCII value. */
enum {
	KEY_BACKSPACE = 8, KEY_TAB = 9, KEY_ENTER = 10, KEY_ESC = 27,
	KEY_UP = 0x100, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN,
	KEY_INSERT, KEY_DELETE,
	KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10,
	KEY_F11, KEY_F12,
	KEY_LSHIFT, KEY_RSHIFT, KEY_LCTRL, KEY_RCTRL, KEY_LALT, KEY_RALT, KEY_SUPER, KEY_CAPS,
	KEY_MENU,
};

#define MOD_SHIFT 0x1
#define MOD_CTRL 0x2
#define MOD_ALT 0x4
#define MOD_SUPER 0x8

enum gesture {
	GESTURE_NONE,
	GESTURE_SCROLL,			/* 2 fingers move */
	GESTURE_PINCH,			/* 2 fingers spread/close: dy = scale delta */
	GESTURE_TWO_FINGER_TAP,		/* right click */
	GESTURE_THREE_SWIPE_UP,		/* overview */
	GESTURE_THREE_SWIPE_DOWN,	/* show desktop */
	GESTURE_THREE_SWIPE_LEFT,	/* app switch */
	GESTURE_THREE_SWIPE_RIGHT,
	GESTURE_FOUR_SWIPE_LEFT,	/* workspace switch */
	GESTURE_FOUR_SWIPE_RIGHT,
};

struct input_event {
	u16 type;
	u16 code;
	i32 value;
	i32 dx, dy;
	u16 mods;
	u32 ch;			/* translated character for EV_KEY presses (0 if none) */
	u64 time_ns;
};

/* Touch contact as reported by a touchpad driver (absolute, 0..4095). */
struct touch_contact {
	u16 id;
	u16 x, y;
	bool down;
};

void input_init(void);
void input_report(const struct input_event *ev);	/* from drivers (IRQ context ok) */
void input_touch_frame(const struct touch_contact *c, int n);	/* -> gesture recognizer */
/* The consumer: the compositor when the desktop runs, else the console tty. */
typedef void (*input_sink_t)(const struct input_event *ev);
void input_set_sink(input_sink_t sink);
u16 input_modifiers(void);
const char *gesture_name(int g);

void ps2_init(void);
#endif
