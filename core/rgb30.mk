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
CLEAN_KERNEL ?= 0
CLEAN_DEFS := $(if $(filter 1,$(CLEAN_KERNEL)),-DBM_CLEAN_KERNEL)
SYSINC  := -nostdinc -isystem $(shell $(CC) -print-file-name=include)
COMMON  := $(ARCH) --specs=picolibc.specs $(SYSINC) -std=c11 -O2 -Wall -Wextra -g -Icore/src -Icore/src/rgb30 \
           -ffunction-sections -fdata-sections $(PLAT_DEF) $(CLEAN_DEFS) -DBM_RGB30 \
           -Wno-format
CFLAGS   = $(COMMON) -D_DEFAULT_SOURCE -Icore/third_party/lua \
           -Icore/third_party/mbedtls/include -Icore/third_party/lwip/src/include -Icore/src/net \
           -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' $(WARN)
# PI_O3_FILES: same knob as the Pi build (see Makefile); default empty, nothing changes.
PI_O3_FILES ?=
ifneq ($(strip $(PI_O3_FILES)),)
$(patsubst %,$(BUILD)/k/%.o,$(PI_O3_FILES)): CFLAGS += -O3
endif
# -Wa,-I$(BUILD): the cartridges built here that bm_embed.S takes with .incbin
ASFLAGS := $(ARCH) $(PLAT_DEF) $(CLEAN_DEFS) -g -Icore/src -Icore/src/rgb30 -Wa,-I$(BUILD)
LDFLAGS := $(ARCH) --specs=picolibc.specs -nostartfiles -static -no-pie -Wl,--gc-sections \
           -Wl,--defsym=KERNEL_BASE=$(KERNEL_BASE)
LDLIBS  := -Wl,--start-group -lc -lm -lgcc -Wl,--end-group

LUA_SRCS := $(wildcard core/third_party/lua/*.c)
# the parts of bm that run unchanged on the RGB30
SHARED_SRCS := core/src/gfx/console.c core/src/gfx/draw.c core/src/gfx/font8x16.c core/src/gfx/font8x14.c \
               core/src/gfx/font16x32.c core/src/gfx/font24x48.c core/src/gfx/font_bm12x32.c core/src/gfx/font_bm18x48.c core/src/gfx/font_inter8x16.c core/src/gfx/font_bmf.c \
               core/src/gfx/font6x12.c core/src/lib/printf.c core/src/lib/crc32.c \
               core/src/script/luavm.c core/src/script/lib_bm.c \
               core/src/kernel/version.c core/src/kernel/crumbs.c core/src/kernel/config.c core/src/kernel/sdlog.c \
               core/src/fs/fat.c core/src/fs/sdlock.c core/src/kernel/fiber.c core/src/kernel/bmz_install.c
# Bluetooth: the Pi's stack (HCI, L2CAP, HID, BLE + SMP) over H5 to the
# Realtek chip; SMP's elliptic curves come from mbedTLS, and WPA2's SHA-1
# and AES (core/src/rgb30/wpa.c)
BT_SRCS := core/src/bt/bt.c core/src/bt/ble.c core/src/bt/hci.c core/src/bt/h5.c core/src/bt/rtlbt.c core/src/bt/smp_crypto.c \
           core/src/usb/hid.c
# all of mbedTLS, as on the Pi: TLS for the updates from GitHub too (the
# linker keeps what is used)
MBEDTLS_SRCS := $(wildcard core/third_party/mbedtls/library/*.c)
# the network: lwIP and the Pi's glue (DHCP, the network console, file
# transfer) over the WiFi (core/src/rgb30/rtw_sta.c)
LWIP_SRCS := $(wildcard core/third_party/lwip/src/core/*.c core/third_party/lwip/src/core/ipv4/*.c) \
             core/third_party/lwip/src/netif/ethernet.c core/third_party/lwip/src/apps/sntp/sntp.c
NET_SRCS := core/src/net/net.c core/src/net/netcon.c core/src/net/netxfer.c core/src/net/cartnet.c core/src/net/wifi_auto.c
# updates from GitHub (System > Updates): HTTPS, the signed manifest
# (manifest-rgb30.txt: kernel8.img and bm/ca.pem), the Pi's update code
NET_SRCS += core/src/net/stream.c core/src/net/tls.c core/src/net/http.c core/src/net/http_kernel.c core/src/net/release.c \
            core/src/kernel/update.c
# the reports to a git repository (core/src/kernel/reports.c, as on the Pi)
NET_SRCS += core/src/net/github.c core/src/net/report.c core/src/kernel/reports.c
# the Market and the nearby consoles, in fibers as on the Pi
NET_SRCS += core/src/net/catalog.c core/src/net/lan.c core/src/kernel/market.c
# ai_pair (bm Agent): the relay CA checked against the pairing code
NET_SRCS += core/src/net/aipair.c core/src/net/aiconf.c
# the Pi's cartridges (.bm, listed for testing; show_bm=0 hides them): the runtime unchanged,
# the Pi's drivers it calls replaced by core/src/rgb30/bm_port.c and bm_input.c; the menu is
# the Pi's (menu_ui.c, its icons) at 360x360
BM_SRCS := $(wildcard core/src/bm/*.c) \
           core/src/audio/audio.c core/src/audio/synth.c core/src/audio/player.c core/src/audio/n8snd.c \
           core/src/audio/presets.c core/src/audio/lua_tone.c \
           core/src/ai/music.c core/src/ai/music_net.c core/src/ai/lua_music.c core/src/ai/nn.c core/src/ai/net.c \
           core/src/kernel/prompts.c core/src/kernel/pointer.c \
           core/src/kernel/menu_ui.c core/src/kernel/menu_bg.c core/src/kernel/icons.c core/src/kernel/syskeys.c core/src/kernel/settings.c core/src/kernel/controls.c \
           core/src/kernel/sdmap.c core/src/kernel/sdmap_core.c core/src/kernel/library.c \
           core/src/kernel/ledstate.c core/src/kernel/notice.c core/src/kernel/splash.c core/src/kernel/logo_data.c
# the Dev tab's bm SDK (RGB30) runs as a tool, as the Pi's suite: the same loop
BM_SRCS += core/src/kernel/devtool.c core/src/kernel/jobs.c
SHARED_SRCS += $(BT_SRCS) $(MBEDTLS_SRCS) $(LWIP_SRCS) $(NET_SRCS) $(BM_SRCS)
RGB30_SRCS := $(wildcard core/src/rgb30/*.c core/src/rgb30/*.S)
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
$(BUILD)/k/core/src/kernel/version.c.o: $(VERSION_STAMP)
$(BUILD)/k/core/src/kernel/version.c.o: CFLAGS += -DBM_VERSION=\"$(VERSION)\" -DBM_BRANCH=\"$(BRANCH)\"
$(BUILD)/k/core/src/rgb30/bm_embed.S.o: core/keys/release-pub.pem core/keys/market-pub.pem core/src/ai/assist.lua core/src/ai/predict.lua core/src/script/bm3d.lua \
                                    core/src/script/bmlib.lua core/src/script/bmnet.lua core/src/ai/padtype.lua core/src/script/riff.lua core/src/script/bmui.lua core/src/script/snes_asm.lua
FORCE:

.DEFAULT_GOAL := all
.PHONY: all test test-bt test-wifi test-audio test-mali qemu clean firmware image sdcard FORCE

all: $(BUILD)/kernel8.img
ifeq ($(PLAT),rk3566)
	@mkdir -p $(DIST)
	cp $(BUILD)/kernel8.img $(DIST)/kernel8.img
endif

# Boot loader (from ROCKNIX's image) and the Realtek firmware: not stored in
# the repository, downloaded and checked once (core/scripts/fetch-rgb30.sh)
FW64 := firmware/rgb30
firmware:
	./scripts/fetch-rgb30.sh $(FW64)

# Whole SD card image: the boot loader at the sectors the RK3566 boot ROM
# and U-Boot's SPL read (64, 16384), then one FAT32 partition "BM" from
# 16 MB with extlinux.conf, kernel8.img and bm/. Write it with balenaEtcher,
# Raspberry Pi Imager or Rufus; updates: copy kernel8.img onto the card.
SD_FILES64 = $(BUILD)/kernel8.img=kernel8.img core/boot/rgb30/extlinux.conf=extlinux/extlinux.conf \
             core/boot/rgb30/LEGGIMI.txt=LEGGIMI.txt core/boot/ca.pem=bm/ca.pem \
             $(FW64)/rtl8821cs_fw.bin=bm/rtl8821cs_fw.bin $(FW64)/rtl8821cs_config.bin=bm/rtl8821cs_config.bin \
             $(FW64)/rtw8821c_fw.bin=bm/rtw8821c_fw.bin \
             $(wildcard $(FW64)/LICENCE.rtlwifi_firmware.txt)$(if $(wildcard $(FW64)/LICENCE.rtlwifi_firmware.txt),=bm/LICENCE.rtlwifi_firmware.txt)
# Yharnam (the Pi's cartridge, from the claude/yharnam branch, 360x360): on
# the SD card for testing (in the Games tab)
YHARNAM := $(BUILD)/bm/yharnam.bm
$(YHARNAM): bm/yharnam/main.lua bm/yharnam/sheet.png bm/yharnam/cover.png core/scripts/mkbm.py \
            bm/yharnam/map_ground.csv bm/yharnam/map_overlay.csv bm/yharnam/map_objects.csv
	@mkdir -p $(dir $@)
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title Yharnam --author bm --res 360x360 \
	    --cover bm/yharnam/cover.png --sheet bm/yharnam/sheet.png --sheet8 \
	    --map ground=bm/yharnam/map_ground.csv --map overlay=bm/yharnam/map_overlay.csv \
	    --map objects=bm/yharnam/map_objects.csv
SD_FILES64 += $(YHARNAM)=bm/yharnam.bm

# RGB30_CONFIG=file: your own bm/config.txt in the image (wifi_ssid, wifi_psk:
# keep that file out of the repository)
SD_FILES64 += $(if $(RGB30_CONFIG),$(RGB30_CONFIG)=bm/config.txt)
image: $(BUILD)/kernel8.img $(YHARNAM)
	@test -f $(FW64)/u-boot.itb || { echo "Run 'make TARGET=rgb30 firmware' first"; exit 1; }
	@test -z "$(RGB30_CONFIG)" || test -f "$(RGB30_CONFIG)" || { echo "RGB30_CONFIG: $(RGB30_CONFIG) not found"; exit 1; }
	@mkdir -p $(DIST)
	$(PYTHON) core/scripts/mksd.py $(DIST)/bm-rgb30.img --size-mib 256 --start-mib 16 --label BM --active \
	    --raw $(FW64)/idbloader.img@64 --raw $(FW64)/u-boot.itb@16384 $(SD_FILES64)
	gzip -9 -k -f $(DIST)/bm-rgb30.img
	@echo "Write $(DIST)/bm-rgb30.img (or .img.gz) to a microSD card, slot TF1."

# The files of the BM partition, to copy onto a card made with `image`;
# SD=dir copies them there (WSL: SD=/mnt/e for the drive E:, named BM)
sdcard: $(BUILD)/kernel8.img $(YHARNAM)
	@mkdir -p $(DIST)/sd/extlinux $(DIST)/sd/bm
	cp $(BUILD)/kernel8.img $(DIST)/sd/kernel8.img
	cp $(YHARNAM) $(DIST)/sd/bm/yharnam.bm
	cp core/boot/rgb30/extlinux.conf $(DIST)/sd/extlinux/
	cp core/boot/rgb30/LEGGIMI.txt $(DIST)/sd/
	cp core/boot/ca.pem $(DIST)/sd/bm/ca.pem
	@if [ -f $(FW64)/rtl8821cs_fw.bin ]; then cp $(FW64)/rtl8821cs_*.bin $(FW64)/rtw8821c_fw.bin $(DIST)/sd/bm/; fi
	@if [ -n "$(RGB30_CONFIG)" ]; then cp "$(RGB30_CONFIG)" $(DIST)/sd/bm/config.txt; fi
	@if [ -n "$(SD)" ]; then sh core/scripts/copy-sd-rgb30.sh $(DIST)/sd "$(SD)"; \
	else echo "Copy the contents of $(DIST)/sd/ to the card's drive named BM (or: make TARGET=rgb30 sdcard SD=/mnt/<letter>)."; fi

$(BUILD)/k/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/k/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/kernel.elf: $(KERNEL_OBJS) core/src/rgb30/kernel.ld
	$(CC) $(LDFLAGS) -T core/src/rgb30/kernel.ld -Wl,-Map=$(BUILD)/kernel.map $(KERNEL_OBJS) $(LDLIBS) -o $@

# arm64 Image: what U-Boot's booti loads (header in core/src/rgb30/start.S)
$(BUILD)/kernel8.img: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(stat -c %s $@) bytes"

# QEMU: the virt build, serial on stdio
qemu:
	$(MAKE) -f core/rgb30.mk PLAT=virt
	$(QEMU64) -M virt,gic-version=3 -cpu cortex-a55 -m 512M -device ramfb -nic none \
	    -kernel build/rgb30-virt/kernel.elf -serial stdio -display none

test: test-bt test-wifi test-audio test-mali
	$(MAKE) -f core/rgb30.mk PLAT=virt all
	$(PYTHON) tests/rgb30/qemu_test.py --build build/rgb30-virt

# H5 and the Realtek firmware set-up on the PC, against a simulated chip
# (with the real firmware when `make TARGET=rgb30 firmware` fetched it)
HOSTCC ?= gcc
build/rgb30-host/h5_test: tests/bt/h5_test.c core/src/bt/h5.c core/src/bt/hci.c core/src/bt/rtlbt.c core/src/bt/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -o $@ tests/bt/h5_test.c core/src/bt/h5.c core/src/bt/hci.c core/src/bt/rtlbt.c

test-bt: build/rgb30-host/h5_test
	$< $(wildcard $(FW64)/rtl8821cs_fw.bin $(FW64)/rtl8821cs_config.bin)

# the WiFi's RX buffers and 802.11 frames on the PC
build/rgb30-host/rtw_frame_test: tests/rgb30/rtw_frame_test.c core/src/rgb30/rtw_frame.c core/src/rgb30/rtw_frame.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -fsanitize=address,undefined -Icore/src -o $@ tests/rgb30/rtw_frame_test.c core/src/rgb30/rtw_frame.c

# WPA2 against published vectors and a handshake computed apart
# (tests/rgb30/wpa_vectors.py writes wpa_vectors.h)
WPA_HOST_SRCS := core/src/rgb30/wpa.c $(addprefix core/third_party/mbedtls/library/,sha1.c aes.c platform_util.c)
build/rgb30-host/wpa_test: tests/rgb30/wpa_test.c tests/rgb30/wpa_vectors.h core/src/rgb30/wpa.h $(WPA_HOST_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -fsanitize=address,undefined -Icore/src -Itests/rgb30 -Icore/third_party/mbedtls/include \
	    -Icore/src/net -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/rgb30/wpa_test.c $(WPA_HOST_SRCS)

# the whole station (scan, WPA2, keys, lwIP's DHCP and a ping) on a
# simulated chip and access points
WIFI_SIM_SRCS := $(addprefix core/src/rgb30/,rtw_sta.c rtw_init.c rtw_io.c rtw_frame.c rtw8821c_table.c) \
                 core/src/lib/printf.c core/src/net/net.c tests/kernel/crumbs_stub.c core/src/net/wifi_auto.c $(WPA_HOST_SRCS) $(LWIP_SRCS)
build/rgb30-host/wifi_sim_test: tests/rgb30/wifi_sim_test.c $(WIFI_SIM_SRCS) core/src/rgb30/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -g -w -fsanitize=address,undefined -DPLAT_RK3566 -DBM_HOST_TEST -Icore/src -Icore/src/rgb30 \
	    -Icore/src/net -Icore/third_party/lwip/src/include -Icore/third_party/mbedtls/include \
	    -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/rgb30/wifi_sim_test.c $(WIFI_SIM_SRCS)

# the sound driver (core/src/rgb30/rk_audio.c) on a simulated CRU, I2S and RK817
build/rgb30-host/audio_sim_test: tests/rgb30/audio_sim_test.c core/src/rgb30/rk_audio.c core/src/lib/printf.c \
                                 core/src/rgb30/*.h core/src/audio/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -g -Wall -Wextra -Wno-format -fsanitize=address,undefined -DPLAT_RK3566 -DBM_HOST_TEST \
	    -Itests/host/shim -Icore/src -Icore/src/rgb30 -o $@ tests/rgb30/audio_sim_test.c core/src/rgb30/rk_audio.c core/src/lib/printf.c

test-audio: build/rgb30-host/audio_sim_test
	build/rgb30-host/audio_sim_test 1188
	build/rgb30-host/audio_sim_test 1200 24
	build/rgb30-host/audio_sim_test 1188 32 31

# M41: the Mali probe (mali.c) on a simulated G52, CRU and PMU
build/rgb30-host/mali_test: tests/rgb30/mali_test.c core/src/rgb30/mali.c core/src/rgb30/mali.h core/src/rgb30/mali_desc.c core/src/rgb30/mali_desc.h core/src/rgb30/mali_shaders.h core/src/rgb30/mali_variants.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -Icore/src/rgb30 -o $@ tests/rgb30/mali_test.c core/src/rgb30/mali.c core/src/rgb30/mali_desc.c

# the draw descriptors against bytes made from Mesa's v7.xml (tests/rgb30/v7pack.py)
build/rgb30-host/mali_desc_test: tests/rgb30/mali_desc_test.c tests/rgb30/mali_desc_golden.h core/src/rgb30/mali_desc.c core/src/rgb30/mali_desc.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -Itests/rgb30 -o $@ tests/rgb30/mali_desc_test.c core/src/rgb30/mali_desc.c

test-mali:
	@echo "test-mali: mali_test and mali_desc_test are off (docs/NEXT.md, tests removed from the CI)"

test-wifi: build/rgb30-host/rtw_frame_test build/rgb30-host/wpa_test build/rgb30-host/wifi_sim_test
	build/rgb30-host/rtw_frame_test
	build/rgb30-host/wpa_test
	build/rgb30-host/wifi_sim_test

clean:
	rm -rf build/rgb30 build/rgb30-virt build/rgb30-host

-include $(KERNEL_OBJS:.o=.d)
