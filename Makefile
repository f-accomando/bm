# bm33 - bare metal console for Raspberry Pi Zero / Zero W (BCM2835)

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
QEMU    ?= qemu-system-arm
PYTHON  ?= python3

BUILD   := build
DIST    := dist
FW_DIR  := firmware

# Serial port of the USB-TTL adapter, used by `make run-serial`.
PORT    ?= /dev/ttyUSB0
BAUD    ?= 115200

VERSION := $(shell git describe --always --dirty 2>/dev/null || echo dev)

ARCH    := -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard
COMMON  := $(ARCH) -std=c11 -O2 -Wall -Wextra -g -Isrc \
           -ffunction-sections -fdata-sections \
           -DUART_BAUD=$(BAUD) -DBM33_VERSION=\"$(VERSION)\"
# Kernel: hosted C on top of newlib (libc, libm), see src/lib/syscalls.c.
CFLAGS  = $(COMMON) -D_DEFAULT_SOURCE -Ithird_party/lua $(WARN)
# Chainloader: freestanding, no libc.
LCFLAGS := $(COMMON) -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns
ASFLAGS := $(ARCH) -g -Isrc -Isrc/kernel
LDFLAGS := $(ARCH) -nostartfiles -Wl,--gc-sections
LDLIBS  := -Wl,--start-group -lc -lm -lgcc -Wl,--end-group
LLDLIBS := -nostdlib -lgcc

LUA_SRCS    := $(wildcard third_party/lua/*.c)
KERNEL_SRCS := $(shell find src -name '*.c' -o -name '*.S') $(LUA_SRCS)
LOADER_SRCS := $(wildcard chainloader/*.S chainloader/*.c) \
               src/drivers/uart.c src/drivers/gpio.c src/drivers/mbox.c \
               src/drivers/prop.c src/drivers/timer.c src/drivers/led.c \
               src/arch/cache.c src/lib/crc32.c

KERNEL_OBJS := $(patsubst %,$(BUILD)/k/%.o,$(KERNEL_SRCS))
LOADER_OBJS := $(patsubst %,$(BUILD)/l/%.o,$(LOADER_SRCS))
LUA_OBJS    := $(patsubst %,$(BUILD)/k/%.o,$(LUA_SRCS))

# Third-party code: its own warning policy, not ours.
$(LUA_OBJS): WARN := -w
# Lua scripts embedded with .incbin
$(BUILD)/k/src/script/embed.S.o: $(wildcard src/script/*.lua)

.DEFAULT_GOAL := all
.PHONY: all clean firmware sdcard sdcard-chainloader qemu qemu-screenshot \
        run-serial test disasm

all: $(BUILD)/kernel.img $(BUILD)/chainloader.img

$(BUILD)/k/%.S.o $(BUILD)/l/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/k/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/l/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(LCFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/kernel.elf: $(KERNEL_OBJS) linker.ld
	$(CC) $(LDFLAGS) -T linker.ld -Wl,-Map=$(BUILD)/kernel.map $(KERNEL_OBJS) $(LDLIBS) -o $@

$(BUILD)/chainloader.elf: $(LOADER_OBJS) chainloader/linker.ld
	$(CC) $(LDFLAGS) -T chainloader/linker.ld -Wl,-Map=$(BUILD)/chainloader.map $(LOADER_OBJS) $(LLDLIBS) -o $@

$(BUILD)/%.img: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(stat -c %s $@) bytes"

disasm: $(BUILD)/kernel.elf $(BUILD)/chainloader.elf
	$(OBJDUMP) -d $(BUILD)/kernel.elf > $(BUILD)/kernel.lst
	$(OBJDUMP) -d $(BUILD)/chainloader.elf > $(BUILD)/chainloader.lst

firmware:
	./scripts/fetch-firmware.sh $(FW_DIR)

# FAT32 boot partition contents. KERNEL=chainloader puts the serial loader on
# the card instead of the kernel (flash it once, then use `make run-serial`).
KERNEL ?= kernel
sdcard: $(BUILD)/$(KERNEL).img
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)
	cp $(FW_DIR)/bootcode.bin $(FW_DIR)/start.elf $(FW_DIR)/fixup.dat $(DIST)/
	cp boot/config.txt $(DIST)/
	cp $(BUILD)/$(KERNEL).img $(DIST)/kernel.img
	@echo "Copy the contents of $(DIST)/ ($(KERNEL)) to the root of a FAT32 SD card."

sdcard-chainloader:
	$(MAKE) sdcard KERNEL=chainloader

# Upload the kernel to a Pi running the chainloader, then open a terminal.
# Rebooting the Pi (monitor command 'r') re-sends the current kernel.img.
run-serial: $(BUILD)/kernel.img
	$(PYTHON) tools/bm33_load.py --baud $(BAUD) $(PORT) $<

# QEMU boots raw images at 0x8000 through -bios, like the real firmware.
QEMU_ARGS := -M raspi0 -serial stdio -serial null

qemu: $(BUILD)/kernel.img
	$(QEMU) $(QEMU_ARGS) -bios $<

qemu-screenshot: $(BUILD)/kernel.img
	./scripts/qemu-screenshot.sh $< $(BUILD)/screen.png

test: all
	$(PYTHON) tests/qemu_test.py --build $(BUILD)

clean:
	rm -rf $(BUILD) $(DIST)

-include $(KERNEL_OBJS:.o=.d) $(LOADER_OBJS:.o=.d)
