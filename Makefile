# bm33 - bare metal console for Raspberry Pi Zero / Zero W (BCM2835)

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
QEMU    ?= qemu-system-arm
PYTHON  ?= python3

# BOOT=stress builds a kernel that runs the rendering stress test at boot
# (its own build directory, so normal and stress objects never mix).
BOOT    ?= normal
ifeq ($(BOOT),stress)
BUILD   := build-stress
BOOT_DEFS := -DBM33_BOOT_STRESS
else
BUILD   := build
BOOT_DEFS :=
endif
DIST    := dist
FW_DIR  := firmware

# Serial port of the USB-TTL adapter, used by `make run-serial`.
PORT    ?= /dev/ttyUSB0
BAUD    ?= 115200

VERSION := $(shell git describe --always --dirty 2>/dev/null || echo dev)

ARCH    := -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard
COMMON  := $(ARCH) -std=c11 -O2 -Wall -Wextra -g -Isrc \
           -ffunction-sections -fdata-sections \
           -DUART_BAUD=$(BAUD) $(BOOT_DEFS)
# Kernel: hosted C on top of newlib (libc, libm), see src/lib/syscalls.c.
CFLAGS  = $(COMMON) -D_DEFAULT_SOURCE -Ithird_party/lua $(WARN)
# Chainloader: freestanding, no libc.
LCFLAGS := $(COMMON) -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns
ASFLAGS := $(ARCH) -g -Isrc -Isrc/kernel -Wa,-I$(BUILD)
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

# The version string lives in one object, rebuilt when `git describe` changes.
VERSION_STAMP := $(BUILD)/version.txt
$(VERSION_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(VERSION)' | cmp -s - $@ || echo '$(VERSION)' > $@
$(BUILD)/k/src/kernel/version.c.o: $(VERSION_STAMP)
$(BUILD)/k/src/kernel/version.c.o: CFLAGS += -DBM33_VERSION=\"$(VERSION)\"
FORCE:

# Third-party code: its own warning policy, not ours.
$(LUA_OBJS): WARN := -w
# Lua scripts embedded with .incbin
$(BUILD)/k/src/script/embed.S.o: $(wildcard src/script/*.lua) spec/s32/conformance/demo.cart \
                                 $(BUILD)/demo.b33 $(BUILD)/stress.b33

$(BUILD)/stress.b33: carts/stress/main.lua scripts/mkb33.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkb33.py -o $@ --lua $< --title "bm33 stress test" --author bm33

# Native demo cartridge (.b33): Lua + sprite sheet + map
DEMO_B33_SRC := carts/demo/main.lua carts/demo/sheet.png carts/demo/map.csv
$(BUILD)/demo.b33: $(DEMO_B33_SRC) scripts/mkb33.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkb33.py -o $@ --lua carts/demo/main.lua --sheet carts/demo/sheet.png \
	    --map carts/demo/map.csv --title "bm33 native demo" --author bm33

# Demo games (Lua only, sprites drawn in code): build/carts/<name>.b33
GAMES := pong snake shooter astrowing
GAME_CARTS := $(patsubst %,$(BUILD)/carts/%.b33,$(GAMES))
title_pong    := Pong
title_snake   := Snake
title_shooter := Star Shooter
title_astrowing := Astro Wing
# carts/<game>/cover.png (optional, scripts/mkcovers.py): printed on the
# cartridge in the menu
.SECONDEXPANSION:
$(BUILD)/carts/%.b33: carts/%/main.lua scripts/mkb33.py $$(wildcard carts/$$*/cover.png)
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkb33.py -o $@ --lua $< --title "$(title_$*)" --author bm33 \
	    $(if $(wildcard carts/$*/cover.png),--cover carts/$*/cover.png)

.DEFAULT_GOAL := all
.PHONY: FORCE all clean firmware image sdcard sdcard-chainloader sdcard-stress qemu qemu-screenshot \
        run-serial test test-s32 test-s32-arm test-b33 test-usb test-audio test-fat disasm

all: $(BUILD)/kernel.img $(BUILD)/chainloader.img $(GAME_CARTS)

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
# Cartridges go to carts/ (the menu also looks in the root directory).
KERNEL ?= kernel
SD_CARTS := $(GAME_CARTS) $(BUILD)/demo.b33 $(BUILD)/stress.b33 spec/s32/conformance/demo.cart
sdcard: $(BUILD)/$(KERNEL).img $(SD_CARTS)
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)/carts
	cp $(FW_DIR)/bootcode.bin $(FW_DIR)/start.elf $(FW_DIR)/fixup.dat $(DIST)/
	cp boot/config.txt $(DIST)/
	cp $(BUILD)/$(KERNEL).img $(DIST)/kernel.img
	cp $(SD_CARTS) $(DIST)/carts/
	@if [ -f $(FW_DIR)/BCM43430A1.hcd ]; then mkdir -p $(DIST)/bm33 && \
	    cp $(FW_DIR)/BCM43430A1.hcd $(DIST)/bm33/ && echo "cp BCM43430A1.hcd -> $(DIST)/bm33/"; fi
	@echo "Copy the contents of $(DIST)/ ($(KERNEL)) to the root of a FAT32 SD card."

# Whole SD card image (MBR + FAT32): firmware, config, kernel and the
# cartridges. Write it with Raspberry Pi Imager ("Use custom"), balenaEtcher
# or dd. Needs dosfstools and mtools.
image: $(BUILD)/kernel.img $(SD_CARTS)
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)
	$(PYTHON) scripts/mksd.py $(DIST)/bm33.img --size-mib 64 --label BM33 \
	    $(FW_DIR)/bootcode.bin=bootcode.bin $(FW_DIR)/start.elf=start.elf \
	    $(FW_DIR)/fixup.dat=fixup.dat boot/config.txt=config.txt \
	    $(BUILD)/kernel.img=kernel.img \
	    $(foreach c,$(SD_CARTS),$(c)=carts/$(notdir $(c))) \
	    $(if $(wildcard $(FW_DIR)/BCM43430A1.hcd),$(FW_DIR)/BCM43430A1.hcd=bm33/BCM43430A1.hcd)

sdcard-chainloader:
	$(MAKE) sdcard KERNEL=chainloader

# SD card contents with the stress-test kernel (make sdcard-stress)
sdcard-stress:
	$(MAKE) sdcard BOOT=stress

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

test: all test-s32 test-b33 test-usb test-fat test-audio
	$(PYTHON) tests/qemu_test.py --build $(BUILD)

$(BUILD)/host/test_b33: tests/b33/test_b33.c src/b33/gfx16.c src/b33/r3d.c src/b33/format.c src/lib/crc32.c src/b33/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/b33/test_b33.c src/b33/gfx16.c src/b33/r3d.c src/b33/format.c src/lib/crc32.c -lm

test-fat: $(BUILD)/host/test_fat
	$(PYTHON) tests/fs/run.py $<

$(BUILD)/host/test_fat: tests/fs/test_fat.c src/fs/fat.c src/fs/fat.h src/drivers/sd.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/fs/test_fat.c src/fs/fat.c

test-audio: $(BUILD)/host/test_audio
	$<

$(BUILD)/host/test_audio: tests/audio/test_audio.c src/audio/synth.c src/audio/iec958.c src/audio/synth.h src/audio/iec958.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/audio/test_audio.c src/audio/synth.c src/audio/iec958.c

test-usb: $(BUILD)/host/test_hid
	$<

$(BUILD)/host/test_hid: tests/usb/test_hid.c src/usb/hid.c src/usb/hid.h src/usb/usb.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/usb/test_hid.c src/usb/hid.c

test-b33: $(BUILD)/host/test_b33 $(BUILD)/demo.b33
	$< $(BUILD)/demo.b33

# s32 conformance (spec/s32): the C core must reproduce lua32's vectors.
S32_CORE := src/s32/cpu.c src/s32/ppu.c src/s32/cart.c src/lib/crc32.c
HOSTCC   ?= cc

$(BUILD)/host/s32_conformance: tests/s32/conformance.c $(S32_CORE) src/s32/s32.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/s32/conformance.c $(S32_CORE)

# Same code built for the ARM1176 with the kernel's compiler, run in qemu-arm
# (semihosting): catches target-specific differences.
$(BUILD)/host/s32_conformance_arm: tests/s32/conformance.c $(S32_CORE) src/s32/s32.h
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -O2 -Isrc --specs=rdimon.specs -o $@ tests/s32/conformance.c $(S32_CORE)

test-s32: $(BUILD)/host/s32_conformance
	$< spec/s32/conformance

test-s32-arm: $(BUILD)/host/s32_conformance_arm
	qemu-arm -cpu arm1176 $< spec/s32/conformance

clean:
	rm -rf build build-stress $(DIST)

-include $(KERNEL_OBJS:.o=.d) $(LOADER_OBJS:.o=.d)
