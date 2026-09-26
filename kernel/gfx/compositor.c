/* Fuhrer compositor + window manager + desktop shell (M10, M11, M12).
 *
 *   framebuffer <- back buffer <- (wallpaper, windows, panel, menus, cursor)
 *
 * Windows belong to processes; each has a pixel surface mapped both here and
 * into the owner's address space (apps draw directly, then "present").
 * Rendering happens in the `compositor` kernel thread, only when something
 * changed, and only the damaged rectangle is copied to the framebuffer.
 *
 * Window management: focus + stacking, move (drag title), resize (drag the
 * corner), minimize / maximize / restore, snap halves, 4 workspaces, an
 * overview, a master/stack tiling mode, Alt/Super+Tab, a lock screen, and a
 * launcher ("start menu"). Touchpad gestures from the recognizer map to
 * overview / desktop / app switch / workspace switch. */
#include "arch/x86_64/cpu.h"
#include "gfx/fb.h"
#include "input.h"
#include "kernel.h"
#include "mm.h"
#include "proc.h"
#include "sched.h"
#include "uapi/fuhrer.h"

#define MAX_WIN 32
#define TITLE_H 28
#define BORDER 1
#define PANEL_H 40
#define EVQ 64
#define WORKSPACES 4
#define WIN_USER_BASE 0x0000300000000000ULL
#define WIN_USER_STRIDE (64ULL << 20)

/* palette */
#define C_PANEL RGB(0x16, 0x1b, 0x26)
#define C_PANEL_HI RGB(0x2a, 0x33, 0x45)
#define C_ACCENT RGB(0x4f, 0x9d, 0xff)
#define C_TITLE_FOCUS RGB(0x25, 0x2d, 0x3d)
#define C_TITLE RGB(0x1c, 0x21, 0x2c)
#define C_TEXT RGB(0xe6, 0xea, 0xf2)
#define C_TEXT_DIM RGB(0x8c, 0x96, 0xa8)
#define C_CLOSE RGB(0xe0, 0x5a, 0x5a)

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
static struct surface back;	/* composition buffer (RAM) */
static struct surface *screen;
static bool running, dirty_all;
static int dx0, dy0, dx1, dy1;	/* damage rectangle */
static int mx, my;		/* pointer */
static bool btn_down;
static int drag = -1, resize = -1, drag_ox, drag_oy;
static int cur_ws;
static bool menu_open, overview, locked, tiling, show_desktop;
static struct waitqueue comp_wq;
static struct mutex comp_lock;	/* window list/surfaces vs. composition */
static u64 frames_drawn, last_clock_s;
static u32 wallpaper = 0;
static u64 compose_ns_total;

static const struct {
	const char *label;
	const char *path;
} launcher[] = {
	{ "Terminal", "/bin/term" },
	{ "Files", "/bin/files" },
	{ "Fuhrer Control Center", "/bin/control" },
	{ "System Monitor", "/bin/monitor" },
	{ "FuhrerWeb", "/bin/web" },
	{ "Text Editor", "/bin/edit" },
	{ "Settings", "/bin/settings" },
};
#define NLAUNCH ((int)ARRAY_LEN(launcher))

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

static bool visible(struct window *w)
{
	return w->used && w->state != W_MIN && w->ws == cur_ws && !show_desktop;
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
	damage_all();
}

static void focus_top_visible(void)
{
	for (int i = nz - 1; i >= 0; i--)
		if (visible(&wins[zorder[i]])) {
			set_focus(zorder[i]);
			return;
		}
	focus = -1;
}

static int work_h(void) { return (int)screen->h - PANEL_H; }

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

static void set_state(struct window *w, enum wstate s)
{
	if (w->state == W_NORMAL && s != W_NORMAL && s != W_MIN) {
		w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
	}
	int sw = (int)screen->w, wh = work_h();
	switch (s) {
	case W_MAX: apply_geometry(w, BORDER, TITLE_H + BORDER, sw - 2 * BORDER, wh - TITLE_H - 2 * BORDER); break;
	case W_SNAP_L: apply_geometry(w, BORDER, TITLE_H + BORDER, sw / 2 - 2 * BORDER, wh - TITLE_H - 2 * BORDER); break;
	case W_SNAP_R: apply_geometry(w, sw / 2 + BORDER, TITLE_H + BORDER, sw / 2 - 2 * BORDER, wh - TITLE_H - 2 * BORDER); break;
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

/* Master/stack tiling of the current workspace's normal windows. */
static void retile(void)
{
	if (!tiling)
		return;
	int idx[MAX_WIN], n = 0;
	for (int i = 0; i < nz; i++) {
		struct window *w = &wins[zorder[i]];
		if (w->used && w->ws == cur_ws && w->state != W_MIN)
			idx[n++] = zorder[i];
	}
	int sw = (int)screen->w, wh = work_h(), gap = 6;
	for (int k = 0; k < n; k++) {
		struct window *w = &wins[idx[n - 1 - k]]; /* focused/top = master */
		if (w->state == W_NORMAL) {
			w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
		}
		w->state = W_TILED;
		int x, y, cw, ch;
		if (n == 1) {
			x = gap; y = gap; cw = sw - 2 * gap; ch = wh - 2 * gap;
		} else if (k == 0) {
			x = gap; y = gap; cw = sw / 2 - gap - gap / 2; ch = wh - 2 * gap;
		} else {
			int sh = (wh - gap) / (n - 1);
			x = sw / 2 + gap / 2; y = gap + (k - 1) * sh; cw = sw / 2 - gap - gap / 2; ch = sh - gap;
		}
		apply_geometry(w, x + BORDER, y + TITLE_H + BORDER, cw - 2 * BORDER, ch - TITLE_H - 2 * BORDER);
	}
}

/* ---- drawing ---- */
static void draw_wallpaper(void)
{
	int w = (int)back.w, h = (int)back.h;
	static const u32 themes[3][2] = {
		{ RGB(0x14, 0x1e, 0x33), RGB(0x2c, 0x1b, 0x3d) },
		{ RGB(0x0f, 0x2a, 0x2a), RGB(0x10, 0x14, 0x20) },
		{ RGB(0x33, 0x1d, 0x14), RGB(0x14, 0x18, 0x22) },
	};
	u32 a = themes[wallpaper % 3][0], b = themes[wallpaper % 3][1];
	for (int y = 0; y < h; y++) {
		u32 t = (u32)(y * 255 / (h ? h : 1));
		u32 r = (((a >> 16) & 255) * (255 - t) + ((b >> 16) & 255) * t) / 255;
		u32 g = (((a >> 8) & 255) * (255 - t) + ((b >> 8) & 255) * t) / 255;
		u32 bl = ((a & 255) * (255 - t) + (b & 255) * t) / 255;
		surf_fill(&back, 0, y, w, 1, RGB(r, g, bl));
	}
	surf_text_scaled(&back, w / 2 - 4 * 8 * 4, h / 2 - 80, "FuhrerOS", RGB(0x9f, 0xb8, 0xe8), 4);
	const char *sub = "own kernel  -  adaptive scheduling research";
	surf_text(&back, w / 2 - (int)strlen(sub) * 4, h / 2, sub, RGB(0x7a, 0x8b, 0xb0));
	const char *hint = "Super: launcher   Super+Tab: switch   Super+T: tiling   Super+1..4: workspaces";
	surf_text(&back, w / 2 - (int)strlen(hint) * 4, h / 2 + 24, hint, RGB(0x5f, 0x6d, 0x8c));
}

static void draw_window(struct window *w, bool focused)
{
	int x, y, fw, fh;
	frame_rect(w, &x, &y, &fw, &fh);
	surf_blend_fill(&back, x + 6, y + 8, fw, fh, 0, 70); /* shadow */
	surf_fill(&back, x, y, fw, fh, focused ? C_ACCENT : RGB(0x3a, 0x42, 0x52));
	surf_fill(&back, x + BORDER, y + BORDER, fw - 2 * BORDER, TITLE_H, focused ? C_TITLE_FOCUS : C_TITLE);
	surf_text(&back, x + 12, y + BORDER + 6, w->title, focused ? C_TEXT : C_TEXT_DIM);
	/* buttons: minimize, maximize, close */
	int bx = x + fw - 3 * 28 - 4, by = y + BORDER + 4;
	surf_round_rect(&back, bx, by, 24, 20, 4, RGB(0x34, 0x3d, 0x50));
	surf_fill(&back, bx + 7, by + 13, 10, 2, C_TEXT);
	surf_round_rect(&back, bx + 28, by, 24, 20, 4, RGB(0x34, 0x3d, 0x50));
	surf_rect(&back, bx + 28 + 7, by + 5, 10, 10, C_TEXT);
	surf_round_rect(&back, bx + 56, by, 24, 20, 4, C_CLOSE);
	surf_char(&back, bx + 56 + 8, by + 2, 'x', C_TEXT, 0, true);
	surf_blit(&back, w->x, w->y, &w->surf, 0, 0, w->w, w->h);
}

static void panel_text_right(int *xr, int y, const char *s, u32 c)
{
	*xr -= (int)strlen(s) * 8 + 16;
	surf_text(&back, *xr, y, s, c);
}

static void draw_panel(void)
{
	int sw = (int)back.w, y0 = (int)back.h - PANEL_H;
	surf_fill(&back, 0, y0, sw, PANEL_H, C_PANEL);
	surf_fill(&back, 0, y0, sw, 1, RGB(0x2d, 0x36, 0x48));
	/* launcher button */
	surf_round_rect(&back, 8, y0 + 6, 104, PANEL_H - 12, 6, menu_open ? C_ACCENT : C_PANEL_HI);
	surf_text(&back, 20, y0 + 12, "FuhrerOS", C_TEXT);
	/* workspaces */
	int x = 124;
	for (int i = 0; i < WORKSPACES; i++) {
		char n[2] = { (char)('1' + i), 0 };
		surf_round_rect(&back, x, y0 + 8, 24, PANEL_H - 16, 5, i == cur_ws ? C_ACCENT : C_PANEL_HI);
		surf_text(&back, x + 8, y0 + 12, n, C_TEXT);
		x += 30;
	}
	x += 10;
	/* task buttons for this workspace */
	for (int i = 0; i < nz; i++) {
		struct window *w = &wins[zorder[i]];
		if (!w->used || w->ws != cur_ws)
			continue;
		char t[20];
		strlcpy(t, w->title, sizeof(t));
		u32 bg = zorder[i] == focus && w->state != W_MIN ? RGB(0x3b, 0x4a, 0x66) : C_PANEL_HI;
		surf_round_rect(&back, x, y0 + 6, 150, PANEL_H - 12, 6, bg);
		surf_text(&back, x + 10, y0 + 12, t, w->state == W_MIN ? C_TEXT_DIM : C_TEXT);
		x += 156;
		if (x > sw - 520)
			break;
	}
	/* tray: adaptive state, cpu, clock */
	int xr = sw;
	char buf[64];
	i64 t = time_unix();
	i64 rem = t % 86400;
	snprintf(buf, sizeof(buf), "%02ld:%02ld", rem / 3600, (rem / 60) % 60);
	panel_text_right(&xr, y0 + 12, buf, C_TEXT);
	struct sched_stats ss;
	sched_get_stats(&ss);
	static u64 lb, li, pct;
	u64 db = ss.busy_ns - lb, di = ss.idle_ns - li;
	if (db + di > 200000000ULL) {
		pct = (db * 100) / (db + di);
		lb = ss.busy_ns;
		li = ss.idle_ns;
	}
	snprintf(buf, sizeof(buf), "CPU %lu%%", pct);
	panel_text_right(&xr, y0 + 12, buf, C_TEXT_DIM);
	u8 conf;
	enum task_class c = profiler_system_class(&conf, NULL);
	snprintf(buf, sizeof(buf), "%s: %s", sched_policy_name(), task_class_name(c));
	panel_text_right(&xr, y0 + 12, buf, C_ACCENT);
	if (tiling)
		panel_text_right(&xr, y0 + 12, "[tiling]", C_TEXT_DIM);
}

static void draw_menu(void)
{
	int w = 280, h = 40 + NLAUNCH * 32 + 40, x = 8, y = (int)back.h - PANEL_H - h - 6;
	surf_blend_fill(&back, x + 6, y + 8, w, h, 0, 90);
	surf_round_rect(&back, x, y, w, h, 10, RGB(0x1e, 0x24, 0x31));
	surf_text(&back, x + 16, y + 14, "Applications", C_TEXT_DIM);
	for (int i = 0; i < NLAUNCH; i++) {
		int iy = y + 40 + i * 32;
		bool hot = mx >= x && mx < x + w && my >= iy && my < iy + 30;
		if (hot)
			surf_round_rect(&back, x + 8, iy, w - 16, 30, 6, RGB(0x33, 0x40, 0x59));
		surf_text(&back, x + 20, iy + 7, launcher[i].label, C_TEXT);
	}
	int by = y + h - 36;
	surf_text(&back, x + 20, by + 8, "Shell on console: close all windows", C_TEXT_DIM);
}

static void draw_overview(void)
{
	surf_blend_fill(&back, 0, 0, (int)back.w, (int)back.h - PANEL_H, RGB(0x05, 0x07, 0x0c), 170);
	int idx[MAX_WIN], n = 0;
	for (int i = 0; i < nz; i++)
		if (wins[zorder[i]].used && wins[zorder[i]].ws == cur_ws)
			idx[n++] = zorder[i];
	if (!n) {
		surf_text(&back, (int)back.w / 2 - 80, (int)back.h / 2, "No windows here", C_TEXT_DIM);
		return;
	}
	int cols = n <= 1 ? 1 : n <= 4 ? 2 : 3, rows = (n + cols - 1) / cols;
	int cw = ((int)back.w - 80) / cols, ch = ((int)back.h - PANEL_H - 80) / rows;
	for (int k = 0; k < n; k++) {
		struct window *w = &wins[idx[k]];
		int cx = 40 + (k % cols) * cw, cy = 40 + (k / cols) * ch;
		int tw = cw - 30, th = ch - 50;
		/* nearest-neighbour thumbnail preserving aspect ratio */
		int sx = w->w, sy = w->h;
		if (sx * th > sy * tw)
			th = sy * tw / sx;
		else
			tw = sx * th / sy;
		surf_fill(&back, cx - 2, cy - 2, tw + 4, th + 4, idx[k] == focus ? C_ACCENT : RGB(0x44, 0x4c, 0x5c));
		for (int yy = 0; yy < th; yy++) {
			u32 *dst = back.px + (size_t)(cy + yy) * back.stride + cx;
			const u32 *src = w->surf.px + (size_t)(yy * sy / th) * w->surf.stride;
			if (cy + yy < 0 || cy + yy >= (int)back.h)
				continue;
			for (int xx = 0; xx < tw && cx + xx < (int)back.w; xx++)
				dst[xx] = src[xx * sx / tw];
		}
		surf_text(&back, cx, cy + th + 8, w->title, C_TEXT);
	}
}

static void draw_lock(void)
{
	surf_fill(&back, 0, 0, (int)back.w, (int)back.h, RGB(0x0b, 0x0e, 0x16));
	i64 rem = time_unix() % 86400;
	char buf[16];
	snprintf(buf, sizeof(buf), "%02ld:%02ld", rem / 3600, (rem / 60) % 60);
	surf_text_scaled(&back, (int)back.w / 2 - 5 * 8 * 3, (int)back.h / 2 - 80, buf, C_TEXT, 6);
	const char *m = "FuhrerOS is locked - press any key";
	surf_text(&back, (int)back.w / 2 - (int)strlen(m) * 4, (int)back.h / 2 + 40, m, C_TEXT_DIM);
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
	dirty_all = false;
	dx0 = dy0 = dx1 = dy1 = 0;
	irq_restore(fl);
	if (locked) {
		draw_lock();
	} else {
		draw_wallpaper();
		for (int i = 0; i < nz; i++) {
			struct window *w = &wins[zorder[i]];
			if (visible(w))
				draw_window(w, zorder[i] == focus);
		}
		if (overview)
			draw_overview();
		draw_panel();
		if (menu_open)
			draw_menu();
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
	frames_drawn++;
	compose_ns_total += time_ns() - t0;
}

static void compositor_thread(void *arg)
{
	u64 last = 0;
	for (;;) {
		u64 f = irq_save();
		if (!dirty_all && dx1 <= dx0) {
			wq_wait(&comp_wq, 1000000000ULL); /* at least once a second: clock */
		}
		irq_restore(f);
		u64 now = time_ns();
		if (now - last < 16000000ULL) /* cap at ~60 fps */
			sleep_ns(16000000ULL - (now - last));
		last = time_ns();
		if ((u64)time_unix() / 60 != last_clock_s) {
			last_clock_s = (u64)time_unix() / 60;
			damage_all();
		}
		/* panel indicators refresh every second */
		static u64 last_panel;
		if (last - last_panel > 1000000000ULL) {
			last_panel = last;
			damage(0, (int)back.h - PANEL_H, (int)back.w, PANEL_H);
		}
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
	memset(win->surf.px, 0x22, bytes);
	win->used = true;
	win->id = id;
	win->owner = p->pid;
	strlcpy(win->title, title, sizeof(win->title));
	wq_init(&win->wq);
	w = MIN(MAX(w, 120), (int)cap_w - 40);
	h = MIN(MAX(h, 60), work_h() - TITLE_H - 40);
	/* cascade new windows */
	static int cascade;
	int x = 60 + (cascade % 8) * 36, y = 50 + TITLE_H + (cascade % 8) * 30;
	cascade++;
	if (x + w > (int)screen->w)
		x = 20;
	if (y + h > work_h())
		y = TITLE_H + 10;
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
		if (wins[i].used && wins[i].owner == pid) {
			/* still alive at this point: unmap from it, then free */
			win_destroy(&wins[i]);
		}
	irq_restore(f);
	mutex_unlock(&comp_lock);
}

static void launch(const char *path)
{
	const char *argv[] = { path, NULL };
	int r = proc_spawn(path, argv, NULL, "/home");
	if (r < 0)
		KLOG("desktop", "cannot launch %s (%d)", path, r);
}

/* ---- input handling ---- */
static int window_at(int x, int y, int *part)
{
	for (int i = nz - 1; i >= 0; i--) {
		struct window *w = &wins[zorder[i]];
		if (!visible(w))
			continue;
		int fx, fy, fw, fh;
		frame_rect(w, &fx, &fy, &fw, &fh);
		if (x < fx || y < fy || x >= fx + fw || y >= fy + fh)
			continue;
		if (y < w->y) {
			int bx = fx + fw - 3 * 28 - 4;
			*part = x >= bx + 56 ? 3 : x >= bx + 28 ? 2 : x >= bx ? 1 : 0; /* 0 title */
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
	if (ws < 0)
		ws = WORKSPACES - 1;
	cur_ws = ws % WORKSPACES;
	show_desktop = false;
	overview = false;
	focus = -1;
	focus_top_visible();
	retile();
	damage_all();
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

static bool handle_shortcut(const struct input_event *ev)
{
	if (ev->type != EV_KEY || !ev->value)
		return false;
	u16 m = ev->mods;
	struct window *fw = focus >= 0 ? &wins[focus] : NULL;
	if (locked) {
		locked = false;
		damage_all();
		return true;
	}
	if (ev->code == KEY_SUPER && !(m & ~MOD_SUPER)) {
		menu_open = !menu_open;
		damage_all();
		return true;
	}
	bool super = m & MOD_SUPER;
	if ((super && ev->code == KEY_TAB) || ((m & MOD_ALT) && ev->code == KEY_TAB)) {
		cycle_focus((m & MOD_SHIFT) ? -1 : 1);
		return true;
	}
	if ((m & MOD_CTRL) && (m & MOD_ALT) && (ev->code == 't')) {
		launch("/bin/term");
		return true;
	}
	if (!super)
		return false;
	switch (ev->code) {
	case KEY_LEFT: if (fw) set_state(fw, W_SNAP_L); return true;
	case KEY_RIGHT: if (fw) set_state(fw, W_SNAP_R); return true;
	case KEY_UP: if (fw) set_state(fw, fw->state == W_MAX ? W_NORMAL : W_MAX); return true;
	case KEY_DOWN: if (fw) set_state(fw, fw->state == W_NORMAL ? W_MIN : W_NORMAL); return true;
	case 'd': show_desktop = !show_desktop; damage_all(); return true;
	case 'l': locked = true; menu_open = false; damage_all(); return true;
	case 'o': case 'w': overview = !overview; damage_all(); return true;
	case 't':
		tiling = !tiling;
		if (!tiling)
			for (int i = 0; i < MAX_WIN; i++)
				if (wins[i].used && wins[i].state == W_TILED)
					set_state(&wins[i], W_NORMAL);
		retile();
		damage_all();
		return true;
	case 'e': launch("/bin/files"); return true;
	case 'q': if (fw) push_event(fw, (struct fu_wevent){ .type = WEV_CLOSE }); return true;
	}
	if (ev->code >= '1' && ev->code <= '4') {
		int ws = ev->code - '1';
		if ((m & MOD_SHIFT) && fw) {
			fw->ws = ws;
			focus_top_visible();
		} else {
			switch_workspace(ws);
		}
		damage_all();
		return true;
	}
	return false;
}

static void handle_gesture(const struct input_event *ev)
{
	switch (ev->code) {
	case GESTURE_THREE_SWIPE_UP: overview = true; break;
	case GESTURE_THREE_SWIPE_DOWN: overview = false; show_desktop = !show_desktop; break;
	case GESTURE_THREE_SWIPE_LEFT: cycle_focus(-1); break;
	case GESTURE_THREE_SWIPE_RIGHT: cycle_focus(1); break;
	case GESTURE_FOUR_SWIPE_LEFT: switch_workspace(cur_ws - 1); break;
	case GESTURE_FOUR_SWIPE_RIGHT: switch_workspace(cur_ws + 1); break;
	default: return;
	}
	damage_all();
}

static void click(int x, int y)
{
	int sw = (int)screen->w, y0 = (int)screen->h - PANEL_H;
	if (menu_open) {
		int w = 280, h = 40 + NLAUNCH * 32 + 40, mx0 = 8, my0 = y0 - h - 6;
		menu_open = false;
		damage_all();
		if (x >= mx0 && x < mx0 + w && y >= my0 + 40 && y < my0 + 40 + NLAUNCH * 32) {
			launch(launcher[(y - my0 - 40) / 32].path);
			return;
		}
		if (x >= mx0 && x < mx0 + w && y >= my0 && y < my0 + h)
			return;
	}
	if (overview) {
		/* pick the thumbnail under the pointer by reusing the layout math */
		int idx[MAX_WIN], n = 0;
		for (int i = 0; i < nz; i++)
			if (wins[zorder[i]].used && wins[zorder[i]].ws == cur_ws)
				idx[n++] = zorder[i];
		int cols = n <= 1 ? 1 : n <= 4 ? 2 : 3, rows = n ? (n + cols - 1) / cols : 1;
		int cw = (sw - 80) / cols, ch = ((int)screen->h - PANEL_H - 80) / rows;
		int k = ((y - 40) / ch) * cols + (x - 40) / cw;
		overview = false;
		if (x >= 40 && y >= 40 && k >= 0 && k < n) {
			if (wins[idx[k]].state == W_MIN)
				wins[idx[k]].state = W_NORMAL;
			set_focus(idx[k]);
		}
		damage_all();
		return;
	}
	if (y >= y0) { /* panel */
		if (x < 116) {
			menu_open = true;
		} else if (x < 124 + WORKSPACES * 30) {
			switch_workspace((x - 124) / 30);
		} else {
			int bx = 124 + WORKSPACES * 30 + 10;
			for (int i = 0; i < nz; i++) {
				struct window *w = &wins[zorder[i]];
				if (!w->used || w->ws != cur_ws)
					continue;
				if (x >= bx && x < bx + 150) {
					if (zorder[i] == focus && w->state != W_MIN)
						set_state(w, W_MIN);
					else {
						if (w->state == W_MIN)
							w->state = W_NORMAL;
						show_desktop = false;
						set_focus(zorder[i]);
					}
					break;
				}
				bx += 156;
			}
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
	case 0: drag = idx; drag_ox = x - w->x; drag_oy = y - w->y; break;
	case 1: set_state(w, W_MIN); break;
	case 2: set_state(w, w->state == W_MAX ? W_NORMAL : W_MAX); break;
	case 3: push_event(w, (struct fu_wevent){ .type = WEV_CLOSE }); break;
	case 4: resize = idx; drag_ox = x; drag_oy = y; break;
	case 5:
		push_event(w, (struct fu_wevent){ .type = WEV_MOUSE_BUTTON, .x = x - w->x, .y = y - w->y,
						   .value = 1, .mods = input_modifiers() });
		break;
	}
}

static void compositor_input(const struct input_event *ev)
{
	u64 f = irq_save();
	switch (ev->type) {
	case EV_KEY:
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
			w->y = MAX(my - drag_oy, TITLE_H);
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
		if (menu_open)
			damage_all();
		break;
	}
	case EV_BUTTON:
		if (ev->code == 1 && ev->value) {
			btn_down = true;
			click(mx, my);
		} else if (ev->code == 1) {
			btn_down = false;
			if (drag >= 0 && my < 4) /* drop at the top edge: maximise */
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

/* ---- syscalls ---- */
i64 sys_win(u64 nr, u64 a, u64 b, u64 c, u64 d, u64 e)
{
	struct process *p = proc_current();
	if (nr == SYS_WIN_INFO) {
		struct fu_screen s = { screen ? screen->w : 0, screen ? screen->h - PANEL_H : 0, running };
		return copy_to_user((void *)a, &s, sizeof(s));
	}
	if (nr == SYS_DESKTOP_CTL) {
		switch (a) {
		case 1: /* start */
			if (!screen)
				return -E_NOSYS;
			if (!running) {
				extern void desktop_start(void);
				desktop_start();
			}
			return 0;
		case 2: wallpaper = (u32)b; damage_all(); kick(); return 0;
		case 3: return running;
		case 4: launch("/bin/term"); return 0;
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
			if (overview)
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
	pb_printf(pb, "running: %d\nworkspace: %d\ntiling: %d\nwindows:", running, cur_ws + 1, tiling);
	int n = 0;
	for (int i = 0; i < MAX_WIN; i++)
		n += wins[i].used;
	pb_printf(pb, " %d\nframes: %lu\navg_compose_us: %lu\n", n, frames_drawn,
		  frames_drawn ? compose_ns_total / frames_drawn / 1000 : 0);
	for (int i = 0; i < MAX_WIN; i++)
		if (wins[i].used)
			pb_printf(pb, "win %d pid %d ws %d %dx%d at %d,%d state %d presents %u '%s'\n", i,
				  wins[i].owner, wins[i].ws + 1, wins[i].w, wins[i].h, wins[i].x, wins[i].y,
				  wins[i].state, wins[i].presents, wins[i].title);
}

void desktop_start(void)
{
	screen = fb_screen();
	if (!screen || running)
		return;
	back.w = screen->w;
	back.h = screen->h;
	back.stride = screen->w;
	back.px = vmalloc_pages(ALIGN_UP((u64)back.w * back.h * 4, PAGE_SIZE) / PAGE_SIZE);
	wq_init(&comp_wq);
	mutex_init(&comp_lock);
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
	procfs_register("desktop", (void (*)(void *))gen_desktop);
}
