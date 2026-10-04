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
BRANCH := $(or $(GITHUB_HEAD_REF),$(GITHUB_REF_NAME),$(shell git rev-parse --abbrev-ref HEAD 2>/dev/null),unknown)

# -Wno-format: uint32_t is unsigned long on the Pi and unsigned int here; the
# shared code prints it with %lu (kprintf ignores the l) and would warn everywhere.
# Ubuntu's gcc defaults (PIE, stack protector, fortify, branch protection)
# are for Linux programs: off for a kernel. Outline atomics need getauxval.
ARCH    := -mcpu=cortex-a55 -mno-outline-atomics -mbranch-protection=none \
           -fno-pie -fno-stack-protector -U_FORTIFY_SOURCE
# Only picolibc's headers and the compiler's own: Ubuntu's AArch64 cross
# compiler also searches glibc's (libc6-dev-arm64-cross, which apt installs
# with it) and the PC's /usr/include, which are not a kernel's (mbedTLS
# found sys/socket.h there, and it clashed with picolibc's types)
SYSINC  := -nostdinc -isystem $(shell $(CC) -print-file-name=include)
COMMON  := $(ARCH) --specs=picolibc.specs $(SYSINC) -std=c11 -O2 -Wall -Wextra -g -Isrc -Isrc/rgb30 \
           -ffunction-sections -fdata-sections $(PLAT_DEF) -DBM_RGB30 \
           -Wno-format
CFLAGS   = $(COMMON) -D_DEFAULT_SOURCE -Ithird_party/lua \
           -Ithird_party/mbedtls/include -Ithird_party/lwip/src/include -Isrc/net \
           -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' $(WARN)
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
# Bluetooth: the Pi's stack (HCI, L2CAP, HID, BLE + SMP) over H5 to the
# Realtek chip; SMP's elliptic curves come from mbedTLS, and WPA2's SHA-1
# and AES (src/rgb30/wpa.c)
BT_SRCS := src/bt/bt.c src/bt/ble.c src/bt/hci.c src/bt/h5.c src/bt/rtlbt.c src/bt/smp_crypto.c \
           src/usb/hid.c
# all of mbedTLS, as on the Pi: TLS for the updates from GitHub too (the
# linker keeps what is used)
MBEDTLS_SRCS := $(wildcard third_party/mbedtls/library/*.c)
# the network: lwIP and the Pi's glue (DHCP, the network console, file
# transfer) over the WiFi (src/rgb30/rtw_sta.c)
LWIP_SRCS := $(wildcard third_party/lwip/src/core/*.c third_party/lwip/src/core/ipv4/*.c) \
             third_party/lwip/src/netif/ethernet.c third_party/lwip/src/apps/sntp/sntp.c
NET_SRCS := src/net/net.c src/net/netcon.c src/net/netxfer.c src/net/cartnet.c
# updates from GitHub (System > Updates): HTTPS, the signed manifest
# (manifest-rgb30.txt: kernel8.img and bm/ca.pem), the Pi's update code
NET_SRCS += src/net/stream.c src/net/tls.c src/net/http.c src/net/http_kernel.c src/net/release.c \
            src/kernel/update.c
# the reports to a git repository (src/kernel/reports.c, as on the Pi)
NET_SRCS += src/net/github.c src/net/report.c src/kernel/reports.c
# the Pi's cartridges (.bm, listed for testing; show_bm=0 hides them): the runtime unchanged,
# the Pi's drivers it calls replaced by src/rgb30/bm_port.c and bm_input.c; the menu is
# the Pi's (menu_ui.c, its icons) at 360x360
BM_SRCS := $(filter-out src/bm/stress.c src/bm/roombench.c,$(wildcard src/bm/*.c)) \
           src/audio/player.c src/audio/n8snd.c src/kernel/prompts.c src/kernel/pointer.c \
           src/kernel/menu_ui.c src/kernel/icons.c src/kernel/syskeys.c
SHARED_SRCS += $(BT_SRCS) $(MBEDTLS_SRCS) $(LWIP_SRCS) $(NET_SRCS) $(BM_SRCS)
RGB30_SRCS := $(wildcard src/rgb30/*.c src/rgb30/*.S)
KERNEL_SRCS := $(RGB30_SRCS) $(SHARED_SRCS) $(LUA_SRCS)
KERNEL_OBJS := $(patsubst %,$(BUILD)/k/%.o,$(KERNEL_SRCS))
LUA_OBJS    := $(patsubst %,$(BUILD)/k/%.o,$(LUA_SRCS))
MBEDTLS_OBJS := $(patsubst %,$(BUILD)/k/%.o,$(MBEDTLS_SRCS))
LWIP_OBJS   := $(patsubst %,$(BUILD)/k/%.o,$(LWIP_SRCS))
$(LUA_OBJS) $(MBEDTLS_OBJS) $(LWIP_OBJS): WARN := -w

VERSION_STAMP := $(BUILD)/version.txt
$(VERSION_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(VERSION) $(BRANCH)' | cmp -s - $@ || echo '$(VERSION) $(BRANCH)' > $@
$(BUILD)/k/src/kernel/version.c.o: $(VERSION_STAMP)
$(BUILD)/k/src/kernel/version.c.o: CFLAGS += -DBM_VERSION=\"$(VERSION)\" -DBM_BRANCH=\"$(BRANCH)\"
$(BUILD)/k/src/rgb30/bm_embed.S.o: keys/release-pub.pem
FORCE:

.DEFAULT_GOAL := all
.PHONY: all test test-bt test-wifi qemu clean firmware image sdcard FORCE

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
             boot/rgb30/LEGGIMI.txt=LEGGIMI.txt boot/ca.pem=bm/ca.pem \
             $(FW64)/rtl8821cs_fw.bin=bm/rtl8821cs_fw.bin $(FW64)/rtl8821cs_config.bin=bm/rtl8821cs_config.bin \
             $(FW64)/rtw8821c_fw.bin=bm/rtw8821c_fw.bin \
             $(wildcard $(FW64)/LICENCE.rtlwifi_firmware.txt)$(if $(wildcard $(FW64)/LICENCE.rtlwifi_firmware.txt),=bm/LICENCE.rtlwifi_firmware.txt)
# Yharnam (the Pi's cartridge, from the claude/yharnam branch, 256x256): on
# the SD card for testing (in the Games tab) and in the QEMU tests
YHARNAM := $(BUILD)/carts/yharnam.bm
$(YHARNAM): carts/yharnam/main.lua carts/yharnam/sheet.png carts/yharnam/cover.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title Yharnam --author bm --res 256x256 \
	    --cover carts/yharnam/cover.png --sheet carts/yharnam/sheet.png --sheet8
SD_FILES64 += $(YHARNAM)=bm/yharnam.bm

# RGB30_CONFIG=file: your own bm/config.txt in the image (wifi_ssid, wifi_psk:
# keep that file out of the repository)
SD_FILES64 += $(if $(RGB30_CONFIG),$(RGB30_CONFIG)=bm/config.txt)
image: $(BUILD)/kernel8.img $(YHARNAM)
	@test -f $(FW64)/u-boot.itb || { echo "Run 'make TARGET=rgb30 firmware' first"; exit 1; }
	@test -z "$(RGB30_CONFIG)" || test -f "$(RGB30_CONFIG)" || { echo "RGB30_CONFIG: $(RGB30_CONFIG) not found"; exit 1; }
	@mkdir -p $(DIST)
	$(PYTHON) scripts/mksd.py $(DIST)/bm-rgb30.img --size-mib 256 --start-mib 16 --label BM --active \
	    --raw $(FW64)/idbloader.img@64 --raw $(FW64)/u-boot.itb@16384 $(SD_FILES64)
	gzip -9 -k -f $(DIST)/bm-rgb30.img
	@echo "Write $(DIST)/bm-rgb30.img (or .img.gz) to a microSD card, slot TF1."

# The files of the BM partition, to copy onto a card made with `image`;
# SD=dir copies them there (WSL: SD=/mnt/e for the drive E:, named BM)
sdcard: $(BUILD)/kernel8.img $(YHARNAM)
	@mkdir -p $(DIST)/sd/extlinux $(DIST)/sd/bm
	cp $(BUILD)/kernel8.img $(DIST)/sd/kernel8.img
	cp $(YHARNAM) $(DIST)/sd/bm/yharnam.bm
	cp boot/rgb30/extlinux.conf $(DIST)/sd/extlinux/
	cp boot/rgb30/LEGGIMI.txt $(DIST)/sd/
	cp boot/ca.pem $(DIST)/sd/bm/ca.pem
	@if [ -f $(FW64)/rtl8821cs_fw.bin ]; then cp $(FW64)/rtl8821cs_*.bin $(FW64)/rtw8821c_fw.bin $(DIST)/sd/bm/; fi
	@if [ -n "$(RGB30_CONFIG)" ]; then cp "$(RGB30_CONFIG)" $(DIST)/sd/bm/config.txt; fi
	@if [ -n "$(SD)" ]; then sh scripts/copy-sd-rgb30.sh $(DIST)/sd "$(SD)"; \
	else echo "Copy the contents of $(DIST)/sd/ to the card's drive named BM (or: make TARGET=rgb30 sdcard SD=/mnt/<letter>)."; fi

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

test: test-bt test-wifi
	$(MAKE) -f rgb30.mk PLAT=virt all build/rgb30-virt/carts/yharnam.bm
	$(PYTHON) tests/rgb30/qemu_test.py --build build/rgb30-virt

# H5 and the Realtek firmware set-up on the PC, against a simulated chip
# (with the real firmware when `make TARGET=rgb30 firmware` fetched it)
HOSTCC ?= gcc
build/rgb30-host/h5_test: tests/bt/h5_test.c src/bt/h5.c src/bt/hci.c src/bt/rtlbt.c src/bt/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -o $@ tests/bt/h5_test.c src/bt/h5.c src/bt/hci.c src/bt/rtlbt.c

test-bt: build/rgb30-host/h5_test
	$< $(wildcard $(FW64)/rtl8821cs_fw.bin $(FW64)/rtl8821cs_config.bin)

# the WiFi's RX buffers and 802.11 frames on the PC
build/rgb30-host/rtw_frame_test: tests/rgb30/rtw_frame_test.c src/rgb30/rtw_frame.c src/rgb30/rtw_frame.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -fsanitize=address,undefined -Isrc -o $@ tests/rgb30/rtw_frame_test.c src/rgb30/rtw_frame.c

# WPA2 against published vectors and a handshake computed apart
# (tests/rgb30/wpa_vectors.py writes wpa_vectors.h)
WPA_HOST_SRCS := src/rgb30/wpa.c $(addprefix third_party/mbedtls/library/,sha1.c aes.c platform_util.c)
build/rgb30-host/wpa_test: tests/rgb30/wpa_test.c tests/rgb30/wpa_vectors.h src/rgb30/wpa.h $(WPA_HOST_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -fsanitize=address,undefined -Isrc -Itests/rgb30 -Ithird_party/mbedtls/include \
	    -Isrc/net -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/rgb30/wpa_test.c $(WPA_HOST_SRCS)

# the whole station (scan, WPA2, keys, lwIP's DHCP and a ping) on a
# simulated chip and access points
WIFI_SIM_SRCS := $(addprefix src/rgb30/,rtw_sta.c rtw_init.c rtw_io.c rtw_frame.c rtw8821c_table.c) \
                 src/lib/printf.c src/net/net.c $(WPA_HOST_SRCS) $(LWIP_SRCS)
build/rgb30-host/wifi_sim_test: tests/rgb30/wifi_sim_test.c $(WIFI_SIM_SRCS) src/rgb30/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -g -w -fsanitize=address,undefined -DPLAT_RK3566 -DBM_HOST_TEST -Isrc -Isrc/rgb30 \
	    -Isrc/net -Ithird_party/lwip/src/include -Ithird_party/mbedtls/include \
	    -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/rgb30/wifi_sim_test.c $(WIFI_SIM_SRCS)

test-wifi: build/rgb30-host/rtw_frame_test build/rgb30-host/wpa_test build/rgb30-host/wifi_sim_test
	build/rgb30-host/rtw_frame_test
	build/rgb30-host/wpa_test
	build/rgb30-host/wifi_sim_test

clean:
	rm -rf build/rgb30 build/rgb30-virt build/rgb30-host

-include $(KERNEL_OBJS:.o=.d)
