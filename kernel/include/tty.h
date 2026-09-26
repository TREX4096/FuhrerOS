#ifndef FUHRER_TTY_H
#define FUHRER_TTY_H
#include "types.h"

#include "uapi/fuhrer.h"

struct file;
struct input_event;
void tty_init(void);
void tty_input_char(char c);
void tty_key_event(const struct input_event *ev);
struct file *tty_open_console(int flags);
#endif
