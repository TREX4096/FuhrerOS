/* fsh - the FuhrerOS shell.
 *
 * Features: line editing with history (raw tty mode, arrow keys), pipelines
 * (a | b | c), redirection (< > >>), background jobs (&), `;` sequencing,
 * builtins (cd, pwd, exit, help, clear, echo, time, history, sched, jobs),
 * and script mode (`sh -c "cmd"` / `sh file`). Commands are looked up in
 * /bin. */
#include "fu.h"

#define MAXARGS 32
#define HIST 32

static char history[HIST][256];
static int hcount;
static int last_status;
static bool interactive;

static void prompt(void)
{
	char cwd[256];
	getcwd(cwd, sizeof(cwd));
	printf("\033[1;32mfuhrer\033[0m:\033[1;34m%s\033[0m%s ", cwd, last_status ? "\033[31m$\033[0m" : "$");
	flush();
}

/* Raw-mode line editor: left/right, backspace, delete, up/down history. */
static int edit_line(char *buf, int max)
{
	int len = 0, pos = 0, hpos = hcount;
	ioctl(0, TTY_SETMODE, 0);
	for (;;) {
		char c;
		if (read(0, &c, 1) <= 0) {
			ioctl(0, TTY_SETMODE, TTY_ECHO | TTY_CANON);
			return -1;
		}
		if (c == '\n' || c == '\r') {
			write(1, "\n", 1);
			break;
		}
		if (c == 4 && len == 0) { /* ^D */
			ioctl(0, TTY_SETMODE, TTY_ECHO | TTY_CANON);
			return -1;
		}
		if (c == 3) { /* ^C */
			write(1, "^C\n", 3);
			len = pos = 0;
			break;
		}
		if (c == 12) { /* ^L */
			write(1, "\033[2J\033[H", 7);
			prompt();
			write(1, buf, (size_t)len);
			continue;
		}
		if (c == 27) {
			char seq[3] = { 0 };
			read(0, &seq[0], 1);
			read(0, &seq[1], 1);
			if (seq[1] == '3')
				read(0, &seq[2], 1); /* "~" of Delete */
			int oldlen = len;
			if (seq[1] == 'A' || seq[1] == 'B') {
				if (seq[1] == 'A' && hpos > 0)
					hpos--;
				else if (seq[1] == 'B' && hpos < hcount)
					hpos++;
				const char *h = hpos < hcount ? history[hpos % HIST] : "";
				/* erase current line */
				while (pos < len) { write(1, " ", 1); pos++; }
				while (pos > 0) { write(1, "\b \b", 3); pos--; }
				(void)oldlen;
				len = (int)strlcpy(buf, h, (size_t)max);
				pos = len;
				write(1, buf, (size_t)len);
			} else if (seq[1] == 'C' && pos < len) {
				write(1, &buf[pos++], 1);
			} else if (seq[1] == 'D' && pos > 0) {
				write(1, "\b", 1);
				pos--;
			} else if (seq[1] == '3' && pos < len) {
				memmove(buf + pos, buf + pos + 1, (size_t)(len - pos - 1));
				len--;
				write(1, buf + pos, (size_t)(len - pos));
				write(1, " ", 1);
				for (int i = pos; i <= len; i++)
					write(1, "\b", 1);
			}
			continue;
		}
		if (c == 8 || c == 127) {
			if (pos > 0) {
				memmove(buf + pos - 1, buf + pos, (size_t)(len - pos));
				pos--;
				len--;
				write(1, "\b", 1);
				write(1, buf + pos, (size_t)(len - pos));
				write(1, " ", 1);
				for (int i = pos; i <= len; i++)
					write(1, "\b", 1);
			}
			continue;
		}
		if ((unsigned char)c < 32 && c != '\t')
			continue;
		if (len < max - 1) {
			memmove(buf + pos + 1, buf + pos, (size_t)(len - pos));
			buf[pos] = c;
			len++;
			write(1, buf + pos, (size_t)(len - pos));
			pos++;
			for (int i = pos; i < len; i++)
				write(1, "\b", 1);
		}
	}
	buf[len] = 0;
	ioctl(0, TTY_SETMODE, TTY_ECHO | TTY_CANON);
	if (len && (hcount == 0 || strcmp(history[(hcount - 1) % HIST], buf)))
		strlcpy(history[hcount++ % HIST], buf, 256);
	return len;
}

struct cmd {
	char *argv[MAXARGS];
	int argc;
	char *in, *out;
	bool append;
};

/* Tokenise one pipeline segment; supports "double quotes". */
static int parse_cmd(char *s, struct cmd *c)
{
	memset(c, 0, sizeof(*c));
	while (*s) {
		while (isspace(*s))
			s++;
		if (!*s)
			break;
		char **target = NULL;
		if (*s == '<' || *s == '>') {
			if (*s == '>' && s[1] == '>') {
				c->append = true;
				s++;
			}
			target = *s == '<' ? &c->in : &c->out;
			s++;
			while (isspace(*s))
				s++;
		}
		char *tok = s, *w = s;
		bool q = false;
		while (*s && (q || !isspace(*s))) {
			if (*s == '"') {
				q = !q;
				s++;
				continue;
			}
			*w++ = *s++;
		}
		if (*s)
			s++;
		*w = 0;
		if (target)
			*target = tok;
		else if (c->argc < MAXARGS - 1)
			c->argv[c->argc++] = tok;
	}
	c->argv[c->argc] = NULL;
	return c->argc;
}

static void resolve_path(const char *name, char *out, size_t n)
{
	if (strchr(name, '/'))
		strlcpy(out, name, n);
	else
		snprintf(out, n, "/bin/%s", name);
}

static const char *help_text =
	"FuhrerOS shell builtins:\n"
	"  cd DIR   pwd   echo ARGS   clear   history   time CMD   exit [N]   help\n"
	"Commands in /bin (try them):\n"
	"  ls cat cp mv rm mkdir touch write head wc grep stat df   (files)\n"
	"  ps top kill mem cpu uptime uname date dmesg lspci         (system)\n"
	"  sched [policy|prio PID N]   schedbench   iobench   cachectl (research)\n"
	"  ifconfig ping dns fetch httpd nc                          (network)\n"
	"  desktop                                                   (graphical desktop)\n"
	"Syntax: a | b   > file   >> file   < file   cmd &   a ; b\n";

static int run_pipeline(char *line, bool background);

static bool builtin(struct cmd *c, int *status)
{
	const char *n = c->argv[0];
	if (!strcmp(n, "cd")) {
		int r = chdir(c->argc > 1 ? c->argv[1] : "/home");
		if (r < 0)
			dprintf(2, "cd: %s: %s\n", c->argc > 1 ? c->argv[1] : "/home", strerror(r));
		*status = r < 0;
		return true;
	}
	if (!strcmp(n, "pwd")) {
		char b[256];
		puts(getcwd(b, sizeof(b)) ? b : "?");
		*status = 0;
		return true;
	}
	if (!strcmp(n, "exit")) {
		exit(c->argc > 1 ? atoi(c->argv[1]) : 0);
	}
	if (!strcmp(n, "help")) {
		printf("%s", help_text);
		*status = 0;
		return true;
	}
	if (!strcmp(n, "clear")) {
		printf("\033[2J\033[H");
		*status = 0;
		return true;
	}
	if (!strcmp(n, "history")) {
		int start = hcount > HIST ? hcount - HIST : 0;
		for (int i = start; i < hcount; i++)
			printf("%4d  %s\n", i + 1, history[i % HIST]);
		*status = 0;
		return true;
	}
	if (!strcmp(n, "time") && c->argc > 1) {
		char rest[512] = "";
		for (int i = 1; i < c->argc; i++) {
			strlcat(rest, c->argv[i], sizeof(rest));
			strlcat(rest, " ", sizeof(rest));
		}
		uint64_t t0 = uptime_ns();
		*status = run_pipeline(rest, false);
		uint64_t dt = uptime_ns() - t0;
		printf("real %lu.%03lus\n", dt / 1000000000UL, (dt / 1000000UL) % 1000);
		return true;
	}
	return false;
}

static int run_pipeline(char *line, bool background)
{
	char *segs[8];
	int n = 0;
	char *save, *s = strtok_r(line, "|", &save);
	while (s && n < 8) {
		segs[n++] = s;
		s = strtok_r(NULL, "|", &save);
	}
	if (!n)
		return 0;
	struct cmd cmds[8];
	for (int i = 0; i < n; i++)
		if (!parse_cmd(segs[i], &cmds[i]))
			return 0;
	int status = 0;
	if (n == 1 && builtin(&cmds[0], &status))
		return status;

	pid_t pids[8];
	int prev_rd = -1;
	for (int i = 0; i < n; i++) {
		int stdio[3] = { 0, 1, 2 };
		int p[2] = { -1, -1 };
		int opened_in = -1, opened_out = -1;
		if (prev_rd >= 0)
			stdio[0] = prev_rd;
		if (i < n - 1) {
			if (pipe(p) < 0) {
				dprintf(2, "sh: pipe failed\n");
				return 1;
			}
			stdio[1] = p[1];
		}
		if (cmds[i].in) {
			opened_in = open(cmds[i].in, O_RDONLY);
			if (opened_in < 0) {
				dprintf(2, "sh: %s: %s\n", cmds[i].in, strerror(opened_in));
				return 1;
			}
			stdio[0] = opened_in;
		}
		if (cmds[i].out) {
			opened_out = open(cmds[i].out, O_WRONLY | O_CREAT | (cmds[i].append ? O_APPEND : O_TRUNC));
			if (opened_out < 0) {
				dprintf(2, "sh: %s: %s\n", cmds[i].out, strerror(opened_out));
				return 1;
			}
			stdio[1] = opened_out;
		}
		char path[128];
		resolve_path(cmds[i].argv[0], path, sizeof(path));
		pids[i] = spawn(path, (const char *const *)cmds[i].argv, stdio);
		if (pids[i] < 0)
			dprintf(2, "sh: %s: %s\n", cmds[i].argv[0], strerror(pids[i]));
		if (prev_rd >= 0)
			close(prev_rd);
		if (p[1] >= 0)
			close(p[1]);
		if (opened_in >= 0)
			close(opened_in);
		if (opened_out >= 0)
			close(opened_out);
		prev_rd = p[0];
	}
	if (background) {
		printf("[bg] pid %d\n", pids[n - 1]);
		return 0;
	}
	/* Ctrl-C goes to the last process of the pipeline. */
	if (interactive && pids[n - 1] > 0)
		ioctl(0, TTY_SETFG, (unsigned long)pids[n - 1]);
	for (int i = 0; i < n; i++)
		if (pids[i] > 0) {
			int st = 0;
			wait(pids[i], &st);
			status = st;
		}
	if (interactive)
		ioctl(0, TTY_SETFG, 0);
	return status;
}

static int run_line(char *line)
{
	char *hash = strchr(line, '#');
	if (hash && (hash == line || isspace(hash[-1])))
		*hash = 0;
	char *save, *part = strtok_r(line, ";", &save);
	int st = 0;
	while (part) {
		while (isspace(*part))
			part++;
		size_t l = strlen(part);
		while (l && isspace(part[l - 1]))
			part[--l] = 0;
		bool bg = l && part[l - 1] == '&';
		if (bg)
			part[--l] = 0;
		if (l)
			st = run_pipeline(part, bg);
		part = strtok_r(NULL, ";", &save);
	}
	last_status = st;
	return st;
}

int main(int argc, char **argv)
{
	char line[512];
	if (argc > 2 && !strcmp(argv[1], "-c")) {
		strlcpy(line, argv[2], sizeof(line));
		return run_line(line);
	}
	if (argc > 1 && !strcmp(argv[1], "-i")) {
		/* driven by the desktop terminal through pipes: it edits lines
		 * itself, so we print prompts and read whole lines */
		printf("\033[1;36mFuhrerOS shell\033[0m - type 'help' for commands\n");
		chdir("/home");
		for (;;) {
			prompt();
			if (readline(0, line, sizeof(line)) <= 0)
				return 0;
			char *nl = strchr(line, '\n');
			if (nl)
				*nl = 0;
			run_line(line);
		}
	}
	if (argc > 1) { /* script file */
		int fd = open(argv[1], O_RDONLY);
		if (fd < 0) {
			dprintf(2, "sh: %s: %s\n", argv[1], strerror(fd));
			return 1;
		}
		int st = 0;
		while (readline(fd, line, sizeof(line)) > 0) {
			char *nl = strchr(line, '\n');
			if (nl)
				*nl = 0;
			st = run_line(line);
		}
		close(fd);
		return st;
	}
	struct stat st;
	interactive = fstat(0, &st) == 0 && st.type == VT_CHAR;
	if (interactive) {
		printf("\n\033[1;36mFuhrerOS shell\033[0m - type 'help' for commands\n");
		chdir("/home");
	}
	for (;;) {
		if (interactive)
			prompt();
		int n = interactive ? edit_line(line, sizeof(line)) : (int)readline(0, line, sizeof(line));
		if (n < 0)
			break;
		char *nl = strchr(line, '\n');
		if (nl)
			*nl = 0;
		run_line(line);
	}
	return 0;
}
