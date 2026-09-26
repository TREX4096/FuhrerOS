/* Fuhrer compositor + window manager + desktop shell (M10, M11, M12;
 * visual design: UI_SUGGESTION.md, decisions UI-D-001.. in docs/ui-design.md).
 *
 *   framebuffer <- back buffer <- (wallpaper cache, windows, top bar, launcher,
 *                                  switcher, toasts, cursor)
 *
 * Windows belong to processes; each has a pixel surface mapped both here and
 * into the owner's address space (apps draw directly, then "present").
 * Rendering happens in the `compositor` kernel thread, only when something
 * changed, and only the damaged rectangle is copied to the framebuffer.
 *
 * Desktop: a compact top bar (logo/launcher, named workspaces, status,
 * clock), up to 9 workspaces each in floating, tiling (master/stack with
 * ratio, orientation, swap, per-window float) or focus mode, a searchable
 * launcher / command palette, an Alt+Tab switcher, an overview, a lock
 * screen, non-blocking notifications (optionally for adaptive-kernel class
 * changes), dark/light themes with an accent colour, and touchpad gestures
 * whose actions are configurable (Settings -> Touchpad). */
#include "arch/x86_64/cpu.h"
#include "blk.h"
#include "gfx/fb.h"
#include "input.h"
#include "kernel.h"
#include "mm.h"
#include "net.h"
#include "proc.h"
#include "sched.h"
#include "uapi/fuhrer.h"
#include "vfs.h"

#define MAX_WIN 32
#define TITLE_H 30
#define BORDER 1
#define RADIUS 10
#define BAR_H 32
#define EVQ 64
#define GAP 8
#define WIN_USER_BASE 0x0000300000000000ULL
#define WIN_USER_STRIDE (64ULL << 20)

/* ---- theme ---- */
struct theme {
	u32 wall0, wall1, bar, chip, chip_hi, surface, surface_hi, title, title_focus, border, text,
		text_dim, accent, close, good, warn, bad;
};
static struct theme T;
static const u32 accents[] = { RGB(0xe8, 0x2a, 0x36), RGB(0x4f, 0x9d, 0xff), RGB(0x2e, 0xc4, 0xa8),
			       RGB(0xf2, 0xa9, 0x3b), RGB(0xa7, 0x7b, 0xf3) };
static const char *const accent_names[] = { "Fuhrer red", "Blue", "Teal", "Amber", "Violet" };
#define NACCENT ((int)ARRAY_LEN(accents))
static const char *const wallpaper_names[] = { "Graphite", "Midnight", "Ember" };

static struct fu_desk_cfg cfg = {
	.dark = 1, .accent = 0, .wallpaper = 0, .workspaces = 5, .tap_to_click = 1, .natural_scroll = 0,
	.adapt_notify = 0, .reduce_motion = 0, .typing_block_ms = 250, .pointer_speed = 4, .scroll_speed = 4,
	.gesture = { [GS_THREE_LEFT] = GA_WS_PREV, [GS_THREE_RIGHT] = GA_WS_NEXT, [GS_THREE_UP] = GA_OVERVIEW,
		     [GS_THREE_DOWN] = GA_DESKTOP, [GS_THREE_TAP] = GA_LAUNCHER, [GS_FOUR_LEFT] = GA_WS_PREV,
		     [GS_FOUR_RIGHT] = GA_WS_NEXT, [GS_FOUR_UP] = GA_OVERVIEW, [GS_FOUR_DOWN] = GA_DESKTOP,
		     [GS_FOUR_TAP] = GA_CONTROL },
	.ws_name = { "General", "Development", "Browser", "Research", "Misc", "Six", "Seven", "Eight", "Nine" },
};

static u32 mix(u32 a, u32 b, u32 t) /* t/255 of b over a */
{
	u32 r = (((a >> 16) & 255) * (255 - t) + ((b >> 16) & 255) * t) / 255;
	u32 g = (((a >> 8) & 255) * (255 - t) + ((b >> 8) & 255) * t) / 255;
	u32 bl = ((a & 255) * (255 - t) + (b & 255) * t) / 255;
	return RGB(r, g, bl);
}

static void theme_build(void)
{
	u32 acc = accents[cfg.accent % NACCENT];
	if (cfg.dark) {
		T = (struct theme){ RGB(0x0d, 0x0f, 0x14), RGB(0x17, 0x1b, 0x24), RGB(0x0c, 0x0e, 0x13),
				    RGB(0x1b, 0x1f, 0x28), RGB(0x27, 0x2d, 0x39), RGB(0x16, 0x19, 0x21),
				    RGB(0x22, 0x27, 0x32), RGB(0x15, 0x18, 0x1f), RGB(0x1c, 0x20, 0x29),
				    RGB(0x2b, 0x30, 0x3b), RGB(0xe7, 0xe9, 0xee), RGB(0x8b, 0x93, 0xa5), acc,
				    RGB(0xe5, 0x48, 0x4d), RGB(0x6f, 0xcf, 0x8a), RGB(0xf2, 0xc1, 0x5b),
				    RGB(0xe5, 0x48, 0x4d) };
	} else {
		T = (struct theme){ RGB(0xe8, 0xea, 0xee), RGB(0xd6, 0xda, 0xe1), RGB(0xf7, 0xf8, 0xfa),
				    RGB(0xe6, 0xe8, 0xec), RGB(0xd4, 0xd8, 0xe0), RGB(0xff, 0xff, 0xff),
				    RGB(0xee, 0xf0, 0xf3), RGB(0xef, 0xf1, 0xf4), RGB(0xfc, 0xfc, 0xfd),
				    RGB(0xc6, 0xcb, 0xd4), RGB(0x1a, 0x1e, 0x26), RGB(0x5b, 0x63, 0x72), acc,
				    RGB(0xd9, 0x3b, 0x41), RGB(0x2f, 0x9e, 0x57), RGB(0xb7, 0x80, 0x12),
				    RGB(0xd9, 0x3b, 0x41) };
	}
	if (cfg.wallpaper % 3 == 1) { /* Midnight: blue-tinted */
		T.wall0 = mix(T.wall0, RGB(0x10, 0x1c, 0x3a), cfg.dark ? 150 : 60);
		T.wall1 = mix(T.wall1, RGB(0x1c, 0x14, 0x33), cfg.dark ? 140 : 50);
	} else if (cfg.wallpaper % 3 == 2) { /* Ember: warm glow */
		T.wall1 = mix(T.wall1, RGB(0x3a, 0x10, 0x14), cfg.dark ? 170 : 50);
	}
}

/* ---- windows ---- */
enum wstate { W_NORMAL, W_MIN, W_MAX, W_SNAP_L, W_SNAP_R, W_TILED };

struct window {
	bool used;
	int id;
	pid_t owner;
	char title[48];
	int x, y, w, h;		/* client area */
	int rx, ry, rw, rh;	/* restore geometry */
	enum wstate state;
	int ws;			/* workspace */
	bool floating;		/* excluded from tiling */
	u32 seq;		/* tile order (lower = earlier = master) */
	struct surface surf;	/* kernel view of the pixels */
	paddr_t *frames;
	u64 nframes;
	vaddr_t user_va;
	struct fu_wevent q[EVQ];
	u32 qh, qt;
	struct waitqueue wq;
	u64 last_present;
	u32 presents;
};

static struct window wins[MAX_WIN];
static int zorder[MAX_WIN];	/* bottom .. top, indices into wins */
static int nz;
static int focus = -1;
static u32 next_seq;
static struct surface back;	/* composition buffer (RAM) */
static struct surface wallc;	/* pre-rendered wallpaper */
static bool wall_valid;
static struct surface *screen;
static bool running, dirty_all;
static int dx0, dy0, dx1, dy1;	/* damage rectangle */
static int mx, my;		/* pointer */
static bool btn_down;
static int drag = -1, resize = -1, drag_ox, drag_oy;
static int cur_ws;
static u8 ws_mode[DESK_MAX_WS];
static u8 ws_ratio[DESK_MAX_WS];	/* master share, percent */
static u8 ws_orient[DESK_MAX_WS];	/* 0 master left, 1 master top */
static bool menu_open, overview, locked, show_desktop;
static bool super_down, super_used;	/* Super alone (press+release) = launcher */
static bool switcher;			/* Alt+Tab overlay while Alt is held */
static int switch_sel;
static struct waitqueue comp_wq;
static struct mutex comp_lock;	/* window list/surfaces vs. composition */
static u64 frames_drawn, last_clock_s;
static u64 compose_ns_total, compose_ns_max;
static u64 input_pending_ns, input_lat_total, input_lat_max, input_lat_n;
static u64 ws_switch_pending_ns, ws_switch_last_us;

static int work_y(void) { return BAR_H; }
static int work_h(void) { return (int)screen->h - BAR_H; }

static void damage(int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0)
		return;
	if (dx1 <= dx0) {
		dx0 = x; dy0 = y; dx1 = x + w; dy1 = y + h;
	} else {
		dx0 = MIN(dx0, x); dy0 = MIN(dy0, y);
		dx1 = MAX(dx1, x + w); dy1 = MAX(dy1, y + h);
	}
}
static void damage_all(void) { dirty_all = true; }
static void kick(void) { wq_wake_one(&comp_wq); }

static struct window *win_by_id(int id)
{
	if (id < 0 || id >= MAX_WIN || !wins[id].used)
		return NULL;
	return &wins[id];
}

static void push_event(struct window *w, struct fu_wevent ev)
{
	ev.win = w->id;
	if (w->qh - w->qt >= EVQ)
		w->qt++; /* drop oldest */
	w->q[w->qh++ % EVQ] = ev;
	wq_wake_all(&w->wq);
}

static void broadcast(u32 type)
{
	for (int i = 0; i < MAX_WIN; i++)
		if (wins[i].used)
			push_event(&wins[i], (struct fu_wevent){ .type = type });
}

static bool visible(struct window *w)
{
	if (!w->used || w->state == W_MIN || w->ws != cur_ws || show_desktop)
		return false;
	if (ws_mode[cur_ws] == MODE_FOCUS && focus >= 0 && wins[focus].ws == cur_ws)
		return w->id == focus;
	return true;
}

static void frame_rect(struct window *w, int *x, int *y, int *fw, int *fh)
{
	*x = w->x - BORDER;
	*y = w->y - TITLE_H - BORDER;
	*fw = w->w + 2 * BORDER;
	*fh = w->h + TITLE_H + 2 * BORDER;
}

static void raise(int idx)
{
	int pos = -1;
	for (int i = 0; i < nz; i++)
		if (zorder[i] == idx)
			pos = i;
	if (pos < 0)
		return;
	for (int i = pos; i < nz - 1; i++)
		zorder[i] = zorder[i + 1];
	zorder[nz - 1] = idx;
}

static void retile(void);

static void set_focus(int idx)
{
	if (focus == idx)
		return;
	if (focus >= 0 && wins[focus].used)
		push_event(&wins[focus], (struct fu_wevent){ .type = WEV_FOCUS, .value = 0 });
	focus = idx;
	if (idx >= 0) {
		raise(idx);
		push_event(&wins[idx], (struct fu_wevent){ .type = WEV_FOCUS, .value = 1 });
	}
	if (ws_mode[cur_ws] == MODE_FOCUS)
		retile();
	damage_all();
}

static void focus_top_visible(void)
{
	for (int i = nz - 1; i >= 0; i--) {
		struct window *w = &wins[zorder[i]];
		if (w->used && w->ws == cur_ws && w->state != W_MIN && !show_desktop) {
			set_focus(zorder[i]);
			return;
		}
	}
	focus = -1;
}

static void apply_geometry(struct window *w, int x, int y, int cw, int ch)
{
	cw = MIN(MAX(cw, 120), (int)w->surf.w);
	ch = MIN(MAX(ch, 60), (int)w->surf.h);
	bool resized = cw != w->w || ch != w->h;
	w->x = x;
	w->y = y;
	w->w = cw;
	w->h = ch;
	if (resized)
		push_event(w, (struct fu_wevent){ .type = WEV_RESIZE, .x = cw, .y = ch });
	damage_all();
}

/* place a frame (outer rectangle) */
static void place_frame(struct window *w, int x, int y, int fw, int fh)
{
	apply_geometry(w, x + BORDER, y + TITLE_H + BORDER, fw - 2 * BORDER, fh - TITLE_H - 2 * BORDER);
}

static void save_restore(struct window *w)
{
	if (w->state == W_NORMAL) {
		w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
	}
}

static void set_state(struct window *w, enum wstate s)
{
	if (s != W_NORMAL && s != W_MIN)
		save_restore(w);
	int sw = (int)screen->w, y0 = work_y(), wh = work_h();
	switch (s) {
	case W_MAX: place_frame(w, 0, y0, sw, wh); break;
	case W_SNAP_L: place_frame(w, GAP, y0 + GAP, sw / 2 - GAP - GAP / 2, wh - 2 * GAP); break;
	case W_SNAP_R: place_frame(w, sw / 2 + GAP / 2, y0 + GAP, sw / 2 - GAP - GAP / 2, wh - 2 * GAP); break;
	case W_NORMAL:
		if (w->state != W_MIN && w->rw)
			apply_geometry(w, w->rx, w->ry, w->rw, w->rh);
		break;
	default: break;
	}
	w->state = s;
	if (s == W_MIN && focus == w->id)
		focus_top_visible();
	damage_all();
}

static int tiled_windows(int *idx)
{
	int n = 0;
	for (int i = 0; i < MAX_WIN; i++) {
		struct window *w = &wins[i];
		if (w->used && w->ws == cur_ws && w->state != W_MIN && !w->floating)
			idx[n++] = i;
	}
	for (int a = 1; a < n; a++) /* insertion sort by tile order */
		for (int b = a; b > 0 && wins[idx[b]].seq < wins[idx[b - 1]].seq; b--) {
			int t = idx[b];
			idx[b] = idx[b - 1];
			idx[b - 1] = t;
		}
	return n;
}

/* Arrange the current workspace for its mode. Tiling: the first window in
 * tile order is the master, the others share the stack. */
static void retile(void)
{
	int mode = ws_mode[cur_ws], sw = (int)screen->w, y0 = work_y(), wh = work_h();
	if (mode == MODE_FOCUS) {
		if (focus >= 0 && wins[focus].ws == cur_ws && wins[focus].state != W_MIN) {
			struct window *w = &wins[focus];
			save_restore(w);
			w->state = W_TILED;
			place_frame(w, 0, y0, sw, wh);
		}
		return;
	}
	if (mode != MODE_TILING)
		return;
	int idx[MAX_WIN], n = tiled_windows(idx);
	int ratio = ws_ratio[cur_ws] ? ws_ratio[cur_ws] : 50;
	bool top = ws_orient[cur_ws];
	for (int k = 0; k < n; k++) {
		struct window *w = &wins[idx[k]];
		save_restore(w);
		w->state = W_TILED;
		int ax = GAP, ay = y0 + GAP, aw = sw - 2 * GAP, ah = wh - 2 * GAP;
		if (n == 1) {
			place_frame(w, ax, ay, aw, ah);
			continue;
		}
		if (!top) {
			int mw = aw * ratio / 100;
			if (k == 0) {
				place_frame(w, ax, ay, mw - GAP / 2, ah);
			} else {
				int sh = (ah + GAP) / (n - 1);
				place_frame(w, ax + mw + GAP / 2, ay + (k - 1) * sh, aw - mw - GAP / 2, sh - GAP);
			}
		} else {
			int mh = ah * ratio / 100;
			if (k == 0) {
				place_frame(w, ax, ay, aw, mh - GAP / 2);
			} else {
				int sw2 = (aw + GAP) / (n - 1);
				place_frame(w, ax + (k - 1) * sw2, ay + mh + GAP / 2, sw2 - GAP, ah - mh - GAP / 2);
			}
		}
	}
}

static void set_mode(int mode)
{
	ws_mode[cur_ws] = (u8)mode;
	/* leaving tiling/focus: tiled windows get their floating geometry back */
	if (mode == MODE_FLOATING)
		for (int i = 0; i < MAX_WIN; i++)
			if (wins[i].used && wins[i].ws == cur_ws && wins[i].state == W_TILED) {
				wins[i].state = W_NORMAL;
				if (wins[i].rw)
					apply_geometry(&wins[i], wins[i].rx, wins[i].ry, wins[i].rw, wins[i].rh);
			}
	if (mode == MODE_TILING)
		for (int i = 0; i < MAX_WIN; i++)
			if (wins[i].used && wins[i].ws == cur_ws && wins[i].state == W_TILED)
				wins[i].state = W_NORMAL;
	retile();
	damage_all();
}

/* ---- notifications ---- */
#define NTOAST 4
static struct toast {
	bool used;
	char title[40];
	char body[2][56];
	u64 t;
} toasts[NTOAST];

static void notify(const char *title, const char *l1, const char *l2)
{
	int slot = 0;
	for (int i = 0; i < NTOAST; i++) {
		if (!toasts[i].used) {
			slot = i;
			break;
		}
		if (toasts[i].t < toasts[slot].t)
			slot = i;
	}
	struct toast *t = &toasts[slot];
	t->used = true;
	strlcpy(t->title, title, sizeof(t->title));
	strlcpy(t->body[0], l1 ? l1 : "", sizeof(t->body[0]));
	strlcpy(t->body[1], l2 ? l2 : "", sizeof(t->body[1]));
	t->t = time_ns();
	damage_all();
	kick();
}

/* ---- drawing helpers ---- */
static int corner_inset(int r, int d) /* d: 1..r rows from the rounded edge */
{
	int t = r * r - d * d, q = 0;
	while ((q + 1) * (q + 1) <= t)
		q++;
	return r - q;
}

/* soft shadow: a few translucent layers */
static void shadow(int x, int y, int w, int h, int r)
{
	for (int i = 3; i >= 1; i--)
		surf_blend_fill(&back, x - i * 2 + 2, y - i * 2 + 5, w + i * 4 - 4, h + i * 4 - 4, 0,
				(u8)(cfg.dark ? 26 : 14));
	(void)r;
}

static void text_clip(int x, int y, const char *s, int maxw, u32 c)
{
	char buf[96];
	int n = maxw / FONT_W;
	if (n <= 0)
		return;
	strlcpy(buf, s, sizeof(buf));
	if ((int)strlen(buf) > n) {
		if (n >= 3) {
			buf[n - 2] = '.';
			buf[n - 1] = '.';
		}
		buf[n] = 0;
	}
	surf_text(&back, x, y, buf, c);
}

static void pill(int x, int y, int w, int h, u32 c) { surf_round_rect(&back, x, y, w, h, h / 2, c); }

/* ---- wallpaper (rendered once per theme into wallc) ---- */
static void render_wallpaper(void)
{
	int w = (int)wallc.w, h = (int)wallc.h;
	for (int y = 0; y < h; y++) {
		u32 c = mix(T.wall0, T.wall1, (u32)(y * 255 / (h ? h : 1)));
		u32 *row = wallc.px + (size_t)y * wallc.stride;
		for (int x = 0; x < w; x++)
			row[x] = c;
	}
	/* a faint accent glow behind the logo */
	int cx = w / 2, cy = h / 2 - 20, rr = MIN(w, h) / 2;
	for (int y = MAX(cy - rr, 0); y < MIN(cy + rr, h); y++) {
		u32 *row = wallc.px + (size_t)y * wallc.stride;
		for (int x = MAX(cx - rr * 2, 0); x < MIN(cx + rr * 2, w); x++) {
			int ddx = (x - cx) / 2, ddy = y - cy;
			int d2 = ddx * ddx + ddy * ddy;
			if (d2 >= rr * rr)
				continue;
			u32 a = (u32)(rr * rr - d2) * (cfg.dark ? 28 : 18) / (u32)(rr * rr);
			row[x] = mix(row[x], T.accent, a);
		}
	}
	int lw = logo_width(64);
	surf_logo(&wallc, w / 2 - lw / 2, h / 2 - 52, 64, mix(T.wall1, T.text, cfg.dark ? 150 : 170),
		  mix(T.wall1, T.accent, 200));
	const char *sub = "own kernel  -  workload-aware adaptive scheduling";
	surf_text(&wallc, w / 2 - (int)strlen(sub) * 4, h / 2 + 30, sub, mix(T.wall1, T.text_dim, 200));
	const char *hint = "Super  launcher      Super+Tab  overview      Super+T  layout      Super+F  Control Center";
	surf_text(&wallc, w / 2 - (int)strlen(hint) * 4, h - 40, hint, mix(T.wall1, T.text_dim, 150));
	wall_valid = true;
}

/* ---- windows ---- */
static int title_buttons_x(int fx, int fw) { return fx + fw - 3 * 26 - 8; }

static void draw_window(struct window *w, bool focused)
{
	int x, y, fw, fh;
	frame_rect(w, &x, &y, &fw, &fh);
	bool tiled_full = w->state == W_TILED && ws_mode[cur_ws] == MODE_FOCUS;
	int r = tiled_full || w->state == W_MAX ? 0 : RADIUS;
	if (r)
		shadow(x, y, fw, fh, r);
	surf_round_rect(&back, x, y, fw, fh, r, focused ? T.accent : T.border);
	surf_round_rect(&back, x + 1, y + 1, fw - 2, TITLE_H + r, r ? r - 1 : 0,
			focused ? T.title_focus : T.title);
	/* title: focus dot + text */
	surf_round_rect(&back, x + 14, y + TITLE_H / 2 - 3, 8, 8, 4, focused ? T.accent : T.chip_hi);
	int bx = title_buttons_x(x, fw);
	text_clip(x + 30, y + (TITLE_H - FONT_H) / 2 + 1, w->title, bx - x - 40,
		  focused ? T.text : T.text_dim);
	/* buttons: minimise, maximise, close (hover highlighted) */
	int by = y + (TITLE_H - 22) / 2 + 1;
	for (int b = 0; b < 3; b++) {
		int bxx = bx + b * 26;
		bool hot = mx >= bxx && mx < bxx + 22 && my >= by && my < by + 22;
		u32 fg = focused ? T.text : T.text_dim;
		if (hot) {
			surf_round_rect(&back, bxx, by, 22, 22, 6, b == 2 ? T.close : T.chip_hi);
			if (b == 2)
				fg = RGB(0xff, 0xff, 0xff);
		}
		int cx = bxx + 11, cy = by + 11;
		if (b == 0) {
			surf_fill(&back, cx - 5, cy + 3, 10, 2, fg);
		} else if (b == 1) {
			surf_rect(&back, cx - 5, cy - 5, 10, 10, fg);
			surf_fill(&back, cx - 5, cy - 5, 10, 2, fg);
		} else {
			for (int k = -4; k <= 4; k++) {
				surf_fill(&back, cx + k, cy + k, 2, 1, fg);
				surf_fill(&back, cx + k, cy - k, 2, 1, fg);
			}
		}
	}
	/* client area with rounded bottom corners */
	int rr = r ? r - 1 : 0;
	for (int j = 0; j < w->h; j++) {
		int py = w->y + j;
		if (py < 0 || py >= (int)back.h)
			continue;
		int in = 0;
		if (rr && j >= w->h - rr)
			in = corner_inset(rr, j - (w->h - rr - 1));
		int x0 = MAX(w->x + in, 0), x1 = MIN(w->x + w->w - in, (int)back.w);
		if (x1 <= x0)
			continue;
		memcpy(back.px + (size_t)py * back.stride + x0,
		       w->surf.px + (size_t)j * w->surf.stride + (x0 - w->x), (size_t)(x1 - x0) * 4);
	}
}

/* ---- top bar ---- */
static struct {
	int logo_x1;
	int ws_x0[DESK_MAX_WS], ws_x1[DESK_MAX_WS];
	int mode_x0, mode_x1;
	int status_x0, status_x1;
} bar;

static u64 cpu_pct, ram_pct, net_rx_rate, net_tx_rate;
static void sample_status(void)
{
	static u64 lb, li, last_ns, lrx, ltx;
	struct sched_stats ss;
	sched_get_stats(&ss);
	u64 db = ss.busy_ns - lb, di = ss.idle_ns - li;
	if (db + di < 500000000ULL)
		return;
	cpu_pct = (db * 100) / (db + di);
	lb = ss.busy_ns;
	li = ss.idle_ns;
	struct pmm_stats ps;
	pmm_get_stats(&ps);
	ram_pct = ps.total_frames ? ps.used_frames * 100 / ps.total_frames : 0;
	struct netif *n = net_default_if();
	u64 now = time_ns();
	if (n && last_ns) {
		u64 dt = now - last_ns;
		net_rx_rate = (n->rx_bytes - lrx) * 1000000000ULL / (dt ? dt : 1);
		net_tx_rate = (n->tx_bytes - ltx) * 1000000000ULL / (dt ? dt : 1);
	}
	if (n) {
		lrx = n->rx_bytes;
		ltx = n->tx_bytes;
	}
	last_ns = now;
}

static void fmt_rate(char *b, int n, u64 v)
{
	if (v >= 1048576)
		snprintf(b, (size_t)n, "%lu.%luM", v / 1048576, v % 1048576 * 10 / 1048576);
	else
		snprintf(b, (size_t)n, "%luK", v / 1024);
}

static const char *const mode_names[] = { "floating", "tiling", "focus" };

static void draw_bar(void)
{
	int sw = (int)back.w, cy = (BAR_H - FONT_H) / 2;
	surf_fill(&back, 0, 0, sw, BAR_H, T.bar);
	surf_fill(&back, 0, BAR_H - 1, sw, 1, mix(T.bar, T.border, 160));
	/* logo = launcher button */
	int lw = logo_width(12);
	if (menu_open)
		pill(6, 4, lw + 20, BAR_H - 8, T.chip_hi);
	surf_logo(&back, 16, (BAR_H - 12) / 2, 12, T.text, T.accent);
	bar.logo_x1 = lw + 26;
	/* workspaces */
	int x = bar.logo_x1 + 10;
	for (int i = 0; i < cfg.workspaces; i++) {
		bool any = false;
		for (int k = 0; k < MAX_WIN; k++)
			any |= wins[k].used && wins[k].ws == i;
		char lbl[24];
		if (i == cur_ws)
			snprintf(lbl, sizeof(lbl), "%d %s", i + 1, cfg.ws_name[i]);
		else
			snprintf(lbl, sizeof(lbl), "%d", i + 1);
		int w = (int)strlen(lbl) * FONT_W + 16;
		bar.ws_x0[i] = x;
		bar.ws_x1[i] = x + w;
		if (i == cur_ws)
			pill(x, 5, w, BAR_H - 10, T.accent);
		surf_text(&back, x + 8, cy, lbl, i == cur_ws ? RGB(0xff, 0xff, 0xff) : any ? T.text : T.text_dim);
		if (any && i != cur_ws)
			surf_fill(&back, x + w / 2 - 2, BAR_H - 6, 4, 2, T.accent);
		x += w + 4;
	}
	/* layout mode */
	x += 8;
	char mb[16];
	snprintf(mb, sizeof(mb), "%s", mode_names[ws_mode[cur_ws] % 3]);
	bar.mode_x0 = x;
	bar.mode_x1 = x + (int)strlen(mb) * FONT_W + 16;
	pill(x, 5, bar.mode_x1 - x, BAR_H - 10, T.chip);
	surf_text(&back, x + 8, cy, mb, T.text_dim);
	x = bar.mode_x1 + 12;
	/* clock (far right) */
	int xr = sw - 12;
	char buf[64];
	i64 rem = time_unix() % 86400;
	snprintf(buf, sizeof(buf), "%02ld:%02ld", rem / 3600, (rem / 60) % 60);
	xr -= (int)strlen(buf) * FONT_W;
	surf_text(&back, xr, cy, buf, T.text);
	xr -= 14;
	/* status group -> Control Center */
	char rx[12], tx[12];
	fmt_rate(rx, sizeof(rx), net_rx_rate);
	fmt_rate(tx, sizeof(tx), net_tx_rate);
	enum task_class c = profiler_stable_class();
	char st[128];
	snprintf(st, sizeof(st), "%s: %s   CPU %lu%%   RAM %lu%%   NET %s/%s", sched_policy_name(),
		 task_class_name(c), cpu_pct, ram_pct, rx, tx);
	int stw = (int)strlen(st) * FONT_W + 20;
	bar.status_x1 = xr;
	bar.status_x0 = xr - stw;
	bool hot = my < BAR_H && mx >= bar.status_x0 && mx < bar.status_x1;
	pill(bar.status_x0, 5, stw, BAR_H - 10, hot ? T.chip_hi : T.chip);
	surf_text(&back, bar.status_x0 + 10, cy, st, T.text_dim);
	char head[48];
	snprintf(head, sizeof(head), "%s: %s", sched_policy_name(), task_class_name(c));
	surf_text(&back, bar.status_x0 + 10, cy, head, T.accent);
	/* focused window title in the middle space */
	if (focus >= 0 && wins[focus].used && wins[focus].ws == cur_ws && x < bar.status_x0 - 40)
		text_clip(x, cy, wins[focus].title, bar.status_x0 - 20 - x, T.text_dim);
}

/* ---- launcher / command palette ---- */
enum { A_APP, A_LOCK, A_POWEROFF, A_REBOOT, A_DESKTOP, A_OVERVIEW, A_MODE, A_THEME, A_WALL, A_ACCENT,
       A_SCHED, A_CACHE, A_WS, A_WS_ADD, A_WS_DEL, A_RENAME, A_NOTIFY, A_OPEN_FILE, A_OPEN_DIR,
       A_OPEN_PATH, A_CONTROL };

struct litem {
	char label[64];
	const char *kind;
	int action;
	int iarg;
	char arg[96];
};

static const struct {
	const char *label, *path;
} apps[] = {
	{ "Terminal", "/bin/term" },
	{ "Files", "/bin/files" },
	{ "Fuhrer Control Center", "/bin/control" },
	{ "System Monitor", "/bin/monitor" },
	{ "FuhrerWeb", "/bin/web" },
	{ "Text Editor", "/bin/edit" },
	{ "Settings", "/bin/settings" },
};

static const struct {
	const char *label, *keys;
	int action, iarg;
	const char *arg;
} commands[] = {
	{ "Lock screen", "lock", A_LOCK, 0, NULL },
	{ "Shut down", "shutdown poweroff power off", A_POWEROFF, 0, NULL },
	{ "Reboot", "reboot restart", A_REBOOT, 0, NULL },
	{ "Show desktop", "desktop", A_DESKTOP, 0, NULL },
	{ "Overview", "overview windows expose", A_OVERVIEW, 0, NULL },
	{ "Layout: floating", "floating layout mode", A_MODE, MODE_FLOATING, NULL },
	{ "Layout: tiling", "tiling tile layout mode", A_MODE, MODE_TILING, NULL },
	{ "Layout: focus", "focus layout mode fullscreen", A_MODE, MODE_FOCUS, NULL },
	{ "Theme: dark", "theme dark appearance", A_THEME, 1, NULL },
	{ "Theme: light", "theme light appearance", A_THEME, 0, NULL },
	{ "Next wallpaper", "wallpaper background", A_WALL, 0, NULL },
	{ "Next accent colour", "accent colour color", A_ACCENT, 0, NULL },
	{ "Scheduler: adaptive", "sched scheduler policy cpu", A_SCHED, 0, "adaptive" },
	{ "Scheduler: low latency", "sched scheduler policy cpu", A_SCHED, 0, "low_latency" },
	{ "Scheduler: round robin", "sched scheduler policy cpu", A_SCHED, 0, "round_robin" },
	{ "Scheduler: priority", "sched scheduler policy cpu", A_SCHED, 0, "priority" },
	{ "Cache: adaptive", "cache storage policy disk", A_CACHE, 0, "adaptive" },
	{ "Cache: lru", "cache storage policy disk", A_CACHE, 0, "lru" },
	{ "Cache: read-ahead", "cache storage policy disk", A_CACHE, 0, "readahead" },
	{ "Add workspace", "workspace new add create", A_WS_ADD, 0, NULL },
	{ "Remove last workspace", "workspace remove delete", A_WS_DEL, 0, NULL },
	{ "Adaptive notifications on/off", "notifications adaptive research", A_NOTIFY, 0, NULL },
};

#define MAXITEMS 40
static struct litem items[MAXITEMS];
static int nitems, lsel;
static char query[48];
static int qlen;

/* file index for search (built when the launcher opens) */
#define MAXFILES 192
static struct {
	char path[96];
	const char *name;
	bool dir;
} files[MAXFILES];
static int nfiles;	/* published entries (read from interrupt context) */
static int nidx;	/* entries being built by the compositor thread */

static void index_dir(const char *path, int depth)
{
	struct file *f;
	if (depth > 3 || nidx >= MAXFILES || vfs_open("/", path, O_RDONLY | O_DIRECTORY, &f) < 0)
		return;
	struct dirent de;
	for (u64 i = 0; nidx < MAXFILES && file_readdir(f, i, &de) == 0; i++) {
		if (de.name[0] == '.')
			continue;
		int k = nidx++;
		snprintf(files[k].path, sizeof(files[k].path), "%s/%s", path, de.name);
		files[k].name = files[k].path + strlen(path) + 1;
		files[k].dir = de.type == VT_DIR;
		if (files[k].dir)
			index_dir(files[k].path, depth + 1);
	}
	file_close(f);
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* case-insensitive: 2 = prefix, 1 = substring, 0 = no match */
static int match(const char *hay, const char *needle)
{
	if (!*needle)
		return 1;
	for (int i = 0; hay[i]; i++) {
		int k = 0;
		while (needle[k] && hay[i + k] && lower(hay[i + k]) == lower(needle[k]))
			k++;
		if (!needle[k])
			return i == 0 ? 2 : 1;
	}
	return 0;
}

static struct litem *add_item(const char *label, const char *kind, int action, int iarg, const char *arg)
{
	if (nitems >= MAXITEMS)
		return NULL;
	struct litem *it = &items[nitems++];
	strlcpy(it->label, label, sizeof(it->label));
	it->kind = kind;
	it->action = action;
	it->iarg = iarg;
	strlcpy(it->arg, arg ? arg : "", sizeof(it->arg));
	return it;
}

static void build_items(void)
{
	nitems = 0;
	lsel = 0;
	const char *q = query;
	bool cmd_only = q[0] == '>';
	if (cmd_only) {
		q++;
		while (*q == ' ')
			q++;
	}
	/* typed commands with an argument */
	if (!strncmp(q, "workspace ", 10) || !strncmp(q, "ws ", 3)) {
		const char *a = q[0] == 'w' && q[1] == 's' && q[2] == ' ' ? q + 3 : q + 10;
		int n = (int)strtol(a, NULL, 10);
		if (n >= 1 && n <= cfg.workspaces) {
			char l[64];
			snprintf(l, sizeof(l), "Go to workspace %d (%s)", n, cfg.ws_name[n - 1]);
			add_item(l, "Command", A_WS, n - 1, NULL);
		}
	}
	if (!strncmp(q, "rename ", 7) && q[7]) {
		char l[64];
		snprintf(l, sizeof(l), "Rename workspace %d to \"%s\"", cur_ws + 1, q + 7);
		add_item(l, "Command", A_RENAME, 0, q + 7);
	}
	if (!strncmp(q, "open ", 5) && q[5]) { /* resolved later, in thread context */
		char path[96], l[64];
		if (q[5] == '/')
			strlcpy(path, q + 5, sizeof(path));
		else
			snprintf(path, sizeof(path), "/home/%s", q + 5);
		snprintf(l, sizeof(l), "Open %s", path);
		add_item(l, "Command", A_OPEN_PATH, 0, path);
	}
	for (int pass = 2; pass >= 1; pass--) { /* prefix matches first */
		if (!cmd_only)
			for (int i = 0; i < (int)ARRAY_LEN(apps); i++)
				if (match(apps[i].label, q) == pass)
					add_item(apps[i].label, "App", A_APP, i, apps[i].path);
		for (int i = 0; i < (int)ARRAY_LEN(commands); i++) {
			int m = match(commands[i].label, q);
			if (m != pass && !(pass == 1 && !m && *q && match(commands[i].keys, q)))
				continue;
			if (!*q && !cmd_only)
				continue; /* empty query: apps only */
			add_item(commands[i].label, "Command", commands[i].action, commands[i].iarg,
				 commands[i].arg);
		}
	}
	if (!cmd_only && qlen >= 2)
		for (int i = 0; i < nfiles; i++)
			if (match(files[i].name, q))
				add_item(files[i].path, files[i].dir ? "Folder" : "File",
					 files[i].dir ? A_OPEN_DIR : A_OPEN_FILE, 0, files[i].path);
}

/* Input arrives in interrupt context, where nothing may sleep. Work that
 * can block (spawning a process, reading directories, switching the cache
 * policy) is queued here and done by the compositor thread. */
enum { D_LAUNCH, D_INDEX, D_SCHED, D_CACHE, D_OPEN };
#define NDEFER 8
static struct deferred {
	int kind;
	char path[64];
	char arg[96];
} dq[NDEFER];
static u32 dq_head, dq_tail;

static void defer(int kind, const char *path, const char *arg)
{
	if (dq_head - dq_tail >= NDEFER)
		return; /* full: drop (a user can repeat the action) */
	struct deferred *d = &dq[dq_head++ % NDEFER];
	d->kind = kind;
	strlcpy(d->path, path ? path : "", sizeof(d->path));
	strlcpy(d->arg, arg ? arg : "", sizeof(d->arg));
	kick();
}

static void run_deferred(void)
{
	for (;;) {
		u64 f = irq_save();
		if (dq_tail == dq_head) {
			irq_restore(f);
			return;
		}
		struct deferred d = dq[dq_tail++ % NDEFER];
		irq_restore(f);
		switch (d.kind) {
		case D_LAUNCH: {
			const char *argv[] = { d.path, d.arg[0] ? d.arg : NULL, NULL };
			int r = proc_spawn(d.path, argv, NULL, "/home");
			if (r < 0)
				KLOG("desktop", "cannot launch %s (%d)", d.path, r);
			break;
		}
		case D_INDEX:
			f = irq_save();
			nfiles = 0; /* unpublish while rebuilding */
			irq_restore(f);
			nidx = 0;
			index_dir("/home", 0);
			f = irq_save();
			nfiles = nidx;
			if (menu_open)
				build_items();
			damage_all();
			irq_restore(f);
			break;
		case D_SCHED:
			sched_set_policy(d.arg);
			break;
		case D_CACHE:
			bcache_set_policy(d.arg);
			break;
		case D_OPEN: {
			struct stat st;
			if (vfs_stat("/", d.arg, &st) < 0) {
				f = irq_save();
				notify("Open", "not found:", d.arg);
				irq_restore(f);
				break;
			}
			const char *app = st.type == VT_DIR ? "/bin/files" : "/bin/edit";
			const char *argv[] = { app, d.arg, NULL };
			proc_spawn(app, argv, NULL, "/home");
			break;
		}
		}
	}
}

static void open_launcher(bool on)
{
	menu_open = on;
	if (on) {
		qlen = 0;
		query[0] = 0;
		build_items();
		defer(D_INDEX, NULL, NULL);
	}
	damage_all();
}

static void launch_argv(const char *path, const char *arg) { defer(D_LAUNCH, path, arg); }
static void launch(const char *path) { launch_argv(path, NULL); }

static void focus_or_launch(const char *title, const char *path)
{
	for (int i = 0; i < MAX_WIN; i++)
		if (wins[i].used && !strcmp(wins[i].title, title)) {
			if (wins[i].ws != cur_ws) {
				cur_ws = wins[i].ws;
				retile();
			}
			if (wins[i].state == W_MIN)
				wins[i].state = W_NORMAL;
			show_desktop = false;
			set_focus(i);
			return;
		}
	launch(path);
}

static void apply_cfg(bool theme_changed);
static void switch_workspace(int ws);

static void run_item(struct litem *it)
{
	switch (it->action) {
	case A_APP:
		if (!strcmp(it->arg, "/bin/control")) /* one Control Center: bring it forward */
			focus_or_launch("Fuhrer Control Center", "/bin/control");
		else
			launch(it->arg);
		break;
	case A_LOCK: locked = true; break;
	case A_POWEROFF: launch("/bin/poweroff"); break;
	case A_REBOOT: launch_argv("/bin/poweroff", "-r"); break;
	case A_DESKTOP: show_desktop = true; break;
	case A_OVERVIEW: overview = true; break;
	case A_MODE: set_mode(it->iarg); break;
	case A_THEME: cfg.dark = (u8)it->iarg; apply_cfg(true); break;
	case A_WALL: cfg.wallpaper = (u8)((cfg.wallpaper + 1) % 3); apply_cfg(true); break;
	case A_ACCENT: cfg.accent = (u8)((cfg.accent + 1) % NACCENT); apply_cfg(true); break;
	case A_SCHED:
		defer(D_SCHED, NULL, it->arg);
		notify("Scheduler", "policy set to", it->arg);
		break;
	case A_CACHE:
		defer(D_CACHE, NULL, it->arg);
		notify("Buffer cache", "policy set to", it->arg);
		break;
	case A_WS: switch_workspace(it->iarg); break;
	case A_WS_ADD:
		if (cfg.workspaces < DESK_MAX_WS)
			cfg.workspaces++;
		break;
	case A_WS_DEL:
		if (cfg.workspaces > 1) {
			int last = --cfg.workspaces;
			for (int i = 0; i < MAX_WIN; i++)
				if (wins[i].used && wins[i].ws >= last)
					wins[i].ws = last - 1;
			if (cur_ws >= last)
				switch_workspace(last - 1);
		}
		break;
	case A_RENAME: strlcpy(cfg.ws_name[cur_ws], it->arg, sizeof(cfg.ws_name[0])); break;
	case A_NOTIFY:
		cfg.adapt_notify = !cfg.adapt_notify;
		notify("Adaptive notifications", cfg.adapt_notify ? "on" : "off", NULL);
		break;
	case A_OPEN_FILE: launch_argv("/bin/edit", it->arg); break;
	case A_OPEN_DIR: launch_argv("/bin/files", it->arg); break;
	case A_OPEN_PATH: defer(D_OPEN, NULL, it->arg); break;
	case A_CONTROL: focus_or_launch("Fuhrer Control Center", "/bin/control"); break;
	}
	damage_all();
}

#define L_W 620
#define L_ROW 34
#define L_ROWS 9
static int launcher_x(void) { return ((int)back.w - L_W) / 2; }
static int launcher_y(void) { return BAR_H + (int)back.h / 8; }

static void draw_launcher(void)
{
	surf_blend_fill(&back, 0, BAR_H, (int)back.w, (int)back.h - BAR_H, 0, cfg.dark ? 90 : 50);
	int x = launcher_x(), y = launcher_y(), rows = MIN(nitems, L_ROWS);
	int h = 64 + MAX(rows, 1) * L_ROW + 36;
	shadow(x, y, L_W, h, 14);
	surf_round_rect(&back, x, y, L_W, h, 14, T.border);
	surf_round_rect(&back, x + 1, y + 1, L_W - 2, h - 2, 13, T.surface);
	/* search field */
	surf_round_rect(&back, x + 14, y + 14, L_W - 28, 36, 10, T.surface_hi);
	int gx = x + 32, gy = y + 32; /* magnifier */
	for (int a = -5; a <= 5; a++)
		for (int b = -5; b <= 5; b++) {
			int d = a * a + b * b;
			if (d >= 16 && d <= 30)
				surf_fill(&back, gx + a, gy + b - 2, 1, 1, T.text_dim);
		}
	for (int k = 0; k < 4; k++)
		surf_fill(&back, gx + 4 + k, gy + 2 + k, 2, 2, T.text_dim);
	if (qlen)
		surf_text(&back, x + 52, y + 24, query, T.text);
	else
		surf_text(&back, x + 52, y + 24, "Search applications, files or commands  ( > for commands )",
			  T.text_dim);
	surf_fill(&back, x + 52 + qlen * FONT_W, y + 23, 2, 18, T.accent); /* caret */
	/* results */
	int first = lsel >= L_ROWS ? lsel - L_ROWS + 1 : 0;
	for (int r = 0; r < rows; r++) {
		int i = first + r;
		int iy = y + 60 + r * L_ROW;
		bool hot = mx >= x && mx < x + L_W && my >= iy && my < iy + L_ROW;
		if (i == lsel || hot)
			surf_round_rect(&back, x + 10, iy, L_W - 20, L_ROW - 2, 8,
					i == lsel ? mix(T.surface, T.accent, cfg.dark ? 70 : 50) : T.surface_hi);
		/* icon: first letter in a rounded square */
		u32 ic = !strcmp(items[i].kind, "App") ? T.accent : !strcmp(items[i].kind, "Command") ? T.chip_hi
												  : T.surface_hi;
		surf_round_rect(&back, x + 20, iy + 5, 22, 22, 6, ic);
		char c0 = items[i].label[0];
		if (!strcmp(items[i].kind, "File") || !strcmp(items[i].kind, "Folder"))
			c0 = items[i].kind[1] == 'o' ? '/' : '#';
		surf_char(&back, x + 27, iy + 8, (u8)(c0 >= 'a' && c0 <= 'z' ? c0 - 32 : c0),
			  !strcmp(items[i].kind, "App") ? RGB(0xff, 0xff, 0xff) : T.text, 0, true);
		text_clip(x + 54, iy + 8, items[i].label, L_W - 54 - 90, T.text);
		surf_text(&back, x + L_W - 20 - (int)strlen(items[i].kind) * FONT_W, iy + 8, items[i].kind, T.text_dim);
	}
	if (!nitems)
		surf_text(&back, x + 24, y + 68, "No matches", T.text_dim);
	surf_text(&back, x + 20, y + h - 26, "Enter open    Up/Down select    Esc close", T.text_dim);
}

static void launcher_key(const struct input_event *ev)
{
	switch (ev->code) {
	case KEY_ESC: open_launcher(false); return;
	case KEY_UP: if (lsel > 0) lsel--; break;
	case KEY_DOWN: if (lsel < nitems - 1) lsel++; break;
	case KEY_ENTER:
		if (nitems) {
			struct litem it = items[lsel];
			open_launcher(false);
			run_item(&it);
		}
		return;
	case KEY_BACKSPACE:
		if (qlen)
			query[--qlen] = 0;
		build_items();
		break;
	default:
		if (ev->ch >= 32 && ev->ch < 127 && qlen < (int)sizeof(query) - 1) {
			query[qlen++] = (char)ev->ch;
			query[qlen] = 0;
			build_items();
		}
	}
	damage_all();
}

/* ---- Alt+Tab switcher ---- */
static int switch_list(int *idx)
{
	int n = 0; /* most recently used first = top of the z-order */
	for (int i = nz - 1; i >= 0; i--)
		if (wins[zorder[i]].used && wins[zorder[i]].ws == cur_ws)
			idx[n++] = zorder[i];
	return n;
}

static void draw_switcher(void)
{
	int idx[MAX_WIN], n = switch_list(idx);
	if (!n)
		return;
	int cw = 180, ch = 130, pad = 14, tw = n * (cw + pad) + pad;
	int x = ((int)back.w - tw) / 2, y = (int)back.h / 2 - ch / 2 - 20;
	shadow(x, y, tw, ch + 2 * pad, 14);
	surf_round_rect(&back, x, y, tw, ch + 2 * pad, 14, T.surface);
	for (int k = 0; k < n; k++) {
		struct window *w = &wins[idx[k]];
		int cx = x + pad + k * (cw + pad), cy = y + pad;
		if (k == switch_sel % n)
			surf_round_rect(&back, cx - 4, cy - 4, cw + 8, ch + 8, 10, T.accent);
		surf_round_rect(&back, cx, cy, cw, ch, 8, T.surface_hi);
		int th = ch - 30, tww = cw - 16;
		int sx = w->w, sy = w->h;
		if (sx * th > sy * tww)
			th = sy * tww / sx;
		else
			tww = sx * th / sy;
		for (int yy = 0; yy < th; yy++) {
			int py = cy + 8 + yy;
			if (py < 0 || py >= (int)back.h)
				continue;
			u32 *dst = back.px + (size_t)py * back.stride + cx + 8;
			const u32 *src = w->surf.px + (size_t)(yy * sy / th) * w->surf.stride;
			for (int xx = 0; xx < tww; xx++)
				dst[xx] = src[xx * sx / tww];
		}
		text_clip(cx + 8, cy + ch - 20, w->title, cw - 16, T.text);
	}
}

/* ---- overview: workspace strip + window thumbnails ---- */
static void draw_overview(void)
{
	surf_blend_fill(&back, 0, BAR_H, (int)back.w, (int)back.h - BAR_H, 0, cfg.dark ? 150 : 90);
	int sw = (int)back.w, stripw = 150, striph = 34;
	int sx0 = sw / 2 - (cfg.workspaces * (stripw + 10)) / 2;
	for (int i = 0; i < cfg.workspaces; i++) {
		int x = sx0 + i * (stripw + 10), y = BAR_H + 16;
		surf_round_rect(&back, x, y, stripw, striph, 8, i == cur_ws ? T.accent : T.surface_hi);
		char l[24];
		snprintf(l, sizeof(l), "%d %s", i + 1, cfg.ws_name[i]);
		text_clip(x + 10, y + 9, l, stripw - 20, i == cur_ws ? RGB(0xff, 0xff, 0xff) : T.text);
	}
	int idx[MAX_WIN], n = 0;
	for (int i = 0; i < nz; i++)
		if (wins[zorder[i]].used && wins[zorder[i]].ws == cur_ws)
			idx[n++] = zorder[i];
	int top = BAR_H + 70;
	if (!n) {
		surf_text(&back, sw / 2 - 64, (int)back.h / 2, "No windows here", T.text_dim);
		return;
	}
	int cols = n <= 1 ? 1 : n <= 4 ? 2 : 3, rows = (n + cols - 1) / cols;
	int cw = (sw - 80) / cols, ch = ((int)back.h - top - 40) / rows;
	for (int k = 0; k < n; k++) {
		struct window *w = &wins[idx[k]];
		int cx = 40 + (k % cols) * cw, cy = top + (k / cols) * ch;
		int tw = cw - 30, th = ch - 40;
		int sx = w->w, sy = w->h;
		if (sx * th > sy * tw)
			th = sy * tw / sx;
		else
			tw = sx * th / sy;
		surf_round_rect(&back, cx - 3, cy - 3, tw + 6, th + 6, 6, idx[k] == focus ? T.accent : T.border);
		for (int yy = 0; yy < th; yy++) {
			if (cy + yy < 0 || cy + yy >= (int)back.h)
				continue;
			u32 *dst = back.px + (size_t)(cy + yy) * back.stride + cx;
			const u32 *src = w->surf.px + (size_t)(yy * sy / th) * w->surf.stride;
			for (int xx = 0; xx < tw && cx + xx < (int)back.w; xx++)
				dst[xx] = src[xx * sx / tw];
		}
		text_clip(cx, cy + th + 10, w->title, tw, T.text);
	}
}

static void draw_toasts(void)
{
	int y = BAR_H + 10, w = 340, x = (int)back.w - w - 12;
	u64 now = time_ns();
	for (int i = 0; i < NTOAST; i++) {
		struct toast *t = &toasts[i];
		if (!t->used)
			continue;
		int h = t->body[1][0] ? 74 : 56;
		shadow(x, y, w, h, 10);
		surf_round_rect(&back, x, y, w, h, 10, T.border);
		surf_round_rect(&back, x + 1, y + 1, w - 2, h - 2, 9, T.surface);
		surf_fill(&back, x + 1, y + 10, 3, h - 20, T.accent);
		text_clip(x + 16, y + 10, t->title, w - 90, T.text);
		char age[16];
		u64 s = (now - t->t) / 1000000000ULL;
		snprintf(age, sizeof(age), s ? "%lus ago" : "now", s);
		surf_text(&back, x + w - 12 - (int)strlen(age) * FONT_W, y + 10, age, T.text_dim);
		text_clip(x + 16, y + 30, t->body[0], w - 28, T.text_dim);
		if (t->body[1][0])
			text_clip(x + 16, y + 48, t->body[1], w - 28, T.text_dim);
		y += h + 8;
	}
}

static void draw_lock(void)
{
	int w = (int)back.w, h = (int)back.h;
	for (int y = 0; y < h; y++) /* plain gradient: nothing competes with the clock */
		surf_fill(&back, 0, y, w, 1, mix(T.wall0, T.wall1, (u32)(y * 255 / (h ? h : 1))));
	int lw = logo_width(40);
	surf_logo(&back, w / 2 - lw / 2, h / 2 - 150, 40, T.text, T.accent);
	i64 rem = time_unix() % 86400;
	char buf[16];
	snprintf(buf, sizeof(buf), "%02ld:%02ld", rem / 3600, (rem / 60) % 60);
	surf_text_scaled(&back, w / 2 - 5 * 8 * 5 / 2, h / 2 - 70, buf, T.text, 5);
	const char *m = "Locked  -  press any key";
	surf_text(&back, w / 2 - (int)strlen(m) * 4, h / 2 + 40, m, T.text_dim);
}

static const char *cursor_shape[] = {
	"X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X", "X.......X",
	"X........X", "X.....XXXXX", "X..X..X", "X.X X..X", "XX  X..X", "X    X..X", "     X..X",
	"      XX",
};

static void draw_cursor(void)
{
	for (int y = 0; y < (int)ARRAY_LEN(cursor_shape); y++)
		for (int x = 0; cursor_shape[y][x]; x++) {
			char c = cursor_shape[y][x];
			if (c == ' ')
				continue;
			int px = mx + x, py = my + y;
			if (px >= 0 && py >= 0 && px < (int)back.w && py < (int)back.h)
				back.px[(size_t)py * back.stride + px] = c == 'X' ? 0 : 0xFFFFFF;
		}
}

static void compose(void)
{
	u64 t0 = time_ns();
	/* take the damage atomically; input IRQs may add more meanwhile */
	u64 fl = irq_save();
	bool full = dirty_all;
	int sx0 = dx0, sy0 = dy0, sx1 = dx1, sy1 = dy1;
	u64 in_t = input_pending_ns, ws_t = ws_switch_pending_ns;
	input_pending_ns = ws_switch_pending_ns = 0;
	dirty_all = false;
	dx0 = dy0 = dx1 = dy1 = 0;
	irq_restore(fl);
	if (!wall_valid)
		render_wallpaper();
	if (locked) {
		draw_lock();
	} else {
		memcpy(back.px, wallc.px, (size_t)back.stride * back.h * 4);
		for (int i = 0; i < nz && !overview; i++) {
			struct window *w = &wins[zorder[i]];
			if (visible(w) && !(w->floating && ws_mode[cur_ws] == MODE_TILING))
				draw_window(w, zorder[i] == focus);
		}
		for (int i = 0; i < nz && !overview; i++) { /* floating windows stay above the tiles */
			struct window *w = &wins[zorder[i]];
			if (visible(w) && w->floating && ws_mode[cur_ws] == MODE_TILING)
				draw_window(w, zorder[i] == focus);
		}
		if (overview)
			draw_overview();
		draw_bar();
		draw_toasts();
		if (switcher)
			draw_switcher();
		if (menu_open)
			draw_launcher();
	}
	draw_cursor();
	/* copy the damaged region (whole screen when dirty_all) */
	int x0 = 0, y0 = 0, x1 = (int)back.w, y1 = (int)back.h;
	if (!full) {
		x0 = MAX(sx0 - 20, 0); y0 = MAX(sy0 - 20, 0);
		x1 = MIN(sx1 + 20, (int)back.w); y1 = MIN(sy1 + 20, (int)back.h);
	}
	for (int y = y0; y < y1; y++)
		memcpy(screen->px + (size_t)y * screen->stride + x0, back.px + (size_t)y * back.stride + x0,
		       (size_t)(x1 - x0) * 4);
	u64 t1 = time_ns();
	frames_drawn++;
	compose_ns_total += t1 - t0;
	compose_ns_max = MAX(compose_ns_max, t1 - t0);
	if (in_t) { /* input event -> frame on screen */
		input_lat_total += t1 - in_t;
		input_lat_max = MAX(input_lat_max, t1 - in_t);
		input_lat_n++;
	}
	if (ws_t)
		ws_switch_last_us = (t1 - ws_t) / 1000;
}

/* one-line description of a class change for notifications */
static void adaptive_watch(void)
{
	static enum task_class last = CLASS_COUNT;
	enum task_class c = profiler_stable_class();
	if (last != CLASS_COUNT && c != last && cfg.adapt_notify && c != CLASS_IDLE && last != CLASS_IDLE) {
		char l1[56], l2[56];
		snprintf(l1, sizeof(l1), "Workload: %s -> %s", task_class_name(last), task_class_name(c));
		snprintf(l2, sizeof(l2), "Scheduler: %s (per-class parameters)", sched_policy_name());
		notify("Adaptive kernel", l1, l2);
	}
	last = c;
}

static void compositor_thread(void *arg)
{
	u64 last = 0;
	for (;;) {
		u64 f = irq_save();
		if (!dirty_all && dx1 <= dx0 && dq_head == dq_tail)
			wq_wait(&comp_wq, 1000000000ULL); /* at least once a second: clock, status */
		irq_restore(f);
		u64 now = time_ns();
		if (now - last < 16000000ULL) /* cap at ~60 fps */
			sleep_ns(16000000ULL - (now - last));
		last = time_ns();
		if ((u64)time_unix() / 60 != last_clock_s) {
			last_clock_s = (u64)time_unix() / 60;
			damage_all();
		}
		static u64 last_panel;
		if (last - last_panel > 1000000000ULL) {
			last_panel = last;
			sample_status();
			adaptive_watch();
			damage(0, 0, (int)back.w, BAR_H);
			for (int i = 0; i < NTOAST; i++)
				if (toasts[i].used) {
					if (last - toasts[i].t > 6000000000ULL)
						toasts[i].used = false;
					damage_all(); /* age text / expiry */
				}
		}
		run_deferred();
		if (dirty_all || dx1 > dx0) {
			mutex_lock(&comp_lock); /* interrupts stay enabled while drawing */
			compose();
			mutex_unlock(&comp_lock);
		}
	}
}

/* ---- window lifecycle (syscalls) ---- */
static int win_create(struct process *p, int w, int h, const char *title)
{
	int id = -1;
	for (int i = 0; i < MAX_WIN; i++)
		if (!wins[i].used) {
			id = i;
			break;
		}
	if (id < 0)
		return -E_NOSPC;
	struct window *win = &wins[id];
	memset(win, 0, sizeof(*win));
	/* surface capacity = full screen, so windows can grow without remapping */
	u32 cap_w = screen->w, cap_h = screen->h;
	u64 bytes = (u64)cap_w * cap_h * 4;
	win->nframes = ALIGN_UP(bytes, PAGE_SIZE) / PAGE_SIZE;
	win->frames = kmalloc(win->nframes * sizeof(paddr_t));
	vaddr_t kva = (vaddr_t)vmalloc_pages(win->nframes); /* fresh frames, kernel-mapped */
	if (!kva || !win->frames)
		return -E_NOMEM;
	win->user_va = WIN_USER_BASE + (u64)id * WIN_USER_STRIDE;
	for (u64 i = 0; i < win->nframes; i++) {
		paddr_t f = vmm_translate(vmm_kernel_space(), kva + i * PAGE_SIZE);
		win->frames[i] = f;
		vmm_map_page(p->as, win->user_va + i * PAGE_SIZE, f, VM_USER | VM_WRITE);
	}
	win->surf.px = (u32 *)kva;
	win->surf.w = cap_w;
	win->surf.h = cap_h;
	win->surf.stride = cap_w;
	memset(win->surf.px, cfg.dark ? 0x16 : 0xff, bytes);
	win->used = true;
	win->id = id;
	win->owner = p->pid;
	win->seq = next_seq++;
	strlcpy(win->title, title, sizeof(win->title));
	wq_init(&win->wq);
	w = MIN(MAX(w, 120), (int)cap_w - 40);
	h = MIN(MAX(h, 60), work_h() - TITLE_H - 40);
	/* cascade new windows */
	static int cascade;
	int x = 60 + (cascade % 8) * 36, y = work_y() + 30 + TITLE_H + (cascade % 8) * 30;
	cascade++;
	if (x + w > (int)screen->w)
		x = 20;
	if (y + h > (int)screen->h)
		y = work_y() + TITLE_H + 10;
	win->x = x; win->y = y; win->w = w; win->h = h;
	win->ws = cur_ws;
	zorder[nz++] = id;
	set_focus(id);
	retile();
	damage_all();
	kick();
	return id;
}

static void win_destroy(struct window *w)
{
	for (int i = 0; i < nz; i++)
		if (zorder[i] == w->id) {
			for (int j = i; j < nz - 1; j++)
				zorder[j] = zorder[j + 1];
			nz--;
			break;
		}
	struct process *p = proc_by_pid(w->owner);
	if (p && p->state == PROC_ALIVE)
		for (u64 i = 0; i < w->nframes; i++)
			vmm_unmap_page(p->as, w->user_va + i * PAGE_SIZE, false);
	vfree_pages(w->surf.px, w->nframes);
	kfree(w->frames);
	wq_wake_all(&w->wq);
	w->used = false;
	if (focus == w->id) {
		focus = -1;
		focus_top_visible();
	}
	retile();
	damage_all();
	kick();
}

/* A process died: close its windows (called from the process layer). */
void compositor_process_exited(pid_t pid)
{
	if (!running)
		return;
	mutex_lock(&comp_lock);
	u64 f = irq_save();
	for (int i = 0; i < MAX_WIN; i++)
		if (wins[i].used && wins[i].owner == pid)
			win_destroy(&wins[i]); /* still alive here: unmap from it, then free */
	irq_restore(f);
	mutex_unlock(&comp_lock);
}

/* ---- input handling ---- */
static int window_at(int x, int y, int *part)
{
	for (int pass = 0; pass < 2; pass++) /* floating-over-tiles first */
		for (int i = nz - 1; i >= 0; i--) {
			struct window *w = &wins[zorder[i]];
			bool on_top = w->floating && ws_mode[cur_ws] == MODE_TILING;
			if (!visible(w) || (pass == 0) != on_top)
				continue;
			int fx, fy, fw, fh;
			frame_rect(w, &fx, &fy, &fw, &fh);
			if (x < fx || y < fy || x >= fx + fw || y >= fy + fh)
				continue;
			if (y < w->y) {
				int bx = title_buttons_x(fx, fw);
				*part = x >= bx + 52 ? 3 : x >= bx + 26 ? 2 : x >= bx ? 1 : 0; /* 0 title */
			} else if (x >= fx + fw - 14 && y >= fy + fh - 14) {
				*part = 4; /* resize corner */
			} else {
				*part = 5; /* client */
			}
			return zorder[i];
		}
	return -1;
}

static void switch_workspace(int ws)
{
	int n = cfg.workspaces;
	ws = ((ws % n) + n) % n;
	if (ws == cur_ws && !show_desktop && !overview)
		return;
	cur_ws = ws;
	show_desktop = false;
	overview = false;
	focus = -1;
	focus_top_visible();
	retile();
	ws_switch_pending_ns = time_ns();
	damage_all();
}

static void move_to_workspace(struct window *w, int ws)
{
	if (ws < 0 || ws >= cfg.workspaces)
		return;
	w->ws = ws;
	if (w->state == W_TILED)
		w->state = W_NORMAL;
	retile();
	switch_workspace(ws); /* the window moves with the user */
	set_focus(w->id);
	retile();
}

static void cycle_focus(int dir)
{
	int n = 0, cand[MAX_WIN];
	for (int i = 0; i < MAX_WIN; i++)
		if (wins[i].used && wins[i].ws == cur_ws)
			cand[n++] = i;
	if (!n)
		return;
	int cur = 0;
	for (int i = 0; i < n; i++)
		if (cand[i] == focus)
			cur = i;
	int next = cand[(cur + dir + n) % n];
	if (wins[next].state == W_MIN)
		wins[next].state = W_NORMAL;
	show_desktop = false;
	set_focus(next);
}

/* tiling: focus / swap with the previous or next window in tile order */
static void tile_neighbour(int dir, bool swap)
{
	int idx[MAX_WIN], n = tiled_windows(idx), k = -1;
	for (int i = 0; i < n; i++)
		if (idx[i] == focus)
			k = i;
	if (k < 0 || n < 2)
		return;
	int j = (k + dir + n) % n;
	if (swap) {
		u32 t = wins[idx[k]].seq;
		wins[idx[k]].seq = wins[idx[j]].seq;
		wins[idx[j]].seq = t;
		retile();
	} else {
		set_focus(idx[j]);
	}
}

static void do_action(int a)
{
	switch (a) {
	case GA_WS_PREV: switch_workspace(cur_ws - 1); break;
	case GA_WS_NEXT: switch_workspace(cur_ws + 1); break;
	case GA_OVERVIEW: overview = !overview; break;
	case GA_DESKTOP: overview = false; show_desktop = !show_desktop; if (!show_desktop) focus_top_visible(); break;
	case GA_APP_PREV: cycle_focus(-1); break;
	case GA_APP_NEXT: cycle_focus(1); break;
	case GA_LAUNCHER: open_launcher(!menu_open); break;
	case GA_CONTROL: focus_or_launch("Fuhrer Control Center", "/bin/control"); break;
	}
	damage_all();
}

static bool handle_shortcut(const struct input_event *ev)
{
	if (ev->type != EV_KEY || !ev->value)
		return false;
	u16 m = ev->mods;
	struct window *fw = focus >= 0 && wins[focus].used ? &wins[focus] : NULL;
	if (locked) {
		locked = false;
		damage_all();
		return true;
	}
	bool super = m & MOD_SUPER;
	if ((m & MOD_ALT) && !super && ev->code == KEY_TAB) { /* Alt+Tab switcher */
		int idx[MAX_WIN], n = switch_list(idx);
		if (!n)
			return true;
		if (!switcher) {
			switcher = true;
			switch_sel = n > 1 ? 1 : 0;
		} else {
			switch_sel = (switch_sel + ((m & MOD_SHIFT) ? n - 1 : 1)) % n;
		}
		damage_all();
		return true;
	}
	if ((m & MOD_CTRL) && (m & MOD_ALT) && (ev->code == 't')) {
		launch("/bin/term");
		return true;
	}
	if (!super)
		return false;
	bool tiling = ws_mode[cur_ws] == MODE_TILING;
	switch (ev->code) {
	case KEY_TAB: overview = !overview; damage_all(); return true;
	case KEY_LEFT:
	case KEY_RIGHT: {
		int dir = ev->code == KEY_LEFT ? -1 : 1;
		if (m & MOD_CTRL)
			switch_workspace(cur_ws + dir);
		else if (tiling)
			tile_neighbour(dir, m & MOD_SHIFT);
		else if (fw)
			set_state(fw, dir < 0 ? W_SNAP_L : W_SNAP_R);
		damage_all();
		return true;
	}
	case KEY_UP: if (fw && !tiling) set_state(fw, fw->state == W_MAX ? W_NORMAL : W_MAX); return true;
	case KEY_DOWN: if (fw && !tiling) set_state(fw, fw->state == W_NORMAL ? W_MIN : W_NORMAL); return true;
	case 'd': do_action(GA_DESKTOP); return true;
	case 'l': locked = true; menu_open = false; damage_all(); return true;
	case 'o': case 'w': overview = !overview; damage_all(); return true;
	case 't': set_mode((ws_mode[cur_ws] + 1) % 3); return true;
	case 'f': do_action(GA_CONTROL); return true;
	case 'e': launch("/bin/files"); return true;
	case ' ':
		if (fw) {
			fw->floating = !fw->floating;
			if (fw->floating && fw->state == W_TILED) {
				fw->state = W_NORMAL;
				int sw = (int)screen->w;
				place_frame(fw, sw / 4, work_y() + 60, sw / 2, work_h() / 2);
			}
			retile();
		}
		return true;
	case 'r': ws_orient[cur_ws] ^= 1; retile(); return true;
	case '[': case ']': {
		int r = ws_ratio[cur_ws] ? ws_ratio[cur_ws] : 50;
		r = MIN(MAX(r + (ev->code == '[' ? -5 : 5), 25), 75);
		ws_ratio[cur_ws] = (u8)r;
		retile();
		return true;
	}
	case 'q':
		if (fw && (m & MOD_SHIFT))
			push_event(fw, (struct fu_wevent){ .type = WEV_CLOSE });
		return true;
	}
	/* digits (with Shift the keyboard reports the shifted symbol in ch,
	 * so use the key code, which stays the digit) */
	if (ev->code >= '1' && ev->code <= '9') {
		int ws = ev->code - '1';
		if (ws >= cfg.workspaces)
			return true;
		if ((m & MOD_SHIFT) && fw)
			move_to_workspace(fw, ws);
		else
			switch_workspace(ws);
		damage_all();
		return true;
	}
	return false;
}

static void handle_gesture(const struct input_event *ev)
{
	int g = -1;
	switch (ev->code) {
	case GESTURE_THREE_SWIPE_LEFT: g = GS_THREE_LEFT; break;
	case GESTURE_THREE_SWIPE_RIGHT: g = GS_THREE_RIGHT; break;
	case GESTURE_THREE_SWIPE_UP: g = GS_THREE_UP; break;
	case GESTURE_THREE_SWIPE_DOWN: g = GS_THREE_DOWN; break;
	case GESTURE_THREE_TAP: g = GS_THREE_TAP; break;
	case GESTURE_FOUR_SWIPE_LEFT: g = GS_FOUR_LEFT; break;
	case GESTURE_FOUR_SWIPE_RIGHT: g = GS_FOUR_RIGHT; break;
	case GESTURE_FOUR_SWIPE_UP: g = GS_FOUR_UP; break;
	case GESTURE_FOUR_SWIPE_DOWN: g = GS_FOUR_DOWN; break;
	case GESTURE_FOUR_TAP: g = GS_FOUR_TAP; break;
	case GESTURE_PINCH: /* normalised zoom for the focused app */
		if (focus >= 0 && wins[focus].used)
			push_event(&wins[focus], (struct fu_wevent){ .type = WEV_SCROLL, .value = ev->dy > 0 ? -1 : 1,
								     .mods = MOD_CTRL });
		return;
	default: return;
	}
	/* system gestures take priority over applications (UI_SUGGESTION §25) */
	do_action(cfg.gesture[g]);
}

static void click(int x, int y)
{
	int sw = (int)screen->w;
	for (int i = 0, ty = BAR_H + 10; i < NTOAST; i++) { /* click dismisses a toast */
		if (!toasts[i].used)
			continue;
		int h = toasts[i].body[1][0] ? 74 : 56;
		if (x >= sw - 352 && x < sw - 12 && y >= ty && y < ty + h) {
			toasts[i].used = false;
			damage_all();
			return;
		}
		ty += h + 8;
	}
	if (menu_open) {
		int lx = launcher_x(), ly = launcher_y(), rows = MIN(nitems, L_ROWS);
		int first = lsel >= L_ROWS ? lsel - L_ROWS + 1 : 0;
		if (x >= lx && x < lx + L_W && y >= ly + 60 && y < ly + 60 + rows * L_ROW) {
			struct litem it = items[first + (y - ly - 60) / L_ROW];
			open_launcher(false);
			run_item(&it);
			return;
		}
		if (!(x >= lx && x < lx + L_W && y >= ly && y < ly + 64 + rows * L_ROW + 36))
			open_launcher(false);
		if (y >= BAR_H)
			return;
	}
	if (overview) {
		int stripw = 150, sx0 = sw / 2 - (cfg.workspaces * (stripw + 10)) / 2;
		if (y >= BAR_H + 16 && y < BAR_H + 50 && x >= sx0) {
			int k = (x - sx0) / (stripw + 10);
			if (k < cfg.workspaces) {
				switch_workspace(k);
				overview = true;
				damage_all();
				return;
			}
		}
		int idx[MAX_WIN], n = 0;
		for (int i = 0; i < nz; i++)
			if (wins[zorder[i]].used && wins[zorder[i]].ws == cur_ws)
				idx[n++] = zorder[i];
		int top = BAR_H + 70;
		int cols = n <= 1 ? 1 : n <= 4 ? 2 : 3, rows = n ? (n + cols - 1) / cols : 1;
		int cw = (sw - 80) / cols, ch = ((int)screen->h - top - 40) / rows;
		int k = ((y - top) / ch) * cols + (x - 40) / cw;
		overview = false;
		if (x >= 40 && y >= top && k >= 0 && k < n) {
			if (wins[idx[k]].state == W_MIN)
				wins[idx[k]].state = W_NORMAL;
			show_desktop = false;
			set_focus(idx[k]);
		}
		damage_all();
		return;
	}
	if (y < BAR_H) { /* top bar */
		if (x < bar.logo_x1) {
			open_launcher(!menu_open);
		} else if (x >= bar.mode_x0 && x < bar.mode_x1) {
			set_mode((ws_mode[cur_ws] + 1) % 3);
		} else if (x >= bar.status_x0 && x < bar.status_x1) {
			do_action(GA_CONTROL);
		} else {
			for (int i = 0; i < cfg.workspaces; i++)
				if (x >= bar.ws_x0[i] && x < bar.ws_x1[i])
					switch_workspace(i);
		}
		damage_all();
		return;
	}
	int part;
	int idx = window_at(x, y, &part);
	if (idx < 0)
		return;
	struct window *w = &wins[idx];
	set_focus(idx);
	switch (part) {
	case 0:
		if (w->state != W_TILED)
			drag = idx, drag_ox = x - w->x, drag_oy = y - w->y;
		break;
	case 1: set_state(w, W_MIN); break;
	case 2:
		if (w->state != W_TILED)
			set_state(w, w->state == W_MAX ? W_NORMAL : W_MAX);
		break;
	case 3: push_event(w, (struct fu_wevent){ .type = WEV_CLOSE }); break;
	case 4:
		if (w->state != W_TILED)
			resize = idx, drag_ox = x, drag_oy = y;
		break;
	case 5:
		push_event(w, (struct fu_wevent){ .type = WEV_MOUSE_BUTTON, .x = x - w->x, .y = y - w->y,
						   .value = 1, .mods = input_modifiers() });
		break;
	}
}

static void compositor_input(const struct input_event *ev)
{
	u64 f = irq_save();
	if (!input_pending_ns)
		input_pending_ns = ev->time_ns ? ev->time_ns : time_ns();
	switch (ev->type) {
	case EV_KEY:
		if (ev->code == KEY_SUPER) {
			if (ev->value) {
				super_down = true;
				super_used = false;
			} else {
				if (super_down && !super_used && !locked)
					open_launcher(!menu_open);
				super_down = false;
			}
			break;
		}
		if ((ev->code == KEY_LALT || ev->code == KEY_RALT) && !ev->value && switcher) {
			int idx[MAX_WIN], n = switch_list(idx);
			switcher = false;
			if (n) {
				int t = idx[switch_sel % n];
				if (wins[t].state == W_MIN)
					wins[t].state = W_NORMAL;
				show_desktop = false;
				set_focus(t);
			}
			damage_all();
			break;
		}
		if (super_down && ev->value)
			super_used = true;
		if (menu_open && ev->value && !(ev->mods & MOD_SUPER) && !locked) {
			launcher_key(ev);
			break;
		}
		if (handle_shortcut(ev))
			break;
		if (focus >= 0 && wins[focus].used)
			push_event(&wins[focus], (struct fu_wevent){ .type = WEV_KEY, .key = ev->code,
								     .ch = ev->ch, .value = ev->value,
								     .mods = ev->mods });
		break;
	case EV_POINTER: {
		int ox = mx, oy = my;
		mx = MIN(MAX(mx + ev->dx, 0), (int)screen->w - 1);
		my = MIN(MAX(my + ev->dy, 0), (int)screen->h - 1);
		damage(ox, oy, 12, 18);
		damage(mx, my, 12, 18);
		if (drag >= 0 && wins[drag].used) {
			struct window *w = &wins[drag];
			if (w->state != W_NORMAL)
				set_state(w, W_NORMAL);
			w->x = mx - drag_ox;
			w->y = MAX(my - drag_oy, BAR_H + TITLE_H);
			damage_all();
		} else if (resize >= 0 && wins[resize].used) {
			struct window *w = &wins[resize];
			apply_geometry(w, w->x, w->y, w->w + (mx - drag_ox), w->h + (my - drag_oy));
			drag_ox = mx;
			drag_oy = my;
		} else if (focus >= 0 && wins[focus].used && btn_down) {
			struct window *w = &wins[focus];
			push_event(w, (struct fu_wevent){ .type = WEV_MOUSE_MOVE, .x = mx - w->x, .y = my - w->y, .value = 1 });
		}
		/* hover feedback: launcher rows, title buttons, bar status */
		if (menu_open || my < BAR_H + TITLE_H + 40 || oy < BAR_H + TITLE_H + 40)
			damage_all();
		else
			for (int i = 0; i < nz; i++) {
				struct window *w = &wins[zorder[i]];
				if (visible(w) && my >= w->y - TITLE_H - 2 && my < w->y + 2)
					damage(w->x, w->y - TITLE_H - 2, w->w, TITLE_H + 4);
			}
		break;
	}
	case EV_BUTTON:
		if (ev->code == 1 && ev->value) {
			btn_down = true;
			click(mx, my);
		} else if (ev->code == 1) {
			btn_down = false;
			if (drag >= 0 && my < BAR_H + 4) /* drop at the top edge: maximise */
				set_state(&wins[drag], W_MAX);
			drag = resize = -1;
			if (focus >= 0 && wins[focus].used)
				push_event(&wins[focus], (struct fu_wevent){ .type = WEV_MOUSE_BUTTON,
									     .x = mx - wins[focus].x, .y = my - wins[focus].y, .value = 0 });
		} else if (ev->value) {
			int part, idx = window_at(mx, my, &part);
			if (idx >= 0 && part == 5)
				push_event(&wins[idx], (struct fu_wevent){ .type = WEV_MOUSE_BUTTON, .x = mx - wins[idx].x,
									   .y = my - wins[idx].y, .value = (i32)ev->code });
		}
		break;
	case EV_SCROLL:
		if (focus >= 0 && wins[focus].used)
			push_event(&wins[focus], (struct fu_wevent){ .type = WEV_SCROLL, .value = ev->dy });
		break;
	case EV_GESTURE:
		handle_gesture(ev);
		break;
	}
	irq_restore(f);
	kick();
}

/* ---- configuration ---- */
static void apply_cfg(bool theme_changed)
{
	cfg.workspaces = (u8)MIN(MAX(cfg.workspaces, 1), DESK_MAX_WS);
	cfg.accent %= NACCENT;
	cfg.wallpaper %= 3;
	cfg.pointer_speed = (u8)MIN(MAX(cfg.pointer_speed, 1), 8);
	cfg.scroll_speed = (u8)MIN(MAX(cfg.scroll_speed, 1), 8);
	for (int i = 0; i < GS_COUNT; i++)
		if (cfg.gesture[i] >= GA_COUNT)
			cfg.gesture[i] = GA_NONE;
	for (int i = 0; i < DESK_MAX_WS; i++)
		cfg.ws_name[i][sizeof(cfg.ws_name[i]) - 1] = 0;
	struct input_options o = { .tap_to_click = cfg.tap_to_click, .natural_scroll = cfg.natural_scroll,
				   .typing_block_ms = cfg.typing_block_ms, .pointer_speed = cfg.pointer_speed,
				   .scroll_speed = cfg.scroll_speed };
	input_set_options(&o);
	if (cur_ws >= cfg.workspaces)
		switch_workspace(cfg.workspaces - 1);
	if (theme_changed) {
		theme_build();
		wall_valid = false;
		broadcast(WEV_THEME);
	}
	damage_all();
	kick();
}

static void fill_theme(struct fu_theme *t)
{
	*t = (struct fu_theme){ .dark = cfg.dark, .bg = T.surface, .bg2 = T.surface_hi, .fg = T.text,
				.dim = T.text_dim, .accent = T.accent, .good = T.good, .warn = T.warn,
				.bad = T.bad, .border = T.border };
}

/* ---- syscalls ---- */
i64 sys_win(u64 nr, u64 a, u64 b, u64 c, u64 d, u64 e)
{
	struct process *p = proc_current();
	if (nr == SYS_WIN_INFO) {
		struct fu_screen s = { screen ? screen->w : 0, screen ? screen->h - BAR_H : 0, running };
		return copy_to_user((void *)a, &s, sizeof(s));
	}
	if (nr == SYS_DESKTOP_CTL) {
		switch (a) {
		case DESK_START:
			if (!screen)
				return -E_NOSYS;
			if (!running) {
				extern void desktop_start(void);
				desktop_start();
			}
			return 0;
		case DESK_WALLPAPER: cfg.wallpaper = (u8)(b % 3); apply_cfg(true); return 0;
		case DESK_RUNNING: return running;
		case DESK_TERMINAL: launch("/bin/term"); return 0;
		case DESK_GET_CFG: return copy_to_user((void *)b, &cfg, sizeof(cfg));
		case DESK_SET_CFG: {
			struct fu_desk_cfg n;
			if (copy_from_user(&n, (const void *)b, sizeof(n)) < 0)
				return -E_FAULT;
			bool th = n.dark != cfg.dark || n.accent != cfg.accent || n.wallpaper != cfg.wallpaper;
			u64 f = irq_save();
			cfg = n;
			apply_cfg(th);
			irq_restore(f);
			return 0;
		}
		case DESK_THEME: {
			struct fu_theme t;
			if (!running)
				theme_build();
			fill_theme(&t);
			return copy_to_user((void *)b, &t, sizeof(t));
		}
		case DESK_NOTIFY: {
			char t[40], l1[56] = "";
			if (strncpy_from_user(t, (const char *)b, sizeof(t)) < 0)
				return -E_FAULT;
			if (c && strncpy_from_user(l1, (const char *)c, sizeof(l1)) < 0)
				return -E_FAULT;
			if (!running)
				return -E_NOSYS;
			u64 f = irq_save();
			notify(t, l1, NULL);
			irq_restore(f);
			return 0;
		}
		}
		return -E_INVAL;
	}
	if (!running)
		return -E_NOSYS;
	bool structural = nr == SYS_WIN_CREATE || nr == SYS_WIN_CLOSE;
	if (structural)
		mutex_lock(&comp_lock);
	u64 f = irq_save();
	i64 r = 0;
	struct window *w = nr == SYS_WIN_CREATE ? NULL : win_by_id((int)a);
	if (nr != SYS_WIN_CREATE && nr != SYS_WIN_EVENT && (!w || w->owner != p->pid)) {
		irq_restore(f);
		if (structural)
			mutex_unlock(&comp_lock);
		return -E_BADF;
	}
	switch (nr) {
	case SYS_WIN_CREATE: {
		char title[48];
		if (strncpy_from_user(title, (const char *)c, sizeof(title)) < 0)
			strlcpy(title, "window", sizeof(title));
		r = win_create(p, (int)a, (int)b, title);
		break;
	}
	case SYS_WIN_SURFACE:
		/* returns address; stride in pixels = screen width (capacity) */
		r = (i64)w->user_va;
		break;
	case SYS_WIN_PRESENT: {
		w->presents++;
		w->last_present = time_ns();
		if (visible(w)) {
			int fx, fy, fw, fh;
			frame_rect(w, &fx, &fy, &fw, &fh);
			damage(fx, fy, fw, fh);
			if (overview || switcher || menu_open)
				damage_all();
			kick();
		}
		break;
	}
	case SYS_WIN_EVENT: {
		struct fu_wevent ev = { 0 };
		if ((i64)a >= 0 && !w) {
			r = -E_BADF;
			break;
		}
		u64 deadline = c ? time_ns() + c * 1000000ULL : 0;
		for (;;) {
			struct window *src = NULL;
			if ((i64)a >= 0) {
				if (w->qh != w->qt)
					src = w;
			} else {
				for (int i = 0; i < MAX_WIN; i++)
					if (wins[i].used && wins[i].owner == p->pid && wins[i].qh != wins[i].qt)
						src = &wins[i];
			}
			if (src) {
				ev = src->q[src->qt++ % EVQ];
				break;
			}
			if (c && time_ns() >= deadline) {
				ev.type = WEV_TIMER;
				break;
			}
			struct window *wq_owner = (i64)a >= 0 ? w : NULL;
			if (!wq_owner) /* any-window wait: sleep on the first of ours */
				for (int i = 0; i < MAX_WIN && !wq_owner; i++)
					if (wins[i].used && wins[i].owner == p->pid)
						wq_owner = &wins[i];
			if (!wq_owner) {
				r = -E_BADF;
				break;
			}
			wq_wait(&wq_owner->wq, c ? deadline - time_ns() : 0);
			if (p->killed) {
				r = -E_INTR;
				break;
			}
		}
		if (r == 0) {
			irq_restore(f);
			return copy_to_user((void *)b, &ev, sizeof(ev));
		}
		break;
	}
	case SYS_WIN_CLOSE:
		win_destroy(w);
		break;
	case SYS_WIN_SET_TITLE: {
		char t[48];
		if (strncpy_from_user(t, (const char *)b, sizeof(t)) >= 0)
			strlcpy(w->title, t, sizeof(w->title));
		damage_all();
		kick();
		break;
	}
	case SYS_WIN_RESIZE:
		if (w->state != W_TILED)
			apply_geometry(w, w->x, w->y, (int)b, (int)c);
		kick();
		break;
	default:
		r = -E_NOSYS;
	}
	irq_restore(f);
	if (structural)
		mutex_unlock(&comp_lock);
	return r;
}

struct pbuf;
void pb_printf(struct pbuf *p, const char *fmt, ...);
void procfs_register(const char *name, void (*gen)(void *p));
static void gen_desktop(void *pb)
{
	int n = 0;
	for (int i = 0; i < MAX_WIN; i++)
		n += wins[i].used;
	pb_printf(pb, "running: %d\nworkspace: %d\nworkspaces: %d\nmode: %s\ntheme: %s\naccent: %s\n"
		      "wallpaper: %s\nwindows: %d\n",
		  running, cur_ws + 1, cfg.workspaces, mode_names[ws_mode[cur_ws] % 3], cfg.dark ? "dark" : "light",
		  accent_names[cfg.accent % NACCENT], wallpaper_names[cfg.wallpaper % 3], n);
	pb_printf(pb, "frames: %lu\navg_compose_us: %lu\nmax_compose_us: %lu\n", frames_drawn,
		  frames_drawn ? compose_ns_total / frames_drawn / 1000 : 0, compose_ns_max / 1000);
	pb_printf(pb, "input_to_frame_avg_us: %lu\ninput_to_frame_max_us: %lu\ninput_frames: %lu\n",
		  input_lat_n ? input_lat_total / input_lat_n / 1000 : 0, input_lat_max / 1000, input_lat_n);
	pb_printf(pb, "last_workspace_switch_us: %lu\n", ws_switch_last_us);
	for (int i = 0; i < MAX_WIN; i++)
		if (wins[i].used)
			pb_printf(pb, "win %d pid %d ws %d %dx%d at %d,%d state %d%s presents %u '%s'\n", i,
				  wins[i].owner, wins[i].ws + 1, wins[i].w, wins[i].h, wins[i].x, wins[i].y,
				  wins[i].state, wins[i].floating ? " floating" : "", wins[i].presents,
				  wins[i].title);
}

void desktop_start(void)
{
	screen = fb_screen();
	if (!screen || running)
		return;
	u64 pages = ALIGN_UP((u64)screen->w * screen->h * 4, PAGE_SIZE) / PAGE_SIZE;
	back.w = wallc.w = screen->w;
	back.h = wallc.h = screen->h;
	back.stride = wallc.stride = screen->w;
	back.px = vmalloc_pages(pages);
	wallc.px = vmalloc_pages(pages);
	wq_init(&comp_wq);
	mutex_init(&comp_lock);
	for (int i = 0; i < DESK_MAX_WS; i++)
		ws_ratio[i] = 55;
	theme_build();
	apply_cfg(false);
	mx = (int)screen->w / 2;
	my = (int)screen->h / 2;
	fbcon_detach();		/* the console keeps going to serial */
	console_set_fb(false);
	running = true;
	input_set_sink(compositor_input);
	damage_all();
	task_create_kernel("compositor", compositor_thread, NULL);
	KLOG("desktop", "compositor running at %ux%u", screen->w, screen->h);
}

void compositor_init(void)
{
	screen = fb_screen();
	theme_build();
	procfs_register("desktop", (void (*)(void *))gen_desktop);
}
