CC = gcc
LD = ld

BUILD_DIR ?= build

# -mno-red-zone                    : hardware IRQ delivery pushes the trap frame AT rsp, straight
#                                    through the SysV red zone. Mandatory once an ISR exists.
# -mgeneral-regs-only              : no SSE, which __attribute__((interrupt)) handlers may not use.
# -fno-builtin                     : do not assume a hosted libc.
# -fno-tree-loop-distribute-patterns: keeps -O2 from rewriting the byte loops inside our own
#                                    memset/memcpy into calls to themselves.
CFLAGS = -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone \
         -fno-stack-protector -mgeneral-regs-only \
         -fno-asynchronous-unwind-tables -fcf-protection=none \
         -fno-builtin -fno-tree-loop-distribute-patterns \
         -O2 -Wall -Wextra -Iinc

SRCS = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

IMG = $(BUILD_DIR)/guest.img
ELF = $(BUILD_DIR)/guest.elf

.PHONY: all clean debug

all: $(BUILD_DIR) $(IMG)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(IMG): $(OBJS) guest.ld
	$(LD) -T guest.ld $(OBJS) -o $@

# Identical link emitted as ELF instead of a flat binary, so the section layout
# can be inspected:  make debug && readelf -S build/guest.elf
debug: $(BUILD_DIR) $(ELF)

$(ELF): $(OBJS) guest.ld
	$(LD) -T guest.ld --oformat=elf64-x86-64 $(OBJS) -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
