# bm for the PowKiddy RGB30 (Rockchip RK3566, Cortex-A55, AArch64):
#   make TARGET=rgb30            kernel for the RGB30 (build/rgb30/)
#   make TARGET=rgb30 test       the same kernel for QEMU's virt machine, tested there
# Toolchain (Ubuntu/WSL): apt install gcc-aarch64-linux-gnu picolibc-aarch64-linux-gnu
#                         qemu-system-arm (tests), mtools dosfstools (SD image)
# Included by the Makefile when TARGET=rgb30; the Pi build does not change.

CROSS64 ?= aarch64-linux-gnu-
CC      := $(CROSS64)gcc
OBJCOPY := $(CROSS64)objcopy
OBJDUMP := $(CROSS64)objdump
QEMU64  ?= qemu-system-aarch64
PYTHON  ?= python3
DIST    := dist/rgb30

# PLAT=rk3566 (the console) or PLAT=virt (QEMU, tests)
PLAT    ?= rk3566
ifeq ($(PLAT),virt)
BUILD   := build/rgb30-virt
PLAT_DEF := -DPLAT_VIRT
KERNEL_BASE := 0x40080000
else
BUILD   := build/rgb30
PLAT_DEF := -DPLAT_RK3566
KERNEL_BASE := 0x10000000
endif

VERSION := $(shell git describe --always --dirty 2>/dev/null || echo dev)

# -Wno-format: uint32_t is unsigned long on the Pi and unsigned int here; the
# shared code prints it with %lu (kprintf ignores the l) and would warn everywhere.
# Ubuntu's gcc defaults (PIE, stack protector, fortify, branch protection)
# are for Linux programs: off for a kernel. Outline atomics need getauxval.
ARCH    := -mcpu=cortex-a55 -mno-outline-atomics -mbranch-protection=none \
           -fno-pie -fno-stack-protector -U_FORTIFY_SOURCE
COMMON  := $(ARCH) --specs=picolibc.specs -std=c11 -O2 -Wall -Wextra -g -Isrc -Isrc/rgb30 \
           -ffunction-sections -fdata-sections $(PLAT_DEF) -DBM_RGB30 \
           -Wno-format
CFLAGS   = $(COMMON) -D_DEFAULT_SOURCE -Ithird_party/lua $(WARN)
ASFLAGS := $(ARCH) $(PLAT_DEF) -g -Isrc -Isrc/rgb30
LDFLAGS := $(ARCH) --specs=picolibc.specs -nostartfiles -static -no-pie -Wl,--gc-sections \
           -Wl,--defsym=KERNEL_BASE=$(KERNEL_BASE)
LDLIBS  := -Wl,--start-group -lc -lm -lgcc -Wl,--end-group

LUA_SRCS := $(wildcard third_party/lua/*.c)
# the parts of bm that run unchanged on the RGB30
SHARED_SRCS := src/gfx/console.c src/gfx/draw.c src/gfx/font8x16.c src/gfx/font8x14.c \
               src/gfx/font6x12.c src/lib/printf.c src/lib/crc32.c \
               src/script/luavm.c src/script/lib_bm.c \
               src/kernel/version.c src/kernel/crumbs.c src/kernel/config.c src/fs/fat.c
RGB30_SRCS := $(wildcard src/rgb30/*.c src/rgb30/*.S)
KERNEL_SRCS := $(RGB30_SRCS) $(SHARED_SRCS) $(LUA_SRCS)
KERNEL_OBJS := $(patsubst %,$(BUILD)/k/%.o,$(KERNEL_SRCS))
LUA_OBJS    := $(patsubst %,$(BUILD)/k/%.o,$(LUA_SRCS))
$(LUA_OBJS): WARN := -w

VERSION_STAMP := $(BUILD)/version.txt
$(VERSION_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(VERSION)' | cmp -s - $@ || echo '$(VERSION)' > $@
$(BUILD)/k/src/kernel/version.c.o: $(VERSION_STAMP)
$(BUILD)/k/src/kernel/version.c.o: CFLAGS += -DBM_VERSION=\"$(VERSION)\"
FORCE:

.DEFAULT_GOAL := all
.PHONY: all test qemu clean firmware image sdcard FORCE

all: $(BUILD)/kernel8.img
ifeq ($(PLAT),rk3566)
	@mkdir -p $(DIST)
	cp $(BUILD)/kernel8.img $(DIST)/kernel8.img
endif

# Boot loader (from ROCKNIX's image) and the Realtek firmware: not stored in
# the repository, downloaded and checked once (scripts/fetch-rgb30.sh)
FW64 := firmware/rgb30
firmware:
	./scripts/fetch-rgb30.sh $(FW64)

# Whole SD card image: the boot loader at the sectors the RK3566 boot ROM
# and U-Boot's SPL read (64, 16384), then one FAT32 partition "BM" from
# 16 MiB with extlinux.conf, kernel8.img and bm/. Write it with balenaEtcher,
# Raspberry Pi Imager or Rufus; updates: copy kernel8.img onto the card.
SD_FILES64 = $(BUILD)/kernel8.img=kernel8.img boot/rgb30/extlinux.conf=extlinux/extlinux.conf \
             boot/rgb30/LEGGIMI.txt=LEGGIMI.txt \
             $(FW64)/rtl8821cs_fw.bin=bm/rtl8821cs_fw.bin $(FW64)/rtl8821cs_config.bin=bm/rtl8821cs_config.bin \
             $(wildcard $(FW64)/LICENCE.rtlwifi_firmware.txt)$(if $(wildcard $(FW64)/LICENCE.rtlwifi_firmware.txt),=bm/LICENCE.rtlwifi_firmware.txt)
image: $(BUILD)/kernel8.img
	@test -f $(FW64)/u-boot.itb || { echo "Run 'make TARGET=rgb30 firmware' first"; exit 1; }
	@mkdir -p $(DIST)
	$(PYTHON) scripts/mksd.py $(DIST)/bm-rgb30.img --size-mib 256 --start-mib 16 --label BM --active \
	    --raw $(FW64)/idbloader.img@64 --raw $(FW64)/u-boot.itb@16384 $(SD_FILES64)
	gzip -9 -k -f $(DIST)/bm-rgb30.img
	@echo "Write $(DIST)/bm-rgb30.img (or .img.gz) to a microSD card, slot TF1."

# The files of the BM partition, to copy by hand onto a card made with `image`
sdcard: $(BUILD)/kernel8.img
	@mkdir -p $(DIST)/sd/extlinux $(DIST)/sd/bm
	cp $(BUILD)/kernel8.img $(DIST)/sd/kernel8.img
	cp boot/rgb30/extlinux.conf $(DIST)/sd/extlinux/
	cp boot/rgb30/LEGGIMI.txt $(DIST)/sd/
	@if [ -f $(FW64)/rtl8821cs_fw.bin ]; then cp $(FW64)/rtl8821cs_*.bin $(DIST)/sd/bm/; fi
	@echo "Copy the contents of $(DIST)/sd/ to the BM drive of the card."

$(BUILD)/k/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/k/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/kernel.elf: $(KERNEL_OBJS) src/rgb30/kernel.ld
	$(CC) $(LDFLAGS) -T src/rgb30/kernel.ld -Wl,-Map=$(BUILD)/kernel.map $(KERNEL_OBJS) $(LDLIBS) -o $@

# arm64 Image: what U-Boot's booti loads (header in src/rgb30/start.S)
$(BUILD)/kernel8.img: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(stat -c %s $@) bytes"

# QEMU: the virt build, serial on stdio
qemu:
	$(MAKE) -f rgb30.mk PLAT=virt
	$(QEMU64) -M virt,gic-version=3 -cpu cortex-a55 -m 512M -device ramfb -nic none \
	    -kernel build/rgb30-virt/kernel.elf -serial stdio -display none

test:
	$(MAKE) -f rgb30.mk PLAT=virt
	$(PYTHON) tests/rgb30/qemu_test.py --build build/rgb30-virt

clean:
	rm -rf build/rgb30 build/rgb30-virt

-include $(KERNEL_OBJS:.o=.d)
