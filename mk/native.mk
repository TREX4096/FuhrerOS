# Native build of the FuhrerOS adaptive layer, runtime and benchmarks.
# Used inside the Alpine build container (scripts/build.sh), but works on
# any Linux with gcc + liburing headers:  make -f mk/native.mk [test|install]

CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration \
           -Iadaptive/include -Iruntime/libfuhrer -D_FORTIFY_SOURCE=2 -fstack-protector-strong
LDLIBS_URING = -luring
O       := build/native
DESTDIR ?=
PREFIX  ?= /usr

CORE_SRC := adaptive/common.c adaptive/profiler/profiler.c adaptive/features/features.c \
            adaptive/policy/knobs.c adaptive/policy/policies.c \
            adaptive/controller/config.c adaptive/controller/engine.c
CORE_OBJ := $(CORE_SRC:%.c=$(O)/%.o)
LIB_OBJ  := $(O)/runtime/libfuhrer/fuhrer_io.o

BINS := $(O)/fuhrerd $(O)/fuhrer $(O)/fuhrer-bench
TESTS := $(O)/test_adaptive $(O)/test_libfuhrer

all: $(BINS) $(O)/libfuhrer.a

$(O)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)/libfuhrer-core.a: $(CORE_OBJ)
	$(AR) rcs $@ $^

$(O)/libfuhrer.a: $(LIB_OBJ) $(O)/adaptive/controller/config.o
	$(AR) rcs $@ $^

$(O)/fuhrerd: $(O)/adaptive/controller/fuhrerd.o $(O)/libfuhrer-core.a
	$(CC) $(CFLAGS) $^ -o $@

$(O)/fuhrer: $(O)/runtime/cli/fuhrer.o $(O)/libfuhrer-core.a
	$(CC) $(CFLAGS) $^ -o $@

$(O)/fuhrer-bench: $(O)/benchmarks/fuhrer-bench.o $(LIB_OBJ) $(O)/libfuhrer-core.a
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS_URING) -lpthread -lm

$(O)/test_adaptive: $(O)/tests/unit/test_adaptive.o $(O)/libfuhrer-core.a
	$(CC) $(CFLAGS) $^ -o $@

$(O)/test_libfuhrer: $(O)/tests/unit/test_libfuhrer.o $(LIB_OBJ) $(O)/libfuhrer-core.a
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS_URING)

test: $(TESTS)
	$(O)/test_adaptive
	cd $(O) && ./test_libfuhrer

install: all $(TESTS)
	install -Dm755 $(O)/fuhrerd       $(DESTDIR)$(PREFIX)/sbin/fuhrerd
	install -Dm755 $(O)/fuhrer        $(DESTDIR)$(PREFIX)/bin/fuhrer
	install -Dm755 $(O)/fuhrer-bench  $(DESTDIR)$(PREFIX)/bin/fuhrer-bench
	install -Dm644 $(O)/libfuhrer.a   $(DESTDIR)$(PREFIX)/lib/libfuhrer.a
	install -Dm644 runtime/libfuhrer/fuhrer.h $(DESTDIR)$(PREFIX)/include/fuhrer.h
	install -Dm644 adaptive/include/fuhrer/shm.h $(DESTDIR)$(PREFIX)/include/fuhrer/shm.h
	install -Dm755 $(O)/test_adaptive  $(DESTDIR)$(PREFIX)/libexec/fuhrer/test_adaptive
	install -Dm755 $(O)/test_libfuhrer $(DESTDIR)$(PREFIX)/libexec/fuhrer/test_libfuhrer

clean:
	rm -rf $(O)

.PHONY: all test install clean
