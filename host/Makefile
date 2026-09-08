CC = gcc

BUILD_DIR ?= build

# -pthread belongs in CFLAGS as well as the link line: it defines _REENTRANT
# and selects the right libc paths. -lpthread at link time alone does not.
CFLAGS  = -Wall -Wextra -g -O2 -D_GNU_SOURCE -pthread -Iinc
LDFLAGS = -pthread

SRCS = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

BIN = $(BUILD_DIR)/hypervisor

.PHONY: all clean

all: $(BUILD_DIR) $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BIN): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
