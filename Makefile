# bm - bare metal console for Raspberry Pi Zero / Zero W (BCM2835);
# the same kernel runs on the Pi 1 (A, B, A+, B+). The Pi Zero 2 W
# (BCM2710A1, Cortex-A53) has its own build of the same sources, kernel7.img
#
# make TARGET=rgb30: the PowKiddy RGB30 (RK3566, AArch64), see rgb30.mk
ifeq ($(TARGET),rgb30)
include rgb30.mk
else

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
# the branch, for the reports' names (CI checks out a commit: its variables say which)
BRANCH := $(or $(GITHUB_HEAD_REF),$(GITHUB_REF_NAME),$(shell git rev-parse --abbrev-ref HEAD 2>/dev/null),unknown)

ARCH    := -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard
# kernel7.img, the Pi Zero 2 W: ARMv7 in 32 bit (hardware divide, VFPv4 with
# 32 registers, NEON) tuned for the Cortex-A53; ARMv7 and not ARMv8 so that
# it also runs in QEMU's raspi2b (Cortex-A7, the same peripherals).
# SOC=-DBM_ZERO2: the BCM2710's addresses (src/drivers/mmio.h).
ARCH7   := -march=armv7ve -mtune=cortex-a53 -marm -mfpu=neon-vfpv4 -mfloat-abi=hard
SOC     :=
# The Pi Zero 2 W's build (kernel7.img) is off for now: make ZERO2=1 builds
# it again, puts it on the card, in the image and in the release, and adds
# its tests (test-hyp) to make test. make test-zero2 builds it anyway.
ZERO2   ?= 0
export ZERO2
K7      := $(if $(filter 1,$(ZERO2)),$(BUILD)/kernel7.img)
COMMON  = $(ARCH) $(SOC) -std=c11 -O2 -Wall -Wextra -g -Isrc \
          -ffunction-sections -fdata-sections \
          -DUART_BAUD=$(BAUD) $(BOOT_DEFS)
# Kernel: hosted C on top of newlib (libc, libm), see src/lib/syscalls.c.
CFLAGS  = $(COMMON) -D_DEFAULT_SOURCE -Ithird_party/lua \
          -Ithird_party/lwip/src/include -Isrc/net \
          -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' $(WARN)
# Chainloader: freestanding, no libc.
LCFLAGS := $(COMMON) -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns
ASFLAGS = $(ARCH) $(SOC) -g -Isrc -Isrc/kernel -Wa,-I$(BUILD)
LDFLAGS = $(ARCH) -nostartfiles -Wl,--gc-sections
LDLIBS  := -Wl,--start-group -lc -lm -lgcc -Wl,--end-group
LLDLIBS := -nostdlib -lgcc

LUA_SRCS    := $(wildcard third_party/lua/*.c)
MBEDTLS_SRCS := $(wildcard third_party/mbedtls/library/*.c)
LWIP_SRCS   := $(wildcard third_party/lwip/src/core/*.c third_party/lwip/src/core/ipv4/*.c) \
               third_party/lwip/src/netif/ethernet.c third_party/lwip/src/apps/sntp/sntp.c
KERNEL_SRCS := $(shell find src -path src/rgb30 -prune -o \( -name '*.c' -o -name '*.S' \) -print) $(LUA_SRCS) $(LWIP_SRCS) $(MBEDTLS_SRCS)
LOADER_SRCS := $(wildcard chainloader/*.S chainloader/*.c) \
               src/drivers/uart.c src/drivers/gpio.c src/drivers/mbox.c \
               src/drivers/prop.c src/drivers/timer.c src/drivers/led.c src/drivers/board.c \
               src/arch/cache.c src/lib/crc32.c

KERNEL_OBJS := $(patsubst %,$(BUILD)/k/%.o,$(KERNEL_SRCS))
KERNEL7_OBJS := $(patsubst %,$(BUILD)/k7/%.o,$(KERNEL_SRCS))
LOADER_OBJS := $(patsubst %,$(BUILD)/l/%.o,$(LOADER_SRCS))
LUA_OBJS    := $(patsubst %,$(BUILD)/k/%.o,$(LUA_SRCS))
LWIP_OBJS   := $(patsubst %,$(BUILD)/k/%.o,$(LWIP_SRCS))
MBEDTLS_OBJS := $(patsubst %,$(BUILD)/k/%.o,$(MBEDTLS_SRCS))
THIRD7_OBJS := $(patsubst %,$(BUILD)/k7/%.o,$(LUA_SRCS) $(LWIP_SRCS) $(MBEDTLS_SRCS))

# The objects of kernel7.img (Pi Zero 2 W): the same sources, other flags.
$(BUILD)/k7/% $(BUILD)/kernel7.elf: ARCH := $(ARCH7)
$(BUILD)/k7/%: SOC := -DBM_ZERO2

# The version string lives in one object, rebuilt when `git describe` changes.
VERSION_STAMP := $(BUILD)/version.txt
$(VERSION_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(VERSION) $(BRANCH)' | cmp -s - $@ || echo '$(VERSION) $(BRANCH)' > $@
$(BUILD)/k/src/kernel/version.c.o $(BUILD)/k7/src/kernel/version.c.o: $(VERSION_STAMP)
$(BUILD)/k/src/kernel/version.c.o $(BUILD)/k7/src/kernel/version.c.o: CFLAGS += -DBM_VERSION=\"$(VERSION)\" \
    -DBM_BRANCH=\"$(BRANCH)\"
FORCE:

# Third-party code: its own warning policy, not ours.
$(LUA_OBJS) $(LWIP_OBJS) $(MBEDTLS_OBJS) $(THIRD7_OBJS): WARN := -w
# Lua scripts embedded with .incbin
$(BUILD)/k/src/script/embed.S.o $(BUILD)/k7/src/script/embed.S.o: $(wildcard src/script/*.lua) keys/release-pub.pem keys/market-pub.pem \
                                 $(BUILD)/demo.bm $(BUILD)/stress.bm $(BUILD)/editor.bm $(BUILD)/sound.bm \
                                 $(BUILD)/studio.bm $(BUILD)/animator.bm $(BUILD)/mesh.bm $(BUILD)/pixel.bm \
                                 $(BUILD)/assist.bin src/ai/assist.lua $(BUILD)/assistant.bm \
                                 $(BUILD)/code.bm $(BUILD)/texroom.bm src/ai/predict.lua $(BUILD)/words.lua \
                                 src/ai/padtype.lua

# The development assistant (M30): knowledge base + trained network, built
# into the kernel. The network is trained on the PC (numpy) by `make
# ai-model` and committed, so `make` needs only Python's standard library.
AI_KB := $(wildcard src/ai/kb/*.txt)
$(BUILD)/assist.bin: $(AI_KB) src/ai/assist.weights scripts/mkassist.py scripts/assistlib.py
	@mkdir -p $(dir $@) $(BUILD)/ai
	$(PYTHON) scripts/mkassist.py -o $@ --ref $(BUILD)/ai/ref.txt --snippets $(BUILD)/ai/snippets.txt

ai-model:
	$(PYTHON) scripts/trainassist.py

# The word completion (M30): its dictionaries, from the texts of
# src/ai/words, the Lua of the games and the API of the knowledge base
WORDS_SRC := $(wildcard src/ai/words/*.txt) $(wildcard carts/*/main.lua carts/kitchen/src/*.lua carts/titan/src/*.lua) \
             $(AI_KB) scripts/mkwords.py
$(BUILD)/words.lua: $(WORDS_SRC)
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkwords.py -o $@

# bm Code, the code editor (Dev tab, monitor C)
$(BUILD)/code.bm: carts/code/main.lua carts/code/icon.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --cover carts/code/icon.png --title "bm Code" --author bm

# The assistant on its own, in the Dev tab
$(BUILD)/assistant.bm: carts/assistant/main.lua scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "bm assistant" --author bm

# The editor (M15), built into the kernel
$(BUILD)/editor.bm: carts/editor/main.lua carts/editor/icon.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --cover carts/editor/icon.png \
	    --title "bm SDK" --author bm

# The Sound editor, built into the kernel: its own bank is the demo project
$(BUILD)/sound.bm: carts/sound/main.lua carts/sound/icon.png carts/sound/demo.json scripts/mkbm.py scripts/bmaudio.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --cover carts/sound/icon.png \
	    --audio carts/sound/demo.json --title "bm Sound" --author bm

# bm Studio and bm Animator on the console (M22): the models, then their
# skeletons and animations; their shared code is src/script/bm3d.lua. bm
# Studio's sheet holds the starter tiles (made by the PC version, removed on
# 2026-10-06: the PNG stays).
$(BUILD)/studio.bm: carts/studio/main.lua carts/studio/icon.png carts/studio/sheet.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --cover carts/studio/icon.png \
	    --sheet carts/studio/sheet.png --sheet8 --title "bm Studio" --author bm

$(BUILD)/animator.bm: carts/animator/main.lua carts/animator/icon.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --cover carts/animator/icon.png --title "bm Animator" --author bm

# bm Mesh: the meshes of a .bm (its models and those its code builds), on
# the console.
$(BUILD)/mesh.bm: carts/mesh/main.lua carts/mesh/icon.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --cover carts/mesh/icon.png --title "bm Mesh" --author bm

# bm Pixel: the pixel art of a .bm (its sprite sheet), on the console.
$(BUILD)/pixel.bm: carts/pixel/main.lua carts/pixel/icon.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --cover carts/pixel/icon.png --title "bm Pixel" --author bm

$(BUILD)/stress.bm: carts/stress/main.lua scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "bm stress test" --author bm

# Texture Room (M14, M33): the 3D benchmark of the Dev tab, built into the
# kernel; the benchmark runs it at 320x180 and 640x360 (bm_next_run)
$(BUILD)/texroom.bm: carts/texroom/main.lua carts/texroom/sheet.png carts/texroom/cover.png scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "Texture Room" --author bm --res 320x180 \
	    --cover carts/texroom/cover.png --sheet carts/texroom/sheet.png

# Native demo cartridge (.bm): Lua + sprite sheet + map
DEMO_BM_SRC := carts/demo/main.lua carts/demo/sheet.png carts/demo/map.csv
$(BUILD)/demo.bm: $(DEMO_BM_SRC) scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua carts/demo/main.lua --sheet carts/demo/sheet.png \
	    --map carts/demo/map.csv --title "bm native demo" --author bm

# Demo games (Lua only, sprites drawn in code): build/carts/<name>.bm
GAMES := pong snake shooter astrowing hunt kitchen titan village nano8 overbit yharnam typing write
GAME_CARTS := $(patsubst %,$(BUILD)/carts/%.bm,$(GAMES))
title_pong    := Pong
title_snake   := Snake
title_shooter := Star Shooter
title_astrowing := Astro Wing
title_hunt := Hunter's Night
res_hunt := 320x180
title_kitchen := Chaos Kitchen
title_titan := Titan Clash
title_nano8 := nano8
title_village := Studio Village
res_village := 320x180
title_yharnam := Yharnam
title_typing := Pad Typing
title_write := bm Write
res_yharnam := 256x256
sheet8_yharnam := 1
# Optional per game: carts/<game>/cover.png (printed on the cartridge in the
# menu, scripts/mkcovers.py), sheet.png, map.csv, models.bm or models.glb (3D
# models from bm Studio / bm Animator, sdk/: with their skeletons and
# animations from a .bm; their sprite sheet too when there is no sheet.png),
# res_<game> := 320x180 or 256x256, sheet8_<game> := 1 (the sheet with a
# palette and runs: up to 256 colours, much smaller); the map's other layers
# in carts/<game>/map_<name>.csv, in the order of layers_<game> := name ...
# (R11), the flags of the tiles in flags.csv and named sprite zones in
# sprites.txt (scripts/mkbm.py --flags, --sprites).
.SECONDEXPANSION:
$(BUILD)/carts/%.bm: carts/%/main.lua scripts/mkbm.py scripts/bmmesh.py \
                      $$(wildcard carts/$$*/cover.png carts/$$*/sheet.png carts/$$*/map.csv carts/$$*/models.glb \
                                  carts/$$*/models.bm carts/$$*/flags.csv carts/$$*/sprites.txt) \
                      $$(patsubst %,carts/$$*/map_%.csv,$$(layers_$$*))
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "$(title_$*)" --author bm \
	    --res $(or $(res_$*),640x360) \
	    $(if $(wildcard carts/$*/cover.png),--cover carts/$*/cover.png) \
	    $(if $(wildcard carts/$*/sheet.png),--sheet carts/$*/sheet.png $(if $(sheet8_$*),--sheet8)) \
	    $(if $(wildcard carts/$*/map.csv),--map carts/$*/map.csv) \
	    $(foreach l,$(layers_$*),--map $(l)=carts/$*/map_$(l).csv) \
	    $(if $(wildcard carts/$*/flags.csv),--flags carts/$*/flags.csv) \
	    $(if $(wildcard carts/$*/sprites.txt),--sprites carts/$*/sprites.txt) \
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

# Overbit (M38): a hero shooter in 3D. Several Lua files joined by its
# build.py; the heroes' models and animations (MESH, ANIM) made by
# carts/overbit/art/models.py, the sounds by art/sounds.py.
OVERBIT_SRC := $(sort $(wildcard carts/overbit/src/*.lua))
OVERBIT_ART := $(wildcard carts/overbit/art/*.py carts/overbit/art/heroes/*.py carts/overbit/art/meshy/*.mesh \
                          carts/overbit/art/meshy/*.png carts/overbit/art/meshy/*.rig)
# OVERBIT_CLASSIC=1: the heroes' bodies made of primitives, not the Meshy figures
OVERBIT_MODELS_FLAGS := $(if $(OVERBIT_CLASSIC),--classic)
title_overbit := Overbit
# its screen: 480x270, each pixel 4x4 on a 1080p TV (it was 320x180)
OVERBIT_RES := 480x270
$(BUILD)/overbit/main.lua: $(OVERBIT_SRC) carts/overbit/build.py $(BUILD)/overbit/models.bm
	$(PYTHON) carts/overbit/build.py $@ --map $(BUILD)/overbit/main.map --extra $(BUILD)/overbit/21_map.lua

$(BUILD)/overbit/models.bm: $(OVERBIT_ART) scripts/bmmesh.py scripts/mkbm.py $(BUILD)/host/mappvs
	@mkdir -p $(dir $@)
	$(PYTHON) carts/overbit/art/models.py $@ --map $(BUILD)/overbit/21_map.lua --pvs $(BUILD)/host/mappvs \
	    $(OVERBIT_MODELS_FLAGS)

# what can be seen from where on the maps (carts/overbit/art/mapbake.py)
$(BUILD)/host/mappvs: tools/mappvs.c src/bm/r3d.c src/bm/gfx16.c src/bm/format.c src/lib/crc32.c src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -std=c11 -Wall -Isrc -o $@ tools/mappvs.c src/bm/r3d.c src/bm/gfx16.c src/bm/format.c \
	    src/lib/crc32.c -lm

$(BUILD)/overbit/sounds.json: carts/overbit/art/sounds.py
	@mkdir -p $(dir $@)
	$(PYTHON) carts/overbit/art/sounds.py $@

$(BUILD)/carts/overbit.bm: $(BUILD)/overbit/main.lua $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json \
                           scripts/mkbm.py scripts/bmaudio.py $(wildcard carts/overbit/cover.png)
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "$(title_overbit)" --author bm --res $(OVERBIT_RES) \
	    --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json \
	    $(if $(wildcard carts/overbit/cover.png),--cover carts/overbit/cover.png)

# the optimized release, the .b16 of the Market (the same container, docs/B16.md
# §0; under its 8 MiB): the script without comment and blank lines. The game
# sets its own screen, quality and renderer (85_quality): 1080p on the GPU,
# 640x360 where the ARM draws, saved as the default; BENCHMARK recalibrates.
$(BUILD)/overbit/main.min.lua: $(OVERBIT_SRC) carts/overbit/build.py $(BUILD)/overbit/models.bm
	$(PYTHON) carts/overbit/build.py $@ --minify --extra $(BUILD)/overbit/21_map.lua

$(BUILD)/carts/overbit.b16: $(BUILD)/overbit/main.min.lua $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json \
                            scripts/mkbm.py scripts/bmaudio.py $(wildcard carts/overbit/cover.png)
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "$(title_overbit)" --author bm --res $(OVERBIT_RES) \
	    --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json \
	    $(if $(wildcard carts/overbit/cover.png),--cover carts/overbit/cover.png)
	@test `wc -c < $@` -le 8388608 || { echo "overbit.b16 is over a .b16's 8 MiB"; exit 1; }

# variants that start in a mode: the reel (for docs/img) and the benchmark
$(BUILD)/overbit/%.bm: $(OVERBIT_SRC) carts/overbit/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build.py $(BUILD)/overbit/$*.lua --start $* --extra $(BUILD)/overbit/21_map.lua
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/$*.lua --title "Overbit $*" --author bm --res $(OVERBIT_RES) \
	    --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json

# the range with each hero chosen (tests): range-kaiju.bm, ...
OVERBIT_HEROES := kaiju sarge frost fuse rail orbit akari
$(BUILD)/overbit/range-%.bm: $(OVERBIT_SRC) carts/overbit/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build.py $(BUILD)/overbit/range-$*.lua --start range --hero $* --extra $(BUILD)/overbit/21_map.lua
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/range-$*.lua --title "Overbit $*" --author bm --res $(OVERBIT_RES) \
	    --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json

# the range at 1920x1080 (tests: the GPU's emulator)
$(BUILD)/overbit/range-1080.bm: $(OVERBIT_SRC) carts/overbit/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build.py $(BUILD)/overbit/range-1080.lua --start range --extra $(BUILD)/overbit/21_map.lua \
	    --define 'OVERBIT_RES="1920x1080"'
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/range-1080.lua --title "Overbit 1080" --author bm \
	    --res $(OVERBIT_RES) --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json

# a whole match in a minute (tests): the point opens at once, quick rounds
$(BUILD)/overbit/match-fast.bm: $(OVERBIT_SRC) carts/overbit/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build.py $(BUILD)/overbit/match-fast.lua --start match --extra $(BUILD)/overbit/21_map.lua \
	    --define 'OVERBIT_RULES={unlock=4,cap=3,pct=0.15,round_end=2,match_end=3}'
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/match-fast.lua --title "Overbit match" --author bm \
	    --res $(OVERBIT_RES) --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json

# the benchmark in short (tests): one quality, short phases, a small ring
$(BUILD)/overbit/bench-fast.bm: $(OVERBIT_SRC) carts/overbit/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build.py $(BUILD)/overbit/bench-fast.lua --start bench --extra $(BUILD)/overbit/21_map.lua \
	    --define 'OVERBIT_BENCH_FAST=true' \
	    --define 'OVERBIT_BENCH_FLAGS="res:640/gpu:arm+gpu+aa+gq+vs1+vs+q/q:2/ring:1/save:0"'
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/bench-fast.lua --title "Overbit bench" --author bm \
	    --res $(OVERBIT_RES) --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json

# the calibration (tests): from the heaviest step down, the first that holds becomes the default
$(BUILD)/overbit/bench-cal.bm: $(OVERBIT_SRC) carts/overbit/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build.py $(BUILD)/overbit/bench-cal.lua --start bench --extra $(BUILD)/overbit/21_map.lua \
	    --define 'OVERBIT_BENCH_FLAGS="auto"'
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/bench-cal.lua --title "Overbit" --author bm \
	    --res $(OVERBIT_RES) --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json

# two consoles on the network (tests): on the LAN, and through a relay on this PC
$(BUILD)/overbit/net-test.bm: $(OVERBIT_SRC) carts/overbit/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build.py $(BUILD)/overbit/net-test.lua --extra $(BUILD)/overbit/21_map.lua
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/net-test.lua --title "Overbit" --author bm \
	    --res $(OVERBIT_RES) --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json
$(BUILD)/overbit/net-relay.bm: $(OVERBIT_SRC) carts/overbit/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build.py $(BUILD)/overbit/net-relay.lua --extra $(BUILD)/overbit/21_map.lua \
	    --define 'OVERBIT_RELAY="127.0.0.1:47390"'
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/net-relay.lua --title "Overbit" --author bm \
	    --res $(OVERBIT_RES) --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json

test-overbit: $(BUILD)/host/bmhost-bin $(BUILD)/host/bmhost-gpu $(BUILD)/carts/overbit.bm $(BUILD)/overbit/reel.bm \
              $(BUILD)/overbit/bench.bm $(BUILD)/overbit/bench-fast.bm $(BUILD)/overbit/bench-cal.bm \
              $(BUILD)/overbit/match-fast.bm $(BUILD)/overbit/net-test.bm $(BUILD)/overbit/net-relay.bm \
              $(BUILD)/overbit/range-1080.bm \
              $(foreach h,$(OVERBIT_HEROES),$(BUILD)/overbit/range-$(h).bm)
	$(PYTHON) tests/overbit/run.py $(BUILD)

# Overbit's animation reel as a video (docs/img/overbit-reel-rally.mp4 and
# a GIF of the first half minute): bmhost renders it, ffmpeg encodes it
overbit-reel: $(BUILD)/host/bmhost-bin $(BUILD)/overbit/reel.bm
	$(BUILD)/host/bmhost-bin $(BUILD)/overbit/reel.bm --seconds 107 --video $(BUILD)/overbit/reel.rgb \
	    --wav $(BUILD)/overbit/reel.wav --quiet
	ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size $(OVERBIT_RES) -framerate 60 \
	    -i $(BUILD)/overbit/reel.rgb -i $(BUILD)/overbit/reel.wav -vf "scale=960:540:flags=neighbor" \
	    -c:v libx264 -preset slow -crf 28 -pix_fmt yuv420p -c:a aac -b:a 96k -shortest docs/img/overbit-reel-rally.mp4
	ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size $(OVERBIT_RES) -framerate 60 \
	    -i $(BUILD)/overbit/reel.rgb -t 32 \
	    -vf "fps=15,scale=480:270:flags=neighbor,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=none" \
	    docs/img/overbit-reel-rally.gif
	rm -f $(BUILD)/overbit/reel.rgb

# the reel of the other seven heroes (docs/img/overbit-reel-heroes.mp4) and a
# GIF of their ultimates
OVERBIT_REEL_HEROES := kaiju,sarge,frost,fuse,rail,orbit,akari
$(BUILD)/overbit/reel-heroes.bm: $(OVERBIT_SRC) carts/overbit/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build.py $(BUILD)/overbit/reel-heroes.lua --start reel --hero $(OVERBIT_REEL_HEROES) \
	    --extra $(BUILD)/overbit/21_map.lua
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/reel-heroes.lua --title "Overbit reel" --author bm --res $(OVERBIT_RES) \
	    --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json
overbit-reel-heroes: $(BUILD)/host/bmhost-bin $(BUILD)/overbit/reel-heroes.bm
	$(PYTHON) carts/overbit/tools/reel.py $(BUILD) $(BUILD)/overbit/reel-heroes.bm 232 docs/img/overbit-reel-heroes.mp4 \
	    --size 960x540 --res $(OVERBIT_RES) --crf 30 --gif docs/img/overbit-reel-heroes.gif --gif-shots ULTIMATE

# a match of ten bots on Partenope, filmed round the point, behind and in the
# eyes of the heroes (docs/img/overbit-match.mp4 and a GIF of 20 s)
$(BUILD)/overbit/match-film.bm: $(OVERBIT_SRC) carts/overbit/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build.py $(BUILD)/overbit/match-film.lua --start match --extra $(BUILD)/overbit/21_map.lua \
	    --define OVERBIT_SPECTATE=true --define 'OVERBIT_RULES={unlock=6,pct=0.3,round_end=4,match_end=5}'
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/match-film.lua --title "Overbit match" --author bm \
	    --res $(OVERBIT_RES) --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json
overbit-reel-match: $(BUILD)/host/bmhost-bin $(BUILD)/overbit/match-film.bm
	$(BUILD)/host/bmhost-bin $(BUILD)/overbit/match-film.bm --seconds 90 --video $(BUILD)/overbit/match.rgb \
	    --wav $(BUILD)/overbit/match.wav --quiet
	ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size $(OVERBIT_RES) -framerate 60 \
	    -i $(BUILD)/overbit/match.rgb -i $(BUILD)/overbit/match.wav -vf "scale=960:540:flags=neighbor" \
	    -c:v libx264 -preset slow -crf 30 -pix_fmt yuv420p -c:a aac -b:a 96k -shortest docs/img/overbit-match.mp4
	ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size $(OVERBIT_RES) -framerate 60 \
	    -i $(BUILD)/overbit/match.rgb -ss 8 -t 20 \
	    -vf "fps=10,scale=384:216:flags=neighbor,split[a][b];[a]palettegen=max_colors=80[p];[b][p]paletteuse=dither=none" \
	    docs/img/overbit-match.gif
	rm -f $(BUILD)/overbit/match.rgb

# A Lua interpreter for the PC (the same Lua 5.4 as the console): host tests
# of the Lua cartridges.
$(BUILD)/host/luahost: tests/kitchen/luahost.c $(LUA_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -w -Ithird_party/lua -o $@ tests/kitchen/luahost.c $(LUA_SRCS) -lm

test-kitchen: $(BUILD)/host/luahost $(BUILD)/kitchen/main.lua
	$< tests/kitchen/sim.lua $(BUILD)/kitchen/main.lua $(BUILD)/kitchen/main.map

test-titan: $(BUILD)/host/luahost $(BUILD)/titan/main.lua
	$< tests/titan/sim.lua $(BUILD)/titan/main.lua $(BUILD)/titan/main.map

# bmplay: a Lua cartridge played on the PC with the console's drawing (lights
# by levels too) and sound, its buttons pressed by a bot in Lua;
# tools/bmplay/video.sh BMPLAY CART.bm BOT.lua OUT.mp4 makes a video of it.
# make yharnam-video: a hunt played by tools/bmplay/yharnam_bot.lua, from the
# title to the Butcher slain (build/yharnam-run.mp4; needs ffmpeg)
BMPLAY_SRCS := tools/bmplay/bmplay.c src/bm/gfx16.c src/bm/format.c src/lib/crc32.c src/gfx/font8x16.c \
               src/gfx/font8x14.c src/gfx/font6x12.c src/audio/synth.c src/audio/player.c \
               src/audio/presets.c src/audio/lua_tone.c
$(BUILD)/host/bmplay: $(BMPLAY_SRCS) src/bm/*.h src/audio/*.h $(LUA_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Ithird_party/lua -o $@ $(BMPLAY_SRCS) $(LUA_SRCS) -lm

yharnam-video: $(BUILD)/host/bmplay $(BUILD)/carts/yharnam.bm tools/bmplay/yharnam_bot.lua
	tools/bmplay/video.sh $(BUILD)/host/bmplay $(BUILD)/carts/yharnam.bm tools/bmplay/yharnam_bot.lua \
	    $(BUILD)/yharnam-run.mp4

# Yharnam (a game of the Pi's, and rgb30.mk packs it for the RGB30): the
# street plan, the chunks, a long walk, the cost of a frame; then the fight
# measured, with the paths of the lamps (balance.lua), and the ways of the
# creatures and the phases of the bosses (foes.lua)
test-yharnam: $(BUILD)/host/luahost carts/yharnam/main.lua
	$< tests/yharnam/sim.lua carts/yharnam/main.lua
	$< tests/yharnam/balance.lua carts/yharnam/main.lua
	$< tests/yharnam/foes.lua carts/yharnam/main.lua

# The Sound editor in a fake bm: its banks are the console's format, byte for byte
$(BUILD)/demo.bmau: carts/sound/demo.json scripts/bmaudio.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/bmaudio.py pack $< -o $@

# The games' buttons by name (2026-10-04): keymap(), btn("ok"), controller(),
# prompt() for a player, in bmhost with an input script (players on a
# keyboard, a DS4, an Xbox pad)
$(BUILD)/keymap-test.bm: tests/keymap/cart.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "keymap test"

# The games' loading screen (src/bm/loading.c, 2026-10-04): the retro intro,
# its jingle, the console's page then the game's; the frames in build/loading/
$(BUILD)/host/test_loading: tests/bm/test_loading.c src/bm/loading.c src/bm/loading_logo.c src/bm/loading.h \
                            src/bm/gfx16.c src/gfx/font6x12.c src/lib/printf.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Isrc/bm -o $@ tests/bm/test_loading.c src/bm/loading.c \
	    src/bm/loading_logo.c src/bm/gfx16.c src/gfx/font6x12.c src/lib/printf.c -lm

test-loading: $(BUILD)/host/test_loading
	@mkdir -p $(BUILD)/loading
	$< $(BUILD)/loading

# riff (require "riff", the language of patterns for music, M46): the
# mini-notation, the functions, the scheduler on the sound's clock, the
# live code and the bake, in bmhost; the sound in build/riff/riff.wav
$(BUILD)/riff-test.bm: tests/riff/cart.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "riff test"

test-riff: $(BUILD)/host/bmhost-bin $(BUILD)/riff-test.bm
	@mkdir -p $(BUILD)/riff
	$< $(BUILD)/riff-test.bm --seconds 7 --wav $(BUILD)/riff/riff.wav 2>&1 | tee $(BUILD)/riff-test.log | grep "^riff"
	grep -q "^riff: \([0-9]*\)/\1 checks passed" $(BUILD)/riff-test.log

test-keymap: $(BUILD)/host/bmhost-bin $(BUILD)/keymap-test.bm tests/keymap/input.txt
	$< $(BUILD)/keymap-test.bm --input tests/keymap/input.txt --seconds 8 2>&1 | tee $(BUILD)/keymap-test.log | grep "^keymap"
	grep -q "^keymap: \([0-9]*\)/\1 checks passed" $(BUILD)/keymap-test.log

# The map's layers and the flags of its tiles (R11, 2026-10-04), the
# sheet's named zones and bmlib (R10, require "bmlib"): one cartridge
# checks them in bmhost, packed by mkbm.py from tests/gameapi/ (its
# layers, flags and zones); a frame of it and of the pause in build/gameapi/
GAMEAPI_SRC := tests/gameapi/cart.lua tests/gameapi/map.csv tests/gameapi/map_front.csv tests/gameapi/flags.csv \
               tests/gameapi/sprites.txt
$(BUILD)/gameapi/sheet.png: tests/gameapi/mksheet.py
	@mkdir -p $(dir $@)
	$(PYTHON) $< $@
$(BUILD)/gameapi-test.bm: $(GAMEAPI_SRC) $(BUILD)/gameapi/sheet.png scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua tests/gameapi/cart.lua --sheet $(BUILD)/gameapi/sheet.png \
	    --map tests/gameapi/map.csv --map front=tests/gameapi/map_front.csv --flags tests/gameapi/flags.csv \
	    --sprites tests/gameapi/sprites.txt --title "game api test" --author tests
test-gameapi: $(BUILD)/host/bmhost-bin $(BUILD)/gameapi-test.bm tests/gameapi/input.txt
	@mkdir -p $(BUILD)/gameapi
	$< $(BUILD)/gameapi-test.bm --input tests/gameapi/input.txt --seconds 8 --shots $(BUILD)/gameapi 2>&1 \
	    | tee $(BUILD)/gameapi-test.log | grep "^gameapi"
	grep -q "^gameapi: \([0-9]*\)/\1 checks passed" $(BUILD)/gameapi-test.log

# bm Write (carts/write, 2026-10-06): a document typed, styled, saved in
# /docs and exported in bmhost, then opened again (the session, the
# Markdown); tests/write/check.py reads the files (the PDF's xref too)
test-write: $(BUILD)/host/bmhost-bin $(BUILD)/carts/write.bm tests/write/input.txt tests/write/reopen.txt \
            tests/write/check.py
	rm -rf $(BUILD)/write-sd $(BUILD)/write && mkdir -p $(BUILD)/write-sd $(BUILD)/write
	$< $(BUILD)/carts/write.bm --sd $(BUILD)/write-sd --input tests/write/input.txt --seconds 14 \
	    --shots $(BUILD)/write > $(BUILD)/write/run1.log 2>&1
	$< $(BUILD)/carts/write.bm --sd $(BUILD)/write-sd --input tests/write/reopen.txt --seconds 2 \
	    --shots $(BUILD)/write > $(BUILD)/write/run2.log 2>&1
	$(PYTHON) tests/write/check.py $(BUILD)/write-sd/docs $(BUILD)/write
# The dev kit's profiler of functions (R14, src/bm/profile.c): profile()
# read by the game itself, on the PC's real time (--clock-scale 1)
$(BUILD)/profile-test.bm: tests/profile/cart.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "profile test" --author tests

test-profile: $(BUILD)/host/bmhost-bin $(BUILD)/profile-test.bm
	$< $(BUILD)/profile-test.bm --seconds 10 --clock-scale 1 2>&1 | tee $(BUILD)/profile-test.log | grep "^profile"
	grep -q "^profile: \([0-9]*\)/\1 checks passed" $(BUILD)/profile-test.log

# PS in a game played online (online(), 2026-10-04): the question to the
# player leaving over the game that goes on, back and Esc stay, PS again
# leaves through _leave(); the screen of the question in build/online/
$(BUILD)/online-test.bm: tests/online/cart.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "online test"

test-online: $(BUILD)/host/bmhost-bin $(BUILD)/online-test.bm tests/online/input.txt
	@mkdir -p $(BUILD)/online
	$< $(BUILD)/online-test.bm --input tests/online/input.txt --shots $(BUILD)/online \
	    --seconds 8 2>&1 | tee $(BUILD)/online-test.log | grep "^online"
	grep -q "^online: \([0-9]*\)/\1 checks passed" $(BUILD)/online-test.log
	grep -q "left the online game" $(BUILD)/online-test.log

# bmnet (require "bmnet", games over the network): two bmhost on this PC,
# the lobby, sure messages with a fifth of the packets lost, a lockstep
# match that must be the same on both, a player leaving; on the LAN and
# through the relay (tools/overbit_relay.py)
test-bmnet: $(BUILD)/host/bmhost-bin tests/bmnet/cart.lua tests/bmnet/run.py src/script/bmnet.lua
	$(PYTHON) tests/bmnet/run.py $(BUILD)

test-sound: $(BUILD)/host/luahost $(BUILD)/demo.bmau carts/sound/main.lua
	$< tests/sound/sim.lua carts/sound/main.lua $(BUILD)/demo.bmau

# Resource files (docs/RISORSE.md, scripts/bmres.py): models, images, sounds,
# maps and palettes out of a .bm and back in, conversions, broken files
test-res: $(BUILD)/carts/village.bm $(BUILD)/demo.bm $(BUILD)/sound.bm scripts/bmres.py $(BUILD)/host/test_res
	rm -rf $(BUILD)/res && $(PYTHON) tests/res/test_bmres.py $(BUILD)/carts/village.bm $(BUILD)/demo.bm \
	    $(BUILD)/sound.bm $(BUILD)/res
	$(BUILD)/host/test_res $(BUILD)/res $(BUILD)/carts/village.bm

$(BUILD)/host/test_res: tests/bm/test_res.c src/bm/format.c src/lib/crc32.c src/bm/bm.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/bm/test_res.c src/bm/format.c src/lib/crc32.c

# nano8 on the PC: the loader on every cart, the translator, the API test
# cart, then each shipped cart played for a while (tests/nano8/run.py)
N8_HOST_SRC := src/bm/n8.c src/bm/n8font.c src/bm/n8cart.c src/bm/png.c src/bm/n8lua.c src/audio/n8snd.c \
               src/bm/gfx16.c src/gfx/font8x16.c
$(BUILD)/host/n8host: tests/nano8/n8host.c $(N8_HOST_SRC) src/bm/n8*.h src/audio/n8snd.h $(LUA_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -w -Isrc -Ithird_party/lua -o $@ tests/nano8/n8host.c $(N8_HOST_SRC) \
	    $(filter-out third_party/lua/lua.c third_party/lua/luac.c,$(LUA_SRCS)) -lm

$(BUILD)/host/n8cartinfo: tests/nano8/cartinfo.c src/bm/n8.c src/bm/n8font.c src/bm/n8cart.c src/bm/png.c src/bm/n8*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/nano8/cartinfo.c src/bm/n8.c src/bm/n8font.c src/bm/n8cart.c src/bm/png.c -lm

test-nano8: $(BUILD)/host/n8host $(BUILD)/host/n8cartinfo $(BUILD)/host/luahost $(BUILD)/nano8/main.lua
	$(PYTHON) tests/nano8/run.py --build $(BUILD) $(NANO8_ROMS)

# bmhost: a .bm cartridge on the PC with the console's own runtime (Lua,
# gfx16, r3d, the sound player) and the kernel's services replaced
# (tests/host/stubs.c): frames to PNG or raw video, sound to WAV, input from
# a script. For the reels of the games and for tests with screenshots.
BMHOST_RT := src/bm/runtime.c src/bm/tokens.c src/bm/gfx16.c src/bm/r3d.c src/bm/world3d.c src/bm/format.c src/bm/meshcap.c \
             src/kernel/syskeys.c \
             src/bm/require.c src/kernel/prompts.c src/gfx/font8x16.c src/gfx/font8x14.c \
             src/gfx/font6x12.c src/lib/printf.c src/lib/crc32.c src/audio/audio.c src/audio/synth.c \
             src/audio/player.c src/audio/presets.c src/audio/lua_tone.c src/ai/net.c src/ai/nn.c \
             src/bm/decimate.c src/bm/cutout.c \
             src/bm/glb.c src/bm/json.c src/bm/png.c src/bm/jpeg.c src/bm/loading.c src/bm/loading_logo.c \
             src/bm/profile.c
BMHOST_LUA := $(filter-out third_party/lua/lua.c third_party/lua/luac.c,$(LUA_SRCS))
BMHOST_OBJS := $(patsubst %,$(BUILD)/host/bmhost/%.o,$(BMHOST_RT) $(BMHOST_LUA))
$(BUILD)/host/bmhost/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o $@ $<
$(BUILD)/host/bmhost/runtime-deps: $(wildcard src/bm/*.h src/audio/*.h src/kernel/*.h src/usb/hid.h)
	@mkdir -p $(dir $@) && touch $@
$(BMHOST_OBJS): $(BUILD)/host/bmhost/runtime-deps
$(BUILD)/host/bmhost-bin: tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/host.h tests/host/libs.S \
                          $(BMHOST_OBJS) src/ai/assist.lua src/script/bm3d.lua src/script/bmlib.lua src/script/bmnet.lua src/ai/predict.lua $(BUILD)/words.lua src/ai/padtype.lua \
                          src/script/riff.lua
	$(HOSTCC) -O2 -g -Wall -Wextra -D_DEFAULT_SOURCE -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -I$(BUILD) \
	    -o $@ tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/libs.S $(BMHOST_OBJS) -lm
bmhost: $(BUILD)/host/bmhost-bin

# bmhost-gpu: the same with the GPU's 3D (src/gpu/gpu3d.c) on the V3D
# emulator of the tests (tests/gpu/v3d_emu.c): the frames as the GPU draws
# them, to set them beside the ARM's (slower: the emulator is plain C)
$(BUILD)/host/bmhost-gpu: tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/host.h tests/host/libs.S \
                          $(BMHOST_OBJS) src/ai/assist.lua src/script/bm3d.lua src/script/bmlib.lua src/script/bmnet.lua src/ai/predict.lua $(BUILD)/words.lua src/ai/padtype.lua \
                          src/gpu/gpu3d.c src/gpu/v3d_cl.c \
                          src/gpu/shaders.h tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h
	$(HOSTCC) -std=c11 -O2 -g -w -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free \
	    -c src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -g -Wall -Wextra -D_DEFAULT_SOURCE -DBMHOST_GPU -Itests/host/shim -Isrc -Isrc/bm -Itests/gpu \
	    -Ithird_party/lua -I$(BUILD) -o $@ tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/libs.S \
	    $(BMHOST_OBJS) $@-gpu3d.o src/gpu/v3d_cl.c tests/gpu/v3d_emu.c -lm
bmhost-gpu: $(BUILD)/host/bmhost-gpu

# the frame queue's 2D (M35): frames with the queue off and on, the same
test-queue2d: $(BUILD)/host/bmhost-gpu
	$(PYTHON) tests/gpu/queue2d.py $(BUILD)

.DEFAULT_GOAL := all
.PHONY: FORCE test-keymap test-gameapi test-write test-profile test-online test-bmnet test-loading test-smp test-qpu test-gpu3d test-queue2d test-b3d test-v3d bench3d count-insns all clean firmware image \
        image-pi1 sdcard install sdcard-chainloader sdcard-stress qemu qemu7 qemu-screenshot \
        run-serial test test-bm test-res test-ai test-img2mesh ai-model test-predict test-padtype predict-bench syllables test-usb test-audio \
        test-music music-model \
        test-fat test-kitchen test-titan test-yharnam test-sound test-nano8 test-net test-http test-https test-release \
        release disasm wav test-studio test-prompts test-hyp test-zero2 \
        showreel bmhost bmhost-gpu test-overbit overbit-reel overbit-reel-heroes overbit-reel-match yharnam-video \
        test-catalog test-github test-lan market-seed test-host test-qemu

all: $(BUILD)/kernel.img $(K7) $(BUILD)/chainloader.img $(GAME_CARTS)

# (one rule per directory: a pattern rule with several targets would be one
# recipe making them all)
$(BUILD)/k/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/l/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/k7/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/k/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/k7/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/l/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(LCFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/kernel.elf: $(KERNEL_OBJS) linker.ld
	$(CC) $(LDFLAGS) -T linker.ld -Wl,-Map=$(BUILD)/kernel.map $(KERNEL_OBJS) $(LDLIBS) -o $@

$(BUILD)/kernel7.elf: $(KERNEL7_OBJS) linker.ld
	$(CC) $(LDFLAGS) -T linker.ld -Wl,-Map=$(BUILD)/kernel7.map $(KERNEL7_OBJS) $(LDLIBS) -o $@

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
# Both kernels go on the card (kernel7.img with ZERO2=1): config.txt makes
# the firmware start kernel7.img on a Pi Zero 2 W and kernel.img on the
# other boards, so one card works in every Pi bm runs on.
KERNEL ?= kernel
SD_CARTS := $(GAME_CARTS)
# the WiFi and Bluetooth chips' firmware (make firmware), in bm/: the Zero
# W's BCM43438, then the Zero 2 W's CYW43436 (two versions of the chip)
RADIO_FW := BCM43430A1.hcd brcmfmac43430-sdio.bin brcmfmac43430-sdio.txt brcmfmac43430-sdio.clm_blob \
            SYN43430A1.hcd SYN43430B0.hcd brcmfmac43436-sdio.bin brcmfmac43436-sdio.txt \
            brcmfmac43436-sdio.clm_blob brcmfmac43436s-sdio.bin brcmfmac43436s-sdio.txt
sdcard: $(BUILD)/$(KERNEL).img $(K7) $(SD_CARTS)
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)/carts
	cp $(FW_DIR)/bootcode.bin $(FW_DIR)/start.elf $(FW_DIR)/fixup.dat $(DIST)/
	cp boot/config.txt $(DIST)/
	cp $(BUILD)/$(KERNEL).img $(DIST)/kernel.img
	$(if $(K7),cp $(K7) $(DIST)/kernel7.img,rm -f $(DIST)/kernel7.img)
	rm -f $(DIST)/carts/*.bm       # the old extension (now .bm)
	rm -f $(DIST)/carts/*.cart     # the old .cart format: bm no longer plays it
	cp $(SD_CARTS) $(DIST)/carts/
	mkdir -p $(DIST)/carts/nano8 && cp $(NANO8_ROMS) $(DIST)/carts/nano8/
	mkdir -p $(DIST)/bm && cp boot/ca.pem $(DIST)/bm/ca.pem
	@for f in $(RADIO_FW); do \
	    if [ -f $(FW_DIR)/$$f ]; then mkdir -p $(DIST)/bm && cp $(FW_DIR)/$$f $(DIST)/bm/ && \
	        echo "cp $$f -> $(DIST)/bm/"; fi; done
	@echo "Copy the contents of $(DIST)/ ($(KERNEL)$(if $(K7), and kernel7)) to the root of a FAT32 SD card."

# Whole SD card image (MBR + FAT32): firmware, config, both kernels and the
# cartridges. Write it with Raspberry Pi Imager ("Use custom"), balenaEtcher
# or dd. Needs dosfstools and mtools. kernel.img is the same for every
# BCM2835 board, kernel7.img (ZERO2=1) is the Pi Zero 2 W's; image-pi1 leaves out the
# WiFi/Bluetooth chips' firmware and kernel7.img (the Pi 1 has no radio; on
# the B / B+ the network is the Ethernet).
IMAGE_FILES = $(FW_DIR)/bootcode.bin=bootcode.bin $(FW_DIR)/start.elf=start.elf \
              $(FW_DIR)/fixup.dat=fixup.dat boot/config.txt=config.txt \
              $(BUILD)/kernel.img=kernel.img \
              $(foreach c,$(SD_CARTS),$(c)=carts/$(notdir $(c))) \
              $(foreach r,$(NANO8_ROMS),$(r)=carts/nano8/$(notdir $(r))) \
              boot/ca.pem=bm/ca.pem
image: $(BUILD)/kernel.img $(K7) $(SD_CARTS)
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)
	$(PYTHON) scripts/mksd.py $(DIST)/bm.img --size-mib 64 --label BM $(IMAGE_FILES) \
	    $(if $(K7),$(K7)=kernel7.img) \
	    $(foreach f,$(RADIO_FW),$(if $(wildcard $(FW_DIR)/$(f)),$(FW_DIR)/$(f)=bm/$(f)))

image-pi1: $(BUILD)/kernel.img $(SD_CARTS)
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)
	$(PYTHON) scripts/mksd.py $(DIST)/bm-pi1.img --size-mib 64 --label BM $(IMAGE_FILES)

# The files of a release (M19), in $(DIST)/release: kernel.img and
# kernel7.img (the Pi Zero 2 W's, ZERO2=1), the games, bm/ca.pem and manifest.txt
# with where each goes, its size and SHA-256,
# signed (manifest.sig) with the key in BM_RELEASE_KEY (the environment;
# on GitHub the repository's secret). CI on a tag v*:
#   make release VERSION=v0.1.0 RELEASE_FLAGS=--require-key
# The signature must match keys/release-pub.pem, the key in the kernel.
RELEASE_DIR := $(DIST)/release
RELEASE_PUB ?= keys/release-pub.pem
# RGB30_KERNEL=build/rgb30/kernel8.img (made first with make TARGET=rgb30):
# the RGB30's manifest-rgb30 (kernel8.img, bm/ca.pem) in the same release,
# for its System > Updates
RGB30_KERNEL ?=
release: $(BUILD)/kernel.img $(K7) $(GAME_CARTS)
	rm -rf $(RELEASE_DIR)
	$(PYTHON) scripts/mkrelease.py $(RELEASE_DIR) --version $(VERSION) --commit $$(git rev-parse HEAD) \
	    --file $(BUILD)/kernel.img:/kernel.img $(if $(K7),--file $(K7):/kernel7.img) \
	    $(foreach c,$(GAME_CARTS),--file $(c):/carts/$(notdir $(c))) \
	    --file boot/ca.pem:/bm/ca.pem --pub $(RELEASE_PUB) $(RELEASE_FLAGS)
	$(if $(RGB30_KERNEL),$(PYTHON) scripts/mkrelease.py $(RELEASE_DIR) --manifest manifest-rgb30 \
	    --version $(VERSION) --commit $$(git rev-parse HEAD) --file $(RGB30_KERNEL):/kernel8.img \
	    --file boot/ca.pem:/bm/ca.pem --pub $(RELEASE_PUB) $(RELEASE_FLAGS))

# Copies what make sdcard prepared onto a mounted SD card (SD=/mnt/d by
# default): the kernels, boot files, config.txt, cartridges and the chip
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
	$$S cp $(DIST)/bootcode.bin $(DIST)/start.elf $(DIST)/fixup.dat $(DIST)/config.txt $(DIST)/kernel.img \
	    $(if $(K7),$(DIST)/kernel7.img) $(SD)/ && \
	$$S rm -f $(SD)/carts/demo.cart $(SD)/carts/demo.bm $(SD)/carts/stress.bm \
	    $(SD)/carts/texroom.bm $(SD)/carts/texroom_hd.bm && \
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

# kernel7.img (Pi Zero 2 W) in raspi2b, a Pi 2 B: QEMU has no Zero 2 W, but
# the Pi 2 B has the BCM2710's peripherals (and a Cortex-A7, no radio)
qemu7: $(BUILD)/kernel7.img
	$(QEMU) -M raspi2b -serial stdio -serial null -bios $<

qemu-screenshot: $(BUILD)/kernel.img
	./scripts/qemu-screenshot.sh $< $(BUILD)/screen.png

# make test: the tests on the PC, then those in QEMU. The CI runs them on
# several machines at once: make test-host HOST_SKIP="..." (the PC's, but
# those named), make test-qemu SHARD=K/N (group K of N of the QEMU tests).
HOST_TESTS := test-bm test-res test-usb test-fat test-audio test-music test-riff test-kitchen test-titan test-yharnam test-sound test-nano8 test-net test-http test-https test-img3d \
      test-catalog test-github test-lan test-keymap test-gameapi test-write test-profile test-online test-bmnet test-loading \
      test-release test-smp test-qpu test-gpu3d test-queue2d test-b3d test-v3d test-ai test-predict test-padtype \
      test-studio test-prompts test-overbit $(if $(K7),test-hyp)
# what the QEMU tests read besides `all` (made by the PC's tests too)
QEMU_DEPS := $(BUILD)/host/meshview
QEMU_TESTS = $(PYTHON) tests/qemu_test.py --build $(BUILD) $(if $(SHARD),--shard $(SHARD))

test: all $(HOST_TESTS) $(QEMU_DEPS)
	$(QEMU_TESTS)

test-host: $(filter-out $(HOST_SKIP),$(HOST_TESTS))

test-qemu: all $(QEMU_DEPS)
	$(QEMU_TESTS)

# The same QEMU tests with kernel7.img, the Pi Zero 2 W's, in raspi2b (the
# BCM2710's peripherals); those of the BCM2835 boards alone are skipped
test-zero2: export ZERO2 = 1
test-zero2: all $(BUILD)/kernel7.img test-hyp $(QEMU_DEPS)
	$(QEMU_TESTS) --kernel7

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
test-net: $(BUILD)/host/test_netcon $(BUILD)/host/test_ethnet $(BUILD)/host/test_config
	$(BUILD)/host/test_netcon
	$(BUILD)/host/test_ethnet
	$(BUILD)/host/test_config
	$(PYTHON) tests/net/check_bm_net_config.py

# bm/config.txt (src/kernel/config.c): the file, and the settings from the PC
# (netxfer's C, bm_net.py --config, easy_install.sh)
$(BUILD)/host/test_config: tests/kernel/test_config.c src/kernel/config.c src/kernel/config.h src/lib/printf.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -o $@ tests/kernel/test_config.c src/kernel/config.c src/lib/printf.c

$(BUILD)/host/test_ethnet: tests/net/test_ethnet.c src/net/net.c src/net/net.h src/net/cartnet.c src/net/cartnet.h \
                           src/kernel/fiber.h src/usb/smsc95xx.c src/usb/smsc95xx.h tests/usb/lan9512_sim.c \
                           tests/usb/lan9512_sim.h $(LWIP_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/lwip/src/include -o $@ \
		tests/net/test_ethnet.c src/net/net.c src/net/cartnet.c src/usb/smsc95xx.c tests/usb/lan9512_sim.c $(LWIP_SRCS)

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

# The Market's catalog (M25): scripts/mkmarket.py checks and signs a folder
# of games with a test key, src/net/catalog.c checks signature, records and
# files, the kernel's PNG reader decodes the covers
test-catalog: $(BUILD)/host/test_catalog
	$(PYTHON) tests/net/run_catalog_test.py $(BUILD)/host/test_catalog

$(BUILD)/host/test_catalog: tests/net/test_catalog.c src/net/catalog.c src/net/catalog.h \
                            src/bm/png.c src/bm/png.h $(MBEDTLS_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/mbedtls/include \
		-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/net/test_catalog.c src/net/catalog.c \
		src/bm/png.c $(MBEDTLS_SRCS) -lm

# Games between consoles on the home network (M24): src/net/lan.c on lwIP's
# loopback, sender and receiver in one process
test-lan: $(BUILD)/host/test_lan
	$(BUILD)/host/test_lan

$(BUILD)/host/test_lan: tests/net/test_lan.c src/net/lan.c src/net/lan.h src/net/stream.c $(LWIP_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/lwip/src/include -Ithird_party/mbedtls/include \
		-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/net/test_lan.c src/net/lan.c src/net/stream.c \
		third_party/mbedtls/library/sha256.c third_party/mbedtls/library/platform_util.c $(LWIP_SRCS)

# Publishing to the Market from the console (M25): src/net/github.c over
# POSIX sockets against a fake GitHub API (branch or fork, files, pull request);
# and the reports (src/kernel/reports.c): their names, a file on a branch
test-github: $(BUILD)/host/test_github $(BUILD)/host/test_report $(BUILD)/host/test_reports
	$(BUILD)/host/test_report
	$(BUILD)/host/test_reports
	$(PYTHON) tests/net/run_github_test.py $(BUILD)/host/test_github

# the reports waiting that go by themselves (src/kernel/reports.c), with a
# fake SD card, GitHub, network and clock
$(BUILD)/host/test_reports: tests/kernel/test_reports.c src/kernel/reports.c src/kernel/reports.h src/net/report.c \
                            src/lib/printf.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -o $@ tests/kernel/test_reports.c src/kernel/reports.c src/net/report.c \
		src/lib/printf.c

$(BUILD)/host/test_report: tests/net/test_report.c src/net/report.c src/net/report.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -o $@ tests/net/test_report.c src/net/report.c

$(BUILD)/host/test_github: tests/net/test_github.c src/net/github.c src/net/github.h src/net/http.c src/net/http.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -Isrc/net -DHTTP_USER_AGENT='"test"' -o $@ tests/net/test_github.c \
		src/net/github.c src/net/http.c

# The market's repository (M25): its template (market/) and the games of
# the project, built here, into MARKET (a clone of f-accomando/bm-market).
# A game's version is the day its bytes changed.
MARKET ?= ../bm-market
MARKET_VERSION := $(shell date -u +%Y.%m.%d)
# the games that go to the Market as .b16 (the optimized release, if there is a rule)
MARKET_B16 := overbit
market-seed: $(GAME_CARTS) $(patsubst %,$(BUILD)/carts/%.b16,$(MARKET_B16))
	@test -d $(MARKET) || { echo "MARKET=$(MARKET): clone f-accomando/bm-market there first"; exit 1; }
	mkdir -p $(MARKET)/.github/workflows
	cp market/README.md market/.gitignore $(MARKET)/
	cp market/.github/workflows/market.yml $(MARKET)/.github/workflows/
	for g in $(GAMES); do \
		c=$(BUILD)/carts/$$g.bm; case " $(MARKET_B16) " in *" $$g "*) c=$(BUILD)/carts/$$g.b16;; esac; \
		$(PYTHON) scripts/mkmarket.py $(MARKET)/games --add $$c --id $$g \
			--version $(MARKET_VERSION) --about-file market/about.txt || exit 1; \
	done
	$(PYTHON) scripts/mkmarket.py $(MARKET)/games --check
# kernel7.img's start in Hyp mode, as the Pi Zero 2 W's firmware does it:
# start.S and vectors.S in QEMU's virt machine with the virtualization
# extensions (raspi2b starts in SVC), tests/boot/hyp_main.c on its PL011
HYP_DIR := $(BUILD)/hyp
$(HYP_DIR)/hyp.elf: tests/boot/hyp_main.c $(BUILD)/k7/src/boot/start.S.o $(BUILD)/k7/src/kernel/vectors.S.o linker.ld
	@mkdir -p $(dir $@)
	sed 's/^    \. = 0x8000;/    . = 0x40008000;/' linker.ld > $(HYP_DIR)/linker.ld
	$(CC) $(ARCH7) -DBM_ZERO2 -O2 -Wall -Wextra -Isrc/kernel -ffreestanding -nostdlib -nostartfiles \
	    -T $(HYP_DIR)/linker.ld tests/boot/hyp_main.c $(BUILD)/k7/src/boot/start.S.o \
	    $(BUILD)/k7/src/kernel/vectors.S.o -lgcc -o $@

test-hyp: $(HYP_DIR)/hyp.elf
	$(PYTHON) tests/boot/run_hyp.py $(QEMU) $<

# Bluetooth LE pairing cryptography (SMP), against the spec's sample data
test-smp: $(BUILD)/host/test_smp
	$(BUILD)/host/test_smp

$(BUILD)/host/test_smp: tests/bt/smp_test.c src/bt/smp_crypto.c src/bt/smp_crypto.h $(MBEDTLS_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -Isrc -Isrc/net -Ithird_party/mbedtls/include \
		-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/bt/smp_test.c src/bt/smp_crypto.c $(MBEDTLS_SRCS)

# HTTP client over POSIX sockets, against a local Python server
# the picture-to-model flow (src/net/img3d.c) against a fake service, with the .glb reader
test-img3d: $(BUILD)/host/test_img3d
	$(PYTHON) tests/net/run_img3d_test.py $(BUILD)/host/test_img3d $(BUILD)/img3d

$(BUILD)/host/test_img3d: tests/net/test_img3d.c src/net/img3d.c src/net/http.c src/net/*.h $(GLB_SRCS) src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -Isrc/bm -DHTTP_USER_AGENT='"test"' -o $@ tests/net/test_img3d.c src/net/img3d.c src/net/http.c $(GLB_SRCS) -lm

test-http: $(BUILD)/host/test_http
	$(PYTHON) tests/net/run_http_test.py $(BUILD)/host/test_http

$(BUILD)/host/test_http: tests/net/test_http.c src/net/http.c src/net/http.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -DHTTP_USER_AGENT='"test"' -o $@ tests/net/test_http.c src/net/http.c

test-audio: $(BUILD)/host/test_audio
	$<

# The music assistant (src/ai/music.c): the melody network against the
# Python reference, every recipe, the words of the requests. `make
# music-model` trains the network again (numpy) from src/ai/melodies.txt:
# then commit src/ai/music_net.c. MUSIC_WAV=dir also writes examples.
test-music: $(BUILD)/host/test_music
	$(PYTHON) scripts/trainmusic.py --check
	$< $(MUSIC_WAV)

$(BUILD)/host/test_music: tests/ai/test_music.c src/ai/music.c src/ai/music_net.c src/ai/nn.c src/audio/presets.c \
                          src/audio/player.c src/audio/synth.c src/ai/*.h src/audio/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/ai/test_music.c src/ai/music.c src/ai/music_net.c src/ai/nn.c \
	    src/audio/presets.c src/audio/player.c src/audio/synth.c -lm

music-model:
	$(PYTHON) scripts/trainmusic.py

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

test-usb: $(BUILD)/host/test_hid $(BUILD)/host/test_eth $(BUILD)/host/test_board $(BUILD)/host/test_board7
	$(BUILD)/host/test_hid
	$(BUILD)/host/test_eth
	$(BUILD)/host/test_board
	$(BUILD)/host/test_board7

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

$(BUILD)/host/test_board7: tests/usb/test_board.c src/drivers/board.c src/drivers/board.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -DBM_ZERO2 -Isrc -o $@ tests/usb/test_board.c src/drivers/board.c

# the meshes a cartridge builds in its code (cart_meshes(), bm Mesh)
MESHCAP_SRCS := tests/bm/test_meshcap.c src/bm/meshcap.c src/bm/format.c src/bm/r3d.c src/lib/crc32.c
$(BUILD)/host/test_meshcap: $(MESHCAP_SRCS) src/bm/*.h $(LUA_SRCS) tests/host/libs.S src/script/bmlib.lua src/script/bmnet.lua \
                            $(BUILD)/words.lua
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc/bm -Isrc -Ithird_party/lua -I$(BUILD) -o $@ $(MESHCAP_SRCS) tests/host/libs.S \
	    $(LUA_SRCS) -lm

# the dev kit's token counter (stat(11), code_tokens(), the SDK)
$(BUILD)/host/test_tokens: tests/bm/test_tokens.c src/bm/tokens.c src/bm/tokens.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc/bm -o $@ tests/bm/test_tokens.c src/bm/tokens.c

# the polygon reducer (bm Studio's reduce, tools/bmreduce.py)
DECIMATE_SRCS := tests/bm/test_decimate.c src/bm/decimate.c src/bm/format.c src/lib/crc32.c
$(BUILD)/host/test_decimate: $(DECIMATE_SRCS) src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc/bm -Isrc -o $@ $(DECIMATE_SRCS) -lm

# the .glb reader (the image-to-3D services' files on the console)
GLB_SRCS := src/bm/glb.c src/bm/json.c src/bm/jpeg.c src/bm/decimate.c src/bm/png.c
$(BUILD)/host/test_glb: tests/bm/test_glb.c $(GLB_SRCS) src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc/bm -Isrc -o $@ tests/bm/test_glb.c $(GLB_SRCS) -lm

# the outline maker (a model from a picture without any network, cutout.c)
CUTOUT_SRCS := src/bm/cutout.c $(GLB_SRCS)
$(BUILD)/host/test_cutout: tests/bm/test_cutout.c $(CUTOUT_SRCS) src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc/bm -Isrc -o $@ tests/bm/test_cutout.c $(CUTOUT_SRCS) -lm

# the outline maker as a shared library (ctypes: scripts/bmcutout.py, tools/cutout2mesh.py)
$(BUILD)/host/libbmcutout.so: $(CUTOUT_SRCS) src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -shared -fPIC -o $@ $(CUTOUT_SRCS) -lm

# the reducer as a shared library for the PC tools (ctypes: scripts/bmdecimate.py)
$(BUILD)/host/libbmdecimate.so: src/bm/decimate.c src/bm/decimate.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -shared -fPIC -o $@ src/bm/decimate.c -lm

$(BUILD)/meshcap-test.bm: tests/bm/meshcap_cart.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "meshcap test" --author tests

test-bm: $(BUILD)/host/test_bm $(BUILD)/demo.bm $(BUILD)/host/test_meshcap $(BUILD)/carts/astrowing.bm \
         $(BUILD)/texroom.bm $(BUILD)/carts/kitchen.bm $(BUILD)/meshcap-test.bm \
         $(BUILD)/host/test_decimate $(BUILD)/carts/village.bm $(BUILD)/host/libbmdecimate.so \
         $(BUILD)/host/test_glb $(BUILD)/host/test_cutout $(BUILD)/host/libbmcutout.so $(BUILD)/host/test_tokens
	$< $(BUILD)/demo.bm
	$(BUILD)/host/test_tokens carts/editor/main.lua carts/pong/main.lua
	$(BUILD)/host/test_decimate $(BUILD)/carts/village.bm $(BUILD)/carts/kitchen.bm
	$(PYTHON) tests/bm/run_glb_test.py $(BUILD)/host/test_glb $(BUILD)/glb
	$(PYTHON) tests/bm/run_cutout_test.py $(BUILD)/host/test_cutout $(BUILD)/cutout
	$(PYTHON) tools/cutout2mesh.py $(BUILD)/cutout/lolli_alpha.png -o $(BUILD)/cutout/tool.bm
	$(PYTHON) tools/cutout2mesh.py $(BUILD)/cutout/lolli_white.png -o $(BUILD)/cutout/tool.bm --lathe --name vase
	$(PYTHON) tools/bmreduce.py $(BUILD)/carts/village.bm --ratio 0.5 -o $(BUILD)/village-half.bm
	$(BUILD)/host/test_meshcap src/bm/runtime.c \
	    $(BUILD)/meshcap-test.bm '!stop here,wheel:1,crate:12,cars1_body:1,gem:2' \
	    $(BUILD)/carts/astrowing.bm ship:32,dart,tower,gate,ring:120,laser,bolt,debris,debris2,mark,core,core_hot,turret \
	    $(BUILD)/texroom.bm floor_mesh,walls_mesh,crate_mesh,pillar_mesh \
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
AI_SRCS := src/ai/assist.c src/ai/nn.c src/ai/text.c src/ai/sprite.c src/ai/mesh.c src/ai/mesh_chars.c src/ai/mesh_script.c \
           src/ai/music.c src/ai/music_net.c
$(BUILD)/host/test_ai: tests/ai/test_ai.c $(AI_SRCS) src/ai/*.h src/lib/crc32.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/ai/test_ai.c $(AI_SRCS) src/lib/crc32.c -lm

# Lua for the PC with the `ai` table: the panel's tests
$(BUILD)/host/luaai: tests/ai/luaai.c src/ai/lua_ai.c src/ai/lua_music.c $(AI_SRCS) src/ai/*.h src/lib/crc32.c $(LUA_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -w -DBM_HOST_TEST -Isrc -Ithird_party/lua -o $@ tests/ai/luaai.c src/ai/lua_ai.c src/ai/lua_music.c \
		$(AI_SRCS) src/lib/crc32.c $(LUA_SRCS) -lm

# The 3D recipes drawn on the PC: build/ai/meshes.ppm (every recipe) and
# `meshview one mech out.ppm` (one, from four sides and in its poses)
$(BUILD)/host/meshview: tests/ai/meshview.c $(AI_SRCS) src/ai/*.h src/lib/crc32.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/ai/meshview.c $(AI_SRCS) src/lib/crc32.c -lm

# The word completion on the PC: words, contexts, suggestions, and the
# benchmark texts typed again with Tab
test-predict: $(BUILD)/host/luahost $(BUILD)/words.lua
	$(BUILD)/host/luahost tests/predict/predict_test.lua $(BUILD)

# Typing with the pad (docs/PADTYPE.md): the rules of compose and of the
# on-screen keyboard, every practice text written to the end by padtype's
# coach (the presses a character), then Pad Typing in bmhost copying the
# first text of each language to its result (shots in build/padtype/)
test-padtype: $(BUILD)/host/luahost $(BUILD)/words.lua $(BUILD)/host/bmhost-bin $(BUILD)/carts/typing.bm
	$(BUILD)/host/luahost tests/padtype/pad_test.lua $(BUILD)
	for l in it en lua; do \
	  mkdir -p $(BUILD)/padtype/$$l && \
	  $(BUILD)/host/luahost tests/padtype/script.lua $(BUILD) $$l 1 $(BUILD)/padtype/$$l.txt && \
	  $(BUILD)/host/bmhost-bin $(BUILD)/carts/typing.bm --input $(BUILD)/padtype/$$l.txt \
	      --shots $(BUILD)/padtype/$$l --seconds 300 > $(BUILD)/padtype/$$l.log 2>&1 && \
	  grep "typing: done" $(BUILD)/padtype/$$l.log || { tail -20 $(BUILD)/padtype/$$l.log; exit 1; }; \
	done

# Its benchmark (docs/PREDICT.md): the keys saved on the texts of 100
# characters, then the corpus with each group left out of the dictionary
WORD_FOLDS := giochi informativi lettere narrativa quotidiano tecnica
predict-bench: $(BUILD)/host/luahost $(BUILD)/words.lua
	@mkdir -p $(BUILD)/predict
	for g in $(WORD_FOLDS); do $(PYTHON) scripts/mkwords.py --skip it_$$g -o $(BUILD)/predict/fold_$$g.lua || exit 1; done
	$(BUILD)/host/luahost tests/predict/bench.lua $(BUILD) --folds

# The syllables of Italian, counted on the same texts (docs/PREDICT.md)
syllables:
	$(PYTHON) scripts/syllables.py

test-ai: $(BUILD)/host/test_ai $(BUILD)/assist.bin $(BUILD)/host/luahost $(BUILD)/host/luaai $(BUILD)/host/meshview $(BUILD)/words.lua
	$< $(BUILD)/assist.bin $(BUILD)/ai/ref.txt
	$(BUILD)/host/meshview sheet $(BUILD)/ai/meshes.ppm > /dev/null
	$(BUILD)/host/luahost tests/ai/check_snippets.lua $(BUILD)/ai/snippets.txt
	$(BUILD)/host/luaai $(BUILD)/assist.bin tests/ai/panel_test.lua $(BUILD)
	$(BUILD)/host/luaai $(BUILD)/assist.bin tests/ai/act_test.lua
	$(MAKE) test-img2mesh

# tools/img2mesh.py offline: the recorded replies (the mech in the part
# language) become a .bm with the model, then one more model in it;
# tools/meshy2mesh.py on a .glb made by the test (no call to Meshy)
test-img2mesh: $(BUILD)/host/meshview
	rm -rf $(BUILD)/img2mesh/test $(BUILD)/img2mesh/test.bm && mkdir -p $(BUILD)/img2mesh
	$< one knight $(BUILD)/img2mesh/knight.ppm > /dev/null
	$(PYTHON) tools/img2mesh.py $(BUILD)/img2mesh/knight.ppm -o $(BUILD)/img2mesh/test.bm --name mech \
	    --replay tests/ai/img2mesh/replay --work $(BUILD)/img2mesh/test --rounds 1
	$(PYTHON) tools/img2mesh.py $(BUILD)/img2mesh/knight.ppm -o $(BUILD)/img2mesh/test.bm --name mech2 \
	    --replay tests/ai/img2mesh/replay --work $(BUILD)/img2mesh/test2 --rounds 0
	$(PYTHON) tests/ai/check_img2mesh.py $(BUILD)/img2mesh/test.bm
	$(PYTHON) tests/ai/check_meshy.py $(BUILD)
	$(PYTHON) tests/ai/check_local2mesh.py $(BUILD)/local2mesh

# The console's bm Studio, bm Animator, bm Mesh, bm Pixel and the SDK on the
# PC (luahost), then the files they wrote read by the Python of the build
# (tests/studio/check_files.py) and by the kernel's parser.
test-studio: $(BUILD)/host/test_bm $(BUILD)/demo.bm $(BUILD)/host/luahost $(BUILD)/carts/village.bm \
             $(BUILD)/carts/astrowing.bm $(BUILD)/host/test_meshcap
	rm -rf $(BUILD)/studio3d-sd && mkdir -p $(BUILD)/studio3d-sd/carts
	cp $(BUILD)/carts/village.bm $(BUILD)/studio3d-sd/carts/
	$(BUILD)/host/luahost tests/studio/tools3d_host.lua . $(BUILD)/studio3d-sd
	rm -rf $(BUILD)/mesh-sd && mkdir -p $(BUILD)/mesh-sd/carts
	cp $(BUILD)/carts/village.bm $(BUILD)/carts/astrowing.bm $(BUILD)/mesh-sd/carts/
	$(BUILD)/host/luahost tests/studio/mesh_host.lua . $(BUILD)/mesh-sd
	$(BUILD)/host/test_meshcap src/bm/runtime.c $(BUILD)/mesh-sd/carts/astrowing.bm ship \
	    $(BUILD)/mesh-sd/carts/village.bm "" $(BUILD)/mesh-sd/carts/meshcopy.bm ""
	$(PYTHON) scripts/bmmesh.py $(BUILD)/mesh-sd/carts/astrowing.bm $(BUILD)/mesh-sd/carts/village.bm >/dev/null
	rm -rf $(BUILD)/sdk-sd && mkdir -p $(BUILD)/sdk-sd/carts
	cp $(BUILD)/carts/village.bm $(BUILD)/sdk-sd/carts/
	$(BUILD)/host/luahost tests/studio/sdk_host.lua . $(BUILD)/sdk-sd
	rm -rf $(BUILD)/pixel-sd && mkdir -p $(BUILD)/pixel-sd/carts
	cp $(BUILD)/carts/village.bm $(BUILD)/demo.bm $(BUILD)/pixel-sd/carts/
	$(BUILD)/host/luahost tests/studio/pixel_host.lua . $(BUILD)/pixel-sd
	$(BUILD)/host/test_meshcap src/bm/runtime.c $(BUILD)/pixel-sd/carts/village.bm "" $(BUILD)/pixel-sd/carts/newspr.bm ""
	$(PYTHON) tests/studio/check_files.py $(BUILD)/studio3d-sd $(BUILD)/mesh-sd $(BUILD)/pixel-sd $(BUILD)/carts/village.bm
	$(PYTHON) tests/studio/anim_figure.py $(BUILD)/anim-figure.bm
	$(BUILD)/host/test_bm $(BUILD)/demo.bm $(BUILD)/anim-figure.bm $(BUILD)/studio3d-sd/carts/blocks.bm

# The showreel at the top of the README (docs/showreel.gif and .mp4): the
# villager of Studio Village in a new cartridge, the map in the SDK, the
# code in bm Code with the assistant and the game, on the console in QEMU;
# the cards drawn by Chromium (Playwright), ffmpeg puts it together
# (tools/showreel/)
SHOWREEL := $(BUILD)/showreel
showreel: $(BUILD)/kernel.img
	$(PYTHON) tools/showreel/console.py $(BUILD) $(SHOWREEL)/console
	node tools/showreel/cards.js $(SHOWREEL)/cards
	$(PYTHON) tools/showreel/assemble.py $(SHOWREEL) docs/showreel.mp4 docs/showreel.gif

# Rasterizer bench (M33): checksums of fixed 3D scenes (a change to r3d.c
# that should not change the picture must keep them), and with
# count-insns the ARM instructions per pixel and per triangle (needs
# gcc-arm-linux-gnueabihf and qemu-user).
$(BUILD)/host/bench3d: tests/bm/bench3d.c src/bm/gfx16.c src/bm/r3d.c src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/bm/bench3d.c src/bm/gfx16.c src/bm/r3d.c -lm

bench3d: $(BUILD)/host/bench3d
	$<

count-insns:
	$(PYTHON) tests/bm/count_insns.py

# The GPU backend of the 3D (M33) on a V3D emulator, against the software
# rasterizer, for the four byte orders its probe must find
$(BUILD)/host/test_gpu3d: tests/gpu/test_gpu3d.c tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h src/gpu/gpu3d.c \
                          src/gpu/gpu3d.h src/gpu/v3d_cl.c src/gpu/v3d.h src/gpu/shaders.h src/bm/r3d.c \
                          src/bm/gfx16.c src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free \
	    -c src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Itests/gpu -o $@ tests/gpu/test_gpu3d.c tests/gpu/v3d_emu.c \
	    src/gpu/v3d_cl.c src/bm/r3d.c src/bm/gfx16.c $@-gpu3d.o -lm

# The 3D Bench (src/bm/b3d.c) on the PC: the V3D emulated, quick mode,
# twice (the second run reads the first's report); pages in build/b3d
$(BUILD)/host/b3d_host: tests/bm/b3d_host.c src/bm/b3d.c src/bm/b3d.h tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h \
                        src/gpu/gpu3d.c src/gpu/gpu3d.h src/gpu/v3d_cl.c src/gpu/v3d.h src/gpu/shaders.h \
                        src/gpu/version3d.h src/bm/r3d.c src/bm/gfx16.c src/gfx/font8x16.c src/gfx/font6x12.c src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free \
	    -c src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Itests/gpu -o $@ tests/bm/b3d_host.c src/bm/b3d.c tests/gpu/v3d_emu.c \
	    src/gpu/v3d_cl.c src/bm/r3d.c src/bm/gfx16.c src/gfx/font8x16.c src/gfx/font6x12.c $@-gpu3d.o -lm

test-b3d: $(BUILD)/host/b3d_host
	$< --selftest
	rm -rf $(BUILD)/b3d && mkdir -p $(BUILD)/b3d
	$< $(BUILD)/b3d
	$< $(BUILD)/b3d
	$(PYTHON) tests/bm/check_b3d.py $(BUILD)/b3d

# The V3D driver's job runner against a model of the V3D's registers
$(BUILD)/host/test_v3d: tests/gpu/test_v3d.c tests/gpu/mock/drivers/mmio.h src/gpu/v3d.c src/gpu/v3d.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Wno-format -Itests/gpu/mock -Isrc -o $@ tests/gpu/test_v3d.c src/gpu/v3d.c

test-v3d: $(BUILD)/host/test_v3d
	$<

test-gpu3d: $(BUILD)/host/test_gpu3d
	$< 1 0 0 0 0 0
	$< 0 0 1 1 1 1
	$< 1 1 1 0 1 2
	$< 0 1 0 1 0 0
	$< 1 0 2 0 0 0
	EMU_HANG_ZCLEAR=1 $< 1 0 0 0 0 0
	EMU_VPM_WORDS=1 $< 0 1 0 0 1 0
	EMU_NO_THREADS=1 $< 1 0 0 0 0 0

# QPU shaders (M33): the assembler against shaders run on a Pi, and
# src/gpu/shaders.h up to date with the sources in tools/qpuasm.py
test-qpu:
	$(PYTHON) tools/qpuasm.py -o $(BUILD)/shaders.h
	cmp $(BUILD)/shaders.h src/gpu/shaders.h

HOSTCC ?= cc

clean:
	rm -rf build build-stress $(DIST)

-include $(KERNEL_OBJS:.o=.d) $(KERNEL7_OBJS:.o=.d) $(LOADER_OBJS:.o=.d)

endif   # TARGET
