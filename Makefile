CC ?= cc
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DESTDIR ?=

CPPFLAGS ?= -Isrc
BASE_CFLAGS = -Wall -Wextra -Wshadow -Werror -std=c11 \
	-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
CFLAGS ?= -O2
ALL_CFLAGS = $(BASE_CFLAGS) $(CFLAGS)

TARGET = simple-decision
SOURCES = src/main.c src/decision.c
HEADERS = src/decision.h
BUILD_DIR = build
TEST_TARGET = $(BUILD_DIR)/simple-decision-test

.PHONY: all clean install test test-docs sanitize analyze

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) -o $@ $(SOURCES)

$(TEST_TARGET): $(SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) -DSIMPLE_DECISION_TESTING -o $@ $(SOURCES)

test: all $(TEST_TARGET)
	PROGRAM=./$(TARGET) sh tests/test_add.sh
	PROGRAM=./$(TARGET) sh tests/test_query.sh
	PROGRAM=./$(TARGET) TEST_PROGRAM=./$(TEST_TARGET) sh tests/test_concurrency.sh
	PROGRAM=./$(TARGET) sh tests/test_limits.sh
	$(MAKE) test-docs

test-docs: all
	PROGRAM=./$(TARGET) sh tests/test_docs.sh

install: $(TARGET)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-O1 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer' all $(TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=1 PROGRAM=./$(TARGET) sh tests/test_add.sh
	ASAN_OPTIONS=detect_leaks=1 PROGRAM=./$(TARGET) sh tests/test_query.sh
	ASAN_OPTIONS=detect_leaks=1 PROGRAM=./$(TARGET) \
		TEST_PROGRAM=./$(TEST_TARGET) sh tests/test_concurrency.sh
	ASAN_OPTIONS=detect_leaks=1 PROGRAM=./$(TARGET) sh tests/test_limits.sh

$(BUILD_DIR):
	mkdir -p $@

analyze: | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g -fanalyzer \
		-o $(BUILD_DIR)/simple-decision-analyze $(SOURCES)

clean:
	rm -f $(TARGET)
	rm -rf $(BUILD_DIR)
