CC ?= cc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -g -O2
LDLIBS = -pthread
SRC = src/main.c src/input.c src/parser.c src/builtin.c src/signals.c src/process.c src/redirect.c src/thread.c
OBJ = $(SRC:src/%.c=build/%.o)
TARGET = bin/shellforge
PYTHON ?= python3
DEMOS = bin/process-demo bin/pipe-demo bin/ls-grep-demo bin/zombie-demo
.PHONY: all run clean test asan asan-test web test-web
all: $(TARGET) $(DEMOS)
$(TARGET): $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) $(OBJ) $(LDLIBS) -o $@
build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
bin/process-demo bin/pipe-demo bin/ls-grep-demo: examples/demo_main.c examples/process_demo.c examples/pipe_demo.c examples/ls_grep_demo.c
	@mkdir -p bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@
bin/zombie-demo: examples/zombie_demo.c
	@mkdir -p bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@
run: all
	./$(TARGET)
test: all
	$(PYTHON) -m unittest discover -s tests -p 'test_shell.py' -v
asan: $(SRC) $(wildcard include/*.h)
	@mkdir -p bin
	$(CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Wpedantic -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $(SRC) $(LDLIBS) -o bin/shellforge-asan
asan-test: asan
	SHELLFORGE_BINARY=$(CURDIR)/bin/shellforge-asan $(PYTHON) -m unittest discover -s tests -p 'test_shell.py' -v
web: all
	$(PYTHON) web/server.py
test-web: all
	$(PYTHON) -m unittest discover -s tests -p 'test_web.py' -v
clean:
	rm -rf build bin
-include $(OBJ:.o=.d)
