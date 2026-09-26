/* Intrusive circular doubly-linked list. Usable from C and C++. */
#ifndef FUHRER_LIST_H
#define FUHRER_LIST_H

#include "types.h"

struct list_node {
	struct list_node *prev, *next;
};

#define LIST_INIT(name) { &(name), &(name) }

static inline void list_init(struct list_node *h) { h->prev = h->next = h; }
static inline bool list_empty(const struct list_node *h) { return h->next == h; }
static inline bool list_linked(const struct list_node *n) { return n->next && n->next != n; }

static inline void list_insert_between(struct list_node *n, struct list_node *a,
				       struct list_node *b)
{
	n->prev = a;
	n->next = b;
	a->next = n;
	b->prev = n;
}
static inline void list_push_back(struct list_node *h, struct list_node *n)
{
	list_insert_between(n, h->prev, h);
}
static inline void list_push_front(struct list_node *h, struct list_node *n)
{
	list_insert_between(n, h, h->next);
}
static inline void list_remove(struct list_node *n)
{
	n->prev->next = n->next;
	n->next->prev = n->prev;
	n->prev = n->next = n;
}
static inline struct list_node *list_pop_front(struct list_node *h)
{
	if (list_empty(h))
		return NULL;
	struct list_node *n = h->next;
	list_remove(n);
	return n;
}

#define list_for_each(it, head) for (struct list_node *it = (head)->next; it != (head); it = it->next)
#define list_for_each_safe(it, tmp, head)                                          \
	for (struct list_node *it = (head)->next, *tmp = it->next; it != (head); \
	     it = tmp, tmp = it->next)

#endif
