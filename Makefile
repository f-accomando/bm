# bm - bare metal console for Raspberry Pi Zero / Zero W (BCM2835);
# the same kernel runs on the Pi 1 (A, B, A+, B+)

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
BOOT_DEFS := -DBM_BOOT_STRESS
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
CFLAGS  = $(COMMON) -D_DEFAULT_SOURCE -Ithird_party/lua \
          -Ithird_party/lwip/src/include -Isrc/net \
          -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' $(WARN)
# Chainloader: freestanding, no libc.
LCFLAGS := $(COMMON) -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns
ASFLAGS := $(ARCH) -g -Isrc -Isrc/kernel -Wa,-I$(BUILD)
LDFLAGS := $(ARCH) -nostartfiles -Wl,--gc-sections
LDLIBS  := -Wl,--start-group -lc -lm -lgcc -Wl,--end-group
LLDLIBS := -nostdlib -lgcc

LUA_SRCS    := $(wildcard third_party/lua/*.c)
MBEDTLS_SRCS := $(wildcard third_party/mbedtls/library/*.c)
LWIP_SRCS   := $(wildcard third_party/lwip/src/core/*.c third_party/lwip/src/core/ipv4/*.c) \
               third_party/lwip/src/netif/ethernet.c third_party/lwip/src/apps/sntp/sntp.c
KERNEL_SRCS := $(shell find src -name '*.c' -o -name '*.S') $(LUA_SRCS) $(LWIP_SRCS) $(MBEDTLS_SRCS)
LOADER_SRCS := $(wildcard chainloader/*.S chainloader/*.c) \
               src/drivers/uart.c src/drivers/gpio.c src/drivers/mbox.c \
               src/drivers/prop.c src/drivers/timer.c src/drivers/led.c src/drivers/board.c \
               src/arch/cache.c src/lib/crc32.c

KERNEL_OBJS := $(patsubst %,$(BUILD)/k/%.o,$(KERNEL_SRCS))
LOADER_OBJS := $(patsubst %,$(BUILD)/l/%.o,$(LOADER_SRCS))
LUA_OBJS    := $(patsubst %,$(BUILD)/k/%.o,$(LUA_SRCS))
LWIP_OBJS   := $(patsubst %,$(BUILD)/k/%.o,$(LWIP_SRCS))
MBEDTLS_OBJS := $(patsubst %,$(BUILD)/k/%.o,$(MBEDTLS_SRCS))

# The version string lives in one object, rebuilt when `git describe` changes.
VERSION_STAMP := $(BUILD)/version.txt
$(VERSION_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(VERSION)' | cmp -s - $@ || echo '$(VERSION)' > $@
$(BUILD)/k/src/kernel/version.c.o: $(VERSION_STAMP)
$(BUILD)/k/src/kernel/version.c.o: CFLAGS += -DBM_VERSION=\"$(VERSION)\"
FORCE:

# Third-party code: its own warning policy, not ours.
$(LUA_OBJS) $(LWIP_OBJS) $(MBEDTLS_OBJS): WARN := -w
# Lua scripts embedded with .incbin
$(BUILD)/k/src/script/embed.S.o: $(wildcard src/script/*.lua) keys/release-pub.pem \
                                 $(BUILD)/demo.bm $(BUILD)/stress.bm $(BUILD)/editor.bm $(BUILD)/sound.bm \
                                 $(BUILD)/studio3d.bm $(BUILD)/mesh.bm $(BUILD)/pixel.bm \
                                 $(BUILD)/assist.bin src/ai/assist.lua $(BUILD)/assistant.bm \
                                 $(BUILD)/code.bm

# The development assistant (M30): knowledge base + trained network, built
# into the kernel. The network is trained on the PC (numpy) by `make
# ai-model` and committed, so `make` needs only Python's standard library.
AI_KB := $(wildcard src/ai/kb/*.txt)
$(BUILD)/assist.bin: $(AI_KB) src/ai/assist.weights scripts/mkassist.py scripts/assistlib.py
	@mkdir -p $(dir $@) $(BUILD)/ai
	$(PYTHON) scripts/mkassist.py -o $@ --ref $(BUILD)/ai/ref.txt --snippets $(BUILD)/ai/snippets.txt

ai-model:
	$(PYTHON) scripts/trainassist.py

# bm Code, the code editor (Dev tab, monitor C)
$(BUILD)/code.bm: carts/code/main.lua scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "bm Code" --author bm

# The assistant on its own, in the Dev tab
$(BUILD)/assistant.bm: carts/assistant/main.lua scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "bm assistant" --author bm

# The editor (M15), built into the kernel
$(BUILD)/editor.bm: carts/editor/main.lua carts/editor/cover.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --cover carts/editor/cover.png \
	    --title "bm SDK" --author bm

# The Sound editor, built into the kernel: its own bank is the demo project
$(BUILD)/sound.bm: carts/sound/main.lua carts/sound/cover.png carts/sound/demo.json scripts/mkbm.py scripts/bmaudio.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --cover carts/sound/cover.png \
	    --audio carts/sound/demo.json --title "bm Sound" --author bm

# The 3D studio (M22): models and animations of a .bm, on the console. Its
# sheet holds the starter tiles of bm Studio (carts/studio3d/mkassets.js).
$(BUILD)/studio3d.bm: carts/studio3d/main.lua carts/studio3d/cover.png carts/studio3d/sheet.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --cover carts/studio3d/cover.png \
	    --sheet carts/studio3d/sheet.png --sheet8 --title "bm 3D studio" --author bm

# bm Mesh: the meshes of a .bm (its models and those its code builds), on
# the console. Its cover: carts/mesh/mkcover.js.
$(BUILD)/mesh.bm: carts/mesh/main.lua carts/mesh/cover.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --cover carts/mesh/cover.png --title "bm Mesh" --author bm

# bm Pixel: the pixel art of a .bm (its sprite sheet), on the console. Its
# cover: carts/pixel/mkcover.js.
$(BUILD)/pixel.bm: carts/pixel/main.lua carts/pixel/cover.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --cover carts/pixel/cover.png --title "bm Pixel" --author bm

$(BUILD)/stress.bm: carts/stress/main.lua scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "bm stress test" --author bm

# Native demo cartridge (.bm): Lua + sprite sheet + map
DEMO_BM_SRC := carts/demo/main.lua carts/demo/sheet.png carts/demo/map.csv
$(BUILD)/demo.bm: $(DEMO_BM_SRC) scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua carts/demo/main.lua --sheet carts/demo/sheet.png \
	    --map carts/demo/map.csv --title "bm native demo" --author bm

# Demo games (Lua only, sprites drawn in code): build/carts/<name>.bm
GAMES := pong snake shooter astrowing hunt kitchen titan texroom village nano8
GAME_CARTS := $(patsubst %,$(BUILD)/carts/%.bm,$(GAMES))
title_pong    := Pong
title_snake   := Snake
title_shooter := Star Shooter
title_astrowing := Astro Wing
title_hunt := Hunter's Night
res_hunt := 320x180
title_kitchen := Chaos Kitchen
title_titan := Titan Clash
title_texroom := Texture Room
title_nano8 := nano8
res_texroom := 320x180
title_village := Studio Village
res_village := 320x180
# Optional per game: carts/<game>/cover.png (printed on the cartridge in the
# menu, scripts/mkcovers.py), sheet.png, map.csv, models.bm or models.glb (3D
# models from bm Studio / bm Animator, sdk/: with their skeletons and
# animations from a .bm; their sprite sheet too when there is no sheet.png),
# res_<game> := 320x180.
.SECONDEXPANSION:
$(BUILD)/carts/%.bm: carts/%/main.lua scripts/mkbm.py scripts/bmmesh.py \
                      $$(wildcard carts/$$*/cover.png carts/$$*/sheet.png carts/$$*/map.csv carts/$$*/models.glb \
                                  carts/$$*/models.bm)
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "$(title_$*)" --author bm \
	    --res $(or $(res_$*),640x360) \
	    $(if $(wildcard carts/$*/cover.png),--cover carts/$*/cover.png) \
	    $(if $(wildcard carts/$*/sheet.png),--sheet carts/$*/sheet.png) \
	    $(if $(wildcard carts/$*/map.csv),--map carts/$*/map.csv) \
	    $(if $(wildcard carts/$*/models.bm),--models carts/$*/models.bm,$(if $(wildcard carts/$*/models.glb),--models carts/$*/models.glb))

# Chaos Kitchen (M17) is written in several Lua files, joined by its build.py
KITCHEN_SRC := $(sort $(wildcard carts/kitchen/src/*.lua))
$(BUILD)/kitchen/main.lua: $(KITCHEN_SRC) carts/kitchen/build.py
	$(PYTHON) carts/kitchen/build.py $@ --map $(BUILD)/kitchen/main.map

$(BUILD)/carts/kitchen.bm: $(BUILD)/kitchen/main.lua carts/kitchen/sheet.png carts/kitchen/cover.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "$(title_kitchen)" --author bm \
	    --sheet carts/kitchen/sheet.png --cover carts/kitchen/cover.png

# Titan Clash (M20): several Lua files too; its sheet has more colours than
# fit in RGBA sections, so it goes as SHEET8 (a palette and RLE)
TITAN_SRC := $(sort $(wildcard carts/titan/src/*.lua))
$(BUILD)/titan/main.lua: $(TITAN_SRC) carts/titan/build.py
	$(PYTHON) carts/titan/build.py $@ --map $(BUILD)/titan/main.map

$(BUILD)/carts/titan.bm: $(BUILD)/titan/main.lua carts/titan/sheet.png carts/titan/cover.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "$(title_titan)" --author bm \
	    --sheet carts/titan/sheet.png --sheet8 --cover carts/titan/cover.png

# nano8: plays .p8 / .p8.png carts (machine in src/bm/n8*.c); several Lua
# files too. The carts it ships with (carts/nano8/roms, licenses in
# CREDITS.md) go to carts/nano8/ on the SD card.
NANO8_SRC := $(sort $(wildcard carts/nano8/src/*.lua))
NANO8_ROMS := $(sort $(wildcard carts/nano8/roms/*.p8 carts/nano8/roms/*.p8.png))
$(BUILD)/nano8/main.lua: $(NANO8_SRC) carts/nano8/build.py
	$(PYTHON) carts/nano8/build.py $@ --map $(BUILD)/nano8/main.map

$(BUILD)/carts/nano8.bm: $(BUILD)/nano8/main.lua carts/nano8/cover.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "$(title_nano8)" --author bm \
	    --cover carts/nano8/cover.png

# A Lua interpreter for the PC (the same Lua 5.4 as the console): host tests
# of the Lua cartridges.
$(BUILD)/host/luahost: tests/kitchen/luahost.c $(LUA_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -w -Ithird_party/lua -o $@ tests/kitchen/luahost.c $(LUA_SRCS) -lm

test-kitchen: $(BUILD)/host/luahost $(BUILD)/kitchen/main.lua
	$< tests/kitchen/sim.lua $(BUILD)/kitchen/main.lua $(BUILD)/kitchen/main.map

test-titan: $(BUILD)/host/luahost $(BUILD)/titan/main.lua
	$< tests/titan/sim.lua $(BUILD)/titan/main.lua $(BUILD)/titan/main.map

# The Sound editor in a fake bm: its banks are the console's format, byte for byte
$(BUILD)/demo.bmau: carts/sound/demo.json scripts/bmaudio.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/bmaudio.py pack $< -o $@

test-sound: $(BUILD)/host/luahost $(BUILD)/demo.bmau carts/sound/main.lua
	$< tests/sound/sim.lua carts/sound/main.lua $(BUILD)/demo.bmau

# nano8 on the PC: the loader on every cart, the translator, the API test
# cart, then each shipped cart played for a while (tests/nano8/run.py)
N8_HOST_SRC := src/bm/n8.c src/bm/n8font.c src/bm/n8cart.c src/bm/n8lua.c src/audio/n8snd.c \
               src/bm/gfx16.c src/gfx/font8x16.c
$(BUILD)/host/n8host: tests/nano8/n8host.c $(N8_HOST_SRC) src/bm/n8*.h src/audio/n8snd.h $(LUA_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -w -Isrc -Ithird_party/lua -o $@ tests/nano8/n8host.c $(N8_HOST_SRC) \
	    $(filter-out third_party/lua/lua.c third_party/lua/luac.c,$(LUA_SRCS)) -lm

$(BUILD)/host/n8cartinfo: tests/nano8/cartinfo.c src/bm/n8.c src/bm/n8font.c src/bm/n8cart.c src/bm/n8*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/nano8/cartinfo.c src/bm/n8.c src/bm/n8font.c src/bm/n8cart.c -lm

test-nano8: $(BUILD)/host/n8host $(BUILD)/host/n8cartinfo $(BUILD)/host/luahost $(BUILD)/nano8/main.lua
	$(PYTHON) tests/nano8/run.py --build $(BUILD) $(NANO8_ROMS)

.DEFAULT_GOAL := all
.PHONY: FORCE test-smp all clean firmware image image-pi1 sdcard install sdcard-chainloader sdcard-stress qemu qemu-screenshot \
        run-serial test test-bm test-ai ai-model test-usb test-audio test-fat test-kitchen test-titan test-sound test-nano8 \
        test-net test-http test-https test-release release disasm wav test-studio test-studio-ui studio test-prompts \
        showreel

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
# Only the games: the native demo and the stress test live in the kernel
# (monitor `n`, the Stress test of the Dev tab), not in the Games tab.
KERNEL ?= kernel
SD_CARTS := $(GAME_CARTS)
sdcard: $(BUILD)/$(KERNEL).img $(SD_CARTS)
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)/carts
	cp $(FW_DIR)/bootcode.bin $(FW_DIR)/start.elf $(FW_DIR)/fixup.dat $(DIST)/
	cp boot/config.txt $(DIST)/
	cp $(BUILD)/$(KERNEL).img $(DIST)/kernel.img
	rm -f $(DIST)/carts/*.bm       # the old extension (now .bm)
	rm -f $(DIST)/carts/*.cart     # the old .cart format: bm no longer plays it
	cp $(SD_CARTS) $(DIST)/carts/
	mkdir -p $(DIST)/carts/nano8 && cp $(NANO8_ROMS) $(DIST)/carts/nano8/
	mkdir -p $(DIST)/bm && cp boot/ca.pem $(DIST)/bm/ca.pem
	@if [ -f $(FW_DIR)/BCM43430A1.hcd ]; then mkdir -p $(DIST)/bm && \
	    cp $(FW_DIR)/BCM43430A1.hcd $(DIST)/bm/ && echo "cp BCM43430A1.hcd -> $(DIST)/bm/"; fi
	@for f in brcmfmac43430-sdio.bin brcmfmac43430-sdio.txt brcmfmac43430-sdio.clm_blob; do \
	    if [ -f $(FW_DIR)/$$f ]; then mkdir -p $(DIST)/bm && cp $(FW_DIR)/$$f $(DIST)/bm/ && \
	        echo "cp $$f -> $(DIST)/bm/"; fi; done
	@echo "Copy the contents of $(DIST)/ ($(KERNEL)) to the root of a FAT32 SD card."

# Whole SD card image (MBR + FAT32): firmware, config, kernel and the
# cartridges. Write it with Raspberry Pi Imager ("Use custom"), balenaEtcher
# or dd. Needs dosfstools and mtools. The kernel is the same for every
# BCM2835 board; image-pi1 leaves out the WiFi/Bluetooth chip's firmware
# (the Pi 1 has no radio; on the B / B+ the network is the Ethernet).
IMAGE_FILES = $(FW_DIR)/bootcode.bin=bootcode.bin $(FW_DIR)/start.elf=start.elf \
              $(FW_DIR)/fixup.dat=fixup.dat boot/config.txt=config.txt \
              $(BUILD)/kernel.img=kernel.img \
              $(foreach c,$(SD_CARTS),$(c)=carts/$(notdir $(c))) \
              $(foreach r,$(NANO8_ROMS),$(r)=carts/nano8/$(notdir $(r))) \
              boot/ca.pem=bm/ca.pem
image: $(BUILD)/kernel.img $(SD_CARTS)
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)
	$(PYTHON) scripts/mksd.py $(DIST)/bm.img --size-mib 64 --label BM $(IMAGE_FILES) \
	    $(if $(wildcard $(FW_DIR)/BCM43430A1.hcd),$(FW_DIR)/BCM43430A1.hcd=bm/BCM43430A1.hcd) \
	    $(foreach f,$(wildcard $(FW_DIR)/brcmfmac43430-sdio.*),$(f)=bm/$(notdir $(f)))

image-pi1: $(BUILD)/kernel.img $(SD_CARTS)
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)
	$(PYTHON) scripts/mksd.py $(DIST)/bm-pi1.img --size-mib 64 --label BM $(IMAGE_FILES)

# The files of a release (M19), in $(DIST)/release: kernel.img, the games,
# bm/ca.pem and manifest.txt with where each goes, its size and SHA-256,
# signed (manifest.sig) with the key in BM_RELEASE_KEY (the environment;
# on GitHub the repository's secret). CI on a tag v*:
#   make release VERSION=v0.1.0 RELEASE_FLAGS=--require-key
# The signature must match keys/release-pub.pem, the key in the kernel.
RELEASE_DIR := $(DIST)/release
RELEASE_PUB ?= keys/release-pub.pem
release: $(BUILD)/kernel.img $(GAME_CARTS)
	rm -rf $(RELEASE_DIR)
	$(PYTHON) scripts/mkrelease.py $(RELEASE_DIR) --version $(VERSION) --commit $$(git rev-parse HEAD) \
	    --file $(BUILD)/kernel.img:/kernel.img \
	    $(foreach c,$(GAME_CARTS),--file $(c):/carts/$(notdir $(c))) \
	    --file boot/ca.pem:/bm/ca.pem --pub $(RELEASE_PUB) $(RELEASE_FLAGS)

# Copies what make sdcard prepared onto a mounted SD card (SD=/mnt/d by
# default): kernel, boot files, config.txt, cartridges and the chip
# firmware in bm/. Settings and saves (bm/CONFIG.TXT, bm/SAVE) are
# never touched. Uses sudo when the card is not writable (WSL).
SD ?= /mnt/d
# the console's folder before the project was renamed bm: moved to bm/
OLD_DIR := bm33

install: sdcard
	@test -d $(SD) || { echo "$(SD) is not mounted (sudo mount -t drvfs D: $(SD))"; exit 1; }
	@S=; [ -w $(SD) ] || S=sudo; \
	$$S mkdir -p $(SD)/carts $(SD)/bm && \
	if [ -d $(SD)/$(OLD_DIR) ]; then $$S cp -rn $(SD)/$(OLD_DIR)/. $(SD)/bm/ && $$S rm -rf $(SD)/$(OLD_DIR) && \
	    echo "moved $(OLD_DIR)/ (settings, saves, firmware) to bm/"; fi && \
	$$S cp $(DIST)/bootcode.bin $(DIST)/start.elf $(DIST)/fixup.dat $(DIST)/config.txt $(DIST)/kernel.img $(SD)/ && \
	$$S rm -f $(SD)/carts/demo.cart $(SD)/carts/demo.bm $(SD)/carts/stress.bm && \
	$$S cp -r $(DIST)/carts/* $(SD)/carts/ && \
	if [ -d $(DIST)/bm ]; then $$S cp $(DIST)/bm/* $(SD)/bm/; fi && \
	sync && echo "installed on $(SD): kernel $$(git describe --always --dirty), carts, bm/ firmware" && \
	ls $(SD)/bm

sdcard-chainloader:
	$(MAKE) sdcard KERNEL=chainloader

# SD card contents with the stress-test kernel (make sdcard-stress)
sdcard-stress:
	$(MAKE) sdcard BOOT=stress

# Upload the kernel to a Pi running the chainloader, then open a terminal.
# Rebooting the Pi (monitor command 'r') re-sends the current kernel.img.
run-serial: $(BUILD)/kernel.img
	$(PYTHON) tools/bm_load.py --baud $(BAUD) $(PORT) $<

# QEMU boots raw images at 0x8000 through -bios, like the real firmware.
QEMU_ARGS := -M raspi0 -serial stdio -serial null

qemu: $(BUILD)/kernel.img
	$(QEMU) $(QEMU_ARGS) -bios $<

qemu-screenshot: $(BUILD)/kernel.img
	./scripts/qemu-screenshot.sh $< $(BUILD)/screen.png

test: all test-bm test-usb test-fat test-audio test-kitchen test-titan test-sound test-nano8 test-net test-http test-https \
      test-release test-smp test-ai test-studio test-prompts
	$(PYTHON) tests/qemu_test.py --build $(BUILD)

$(BUILD)/host/test_bm: tests/bm/test_bm.c src/bm/gfx16.c src/bm/r3d.c src/bm/format.c src/lib/crc32.c src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/bm/test_bm.c src/bm/gfx16.c src/bm/r3d.c src/bm/format.c src/lib/crc32.c -lm

test-fat: $(BUILD)/host/test_fat
	$(PYTHON) tests/fs/run.py $<

$(BUILD)/host/test_fat: tests/fs/test_fat.c src/fs/fat.c src/fs/fat.h src/drivers/sd.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/fs/test_fat.c src/fs/fat.c

# Network console on lwIP's loopback interface (the WiFi chip is not in QEMU),
# then lwIP on the Pi 1 B's Ethernet with a simulated LAN9512 and a DHCP peer
test-net: $(BUILD)/host/test_netcon $(BUILD)/host/test_ethnet
	$(BUILD)/host/test_netcon
	$(BUILD)/host/test_ethnet

$(BUILD)/host/test_ethnet: tests/net/test_ethnet.c src/net/net.c src/net/net.h src/usb/smsc95xx.c src/usb/smsc95xx.h \
                           tests/usb/lan9512_sim.c tests/usb/lan9512_sim.h $(LWIP_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/lwip/src/include -o $@ \
		tests/net/test_ethnet.c src/net/net.c src/usb/smsc95xx.c tests/usb/lan9512_sim.c $(LWIP_SRCS)

$(BUILD)/host/test_netcon: tests/net/test_netcon.c src/net/netcon.c src/net/netxfer.c src/net/stream.c src/net/*.h src/lib/crc32.c $(LWIP_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/lwip/src/include -o $@ \
		tests/net/test_netcon.c src/net/netcon.c src/net/netxfer.c src/net/stream.c src/lib/crc32.c $(LWIP_SRCS)

# HTTPS: http.c + tls.c + mbedTLS (the kernel's configuration) over POSIX
# sockets, against local TLS servers with a test CA made by openssl
test-https: $(BUILD)/host/test_https
	$(PYTHON) tests/net/run_https_test.py $(BUILD)/host/test_https

$(BUILD)/host/test_https: tests/net/test_https.c tests/net/stream_posix.c src/net/tls.c src/net/http.c \
                          src/net/http_kernel.c src/net/*.h $(MBEDTLS_SRCS) boot/ca.pem
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/mbedtls/include \
		-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -DHTTP_USER_AGENT='"test"' -o $@ \
		tests/net/test_https.c tests/net/stream_posix.c src/net/tls.c src/net/http.c \
		src/net/http_kernel.c $(MBEDTLS_SRCS)

# Release manifests (M19): scripts/mkrelease.py signs with a test key made
# by openssl, src/net/release.c checks signature, lines and files
test-release: $(BUILD)/host/test_release
	$(PYTHON) tests/net/run_release_test.py $(BUILD)/host/test_release

$(BUILD)/host/test_release: tests/net/test_release.c src/net/release.c src/net/release.h $(MBEDTLS_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/mbedtls/include \
		-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/net/test_release.c src/net/release.c $(MBEDTLS_SRCS)

# Bluetooth LE pairing cryptography (SMP), against the spec's sample data
test-smp: $(BUILD)/host/test_smp
	$(BUILD)/host/test_smp

$(BUILD)/host/test_smp: tests/bt/smp_test.c src/bt/smp_crypto.c src/bt/smp_crypto.h $(MBEDTLS_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -Isrc -Isrc/net -Ithird_party/mbedtls/include \
		-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/bt/smp_test.c src/bt/smp_crypto.c $(MBEDTLS_SRCS)

# HTTP client over POSIX sockets, against a local Python server
test-http: $(BUILD)/host/test_http
	$(PYTHON) tests/net/run_http_test.py $(BUILD)/host/test_http

$(BUILD)/host/test_http: tests/net/test_http.c src/net/http.c src/net/http.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -DHTTP_USER_AGENT='"test"' -o $@ tests/net/test_http.c src/net/http.c

test-audio: $(BUILD)/host/test_audio
	$<

$(BUILD)/host/test_audio: tests/audio/test_audio.c src/audio/synth.c src/audio/player.c src/audio/iec958.c src/audio/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/audio/test_audio.c src/audio/synth.c src/audio/player.c src/audio/iec958.c -lm

# A song (or SFX=n) of a sound bank as a WAV file, made on the PC by the
# console's synthesizer: make wav BANK=carts/sound/demo.json SONG=0
BANK ?= carts/sound/demo.json
SONG ?= 0
wav: $(BUILD)/host/bmrender
	$(PYTHON) scripts/bmaudio.py pack $(BANK) -o $(BUILD)/wav.bmau
	$< $(BUILD)/wav.bmau $(BUILD)/$(if $(SFX),sfx$(SFX),song$(SONG)).wav $(if $(SFX),sfx $(SFX),song $(SONG)) $(SECONDS)

$(BUILD)/host/bmrender: tests/audio/render.c src/audio/synth.c src/audio/player.c src/audio/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/audio/render.c src/audio/synth.c src/audio/player.c -lm

test-usb: $(BUILD)/host/test_hid $(BUILD)/host/test_eth $(BUILD)/host/test_board
	$(BUILD)/host/test_hid
	$(BUILD)/host/test_eth
	$(BUILD)/host/test_board

$(BUILD)/host/test_hid: tests/usb/test_hid.c src/usb/hid.c src/usb/hid.h src/usb/usb.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/usb/test_hid.c src/usb/hid.c

# Ethernet of the Pi 1 B (LAN951x) against a simulated chip
$(BUILD)/host/test_eth: tests/usb/test_eth.c tests/usb/lan9512_sim.c tests/usb/lan9512_sim.h \
                        src/usb/smsc95xx.c src/usb/smsc95xx.h src/usb/usb.h src/usb/dwc2.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Wno-format -Isrc -o $@ tests/usb/test_eth.c tests/usb/lan9512_sim.c src/usb/smsc95xx.c

$(BUILD)/host/test_board: tests/usb/test_board.c src/drivers/board.c src/drivers/board.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/usb/test_board.c src/drivers/board.c

# the meshes a cartridge builds in its code (cart_meshes(), bm Mesh)
MESHCAP_SRCS := tests/bm/test_meshcap.c src/bm/meshcap.c src/bm/format.c src/bm/r3d.c src/lib/crc32.c
$(BUILD)/host/test_meshcap: $(MESHCAP_SRCS) src/bm/*.h $(LUA_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc/bm -Isrc -Ithird_party/lua -o $@ $(MESHCAP_SRCS) $(LUA_SRCS) -lm

$(BUILD)/meshcap-test.bm: tests/bm/meshcap_cart.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "meshcap test" --author tests

test-bm: $(BUILD)/host/test_bm $(BUILD)/demo.bm $(BUILD)/host/test_meshcap $(BUILD)/carts/astrowing.bm \
         $(BUILD)/carts/texroom.bm $(BUILD)/carts/kitchen.bm $(BUILD)/meshcap-test.bm
	$< $(BUILD)/demo.bm
	$(BUILD)/host/test_meshcap src/bm/runtime.c \
	    $(BUILD)/meshcap-test.bm '!stop here,wheel:1,cars1_body:1,gem:2' \
	    $(BUILD)/carts/astrowing.bm ship:32,dart,tower,gate,ring:120,laser,bolt,debris,debris2,mark,core,core_hot,turret \
	    $(BUILD)/carts/texroom.bm floor_mesh,walls_mesh,crate_mesh,pillar_mesh \
	    $(BUILD)/carts/kitchen.bm chef_classic1_body,chef1_body,plate,dplate

# The button prompts (bm-ui): every one checked, and both sets drawn 3x as
# on a TV into build/prompts/prompts.png (the menu) and chips.png (the apps)
$(BUILD)/host/test_prompts: tests/ui/test_prompts.c src/kernel/prompts.c src/kernel/prompts.h \
                            src/gfx/font8x16.c src/gfx/font6x12.c src/lib/crc32.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/ui/test_prompts.c src/kernel/prompts.c \
		src/gfx/font8x16.c src/gfx/font6x12.c src/lib/crc32.c -lm

test-prompts: $(BUILD)/host/test_prompts
	@mkdir -p $(BUILD)/prompts
	$< $(BUILD)/prompts

# The assistant (M30): C features and network against the Python reference,
# answers to the held-out questions, sprite generator
AI_SRCS := src/ai/assist.c src/ai/nn.c src/ai/text.c src/ai/sprite.c
$(BUILD)/host/test_ai: tests/ai/test_ai.c $(AI_SRCS) src/ai/*.h src/lib/crc32.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/ai/test_ai.c $(AI_SRCS) src/lib/crc32.c -lm

# Lua for the PC with the `ai` table: the panel's tests
$(BUILD)/host/luaai: tests/ai/luaai.c src/ai/lua_ai.c $(AI_SRCS) src/ai/*.h src/lib/crc32.c $(LUA_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -w -DBM_HOST_TEST -Isrc -Ithird_party/lua -o $@ tests/ai/luaai.c src/ai/lua_ai.c \
		$(AI_SRCS) src/lib/crc32.c $(LUA_SRCS) -lm

test-ai: $(BUILD)/host/test_ai $(BUILD)/assist.bin $(BUILD)/host/luahost $(BUILD)/host/luaai
	$< $(BUILD)/assist.bin $(BUILD)/ai/ref.txt
	$(BUILD)/host/luahost tests/ai/check_snippets.lua $(BUILD)/ai/snippets.txt
	$(BUILD)/host/luaai $(BUILD)/assist.bin tests/ai/panel_test.lua
	$(BUILD)/host/luaai $(BUILD)/assist.bin tests/ai/act_test.lua

# bm Studio (sdk/studio): its core in Node (the .bm, PNG and glTF it writes,
# the editing geometry), then the same files read by the Python of the build
# and by the kernel's parser. Skipped without Node.
test-studio: $(BUILD)/host/test_bm $(BUILD)/demo.bm $(BUILD)/host/luahost $(BUILD)/carts/village.bm \
             $(BUILD)/carts/astrowing.bm $(BUILD)/host/test_meshcap
	rm -rf $(BUILD)/studio3d-sd && mkdir -p $(BUILD)/studio3d-sd/carts
	cp $(BUILD)/carts/village.bm $(BUILD)/studio3d-sd/carts/
	$(BUILD)/host/luahost tests/studio/studio3d_host.lua . $(BUILD)/studio3d-sd
	rm -rf $(BUILD)/mesh-sd && mkdir -p $(BUILD)/mesh-sd/carts
	cp $(BUILD)/carts/village.bm $(BUILD)/carts/astrowing.bm $(BUILD)/mesh-sd/carts/
	$(BUILD)/host/luahost tests/studio/mesh_host.lua . $(BUILD)/mesh-sd
	$(BUILD)/host/test_meshcap src/bm/runtime.c $(BUILD)/mesh-sd/carts/astrowing.bm ship \
	    $(BUILD)/mesh-sd/carts/village.bm "" $(BUILD)/mesh-sd/carts/meshcopy.bm ""
	$(PYTHON) scripts/bmmesh.py $(BUILD)/mesh-sd/carts/astrowing.bm $(BUILD)/mesh-sd/carts/village.bm >/dev/null
	rm -rf $(BUILD)/pixel-sd && mkdir -p $(BUILD)/pixel-sd/carts
	cp $(BUILD)/carts/village.bm $(BUILD)/demo.bm $(BUILD)/pixel-sd/carts/
	$(BUILD)/host/luahost tests/studio/pixel_host.lua . $(BUILD)/pixel-sd
	$(BUILD)/host/test_meshcap src/bm/runtime.c $(BUILD)/pixel-sd/carts/village.bm "" $(BUILD)/pixel-sd/carts/newspr.bm ""
	@if command -v node >/dev/null 2>&1; then \
	    node tests/studio/test_core.js $(BUILD)/studio-test.bm && \
	    $(PYTHON) tests/studio/check_cart.py $(BUILD)/studio-test.bm && \
	    node tests/studio/check_studio3d.js $(BUILD)/studio3d-sd $(BUILD)/carts/village.bm && \
	    node tests/studio/check_mesh.js $(BUILD)/mesh-sd $(BUILD)/carts/village.bm && \
	    node tests/studio/check_pixel.js $(BUILD)/pixel-sd $(BUILD)/carts/village.bm && \
	    $(BUILD)/host/test_bm $(BUILD)/demo.bm $(BUILD)/studio-test.bm $(BUILD)/studio-test-anim.bm \
	        $(BUILD)/studio3d-sd/carts/blocks.bm; \
	else echo "test-studio: node not found, skipped"; fi

# The same in a browser (Playwright + Chromium, not needed by `make test`):
# bm Studio and bm Animator with the mouse, saving; screenshots in build/studio/
test-studio-ui:
	node tests/studio/test_ui.js $(BUILD)/studio
	node tests/studio/test_animator_ui.js $(BUILD)/studio

# The showreel at the top of the README (docs/showreel.gif and .mp4): a
# villager made from nothing in bm Studio and bm Animator (Playwright +
# Chromium), then the map in the SDK, the code in bm Code with the assistant
# and the game, on the console in QEMU; ffmpeg puts it together (about 4
# minutes, tools/showreel/)
SHOWREEL := $(BUILD)/showreel
showreel: $(BUILD)/kernel.img
	node tools/showreel/web.js $(SHOWREEL)/web
	$(PYTHON) tools/showreel/console.py $(BUILD) $(SHOWREEL)/web $(SHOWREEL)/console
	node tools/showreel/cards.js $(SHOWREEL)/cards
	$(PYTHON) tools/showreel/assemble.py $(SHOWREEL) docs/showreel.mp4 docs/showreel.gif

# bm Studio and bm Animator on http://localhost:8765 (they also open from
# the files, sdk/studio/index.html and sdk/animator/index.html, in Chrome or Edge)
studio:
	@echo "bm Studio:   http://localhost:8765/studio/"
	@echo "bm Animator: http://localhost:8765/animator/   (Ctrl+C to stop)"
	$(PYTHON) -m http.server 8765 --bind 127.0.0.1 --directory sdk

HOSTCC ?= cc

clean:
	rm -rf build build-stress $(DIST)

-include $(KERNEL_OBJS:.o=.d) $(LOADER_OBJS:.o=.d)
