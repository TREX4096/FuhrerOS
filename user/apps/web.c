/* web - FuhrerWeb: an HTTP page viewer, the first step of the browser
 * foundation (NEW_EXPLANATION §28). It is deliberately NOT a browser engine:
 * it fetches over FuhrerOS's own TCP/IP stack, strips markup, and renders
 * headings, paragraphs, list items and links (clickable) as wrapped text.
 * No CSS, JavaScript, images or TLS. Porting a real engine is future work. */
#include "gui.h"

#define MAXBYTES (256 * 1024)
#define MAXRUNS 4000
#define MAXLINKS 200

enum { R_TEXT, R_H1, R_H2, R_LINK, R_BR, R_PARA, R_LI };
struct run {
	int kind;
	int link;
	char *text;
};

static char *page;
static int page_len;
static struct run runs[MAXRUNS];
static int nruns;
static char links[MAXLINKS][256];
static int nlinks;
static char url[256] = "http://example.com/";
static char title[128] = "FuhrerWeb";
static char status[160];
static int scroll;
static char urlbar[256];
static bool editing_url;
/* on-screen link hit boxes */
static struct { int x, y, w, link; } hits[512];
static int nhits;

static int http_get(const char *u, char *out, int max, char *err, int errn)
{
	if (!strncmp(u, "https://", 8)) {
		snprintf(err, (size_t)errn, "https is not supported (no TLS yet)");
		return -1;
	}
	if (!strncmp(u, "http://", 7))
		u += 7;
	char host[128];
	const char *slash = strchr(u, '/');
	size_t hl = slash ? (size_t)(slash - u) : strlen(u);
	if (hl >= sizeof(host))
		return -1;
	memcpy(host, u, hl);
	host[hl] = 0;
	uint16_t port = 80;
	char *c = strchr(host, ':');
	if (c) {
		*c = 0;
		port = (uint16_t)atoi(c + 1);
	}
	uint32_t ip;
	int r = resolve(host, &ip);
	if (r < 0) {
		snprintf(err, (size_t)errn, "cannot resolve %s: %s", host, strerror(r));
		return -1;
	}
	int fd = socket(AF_INET, SOCK_STREAM);
	if ((r = connect(fd, ip, port)) < 0) {
		snprintf(err, (size_t)errn, "connect failed: %s", strerror(r));
		close(fd);
		return -1;
	}
	char req[512];
	int n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: FuhrerWeb/0.1\r\n\r\n",
			 slash ? slash : "/", host);
	send(fd, req, (size_t)n);
	int got = 0;
	ssize_t k;
	while (got < max - 1 && (k = recv(fd, out + got, (size_t)(max - 1 - got))) > 0)
		got += (int)k;
	close(fd);
	out[got] = 0;
	char *body = strstr(out, "\r\n\r\n");
	int status_code = got > 12 ? atoi(out + 9) : 0;
	if (!body) {
		snprintf(err, (size_t)errn, "malformed HTTP response");
		return -1;
	}
	body += 4;
	int blen = got - (int)(body - out);
	memmove(out, body, (size_t)blen + 1);
	snprintf(err, (size_t)errn, "HTTP %d, %d bytes", status_code, blen);
	return blen;
}

static void add_run(int kind, int link, char *t)
{
	if (nruns < MAXRUNS)
		runs[nruns++] = (struct run){ kind, link, t };
}

static void resolve_link(const char *href, char *out)
{
	if (!strncmp(href, "http://", 7) || !strncmp(href, "https://", 8)) {
		strlcpy(out, href, 256);
	} else if (href[0] == '/') {
		const char *p = url + 7;
		const char *sl = strchr(p, '/');
		size_t hl = sl ? (size_t)(sl - url) : strlen(url);
		snprintf(out, 256, "%.*s%s", (int)hl, url, href);
	} else {
		const char *last = strrchr(url + 7, '/');
		size_t bl = last ? (size_t)(last - url) + 1 : strlen(url);
		snprintf(out, 256, "%.*s%s%s", (int)bl, url, last ? "" : "/", href);
	}
}

/* Tag-soup parser: text between tags becomes runs. */
static void parse(void)
{
	nruns = nlinks = 0;
	int kind = R_TEXT, link = -1;
	bool in_title = false, skip = false;
	char *p = page;
	while (*p) {
		if (*p == '<') {
			char *e = strchr(p, '>');
			if (!e)
				break;
			*e = 0;
			char *tag = p + 1;
			bool close = *tag == '/';
			if (close)
				tag++;
			char name[12] = { 0 };
			for (int i = 0; i < 11 && tag[i] && tag[i] != ' ' && tag[i] != '/'; i++)
				name[i] = (char)tolower(tag[i]);
			if (!strcmp(name, "script") || !strcmp(name, "style"))
				skip = !close;
			else if (!strcmp(name, "title"))
				in_title = !close;
			else if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]) {
				kind = close ? R_TEXT : (name[1] <= '2' ? R_H1 : R_H2);
				add_run(R_PARA, -1, NULL);
			} else if (!strcmp(name, "p") || !strcmp(name, "div") || !strcmp(name, "tr")) {
				add_run(R_PARA, -1, NULL);
			} else if (!strcmp(name, "br")) {
				add_run(R_BR, -1, NULL);
			} else if (!strcmp(name, "li") && !close) {
				add_run(R_LI, -1, NULL);
			} else if (!strcmp(name, "a")) {
				if (close) {
					link = -1;
				} else {
					char *h = strstr(tag, "href=");
					if (h && nlinks < MAXLINKS) {
						h += 5;
						char q = *h == '"' || *h == '\'' ? *h++ : ' ';
						char *he = h;
						while (*he && *he != q && *he != '>')
							he++;
						char save = *he;
						*he = 0;
						resolve_link(h, links[nlinks]);
						*he = save;
						link = nlinks++;
					}
				}
			}
			p = e + 1;
			continue;
		}
		char *t = p;
		while (*p && *p != '<')
			p++;
		char save = *p;
		*p = 0;
		/* collapse whitespace and decode a few entities in place */
		char *w = t;
		bool sp = false;
		for (char *r = t; *r; r++) {
			char c = *r;
			if (c == '&') {
				if (!strncmp(r, "&amp;", 5)) { c = '&'; r += 4; }
				else if (!strncmp(r, "&lt;", 4)) { c = '<'; r += 3; }
				else if (!strncmp(r, "&gt;", 4)) { c = '>'; r += 3; }
				else if (!strncmp(r, "&nbsp;", 6)) { c = ' '; r += 5; }
				else if (!strncmp(r, "&quot;", 6)) { c = '"'; r += 5; }
			}
			if (c == '\n' || c == '\r' || c == '\t')
				c = ' ';
			if (c == ' ') {
				if (sp)
					continue;
				sp = true;
			} else {
				sp = false;
			}
			*w++ = c;
		}
		*w = 0;
		if (in_title)
			strlcpy(title, t, sizeof(title));
		else if (!skip && t[0] && strcmp(t, " "))
			add_run(link >= 0 ? R_LINK : kind, link, strdup(t));
		*p = save;
	}
}

static void navigate(const char *u)
{
	strlcpy(url, u, sizeof(url));
	strlcpy(urlbar, u, sizeof(urlbar));
	snprintf(status, sizeof(status), "loading %s ...", u);
	strlcpy(title, "FuhrerWeb", sizeof(title));
	page_len = http_get(u, page, MAXBYTES, status, sizeof(status));
	if (page_len < 0) {
		page[0] = 0;
		page_len = 0;
	}
	parse();
	scroll = 0;
}

static void render(struct gwin *g)
{
	g_fill(g, 0, 0, g->w, g->h, RGB(0xf4, 0xf5, 0xf7));
	/* toolbar */
	g_fill(g, 0, 0, g->w, 40, RGB(0x22, 0x28, 0x36));
	g_round(g, 8, 7, g->w - 16, 26, 8, editing_url ? RGB(0x33, 0x3d, 0x52) : RGB(0x2b, 0x33, 0x44));
	g_text(g, 20, 12, urlbar, T_FG);
	if (editing_url)
		g_fill(g, 20 + (int)strlen(urlbar) * FONT_W, 12, 2, 16, T_ACCENT);
	/* content */
	nhits = 0;
	int x = 24, y = 56 - scroll, maxx = g->w - 24;
	uint32_t ink = RGB(0x1e, 0x22, 0x2b);
	for (int i = 0; i < nruns; i++) {
		struct run *r = &runs[i];
		if (r->kind == R_PARA) { if (x > 24) { x = 24; y += 26; } else y += 8; continue; }
		if (r->kind == R_BR) { x = 24; y += 20; continue; }
		if (r->kind == R_LI) { x = 24; y += 20; if (y > 40 && y < g->h) g_text(g, x, y, "*", ink); x = 40; continue; }
		int scale = r->kind == R_H1 ? 2 : 1;
		uint32_t col = r->kind == R_LINK ? RGB(0x1a, 0x5f, 0xd6) : r->kind == R_H2 ? RGB(0x12, 0x3c, 0x7c) : ink;
		/* word wrap */
		char *s = r->text;
		while (*s) {
			char *e = s;
			while (*e && *e != ' ')
				e++;
			int wl = (int)(e - s) + (*e == ' ');
			int wpx = wl * FONT_W * scale;
			if (x + wpx > maxx && x > 24) {
				x = 24;
				y += (FONT_H + 4) * scale;
			}
			if (y > 40 && y < g->h - 20) {
				if (scale == 1)
					g_textn(g, x, y, s, wl, col);
				else {
					char tmp[128];
					int n = MIN(wl, 127);
					memcpy(tmp, s, (size_t)n);
					tmp[n] = 0;
					g_text_big(g, x, y, tmp, col, 2);
				}
				if (r->kind == R_LINK) {
					g_fill(g, x, y + FONT_H, wpx - (*e == ' ' ? FONT_W : 0), 1, col);
					if (nhits < 512)
						hits[nhits++] = (typeof(hits[0])){ x, y, wpx, r->link };
				}
			}
			x += wpx;
			s = *e ? e + 1 : e;
		}
		if (scale == 2) {
			x = 24;
			y += 40;
		}
	}
	g_fill(g, 0, g->h - 22, g->w, 22, RGB(0xe2, 0xe5, 0xea));
	g_text(g, 8, g->h - 19, status, RGB(0x4a, 0x52, 0x60));
	gwin_present(g);
	gwin_title(g, title);
}

int main(int argc, char **argv)
{
	page = malloc(MAXBYTES);
	struct gwin g;
	if (gwin_open(&g, 820, 560, "FuhrerWeb") < 0)
		return 1;
	navigate(argc > 1 ? argv[1] : url);
	for (;;) {
		render(&g);
		struct fu_wevent ev;
		if (gwin_event(&g, &ev, 0) < 0)
			continue;
		if (ev.type == WEV_CLOSE) {
			gwin_close(&g);
			return 0;
		}
		if (ev.type == WEV_SCROLL)
			scroll = MAX(0, scroll + ev.value * 40);
		if (ev.type == WEV_MOUSE_BUTTON && ev.value == 1) {
			editing_url = ev.y < 40;
			for (int i = 0; i < nhits; i++)
				if (ev.x >= hits[i].x && ev.x < hits[i].x + hits[i].w && ev.y >= hits[i].y &&
				    ev.y < hits[i].y + FONT_H) {
					navigate(links[hits[i].link]);
					break;
				}
		}
		if (ev.type == WEV_KEY && ev.value) {
			if (editing_url) {
				size_t l = strlen(urlbar);
				if (ev.ch == '\n') {
					editing_url = false;
					navigate(urlbar);
				} else if (ev.ch == 8 && l) {
					urlbar[l - 1] = 0;
				} else if (ev.ch >= 32 && ev.ch < 127 && l < sizeof(urlbar) - 1) {
					urlbar[l] = (char)ev.ch;
					urlbar[l + 1] = 0;
				}
			} else if (ev.key == K_DOWN || ev.key == K_PGDN) {
				scroll += ev.key == K_PGDN ? 400 : 40;
			} else if (ev.key == K_UP || ev.key == K_PGUP) {
				scroll = MAX(0, scroll - (ev.key == K_PGUP ? 400 : 40));
			} else if (ev.ch == 'l' || (ev.mods & M_CTRL)) {
				editing_url = true;
				urlbar[0] = 0;
			}
		}
	}
}
