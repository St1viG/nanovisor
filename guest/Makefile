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
         -O2 -Wall -Wextra -Iinc -I../common

# lib/ is linked into every image; each tests/<name>.c becomes build/<name>.img.
LIB_SRCS  = $(wildcard lib/*.c)
LIB_OBJS  = $(patsubst lib/%.c,$(BUILD_DIR)/lib/%.o,$(LIB_SRCS))
TEST_SRCS = $(wildcard tests/*.c)
TEST_OBJS = $(patsubst tests/%.c,$(BUILD_DIR)/tests/%.o,$(TEST_SRCS))
IMGS      = $(patsubst tests/%.c,$(BUILD_DIR)/%.img,$(TEST_SRCS))
ELFS      = $(patsubst tests/%.c,$(BUILD_DIR)/%.elf,$(TEST_SRCS))
DEPS      = $(LIB_OBJS:.o=.d) $(TEST_OBJS:.o=.d)

.PHONY: all clean debug dirs

all: dirs $(IMGS) $(BUILD_DIR)/guest.img

dirs:
	@mkdir -p $(BUILD_DIR)/lib $(BUILD_DIR)/tests

$(BUILD_DIR)/lib/%.o: lib/%.c | dirs
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/tests/%.o: tests/%.c | dirs
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/%.img: $(BUILD_DIR)/tests/%.o $(LIB_OBJS) guest.ld
	$(LD) -T guest.ld $< $(LIB_OBJS) -o $@

# The spec names guest.img as the canonical guest binary; keep it pointing at
# the baseline image so the phase 0 gate keeps working unchanged.
$(BUILD_DIR)/guest.img: $(BUILD_DIR)/hello.img
	cp $< $@

# Same links emitted as ELF, so the layout can be checked:
#   make debug && readelf -S build/hello.elf
debug: dirs $(ELFS)

$(BUILD_DIR)/%.elf: $(BUILD_DIR)/tests/%.o $(LIB_OBJS) guest.ld
	$(LD) -T guest.ld --oformat=elf64-x86-64 $< $(LIB_OBJS) -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
