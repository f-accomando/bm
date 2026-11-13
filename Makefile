# bm - bare metal console for Raspberry Pi Zero / Zero W (BCM2835);
# the same kernel runs on the Pi 1 (A, B, A+, B+). The Pi Zero 2 W
# (BCM2710A1, Cortex-A53) has its own build of the same sources, kernel7.img
#
# make TARGET=rgb30: the PowKiddy RGB30 (RK3566, AArch64), see core/rgb30.mk
ifeq ($(TARGET),rgb30)
include core/rgb30.mk
else

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

VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
# the branch, for the reports' names (CI checks out a commit: its variables say which)
BRANCH := $(or $(GITHUB_HEAD_REF),$(GITHUB_REF_NAME),$(shell git rev-parse --abbrev-ref HEAD 2>/dev/null),unknown)

ARCH    := -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard
# kernel7.img, the Pi Zero 2 W: ARMv7 in 32 bit (hardware divide, VFPv4 with
# 32 registers, NEON) tuned for the Cortex-A53; ARMv7 and not ARMv8 so that
# it also runs in QEMU's raspi2b (Cortex-A7, the same peripherals).
# SOC=-DBM_ZERO2: the BCM2710's addresses (core/src/drivers/mmio.h).
ARCH7   := -march=armv7ve -mtune=cortex-a53 -marm -mfpu=neon-vfpv4 -mfloat-abi=hard
SOC     :=
# The Pi Zero 2 W's build (kernel7.img) is off for now: make ZERO2=1 builds
# it again, puts it on the card, in the image and in the release, and adds
# its tests (test-hyp) to make test. make test-zero2 builds it anyway.
ZERO2   ?= 0
export ZERO2
CLEAN_KERNEL ?= 0
CLEAN_DEFS := $(if $(filter 1,$(CLEAN_KERNEL)),-DBM_CLEAN_KERNEL)
K7      := $(if $(filter 1,$(ZERO2)),$(BUILD)/kernel7.img)
COMMON  = $(ARCH) $(SOC) -std=c11 -O2 -Wall -Wextra -g -Icore/src -Icore/third_party \
          -ffunction-sections -fdata-sections \
          -DUART_BAUD=$(BAUD) $(CLEAN_DEFS)
# Kernel: hosted C on top of newlib (libc, libm), see core/src/lib/syscalls.c.
CFLAGS  = $(COMMON) -D_DEFAULT_SOURCE -Icore/third_party/lua \
          -Icore/third_party/lwip/src/include -Icore/src/net \
          -Icore/third_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' $(WARN)
# PI_O3_FILES: source files built with -O3 instead of -O2 (default empty: every file -O2,
# the output does not change). Example: make PI_O3_FILES="core/src/bm/gfx16.c core/src/bm/r3d.c".
# Paths as in KERNEL_SRCS. The flags are not a prerequisite: after you change the
# variable, run make clean (or delete the .o files) so the files are built again.
# Off for now; the accept rule is in docs/progress/graphics.md (Open work).
PI_O3_FILES ?=
ifneq ($(strip $(PI_O3_FILES)),)
$(patsubst %,$(BUILD)/k/%.o,$(PI_O3_FILES)): CFLAGS += -O3
endif
# Chainloader: freestanding, no libc.
LCFLAGS := $(COMMON) -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns
ASFLAGS = $(ARCH) $(SOC) $(CLEAN_DEFS) -g -Icore/src -Icore/src/kernel -Wa,-I$(BUILD)
LDFLAGS = $(ARCH) -nostartfiles -Wl,--gc-sections
LDLIBS  := -Wl,--start-group -lc -lm -lgcc -Wl,--end-group
LLDLIBS := -nostdlib -lgcc

LUA_SRCS    := $(wildcard core/third_party/lua/*.c)
MBEDTLS_SRCS := $(wildcard core/third_party/mbedtls/library/*.c)
LWIP_SRCS   := $(wildcard core/third_party/lwip/src/core/*.c core/third_party/lwip/src/core/ipv4/*.c) \
               core/third_party/lwip/src/netif/ethernet.c core/third_party/lwip/src/apps/sntp/sntp.c
KERNEL_SRCS := $(shell find core/src -path core/src/rgb30 -prune -o \( -name '*.c' -o -name '*.S' \) -print) $(LUA_SRCS) $(LWIP_SRCS) $(MBEDTLS_SRCS)
LOADER_SRCS := $(wildcard core/chainloader/*.S core/chainloader/*.c) \
               core/src/drivers/uart.c core/src/drivers/gpio.c core/src/drivers/mbox.c \
               core/src/drivers/prop.c core/src/drivers/timer.c core/src/drivers/led.c core/src/drivers/board.c \
               core/src/arch/cache.c core/src/lib/crc32.c

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
$(BUILD)/k/core/src/kernel/version.c.o $(BUILD)/k7/core/src/kernel/version.c.o: $(VERSION_STAMP)
$(BUILD)/k/core/src/kernel/version.c.o $(BUILD)/k7/core/src/kernel/version.c.o: CFLAGS += -DBM_VERSION=\"$(VERSION)\" \
    -DBM_BRANCH=\"$(BRANCH)\"
FORCE:

# Third-party code: its own warning policy, not ours.
$(LUA_OBJS) $(LWIP_OBJS) $(MBEDTLS_OBJS) $(THIRD7_OBJS): WARN := -w
EMBED_TOOLS := $(if $(filter 1,$(CLEAN_KERNEL)),,$(BUILD)/editor.bm $(BUILD)/sound.bm \
               $(BUILD)/studio.bm $(BUILD)/animator.bm $(BUILD)/mesh.bm $(BUILD)/pixel.bm \
               $(BUILD)/assistant.bm $(BUILD)/code.bm)
# Lua scripts embedded with .incbin
$(BUILD)/k/core/src/script/embed.S.o $(BUILD)/k7/core/src/script/embed.S.o: $(wildcard core/src/script/*.lua) core/keys/release-pub.pem core/keys/market-pub.pem \
                                 $(EMBED_TOOLS) \
                                 $(BUILD)/assist.bin core/src/ai/assist.lua \
                                 core/src/ai/predict.lua $(BUILD)/words.lua \
                                 core/src/ai/padtype.lua

# The development assistant (M30): knowledge base + trained network, built
# into the kernel. The network is trained on the PC (numpy) by `make
# ai-model` and committed, so `make` needs only Python's standard library.
AI_KB := $(wildcard core/src/ai/kb/*.txt)
$(BUILD)/assist.bin: $(AI_KB) core/src/ai/assist.weights core/scripts/mkassist.py core/scripts/assistlib.py
	@mkdir -p $(dir $@) $(BUILD)/ai
	$(PYTHON) core/scripts/mkassist.py -o $@ --ref $(BUILD)/ai/ref.txt --snippets $(BUILD)/ai/snippets.txt

ai-model:
	$(PYTHON) core/scripts/trainassist.py

# The word completion (M30): its dictionaries, from the texts of
# core/src/ai/words, the Lua of the games and the API of the knowledge base
WORDS_SRC := $(wildcard core/src/ai/words/*.txt) $(wildcard bm/*/main.lua bm/kitchen/src/*.lua bm/titan/src/*.lua) core/src/ai/api.tsv core/scripts/api_lint_allow.txt \
             $(AI_KB) core/scripts/mkwords.py
$(BUILD)/words.lua: $(WORDS_SRC)
	@mkdir -p $(dir $@)
	$(PYTHON) core/scripts/mkwords.py -o $@

# The built-in tools (code, assistant, editor, sound, studio, animator, mesh, pixel) are built as
# $(BUILD)/<tool>.bm by the cart rule below (TOOL_BMS).

# ---- Cartridges: one data-driven recipe (how to add a cart: docs/PROJECT-LAYOUT.md) ----
# A cart <k> needs only bm/<k>/main.lua. The recipe adds each optional file that exists:
#   bm.info, cover.png, sheet.png, map.csv, map_<layer>.csv (layers_<k>), flags.csv, sprites.txt,
#   models.bm or models.glb; in bm/<k>/assets/ or in bm/<k>/.
# Per-cart variables, only where a cart differs (all optional):
#   cdir_<k>    folder of the cart (default bm/<k>)
#   lua_<k>     the Lua script (default <cdir>/main.lua; a joined one: $(BUILD)/<k>/main.lua)
#   bminfo_<k>  the bm.info file;  noinfo_<k> := 1 builds with --title/--author/--res (title_<k>, res_<k>)
#   cover_<k>, sheet_<k>, models_<k>, audio_<k>   a file other than the default
#   sheet8_<k> := 1            the sheet with a palette and runs (up to 256 colours, much smaller)
#   layers_<k> := name ...     the map layers, files map_<name>.csv, in this order
#   extra_<k>   more mkbm.py flags, at the end
# The key <k> is the cart name.
c_dir    = $(or $(cdir_$(1)),bm/$(1))
c_asset  = $(firstword $(wildcard $(call c_dir,$(1))/assets/$(2) $(call c_dir,$(1))/$(2)))
c_lua    = $(or $(lua_$(1)),$(wildcard $(call c_dir,$(1))/main.lua))
c_b      = $(or $(b_$(1)),$(wildcard $(call c_dir,$(1))/main.b))
c_info   = $(if $(noinfo_$(1)),,$(or $(bminfo_$(1)),$(wildcard $(call c_dir,$(1))/bm.info)))
c_cover  = $(or $(cover_$(1)),$(wildcard $(call c_dir,$(1))/cover.png))
c_sheet  = $(or $(sheet_$(1)),$(call c_asset,$(1),sheet.png))
c_map    = $(call c_asset,$(1),map.csv)
c_layer  = $(call c_asset,$(1),map_$(2).csv)
c_flags  = $(call c_asset,$(1),flags.csv)
c_sprite = $(call c_asset,$(1),sprites.txt)
c_models = $(or $(models_$(1)),$(call c_asset,$(1),models.bm),$(call c_asset,$(1),models.glb))
# the files a cart is built from: the prerequisites of its rule
c_deps   = $(call c_b,$(1)) $(call c_info,$(1)) $(call c_cover,$(1)) $(call c_sheet,$(1)) $(call c_map,$(1)) \
           $(foreach l,$(layers_$(1)),$(call c_layer,$(1),$(l))) $(call c_flags,$(1)) \
           $(call c_sprite,$(1)) $(call c_models,$(1)) $(audio_$(1)) $(if $(audio_$(1)),core/scripts/bmaudio.py)

define CART_RECIPE
@mkdir -p $(dir $@)
$(PYTHON) core/scripts/mkbm.py -o $@ \
	    $(if $(call c_lua,$(1)),--lua $(call c_lua,$(1))) \
	    $(if $(call c_b,$(1)),--b $(call c_b,$(1))) \
	    $(if $(call c_info,$(1)),--info $(call c_info,$(1)),--title "$(title_$(1))" --author bm --res $(or $(res_$(1)),640x360)) \
	    $(if $(call c_cover,$(1)),--cover $(call c_cover,$(1))) \
	    $(if $(call c_sheet,$(1)),--sheet $(call c_sheet,$(1)) $(if $(sheet8_$(1)),--sheet8)) \
	    $(if $(call c_map,$(1)),--map $(call c_map,$(1))) \
	    $(foreach l,$(layers_$(1)),--map $(l)=$(call c_layer,$(1),$(l))) \
	    $(if $(call c_flags,$(1)),--flags $(call c_flags,$(1))) \
	    $(if $(call c_sprite,$(1)),--sprites $(call c_sprite,$(1))) \
	    $(if $(call c_models,$(1)),--models $(call c_models,$(1))) \
	    $(if $(audio_$(1)),--audio $(audio_$(1))) $(extra_$(1))
endef

.SECONDEXPANSION:
# The built-in tools: $(BUILD)/<tool>.bm. They print bm/<tool>/icon.png on the cartridge.
TOOL_BMS := code assistant editor sound studio animator mesh pixel snes_sdk
$(foreach t,$(TOOL_BMS),$(eval cover_$(t) := bm/$(t)/icon.png))
cover_assistant := bm/assistant/icon.png
# the Assistant app carries the dictionaries and the knowledge file (DATA section): a clean kernel has none
extra_assistant := --data words.lua=$(BUILD)/words.lua --data assist.bin=$(BUILD)/assist.bin
$(BUILD)/assistant.bm: $(BUILD)/words.lua $(BUILD)/assist.bin
audio_sound := bm/sound/demo.json
sheet8_studio := 1
$(TOOL_BMS:%=$(BUILD)/%.bm): $(BUILD)/%.bm: $$(call c_lua,$$*) core/scripts/mkbm.py $$(call c_deps,$$*)
	$(call CART_RECIPE,$*)

# Demo games (Lua only, sprites drawn in code): build/bm/<name>.bm
GAMES := pong snake shooter astrowing kitchen titan nano8 overbit yharnam typing write ko2 bench3d font
GAME_CARTS := $(patsubst %,$(BUILD)/bm/%.bm,$(GAMES))
sheet8_yharnam := 1
sheet8_ko2 := 1
# The 3D Bench (Lua edition): its models with bones and baked light, its sprite sheet and the 8 map layers
# of the test map8 are made by bm/bench3d/build/mkassets.py (no binary in the repository)
models_bench3d := $(BUILD)/bench3d/models.bm
extra_bench3d := $(foreach l,1 2 3 4 5 6 7 8,--map l$(l)=$(BUILD)/bench3d/map_l$(l).csv)
$(BUILD)/bench3d/models.bm: bm/bench3d/build/mkassets.py core/scripts/bmmesh.py core/scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) bm/bench3d/build/mkassets.py $(dir $@)
# Yharnam's map drawn by hand (the SDK's map page): bm/yharnam/assets/map_ground.csv, map_overlay.csv and
# map_objects.csv (the placeholders of the houses, trees, lamps... it places)
# (bm/yharnam/build/mkmap.py makes the first one, and takes it back from the console)
layers_yharnam := ground overlay objects
# Test fixture, not a game: the test kit, a cartridge with 8 models, a skeleton and a sheet, made by
# tests/fixtures/mktestkit.py (no binary in the repository). The studio, resource and QEMU tests open
# it. It is not in GAMES, the SD card or the Market. mapkit.bm (below) is the sample with a map.
$(BUILD)/bm/testkit.bm: tests/fixtures/mktestkit.py core/scripts/mkbm.py core/scripts/bmmesh.py
	@mkdir -p $(dir $@)
	$(PYTHON) tests/fixtures/mktestkit.py $@

# The map kit: a second fixture, with a true-colour sheet and a map of 4 layers and no model (mktestkit.py --map)
$(BUILD)/bm/mapkit.bm: tests/fixtures/mktestkit.py core/scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) tests/fixtures/mktestkit.py $@ --map

$(BUILD)/bm/%.bm: $$(call c_lua,$$*) core/scripts/mkbm.py core/scripts/bmmesh.py $$(call c_deps,$$*)
	$(call CART_RECIPE,$*)

# The projects (.bme) of the carts the Dev tab edits: the same cartridge, built the same way
BME_CARTS := kitchen bmweb agent hero
$(BME_CARTS:%=$(BUILD)/bm/%.bme): $(BUILD)/bm/%.bme: $$(call c_lua,$$*) core/scripts/mkbm.py $$(call c_deps,$$*)
	$(call CART_RECIPE,$*)

# Chaos Kitchen (M17) is written in several Lua files, joined by its build.py
KITCHEN_SRC := $(sort $(wildcard bm/kitchen/src/*.lua))
$(BUILD)/kitchen/main.lua: $(KITCHEN_SRC) bm/kitchen/build/build.py
	$(PYTHON) bm/kitchen/build/build.py $@ --map $(BUILD)/kitchen/main.map

lua_kitchen := $(BUILD)/kitchen/main.lua

# bm Web (bm/bmweb): the modules in src/ and main.lua are joined by build/build.py. The project
# (.bme) is the source the Dev tab edits; the .bm is the same cartridge to play. Not in GAMES.
BMWEB_SRC := $(sort $(wildcard bm/bmweb/src/*.lua)) bm/bmweb/main.lua
$(BUILD)/bmweb/main.lua: $(BMWEB_SRC) bm/bmweb/build/build.py
	$(PYTHON) bm/bmweb/build/build.py $@

lua_bmweb := $(BUILD)/bmweb/main.lua

# Kratos Build (bm/kratos): a mesh built part by part by a virtual pointer. One Lua file whose
# PARTS table is made by bm/kratos/build/glb2parts.py. The .bme is the source, the .bm the cartridge.
$(BUILD)/bm/kratos.bm $(BUILD)/bm/kratos.bme: $(BUILD)/bm/kratos.%: bm/kratos/main.lua core/scripts/mkbm.py bm/kratos/bm.info
	@mkdir -p $(dir $@)
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --info bm/kratos/bm.info

# bm SNES (bm/snes): the user interface of a Super Nintendo emulator whose machine is in C (core/src/bm/snes_*.c).
# build.py packs bm/snes/src/main.lua and the cover. The .bme is the source, the .bm the cartridge.
# Not in GAMES yet: the speed on the Pi Zero W and the RGB30 is not measured (docs/progress/snes.md).
$(BUILD)/bm/snes.bm $(BUILD)/bm/snes.bme: $(BUILD)/bm/snes.%: bm/snes/build/build.py $(wildcard bm/snes/src/*.lua) bm/snes/bm.info core/scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) bm/snes/build/build.py $(BUILD)/snes
	cp $(BUILD)/snes/snes.$* $@

# bm PSX (bm/psx): the user interface of a PlayStation 1 emulator whose machine is in C (core/src/bm/psx_*.c).
# build.py packs bm/psx/src/main.lua and the cover. The .bme is the source, the .bm the cartridge.
# Not in GAMES yet: the speed on the Pi Zero W and the RGB30 is not measured (docs/progress/psx.md).
$(BUILD)/bm/psx.bm $(BUILD)/bm/psx.bme: $(BUILD)/bm/psx.%: bm/psx/build/build.py $(wildcard bm/psx/src/*.lua) bm/psx/bm.info core/scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) bm/psx/build/build.py $(BUILD)/psx
	cp $(BUILD)/psx/psx.$* $@

# the cartridge in bmhost with a test PS-EXE made by tests/psx/make_testexe.py (tests/psx/render_test.py reads the picture)
test-psx-cart: $(BUILD)/host/bmhost-bin $(BUILD)/bm/psx.bm tests/psx/render_test.py tests/psx/make_testexe.py
	$(PYTHON) tests/psx/render_test.py $< $(BUILD)/bm/psx.bm

# bm PSX tests: the C core on small MIPS programs, the GTE, the GPU, the DMA, the CD-ROM and the SPU (tests/psx/core_test.c).
PSX_CORE := core/src/bm/psx_cpu.c core/src/bm/psx_gte.c core/src/bm/psx_bus.c core/src/bm/psx_gpu.c core/src/bm/psx_cd.c core/src/bm/psx_spu.c core/src/bm/psx_jit.c
test-psx: tests/psx/core_test.c $(PSX_CORE) $(wildcard core/src/bm/psx*.h)
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O1 -g -Wall -Wextra -Icore/src/bm -o $(BUILD)/host/psx_core_test tests/psx/core_test.c $(PSX_CORE)
	$(BUILD)/host/psx_core_test

# bm SNES tests: the C core on small programs (tests/snes/core_test.c) and the cartridge in bmhost with a test ROM
# made by tests/snes/make_testrom.py (tests/snes/render_test.py reads the pictures).
test-snes: $(BUILD)/host/bmhost-bin $(BUILD)/host/luahost $(BUILD)/bm/snes.bm tests/snes/render_test.py tests/snes/make_testrom.py tests/snes/pic_due_test.lua bm/snes/src/pic.lua
	$(HOSTCC) -O1 -g -Wall -Wextra -Icore/src/bm -o $(BUILD)/host/snes_core_test tests/snes/core_test.c core/src/bm/snes_bus.c core/src/bm/snes_sa1.c core/src/bm/snes_sa1_cpu.c core/src/bm/snes_gsu.c core/src/bm/snes_dsp77.c core/src/bm/snes_cpu.c core/src/bm/snes_ppu.c core/src/bm/snes_spc.c core/src/bm/snes_dsp.c
	$(BUILD)/host/snes_core_test
	$(BUILD)/host/luahost tests/snes/pic_due_test.lua
	$(PYTHON) tests/snes/render_test.py $< $(BUILD)/bm/snes.bm

# The SNES cart test build SNES_M7TEST=1 (opens m7test.sfc, runs the script of its five variants): docs/progress/snes-gpu-pi.md, "Mode 7 test ROM".
test-snes-m7: $(BUILD)/host/bmhost-bin $(BUILD)/host/luahost tests/snes/m7test_cart_test.py tests/snes/m7test/main.s bm/snes/src/main.lua bm/snes/build/build.py
	tests/snes/build_m7test.sh $(BUILD)/m7test.sfc
	$(PYTHON) tests/snes/m7test_cart_test.py $< $(BUILD)/m7test.sfc

# bm SNES SDK: the 65C816 assembler and the ROM builder (core/src/script/snes_asm.lua). The unit tests run in the Lua of the
# repository; the second test builds tests/snes/testrom.s, compares it with the ROM of make_testrom.py and runs it in the core.
SNES_CORE_SRCS := core/src/bm/snes_bus.c core/src/bm/snes_sa1.c core/src/bm/snes_sa1_cpu.c core/src/bm/snes_gsu.c core/src/bm/snes_dsp77.c core/src/bm/snes_cpu.c core/src/bm/snes_ppu.c core/src/bm/snes_spc.c core/src/bm/snes_dsp.c
test-snes-asm: $(BUILD)/host/luahost core/src/script/snes_asm.lua tests/snes/asm_test.lua tests/snes/asm_cli.lua tests/snes/asm_rom_test.py tests/snes/asm_run.c tests/snes/testrom.s tests/snes/make_testrom.py $(wildcard core/src/bm/snes*.h)
	$(BUILD)/host/luahost tests/snes/asm_test.lua
	$(HOSTCC) -O1 -g -Wall -Wextra -Icore/src/bm -o $(BUILD)/host/snes_asm_run tests/snes/asm_run.c $(SNES_CORE_SRCS)
	$(PYTHON) tests/snes/asm_rom_test.py $(BUILD)/host/luahost $(BUILD)/host/snes_asm_run

# bm NES (bm/nes): the user interface of a Famicom / NES emulator whose machine is in C (core/src/bm/nes.c).
# build.py packs bm/nes/src/main.lua and the cover. The .bme is the source, the .bm the cartridge.
# Not in GAMES yet: the speed on the Pi Zero W and the RGB30 is not measured (docs/progress/nes.md).
$(BUILD)/bm/nes.bm $(BUILD)/bm/nes.bme: $(BUILD)/bm/nes.%: bm/nes/build/build.py $(wildcard bm/nes/src/*.lua) bm/nes/bm.info core/scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) bm/nes/build/build.py $(BUILD)/nes
	cp $(BUILD)/nes/nes.$* $@

# bm NES tests: the C core on small programs (tests/nes/core_test.c; with build/nestest.nes and build/nestest.log, the public
# nestest ROM and its log, it also checks the CPU instruction by instruction) and the cartridge in bmhost with a test ROM
# made by tests/nes/make_testrom.py (tests/nes/render_test.py reads the pictures).
test-nes: $(BUILD)/host/bmhost-bin $(BUILD)/bm/nes.bm tests/nes/render_test.py tests/nes/make_testrom.py tests/nes/core_test.c core/src/bm/nes.c core/src/bm/nes.h
	$(HOSTCC) -O1 -g -Wall -Wextra -Icore/src/bm -o $(BUILD)/host/nes_core_test tests/nes/core_test.c core/src/bm/nes.c
	$(BUILD)/host/nes_core_test $(if $(and $(wildcard build/nestest.nes),$(wildcard build/nestest.log)),build/nestest.nes build/nestest.log)
	$(PYTHON) tests/nes/render_test.py $< $(BUILD)/bm/nes.bm
# bm Tape (bm/tape): a studio for the Pi: synth, drums, sequencer, song (TAPE), MIX, FX and punch-in effects with sound (phase 3), a DJ preview scene.
# build.py joins bm/tape/src/*.lua, puts engine.b (compiled on the console by native_compile) at its placeholder
# line, makes the cover and packs the .bme and the .bm. Not in GAMES yet (docs/design/bm-tape.md).
# The sound bank holds 24 samples rendered on the PC from Felucca-Engines (MIT): build.py calls core/scripts/felpack.py, which
# compiles core/tools/felrender/felrender.c with the files of core/third_party/felucca-engines/ and renders bm/tape/sounds/felucca.json.
TAPE_FEL := core/scripts/felpack.py core/tools/felrender/felrender.c bm/tape/sounds/felucca.json core/scripts/bmaudio.py $(wildcard core/third_party/felucca-engines/src/* core/third_party/felucca-engines/tools/*)
# The WAV files and the bank input (build.py makes them again for its own output folder; this rule is for the tests).
$(BUILD)/tape/bank.json: $(TAPE_FEL)
	$(PYTHON) core/scripts/felpack.py $(BUILD)/tape

$(BUILD)/bm/tape.bm $(BUILD)/bm/tape.bme: $(BUILD)/bm/tape.%: bm/tape/build/build.py $(wildcard bm/tape/src/*.lua) bm/tape/src/engine.b bm/tape/bm.info core/scripts/mkbm.py $(TAPE_FEL)
	@mkdir -p $(dir $@)
	$(PYTHON) bm/tape/build/build.py $(BUILD)/tape
	cp $(BUILD)/tape/tape.$* $@

# Test builds: TAPE_TEST=N logs every note the sequencer queues and updates it every N-th frame (a frame of N/60 s).
$(BUILD)/tape-t%/tape.bm: bm/tape/build/build.py bm/tape/test/hook.lua $(wildcard bm/tape/src/*.lua) bm/tape/src/engine.b bm/tape/bm.info core/scripts/mkbm.py $(TAPE_FEL)
	TAPE_TEST=$* $(PYTHON) bm/tape/build/build.py $(BUILD)/tape-t$*

# bm Tape tests: engine.b on the stack VM and as Lua against a Python reference (tests/tape/engine_test.py), the
# cartridge in bmhost: pictures of every page and engine, keys, recording and the error path (render_test.py), the timing of
# the sequencer at 60, 20 and 7.5 frames a second (timing_test.py, the exit test of phase 2) and the sound in a WAV
# (sound_test.py: levels, pan, echo, punch-in effects), the song save and load (song_test.py, the exit test of phase 3), the 24 PCM sounds of Felucca-Engines: WAV format, peak, SHA-256 against tests/tape/felucca_golden.txt, the
# bank of the cartridge (felucca_test.py). bmhost runs the native engine on the PC.
test-tape: $(BUILD)/tape/bank.json $(BUILD)/host/luahost $(BUILD)/host/bmhost-bin $(BUILD)/bm/tape.bm $(BUILD)/tape-t1/tape.bm $(BUILD)/tape-t3/tape.bm $(BUILD)/tape-t8/tape.bm tests/tape/engine_test.py tests/tape/render_test.py tests/tape/timing_test.py tests/tape/sound_test.py tests/tape/song_test.py tests/tape/felucca_test.py tests/tape/felucca_golden.txt core/tools/bml/bml.py
	$(PYTHON) -I tests/tape/engine_test.py $(BUILD)/host/luahost $(BUILD)/tape-test
	$(PYTHON) -I tests/tape/felucca_test.py $(BUILD)/tape $(BUILD)/bm/tape.bm
	$(PYTHON) -I tests/tape/render_test.py $(BUILD)/host/bmhost-bin $(BUILD)/bm/tape.bm
	$(PYTHON) -I tests/tape/timing_test.py $(BUILD)/host/bmhost-bin $(BUILD)/tape-t1/tape.bm $(BUILD)/tape-t3/tape.bm $(BUILD)/tape-t8/tape.bm
	$(PYTHON) -I tests/tape/song_test.py $(BUILD)/host/bmhost-bin $(BUILD)/tape-t1/tape.bm
	$(PYTHON) -I tests/tape/sound_test.py $(BUILD)/host/bmhost-bin $(BUILD)/bm/tape.bm $(BUILD)/tape-test/demo.wav $(BUILD)/tape-t1/tape.bm

# The fast PPU against the reference copy (tests/snes/ppu_ref.c): random states, every line equal bit for bit. `make bench-snes-ppu` times both.
SNES_PPU_SRCS := core/src/bm/snes_bus.c core/src/bm/snes_sa1.c core/src/bm/snes_sa1_cpu.c core/src/bm/snes_gsu.c core/src/bm/snes_dsp77.c core/src/bm/snes_cpu.c core/src/bm/snes_spc.c core/src/bm/snes_dsp.c
SNES_REF_DEFS := -Dppu_line=ppu_line_ref -Dppu_reset=ppu_reset_ref -Dppu_cgram_write=ppu_cgram_write_ref -Dppu_us=ppu_us_ref
$(BUILD)/host/snes_ppu_equiv: tests/snes/ppu_equiv.c tests/snes/ppu_ref.c core/src/bm/snes_ppu.c $(SNES_PPU_SRCS) core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O2 -g -w $(SNES_REF_DEFS) -Icore/src/bm -c -o $(BUILD)/host/snes_ppu_ref.o tests/snes/ppu_ref.c
	$(HOSTCC) -O2 -g -Wall -Wextra -Icore/src/bm -o $@ tests/snes/ppu_equiv.c $(BUILD)/host/snes_ppu_ref.o core/src/bm/snes_ppu.c $(SNES_PPU_SRCS) -lm
test-snes-ppu: $(BUILD)/host/snes_ppu_equiv
	$< equiv 400
# The same equivalence on an ARM1176 in QEMU, with the assembly mix loop (snes_ppu_arm.S): the PC build above runs the C loop only.
test-snes-ppu-arm: tests/snes/ppu_equiv_arm.c tests/snes/ppu_equiv.c tests/snes/ppu_ref.c core/src/bm/snes_ppu.c core/src/bm/snes_ppu_arm.S core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/arm
	$(SNES_ARM_CC) $(SNES_REF_DEFS) -c tests/snes/ppu_ref.c -o $(BUILD)/arm/ppuref.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -c core/src/bm/snes_ppu_arm.S -o $(BUILD)/arm/ppuarm.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -c core/src/bm/snes_cpu_arm.S -o $(BUILD)/arm/cpuarm.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -Icore/src/bm -c core/src/bm/snes_gsu_arm.S -o $(BUILD)/arm/gsuarm.o
	$(SNES_ARM_CC) -DCASES_ARM=$(or $(PPU_ARM_CASES),30) -specs=rdimon.specs -Wl,-Ttext=0x10000 -o $(BUILD)/arm/ppuequiv.elf tests/snes/ppu_equiv_arm.c $(BUILD)/arm/ppuref.o core/src/bm/snes_cpu.c $(SNES_ARM_SRCS) $(BUILD)/arm/ppuarm.o $(BUILD)/arm/cpuarm.o $(BUILD)/arm/gsuarm.o -lm
	$(SNES_ARM_RUN) $(BUILD)/arm/ppuequiv.elf | tee $(BUILD)/arm/ppuequiv.out | tail -3; grep -q 'ppu equivalence: .* ok' $(BUILD)/arm/ppuequiv.out
bench-snes-ppu: $(BUILD)/host/snes_ppu_equiv
	$< bench 200
# The CPU and the memory map on random programs, against the hashes of the code before the speed work (tests/snes/cpu_golden.c).
$(BUILD)/host/snes_cpu_golden: tests/snes/cpu_golden.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O2 -g -Wall -Wextra -Icore/src/bm -o $@ tests/snes/cpu_golden.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c -lm
test-snes-cpu: $(BUILD)/host/snes_cpu_golden
	$<
# The time of the CPU interpreter per instruction on a game-like program (tests/snes/cpu_bench.c). The host is not the Pi: compare two runs.
$(BUILD)/host/snes_cpu_bench: tests/snes/cpu_bench.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O2 -g -Wall -Wextra -Icore/src/bm -o $@ tests/snes/cpu_bench.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c -lm
bench-snes-cpu: $(BUILD)/host/snes_cpu_bench
	$<
# The Super FX (docs/progress/snes-superfx.md): small original programs on a made-up ROM (tests/snes/gsu_test.c). `make bench-snes-gsu` times the interpreter.
$(BUILD)/host/snes_gsu_test: tests/snes/gsu_test.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c core/src/bm/snes_int.h core/src/bm/snes_gsu.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O2 -g -Wall -Wextra -Icore/src/bm -o $@ tests/snes/gsu_test.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c -lm
# The SA-1 (docs/progress/snes-sa1.md): small original programs on a made-up ROM (tests/snes/sa1_test.c).
$(BUILD)/host/snes_sa1_test: tests/snes/sa1_test.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c core/src/bm/snes_int.h core/src/bm/snes_sa1.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O2 -g -Wall -Wextra -Icore/src/bm -o $@ tests/snes/sa1_test.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c -lm
# The debug API (docs/api/snes.md): peek, poke, regs, step, breakpoints on a made-up ROM (tests/snes/debug_test.c).
$(BUILD)/host/snes_debug_test: tests/snes/debug_test.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c core/src/bm/snes_int.h core/src/bm/snes.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O2 -g -Wall -Wextra -Icore/src/bm -o $@ tests/snes/debug_test.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c -lm
test-snes-debug: $(BUILD)/host/snes_debug_test
	$<
test-snes-sa1: $(BUILD)/host/snes_sa1_test
	$<
# the Code tab of bm SNES SDK: colours, F1 texts, errors in plain words, snippets, text buffer
test-snes-code: $(BUILD)/host/luahost core/src/script/snes_asm.lua core/lua/snescode.lua core/lua/asmbuf.lua tests/snes/code_test.lua
	$(BUILD)/host/luahost tests/snes/code_test.lua .

# the Run tab of bm SNES SDK: flags, registers, hex view, breakpoints, lines and addresses (core/lua/snesrun.lua)
test-snes-run: $(BUILD)/host/luahost core/src/script/snes_asm.lua core/lua/snesrun.lua tests/snes/run_test.lua
	$(BUILD)/host/luahost tests/snes/run_test.lua .

# the SNES graphics converters (bm Pixel, bm SNES SDK) (core/lua/snesgfx.lua)
test-snes-gfx: $(BUILD)/host/luahost
	$(BUILD)/host/luahost tests/snes/snesgfx_test.lua .
test-snes-gsu: $(BUILD)/host/snes_gsu_test
	$<
bench-snes-gsu: $(BUILD)/host/snes_gsu_test
	$< bench
# The Super FX interpreter against the plain C reference (tests/snes/gsu_ref.c) on random programs, pieces of random length, byte for byte.
$(BUILD)/host/snes_gsu_diff: tests/snes/gsu_diff.c tests/snes/gsu_ref.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c core/src/bm/snes_int.h core/src/bm/snes_gsu.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O2 -g -Wall -Wextra -Icore/src/bm -Itests/snes -o $@ tests/snes/gsu_diff.c tests/snes/gsu_ref.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c -lm
test-snes-gsu-diff: $(BUILD)/host/snes_gsu_diff
	$<
# Yoshi's Island (Super FX GSU-2) frame hashes on the host and on an ARM1176 in QEMU (tests/snes/smw2_frames.py): needs the user's ROM, BM_SNES_ROMS=/folder
$(BUILD)/host/snes_game_run: tests/snes/game_run.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O1 -DSNES_TRACE -w -Icore/src/bm -Itests/snes -o $@ tests/snes/game_run.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c -lm
test-snes-smw2: $(BUILD)/host/snes_game_run
	$(PYTHON) tests/snes/smw2_frames.py $<
test-snes-smw2-arm: tests/snes/game_run_arm.c tests/snes/game_run.c core/src/bm/snes_ppu_arm.S core/src/bm/snes_cpu_arm.S core/src/bm/snes_gsu_arm.S core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/arm
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -c core/src/bm/snes_ppu_arm.S -o $(BUILD)/arm/ppuarm.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -c core/src/bm/snes_cpu_arm.S -o $(BUILD)/arm/cpuarm.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -Icore/src/bm -c core/src/bm/snes_gsu_arm.S -o $(BUILD)/arm/gsuarm.o
	$(SNES_ARM_CC) -DSNES_TRACE -DGR_ROM='"rom.bin"' -DGR_FW='"-"' -DGR_FRAMES='"4000"' -DGR_SCRIPT='"s.txt"' -DGR_OUT='"p"' -specs=rdimon.specs -Wl,-Ttext=0x10000 -o $(BUILD)/arm/smw2.elf tests/snes/game_run_arm.c core/src/bm/snes_cpu.c $(SNES_ARM_SRCS) $(BUILD)/arm/ppuarm.o $(BUILD)/arm/cpuarm.o $(BUILD)/arm/gsuarm.o -lm
	$(PYTHON) tests/snes/smw2_frames.py $(BUILD)/arm/smw2.elf --qemu
# The Super FX tests on an ARM1176 in QEMU with a count of the ARM instructions per GSU instruction (icount: one instruction is 1 ns, the board timer ticks each microsecond).
test-snes-gsu-arm: tests/snes/gsu_arm.c tests/snes/gsu_test.c tests/snes/gsu_diff.c tests/snes/gsu_ref.c core/src/bm/snes_gsu.c core/src/bm/snes_gsu_arm.S core/src/bm/snes_gsu_off.h core/src/bm/snes_cpu.c core/src/bm/snes_cpu_arm.S core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/arm
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -c core/src/bm/snes_ppu_arm.S -o $(BUILD)/arm/ppuarm.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -c core/src/bm/snes_cpu_arm.S -o $(BUILD)/arm/cpuarm.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -Icore/src/bm -c core/src/bm/snes_gsu_arm.S -o $(BUILD)/arm/gsuarm.o
	$(SNES_ARM_CC) -specs=rdimon.specs -Wl,-Ttext=0x10000 -o $(BUILD)/arm/gsu.elf tests/snes/gsu_arm.c tests/snes/gsu_ref.c core/src/bm/snes_cpu.c $(SNES_ARM_SRCS) $(BUILD)/arm/ppuarm.o $(BUILD)/arm/cpuarm.o $(BUILD)/arm/gsuarm.o -lm
	qemu-system-arm -M versatilepb -cpu arm1176 -m 128 -icount shift=0 -semihosting -nographic -audio none -kernel $(BUILD)/arm/gsu.elf | tee $(BUILD)/arm/gsu.out | tail -16; grep -q 'gsu: .* 0 failed' $(BUILD)/arm/gsu.out; grep -q 'gsu diff: .* ok' $(BUILD)/arm/gsu.out
# The DSP-1/2/3/4 core (docs/progress/snes-dsp.md): the uPD77C25 on small original firmware written in the test, and the cartridge ports in the map.
$(BUILD)/host/snes_dsp77_test: tests/snes/dsp77_test.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c core/src/bm/snes_int.h core/src/bm/snes_dsp77.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O2 -g -Wall -Wextra -Icore/src/bm -o $@ tests/snes/dsp77_test.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c -lm
test-snes-dsp77: $(BUILD)/host/snes_dsp77_test
	$<
# The same test on an ARM1176 in QEMU, with the assembly CPU loop linked (the map and the chip on the real instruction set): needs qemu-system-arm and arm-none-eabi-gcc.
test-snes-dsp77-arm: tests/snes/dsp77_test.c core/src/bm/snes_dsp77.c core/src/bm/snes_dsp77.h core/src/bm/snes_cpu.c core/src/bm/snes_cpu_arm.S core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/arm
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -c core/src/bm/snes_ppu_arm.S -o $(BUILD)/arm/ppuarm.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -c core/src/bm/snes_cpu_arm.S -o $(BUILD)/arm/cpuarm.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -Icore/src/bm -c core/src/bm/snes_gsu_arm.S -o $(BUILD)/arm/gsuarm.o
	$(SNES_ARM_CC) -specs=rdimon.specs -Wl,-Ttext=0x10000 -o $(BUILD)/arm/dsp77.elf tests/snes/dsp77_test.c core/src/bm/snes_cpu.c $(SNES_ARM_SRCS) $(BUILD)/arm/ppuarm.o $(BUILD)/arm/cpuarm.o $(BUILD)/arm/gsuarm.o -lm
	$(SNES_ARM_RUN) $(BUILD)/arm/dsp77.elf | tee $(BUILD)/arm/dsp77.out | tail -3; grep -q 'snes dsp77: .* 0 failed' $(BUILD)/arm/dsp77.out
# The assembly loop of the CPU (snes_cpu_arm.S) on an ARM1176 in QEMU (needs qemu-system-arm and arm-none-eabi-gcc): the golden hashes, and one random instruction
# at a time against the C reference (tests/snes/cpu_diff.c). The PC build does not use the assembly.
SNES_ARM_CC := arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfloat-abi=soft -O2 -std=gnu11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Icore/src/bm -Itests/snes -w
SNES_ARM_SRCS := core/src/bm/snes_bus.c core/src/bm/snes_sa1.c core/src/bm/snes_sa1_cpu.c core/src/bm/snes_ppu.c core/src/bm/snes_spc.c core/src/bm/snes_dsp.c core/src/bm/snes_gsu.c core/src/bm/snes_dsp77.c tests/snes/arm_mmu.c
SNES_ARM_REF := -DSNES_CPU_C -DSNES_NO_DBG -Dcpu_run=ref_run -Dcpu_step=ref_step -Dcpu_reset=ref_reset -Dcpu_get=ref_get -Dcpu_set=ref_set -Dcpu_info=ref_info -Dcpu_waiting=ref_waiting -Dcpu_stopped=ref_stopped
SNES_ARM_RUN := qemu-system-arm -M versatilepb -cpu arm1176 -m 128 -semihosting -nographic -audio none -kernel
test-snes-cpu-arm: tests/snes/cpu_golden_arm.c tests/snes/cpu_diff.c core/src/bm/snes_cpu.c core/src/bm/snes_cpu_arm.S core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/arm
	$(SNES_ARM_CC) $(SNES_ARM_REF) -c core/src/bm/snes_cpu.c -o $(BUILD)/arm/ref.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -c core/src/bm/snes_ppu_arm.S -o $(BUILD)/arm/ppuarm.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -c core/src/bm/snes_cpu_arm.S -o $(BUILD)/arm/cpuarm.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -Icore/src/bm -c core/src/bm/snes_gsu_arm.S -o $(BUILD)/arm/gsuarm.o
	$(SNES_ARM_CC) -specs=rdimon.specs -Wl,-Ttext=0x10000 -o $(BUILD)/arm/golden.elf tests/snes/cpu_golden_arm.c core/src/bm/snes_cpu.c $(SNES_ARM_SRCS) $(BUILD)/arm/ppuarm.o $(BUILD)/arm/cpuarm.o $(BUILD)/arm/gsuarm.o -lm
	$(SNES_ARM_CC) -specs=rdimon.specs -Wl,-Ttext=0x10000 -o $(BUILD)/arm/diff.elf tests/snes/cpu_diff.c core/src/bm/snes_cpu.c $(SNES_ARM_SRCS) $(BUILD)/arm/ref.o $(BUILD)/arm/ppuarm.o $(BUILD)/arm/cpuarm.o $(BUILD)/arm/gsuarm.o -lm
	$(SNES_ARM_RUN) $(BUILD)/arm/golden.elf | tee $(BUILD)/arm/golden.out | tail -3; grep -q 'cpu golden arm: ok' $(BUILD)/arm/golden.out
	$(SNES_ARM_RUN) $(BUILD)/arm/diff.elf | tee $(BUILD)/arm/diff.out | tail -3; grep -q ': ok' $(BUILD)/arm/diff.out
# The S-DSP on random voices, against the hashes of the code before the speed work (tests/snes/dsp_golden.c).
$(BUILD)/host/snes_dsp_golden: tests/snes/dsp_golden.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O2 -g -Wall -Wextra -Icore/src/bm -o $@ tests/snes/dsp_golden.c $(SNES_PPU_SRCS) core/src/bm/snes_ppu.c -lm
test-snes-dsp: $(BUILD)/host/snes_dsp_golden
	$<
bench-snes-dsp: $(BUILD)/host/snes_dsp_golden
	$< bench
# The S-DSP against ares, sample by sample (tests/snes/dsp_ref.cpp). ares (ISC) is not in the repository: make test-snes-audio-ref ARES=/path/to/ares
test-snes-audio-ref:
ifeq ($(ARES),)
	@echo "test-snes-audio-ref: skipped (set ARES=/path/to/ares)"
else
	@mkdir -p $(BUILD)/host
	sed -n '/^auto DSP::main/,/^}/p' $(ARES)/ares/sfc/dsp/dsp.cpp > $(BUILD)/host/dsp_ref_main.inc
	$(HOSTCC) -O2 -c -Icore/src/bm -o $(BUILD)/host/dsp_ref_c0.o core/src/bm/snes_dsp.c
	$(HOSTCC) -O2 -c -Icore/src/bm -o $(BUILD)/host/dsp_ref_c1.o core/src/bm/snes_spc.c
	$(HOSTCC) -O2 -c -Icore/src/bm -o $(BUILD)/host/dsp_ref_c2.o core/src/bm/snes_cpu.c
	$(HOSTCC) -O2 -c -Icore/src/bm -o $(BUILD)/host/dsp_ref_c3.o core/src/bm/snes_bus.c
	$(HOSTCC) -O2 -c -Icore/src/bm -o $(BUILD)/host/dsp_ref_c4.o core/src/bm/snes_gsu.c
	$(HOSTCC) -O2 -c -Icore/src/bm -o $(BUILD)/host/dsp_ref_c5.o core/src/bm/snes_dsp77.c
	$(HOSTCC) -O2 -c -Icore/src/bm -o $(BUILD)/host/dsp_ref_c6.o core/src/bm/snes_ppu.c
	g++ -std=c++20 -O2 -w -I$(BUILD)/host -Icore/src/bm -I$(ARES)/nall -I$(ARES)/ares -o $(BUILD)/host/snes_dsp_ref tests/snes/dsp_ref.cpp $(BUILD)/host/dsp_ref_c?.o -lm
	$(BUILD)/host/snes_dsp_ref 40 40000
	g++ -std=c++20 -O2 -w -Icore/src/bm -I$(ARES)/nall -I$(ARES)/ares -I$(ARES) -I$(ARES)/thirdparty -o $(BUILD)/host/snes_spc_ref tests/snes/spc_ref.cpp $(BUILD)/host/dsp_ref_c?.o -lm
	$(BUILD)/host/snes_spc_ref
endif

# Island Bench (bm/islandbench): the island of the map kit assets/island.bmk (map, models, sprites) with the
# benchmark code in main.lua. The .bme is the source, the .bm the cartridge; the kit is merged in by bmres.py.
$(BUILD)/bm/islandbench.bm $(BUILD)/bm/islandbench.bme: $(BUILD)/bm/islandbench.%: bm/islandbench/main.lua core/scripts/mkbm.py core/scripts/bmres.py bm/islandbench/bm.info bm/islandbench/assets/island.bmk
	@mkdir -p $(dir $@)
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --info bm/islandbench/bm.info
	$(PYTHON) core/scripts/bmres.py add $@ bm/islandbench/assets/island.bmk -o $@

# Dark Night (bm/darknight): the Hunter in the Clinic. main.lua is the code; the map kit art/maps/clinic.bmk and the
# models art/models/dnhunter.bmm (made by art/models/hunter/armed.py --game) and art/models/dnenemy.bmm (art/models/enemies/build.py) are merged in by bmres.py. The .bme is the
# source, the .bm the cartridge.
DARKNIGHT_RES := art/maps/clinic.bmk art/models/dnhunter.bmm art/models/dnenemy.bmm
# the sound bank: samples made by numpy and two songs (bm/darknight/art/sounds.py)
$(BUILD)/darknight/sounds.json: bm/darknight/art/sounds.py
	@mkdir -p $(dir $@)
	$(PYTHON) bm/darknight/art/sounds.py $@
$(BUILD)/bm/darknight.bm $(BUILD)/bm/darknight.bme: $(BUILD)/bm/darknight.%: bm/darknight/main.lua core/scripts/mkbm.py core/scripts/bmres.py bm/darknight/bm.info bm/darknight/cover.png $(DARKNIGHT_RES) $(BUILD)/darknight/sounds.json
	@mkdir -p $(dir $@)
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --info bm/darknight/bm.info --cover bm/darknight/cover.png --audio $(BUILD)/darknight/sounds.json
	$(PYTHON) core/scripts/bmres.py add $@ $(DARKNIGHT_RES) -o $@

# Titan Clash (M20): several Lua files too; its sheet has more colours than
# fit in RGBA sections, so it goes as SHEET8 (a palette and RLE)
TITAN_SRC := $(sort $(wildcard bm/titan/src/*.lua))
$(BUILD)/titan/main.lua: $(TITAN_SRC) bm/titan/build/build.py
	$(PYTHON) bm/titan/build/build.py $@ --map $(BUILD)/titan/main.map

lua_titan := $(BUILD)/titan/main.lua
sheet8_titan := 1

# bm Pixel: main.lua, then the SNES converter module (core/lua/snesgfx.lua) as the global SNESGFX
# (a cart has no other Lua file; the module goes after main.lua, so its line numbers stay)
$(BUILD)/pixel/main.lua: bm/pixel/main.lua core/lua/snesgfx.lua
	@mkdir -p $(dir $@)
	{ cat bm/pixel/main.lua; printf '\nSNESGFX = (function()\n'; cat core/lua/snesgfx.lua; printf '\nend)()\n'; } > $@
lua_pixel := $(BUILD)/pixel/main.lua

# the Learn tab of bm SNES SDK: the lessons (bm/snes_sdk/lessons), their helpers (core/lua/sneslearn.lua); every example assembles and runs in the core
test-snes-learn: $(BUILD)/host/luahost core/src/script/snes_asm.lua core/lua/sneslearn.lua core/lua/snescode.lua core/lua/asmbuf.lua tests/snes/learn_test.lua tests/snes/learn_run.c tests/snes/learn_run.py bm/snes_sdk/lessons/LIST $(wildcard bm/snes_sdk/lessons/*.txt) $(wildcard core/src/bm/snes*.h)
	$(HOSTCC) -O1 -g -Wall -Wextra -Icore/src/bm -o $(BUILD)/host/snes_learn_run tests/snes/learn_run.c $(SNES_CORE_SRCS)
	$(PYTHON) tests/snes/learn_run.py $(BUILD)/host/luahost $(BUILD)/host/snes_learn_run

# bm SNES SDK: main.lua, then the converter module as the global SNESGFX, the Code tab modules as SNESCODE and ASMBUF, the Run tab module as SNESRUN, the Learn tab module as SNESLEARN and the lessons as the table SNESLESSONS
$(BUILD)/snes_sdk/main.lua: bm/snes_sdk/main.lua core/lua/snesgfx.lua core/lua/snescode.lua core/lua/asmbuf.lua core/lua/snesrun.lua core/lua/sneslearn.lua bm/snes_sdk/lessons/LIST $(wildcard bm/snes_sdk/lessons/*.txt)
	@mkdir -p $(dir $@)
	{ cat bm/snes_sdk/main.lua; printf '\nSNESGFX = (function()\n'; cat core/lua/snesgfx.lua; printf '\nend)()\n'; printf '\nSNESCODE = (function()\n'; cat core/lua/snescode.lua; printf '\nend)()\n'; printf '\nASMBUF = (function()\n'; cat core/lua/asmbuf.lua; printf '\nend)()\n'; printf '\nSNESRUN = (function()\n'; cat core/lua/snesrun.lua; printf '\nend)()\n'; printf '\nSNESLEARN = (function()\n'; cat core/lua/sneslearn.lua; printf '\nend)()\n'; printf '\nSNESLESSONS = {\n'; for n in $$(cat bm/snes_sdk/lessons/LIST); do printf '{ name = "%s", text = [==[\n' $$n; cat bm/snes_sdk/lessons/$$n.txt; printf ']==] },\n'; done; printf '}\n'; } > $@
lua_snes_sdk := $(BUILD)/snes_sdk/main.lua

# nano8: plays .p8 / .p8.png carts (machine in core/src/bm/n8*.c); several Lua
# files too. The carts it ships with (bm/nano8/roms, licenses in
# CREDITS.md) go to bm/nano8/ on the SD card.
NANO8_SRC := $(sort $(wildcard bm/nano8/src/*.lua))
NANO8_ROMS := $(sort $(wildcard bm/nano8/roms/*.p8 bm/nano8/roms/*.p8.png))
$(BUILD)/nano8/main.lua: $(NANO8_SRC) bm/nano8/build/build.py
	$(PYTHON) bm/nano8/build/build.py $@ --map $(BUILD)/nano8/main.map

lua_nano8 := $(BUILD)/nano8/main.lua

# Hero Stress (docs/design/hero-stress-cart-and-cel-shader.md, stage 1): a plaza with walking heroes, to
# compare the 3D options on the Pi. Not in GAMES: it does not go in dist/bm or the Market.
# The models and the plaza are committed (bm/hero/assets/, made by build/mkheroes.py and
# build/mkplaza.py); the sources are bm/hero/src/*.lua joined by build/build.py.
HERO_SRC := $(sort $(wildcard bm/hero/src/*.lua))
$(BUILD)/hero/main.lua: $(HERO_SRC) bm/hero/build/build.py
	$(PYTHON) bm/hero/build/build.py $@ --map $(BUILD)/hero/main.map

lua_hero := $(BUILD)/hero/main.lua
layers_hero := floor height walls

# Overbit (M38): a hero shooter in 3D. Several Lua files joined by its
# build.py; the heroes' models and animations (MESH, ANIM) made by
# bm/overbit/art/models.py, the sounds by art/sounds.py.
OVERBIT_SRC := $(sort $(wildcard bm/overbit/src/*.lua))
OVERBIT_ART := $(wildcard bm/overbit/art/*.py bm/overbit/art/heroes/*.py bm/overbit/art/meshy/*.mesh \
                          bm/overbit/art/meshy/*.png bm/overbit/art/meshy/*.rig)
# The heroes' bodies are made of primitives (art/heroes/*.py). OVERBIT_MESHY=1: the Meshy
# figures of art/meshy instead (set aside: their arms and guns fused in the poses)
OVERBIT_MODELS_FLAGS := $(if $(OVERBIT_MESHY),--meshy)
title_overbit := Overbit
# its screen: 480x270, each pixel 4x4 on a 1080p TV (it was 320x180)
OVERBIT_RES := 480x270
# the .bm (for the Pi and the RGB30): three screens on a TV, 640x360 on the ARM, 640x360,
# 1280x720 and 1920x1080 on the GPU (85_quality); the RGB30 offers its square modes
$(BUILD)/overbit/main.lua: $(OVERBIT_SRC) bm/overbit/build/build.py $(BUILD)/overbit/models.bm
	$(PYTHON) bm/overbit/build/build.py $@ --map $(BUILD)/overbit/main.map --extra $(BUILD)/overbit/21_map.lua

$(BUILD)/overbit/models.bm: $(OVERBIT_ART) core/scripts/bmmesh.py core/scripts/mkbm.py $(BUILD)/host/mappvs
	@mkdir -p $(dir $@)
	$(PYTHON) bm/overbit/art/models.py $@ --map $(BUILD)/overbit/21_map.lua --pvs $(BUILD)/host/mappvs \
	    $(OVERBIT_MODELS_FLAGS)

# every hero as an editable .bmm (bm Mesh, bm Animator, bm Studio open it: mesh, skeleton,
# clips and the cells of the sheet it uses): build/overbit/heroes/
OVERBIT_HEROES := rally:rally_mech,rally_pilot,rally_fp,rally_pfp \
  kaiju:kaiju_mech,kaiju_pilot,kaiju_fp,kaiju_pfp \
  sarge:sarge,sarge_fp frost:frost,frost_fp \
  fuse:fuse,fuse_fp,fuse_wheel,fuse_mine,fuse_trap rail:rail,rail_fp \
  orbit:orbit,orbit_fp akari:akari,akari_fp,akari_fox
.PHONY: overbit-heroes
overbit-heroes: $(BUILD)/overbit/models.bm
	@mkdir -p $(BUILD)/overbit/heroes
	@for h in $(OVERBIT_HEROES); do \
	  $(PYTHON) core/scripts/bmres.py extract $< -o $(BUILD)/overbit/heroes/$${h%%:*}.bmm \
	    $$(for m in $$(echo $${h#*:} | tr , ' '); do printf -- '--model %s ' $$m; done) || exit 1; \
	done

# what can be seen from where on the maps (bm/overbit/art/mapbake.py)
$(BUILD)/host/mappvs: core/tools/mappvs.c core/src/bm/r3d.c core/src/bm/gfx16.c core/src/bm/format.c core/src/lib/crc32.c core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -std=c11 -Wall -Icore/src -o $@ core/tools/mappvs.c core/src/bm/r3d.c core/src/bm/gfx16.c core/src/bm/format.c \
	    core/src/lib/crc32.c -lm

$(BUILD)/overbit/sounds.json: bm/overbit/art/sounds.py
	@mkdir -p $(dir $@)
	$(PYTHON) bm/overbit/art/sounds.py $@

lua_overbit := $(BUILD)/overbit/main.lua
models_overbit := $(BUILD)/overbit/models.bm
audio_overbit := $(BUILD)/overbit/sounds.json

# variants that start in a mode: the reel (for docs/img) and the benchmark
$(BUILD)/overbit/%.bm: $(OVERBIT_SRC) bm/overbit/build/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) bm/overbit/build/build.py $(BUILD)/overbit/$*.lua --start $* --extra $(BUILD)/overbit/21_map.lua
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/$*.lua --title "Overbit $*" --author bm --res $(OVERBIT_RES) \
	    --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json

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
$(BUILD)/overbit/reel-heroes.bm: $(OVERBIT_SRC) bm/overbit/build/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) bm/overbit/build/build.py $(BUILD)/overbit/reel-heroes.lua --start reel --hero $(OVERBIT_REEL_HEROES) \
	    --extra $(BUILD)/overbit/21_map.lua
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/reel-heroes.lua --title "Overbit reel" --author bm --res $(OVERBIT_RES) \
	    --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json
overbit-reel-heroes: $(BUILD)/host/bmhost-bin $(BUILD)/overbit/reel-heroes.bm
	$(PYTHON) bm/overbit/build/reel.py $(BUILD) $(BUILD)/overbit/reel-heroes.bm 232 docs/img/overbit-reel-heroes.mp4 \
	    --size 960x540 --res $(OVERBIT_RES) --crf 30 --gif docs/img/overbit-reel-heroes.gif --gif-shots ULTIMATE

# a match of ten bots on Partenope, filmed round the point, behind and in the
# eyes of the heroes (docs/img/overbit-match.mp4 and a GIF of 20 s)
$(BUILD)/overbit/match-film.bm: $(OVERBIT_SRC) bm/overbit/build/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) bm/overbit/build/build.py $(BUILD)/overbit/match-film.lua --start match --extra $(BUILD)/overbit/21_map.lua \
	    --define OVERBIT_SPECTATE=true --define 'OVERBIT_RULES={unlock=6,pct=0.3,round_end=4,match_end=5}'
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/match-film.lua --title "Overbit match" --author bm \
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
	$(HOSTCC) -O2 -w -Icore/third_party/lua -o $@ tests/kitchen/luahost.c $(LUA_SRCS) -lm

# bmplay: a Lua cartridge played on the PC with the console's drawing (lights
# by levels too) and sound, its buttons pressed by a bot in Lua;
# core/tools/bmplay/video.sh BMPLAY CART.bm BOT.lua OUT.mp4 makes a video of it.
# make yharnam-video: a hunt played by core/tools/bmplay/yharnam_bot.lua, from the
# title to the Butcher slain (build/yharnam-run.mp4; needs ffmpeg)
BMPLAY_SRCS := core/tools/bmplay/bmplay.c core/src/bm/gfx16.c core/src/bm/format.c core/src/lib/crc32.c core/src/gfx/font8x16.c \
               core/src/gfx/font8x14.c core/src/gfx/font6x12.c core/src/audio/synth.c core/src/audio/player.c \
               core/src/audio/presets.c core/src/audio/lua_tone.c
$(BUILD)/host/bmplay: $(BMPLAY_SRCS) core/src/bm/*.h core/src/audio/*.h $(LUA_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -Icore/third_party/lua -o $@ $(BMPLAY_SRCS) $(LUA_SRCS) -lm

yharnam-video: $(BUILD)/host/bmplay $(BUILD)/bm/yharnam.bm core/tools/bmplay/yharnam_bot.lua
	core/tools/bmplay/video.sh $(BUILD)/host/bmplay $(BUILD)/bm/yharnam.bm core/tools/bmplay/yharnam_bot.lua \
	    $(BUILD)/yharnam-run.mp4

# Yharnam's first drawn map, from its street plan (bm/yharnam/build/mkmap.py: it
# overwrites map_ground.csv, map_overlay.csv and map_objects.csv, the map
# drawn by hand)
yharnam-map: $(BUILD)/host/luahost
	$(PYTHON) bm/yharnam/build/mkmap.py --luahost $<

# Yharnam's map checks: the street plan as a drawn map, then the borders (walls, gates, lamps, bosses)
test-yharnam: $(BUILD)/host/luahost
	@mkdir -p $(BUILD)/yharnam-map
	$< tests/yharnam/mapgen.lua bm/yharnam/main.lua $(BUILD)/yharnam-map
	$< tests/yharnam/borders.lua bm/yharnam/main.lua
	$< tests/yharnam/borders.lua bm/yharnam/main.lua --drawn
	$< tests/yharnam/passages.lua bm/yharnam/main.lua --drawn

# Yharnam's autoplay on the host: 7200 frames with no key, then quit(); the run must end by itself
test-yharnam-autoplay: $(BUILD)/host/bmplay $(BUILD)/bm/yharnam.bm
	YHARNAM_AUTOPLAY=1 $< $(BUILD)/bm/yharnam.bm --frames 20000 2>&1 | tee $(BUILD)/yharnam-autoplay.log | grep '^autoplay: 7200 frames, 0 deaths'

# The Sound editor in a fake bm: its banks are the console's format, byte for byte
$(BUILD)/demo.bmau: bm/sound/demo.json core/scripts/bmaudio.py
	@mkdir -p $(dir $@)
	$(PYTHON) core/scripts/bmaudio.py pack $< -o $@

# The games' buttons by name (2026-10-04): keymap(), btn("ok"), controller(),
# prompt() for a player, in bmhost with an input script (players on a
# keyboard, a DS4, an Xbox pad)
$(BUILD)/keymap-test.bm: tests/keymap/cart.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "keymap test"

$(BUILD)/controls-test.bm: tests/controls/cart.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "controls test"

# The gameplay kit (lib.world and the states, input, tween, record of bmlib) used by one small game
$(BUILD)/gamekit-test.bm: tests/gamekit/cart.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "gate kit test" --author tests

# The games' loading screen (core/src/bm/loading.c, 2026-10-04): the retro intro,
# its jingle, the console's page then the game's; the frames in build/loading/
$(BUILD)/host/test_loading: tests/bm/test_loading.c core/src/bm/loading.c core/src/bm/loading_logo.c core/src/bm/loading.h \
                            core/src/bm/gfx16.c core/src/gfx/font6x12.c core/src/lib/printf.c core/src/audio/presets.c core/src/audio/synth.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -Icore/src/bm -Icore/third_party/lua -o $@ tests/bm/test_loading.c core/src/bm/loading.c \
	    core/src/bm/loading_logo.c core/src/bm/gfx16.c core/src/gfx/font6x12.c core/src/lib/printf.c core/src/audio/presets.c core/src/audio/synth.c -lm

test-loading: $(BUILD)/host/test_loading
	@mkdir -p $(BUILD)/loading
	$< $(BUILD)/loading

# riff (require "riff", the language of patterns for music, M46): the
# mini-notation, the functions, the scheduler on the sound's clock, the
# live code and the bake, in bmhost; the sound in build/riff/riff.wav
$(BUILD)/riff-test.bm: tests/riff/cart.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "riff test"

test-riff: $(BUILD)/host/bmhost-bin $(BUILD)/riff-test.bm
	@mkdir -p $(BUILD)/riff
	$< $(BUILD)/riff-test.bm --seconds 7 --wav $(BUILD)/riff/riff.wav 2>&1 | tee $(BUILD)/riff-test.log | grep "^riff"
	grep -q "^riff: \([0-9]*\)/\1 checks passed" $(BUILD)/riff-test.log

# Hero Stress (stage 1): the models' counts, the printed line, the keys and the sweep, in bmhost
test-islandbench: $(BUILD)/host/bmhost-bin $(BUILD)/bm/islandbench.bm tests/islandbench/check.py
	$(PYTHON) tests/islandbench/check.py $< $(BUILD)/bm/islandbench.bm $(BUILD)/islandbench/test

# Dark Night (bm/darknight): a script of keys plays the game in bmhost (dodges, attacks, the saw, the charge, the
# pistol, the lock, the hall door, death and a new start) and the test reads the event lines of the game
test-darknight: $(BUILD)/host/bmhost-bin $(BUILD)/bm/darknight.bm tests/darknight/check.py $(BUILD)/host/test_hero_mesh
	$(PYTHON) tests/darknight/check.py $< $(BUILD)/bm/darknight.bm $(BUILD)/darknight/test
	$(BUILD)/host/test_hero_mesh $(BUILD)/bm/darknight.bm hunter_c hunter_o villager huntsman dog    # every character on the GPU (vs_lit2)

# The typed subset of Lua (core/scripts/bmt.py): the compiler refuses type errors, and the C and the Lua of tests/typed/hot.bmt give
# the same results on random cases (progress/typed.md). On demand: it is not in HOST_TESTS.
test-typed: $(BUILD)/host/luahost core/scripts/bmt.py tests/typed/run.py tests/typed/hot.bmt tests/typed/bindhost.c
	$(PYTHON) -I tests/typed/run.py $< $(BUILD)/typed

# The language of bm (core/tools/bml/bml.py and core/src/bm/bmlc.c): stages 1-9 (front end, stack VM, bmvm, ARMv6 A32,
# native loader + cache sync, i32 wrap/arrays/structs/f32 VFPv2, kernel functions, Dark Night enemies, and on-console C compiler)
# compared against Lua and bare-metal ARM1176 VFPv2 QEMU (docs/design/bml-manual.md).
test-bml: $(BUILD)/host/luahost core/tools/bml/bml.py tests/bml/run.py tests/bml/sum.b tests/bml/features.b \
          tests/bml/types_mem.b tests/bml/kernel_fn.b tests/bml/darknight_enemy.b \
          core/src/bm/bmlc.c core/src/bm/bmlc.h core/src/bm/bmnative.c core/src/bm/bmnative.h core/src/bm/api_vm.c core/src/bm/bmvm.c
	$(PYTHON) -I tests/bml/run.py $< $(BUILD)/bml

bench-bml: tests/bml/bench.py tests/bml/darknight_enemy.b core/src/bm/bmlc.c core/src/bm/bmlc.h core/src/bm/bmnative.c core/src/bm/bmnative.h
	$(PYTHON) -I tests/bml/bench.py $(BUILD)/bml-bench

# the editors read the model files: the import code of bm Mesh and bm Animator and the kernel's own checks, in bmhost
test-bmm: $(BUILD)/host/bmhost-bin core/scripts/check_bmm.py
	$(PYTHON) core/scripts/check_bmm.py art/models/hunter.bmm art/models/hunterlo.bmm art/models/dnhunter.bmm art/models/enemies.bmm art/models/dnenemy.bmm

test-hero: $(BUILD)/host/bmhost-bin $(BUILD)/bm/hero.bm tests/hero/check.py bm/hero/build/mkheroes.py
	$(PYTHON) tests/hero/check.py $< $(BUILD)/bm/hero.bm $(BUILD)/hero/test
	$(MAKE) $(BUILD)/host/test_hero_mesh
	$(BUILD)/host/test_hero_mesh bm/hero/assets/models.bm

# The RGB30's charge from the voltage and the current (rows of the run-down of 2026-10-27)
test-battery-curve: tests/rgb30/test_battery_curve.c core/src/rgb30/battery_curve.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $(BUILD)/host/test_battery_curve tests/rgb30/test_battery_curve.c
	$(BUILD)/host/test_battery_curve | tee $(BUILD)/battery-curve-test.log
	grep -q "^battery_curve: \([0-9]*\)/\1 checks passed" $(BUILD)/battery-curve-test.log

# The player's remap of the game buttons (CONTROLS-1): controls= of the config
test-controls: $(BUILD)/host/bmhost-bin $(BUILD)/controls-test.bm tests/controls/input.txt
	BMHOST_CONFIG="controls=c1:x t2:l1 s1:y pa:se mc:x" $< $(BUILD)/controls-test.bm --input tests/controls/input.txt --seconds 4 2>&1 | tee $(BUILD)/controls-test.log | grep "^controls"
	grep -q "^controls: \([0-9]*\)/\1 checks passed" $(BUILD)/controls-test.log

test-gamekit: $(BUILD)/host/bmhost-bin $(BUILD)/gamekit-test.bm tests/gamekit/input.txt
	$< $(BUILD)/gamekit-test.bm --input tests/gamekit/input.txt --seconds 28 2>&1 | tee $(BUILD)/gamekit-test.log | grep "^gamekit"
	grep -q "^gamekit: \([0-9]*\)/\1 checks passed" $(BUILD)/gamekit-test.log

# A game moved onto the kit must play as before: tests/gamekit/trace_tail.lua is added to the end of
# its main.lua, and the digest of every frame (what it draws, plays and saves) is compared with the
# digest kept in tests/gamekit/<game>*.golden (made with the game before the move)
$(BUILD)/pong-trace.bm: bm/pong/main.lua tests/gamekit/trace_tail.lua core/scripts/mkbm.py
	cat bm/pong/main.lua tests/gamekit/trace_tail.lua > $(BUILD)/pong-trace.lua
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $(BUILD)/pong-trace.lua --title "pong trace" --author tests

test-game-trace: $(BUILD)/host/bmhost-bin $(BUILD)/pong-trace.bm
	$< $(BUILD)/pong-trace.bm --input tests/gamekit/pong_input.txt --seconds 60 2>&1 | grep "^trace" > $(BUILD)/pong1.trace
	$< $(BUILD)/pong-trace.bm --input tests/gamekit/pong2_input.txt --seconds 40 2>&1 | grep "^trace" > $(BUILD)/pong2.trace
	cmp $(BUILD)/pong1.trace tests/gamekit/pong1.golden
	cmp $(BUILD)/pong2.trace tests/gamekit/pong2.golden

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
$(BUILD)/gameapi-test.bm: $(GAMEAPI_SRC) $(BUILD)/gameapi/sheet.png core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua tests/gameapi/cart.lua --sheet $(BUILD)/gameapi/sheet.png \
	    --map tests/gameapi/map.csv --map front=tests/gameapi/map_front.csv --flags tests/gameapi/flags.csv \
	    --sprites tests/gameapi/sprites.txt --title "game api test" --author tests
test-gameapi: $(BUILD)/host/bmhost-bin $(BUILD)/gameapi-test.bm tests/gameapi/input.txt
	@mkdir -p $(BUILD)/gameapi
	$< $(BUILD)/gameapi-test.bm --input tests/gameapi/input.txt --seconds 8 --shots $(BUILD)/gameapi 2>&1 \
	    | tee $(BUILD)/gameapi-test.log | grep "^gameapi"
	grep -q "^gameapi: \([0-9]*\)/\1 checks passed" $(BUILD)/gameapi-test.log

# screen(): the modes of a square panel (bmhost --square: 360x360, 720x720, 720x405) and of a TV
# (bmhost), the size after a change, safe_area() and aspect()
$(BUILD)/screen-modes.bm: tests/host/screen_modes.lua core/scripts/mkbm.py
	@mkdir -p $(BUILD)
	@printf 'name: screen modes\nspec: 5\n' > $(BUILD)/screen-modes.info
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --res 360x360 --title "screen modes" --author tests --info $(BUILD)/screen-modes.info
test-screen-modes: $(BUILD)/host/bmhost-bin $(BUILD)/screen-modes.bm
	$< $(BUILD)/screen-modes.bm --square --seconds 4 2>&1 | tee $(BUILD)/screen-modes-square.log | grep "^screen_modes"
	grep -q "^screen_modes: \([0-9]*\)/\1 checks passed" $(BUILD)/screen-modes-square.log
	$< $(BUILD)/screen-modes.bm --seconds 4 2>&1 | tee $(BUILD)/screen-modes-tv.log | grep "^screen_modes"
	grep -q "^screen_modes: \([0-9]*\)/\1 checks passed" $(BUILD)/screen-modes-tv.log

# One game, many screens: screen_modes(), the _screen(w, h) hook, lib.view, lib.anchor, lib.safe_clip and
# lib.fit on every mode of a TV (bmhost) and of a square panel (bmhost --square), for a 640x360 and a 360x360 design
$(BUILD)/layout-640.bm: tests/host/layout.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --res 640x360 --title "layout 640" --author tests
$(BUILD)/layout-360.bm: tests/host/layout.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --res 360x360 --title "layout 360" --author tests
test-layout: $(BUILD)/host/bmhost-bin $(BUILD)/layout-640.bm $(BUILD)/layout-360.bm
	for d in 640 360; do \
	  for m in "" "--square"; do \
	    $< $(BUILD)/layout-$$d.bm $$m --seconds 6 2>&1 | tee $(BUILD)/layout-$$d$$m.log | grep "^layout"; \
	    grep -q "^layout: \([0-9]*\)/\1 checks passed" $(BUILD)/layout-$$d$$m.log || exit 1; \
	  done; \
	done

# Clip modes (APP-30): the kernel's pose_at through animate() and bone3d(), one clip a mode
$(BUILD)/anim-ease.bm: tests/anim_ease_cart.py tests/anim_ease.lua tests/studio/anim_figure.py core/scripts/mkbm.py
	$(PYTHON) tests/anim_ease_cart.py $@
test-anim-ease: $(BUILD)/host/bmhost-bin $(BUILD)/anim-ease.bm
	$< $(BUILD)/anim-ease.bm --seconds 2 2>&1 | tee $(BUILD)/anim-ease.log | grep "^anim-ease"
	grep -q "^anim-ease: \([0-9]*\)/\1 checks passed" $(BUILD)/anim-ease.log

# bm Write (bm/write, 2026-10-06): a document typed, styled, saved in
# /docs and exported in bmhost, then opened again (the session, the
# Markdown); tests/write/check.py reads the files (the PDF's xref too)
test-write: $(BUILD)/host/bmhost-bin $(BUILD)/bm/write.bm tests/write/input.txt tests/write/reopen.txt \
            tests/write/check.py
	rm -rf $(BUILD)/write-sd $(BUILD)/write && mkdir -p $(BUILD)/write-sd $(BUILD)/write
	$< $(BUILD)/bm/write.bm --sd $(BUILD)/write-sd --input tests/write/input.txt --seconds 14 \
	    --shots $(BUILD)/write > $(BUILD)/write/run1.log 2>&1
	$< $(BUILD)/bm/write.bm --sd $(BUILD)/write-sd --input tests/write/reopen.txt --seconds 2 \
	    --shots $(BUILD)/write > $(BUILD)/write/run2.log 2>&1
	$(PYTHON) tests/write/check.py $(BUILD)/write-sd/docs $(BUILD)/write
# The dev kit's profiler of functions (R14, core/src/bm/profile.c): profile()
# read by the game itself, on the PC's real time (--clock-scale 1)
$(BUILD)/profile-test.bm: tests/profile/cart.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "profile test" --author tests

test-profile: $(BUILD)/host/bmhost-bin $(BUILD)/profile-test.bm
	$< $(BUILD)/profile-test.bm --seconds 10 --clock-scale 1 2>&1 | tee $(BUILD)/profile-test.log | grep "^profile"
	grep -q "^profile: \([0-9]*\)/\1 checks passed" $(BUILD)/profile-test.log

# PS in a game played online (online(), 2026-10-04): the question to the
# player leaving over the game that goes on, back and Esc stay, PS again
# leaves through _leave(); the screen of the question in build/online/
$(BUILD)/online-test.bm: tests/online/cart.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "online test"

test-online: $(BUILD)/host/bmhost-bin $(BUILD)/online-test.bm tests/online/input.txt
	@mkdir -p $(BUILD)/online
	$< $(BUILD)/online-test.bm --input tests/online/input.txt --shots $(BUILD)/online \
	    --seconds 8 2>&1 | tee $(BUILD)/online-test.log | grep "^online"
	grep -q "^online: \([0-9]*\)/\1 checks passed" $(BUILD)/online-test.log
	grep -q "left the online game" $(BUILD)/online-test.log

# bmnet (require "bmnet", games over the network): two bmhost on this PC,
# the lobby, sure messages with a fifth of the packets lost, a lockstep
# match that must be the same on both, a player leaving; on the LAN and
# through the relay (core/tools/overbit_relay.py)
test-bmnet: $(BUILD)/host/bmhost-bin tests/bmnet/cart.lua tests/bmnet/run.py core/src/script/bmnet.lua
	$(PYTHON) tests/bmnet/run.py $(BUILD)

# bmweb (bm/bmweb/src): HTML, CSS, layout and URL in pure Lua. No build
# dir needed: run.sh uses build/host/luahost, a Lua in the PATH, or builds a
# private one in a temp folder.
test-web:
	sh tests/web/run.sh

# bm web player stage 1 (web/bmcart.js): the .bm reader in Node; skipped when Node is missing
test-webplayer:
	@if command -v node >/dev/null 2>&1; then node tests/webplayer/test_bmcart.js && node tests/webplayer/test_bmring.js; else echo "test-webplayer: skipped (no node)"; fi

test-sound: $(BUILD)/host/luahost $(BUILD)/demo.bmau bm/sound/main.lua
	$< tests/sound/sim.lua bm/sound/main.lua $(BUILD)/demo.bmau

# Resource files (docs/RISORSE.md, core/scripts/bmres.py): models, images, sounds,
# maps and palettes out of a .bm and back in, conversions, broken files
test-res: $(BUILD)/bm/testkit.bm $(BUILD)/testkit-before.bm $(BUILD)/bm/mapkit.bm $(BUILD)/sound.bm core/scripts/bmres.py $(BUILD)/host/test_res
	rm -rf $(BUILD)/res && $(PYTHON) tests/res/test_bmres.py $(BUILD)/bm/testkit.bm $(BUILD)/bm/mapkit.bm \
	    $(BUILD)/sound.bm $(BUILD)/res
	$(BUILD)/host/test_res $(BUILD)/res $(BUILD)/testkit-before.bm

# a game of before the INFO section: the test kit without INFO and cover (test_res)
$(BUILD)/testkit-before.bm: tests/fixtures/mktestkit.py core/scripts/mkbm.py core/scripts/bmmesh.py
	@mkdir -p $(dir $@)
	$(PYTHON) tests/fixtures/mktestkit.py $@ --plain

$(BUILD)/host/test_res: tests/bm/test_res.c core/src/bm/format.c core/src/lib/crc32.c core/src/bm/bm.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/bm/test_res.c core/src/bm/format.c core/src/lib/crc32.c

# The compatibility policy: the C table is made from docs/spec/compat-policy.toml (mkcompat --check); the C test
# checks every rule; the grid of facts must give the same verdicts as the toml (core/scripts/bm_weight.py classify)
test-compat: $(BUILD)/host/test_compat docs/spec/compat-policy.toml
	$(PYTHON) core/scripts/mkcompat.py --check
	$(BUILD)/host/test_compat
	$(PYTHON) tests/tools/test_compat_policy.py $(BUILD)/host/test_compat

$(BUILD)/host/test_compat: tests/bm/test_compat.c core/src/bm/format.c core/src/bm/compat_table.h core/src/lib/crc32.c core/src/bm/bm.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/bm/test_compat.c core/src/bm/format.c core/src/lib/crc32.c

# nano8 on the PC: the loader on every cart, the translator, the API test
# cart, then each shipped cart played for a while (tests/nano8/run.py)
N8_HOST_SRC := core/src/bm/n8.c core/src/bm/n8font.c core/src/bm/n8cart.c core/src/bm/png.c core/src/bm/n8lua.c core/src/audio/n8snd.c \
               core/src/bm/gfx16.c core/src/gfx/font8x16.c
$(BUILD)/host/n8host: tests/nano8/n8host.c $(N8_HOST_SRC) core/src/bm/n8*.h core/src/audio/n8snd.h $(LUA_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -w -Icore/src -Icore/third_party/lua -o $@ tests/nano8/n8host.c $(N8_HOST_SRC) \
	    $(filter-out core/third_party/lua/lua.c core/third_party/lua/luac.c,$(LUA_SRCS)) -lm

$(BUILD)/host/n8cartinfo: tests/nano8/cartinfo.c core/src/bm/n8.c core/src/bm/n8font.c core/src/bm/n8cart.c core/src/bm/png.c core/src/bm/n8*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/nano8/cartinfo.c core/src/bm/n8.c core/src/bm/n8font.c core/src/bm/n8cart.c core/src/bm/png.c -lm

test-nano8: $(BUILD)/host/n8host $(BUILD)/host/n8cartinfo $(BUILD)/host/luahost $(BUILD)/nano8/main.lua
	$(PYTHON) tests/nano8/run.py --build $(BUILD) $(NANO8_ROMS)

# bmhost: a .bm cartridge on the PC with the console's own runtime (Lua,
# gfx16, r3d, the sound player) and the kernel's services replaced
# (tests/host/stubs.c): frames to PNG or raw video, sound to WAV, input from
# a script. For the reels of the games and for tests with screenshots.
BMHOST_RT := core/src/bm/runtime.c core/src/bm/api_jobs.c core/src/kernel/jobs.c core/src/kernel/offload.c core/src/bm/api_world.c core/src/bm/api_anim.c core/src/bm/api_net.c core/src/bm/api_save.c core/src/bm/api_shot.c core/src/bm/api_docs.c core/src/bm/api_snes.c core/src/bm/api_nes.c core/src/bm/nes.c core/src/bm/api_psx.c core/src/bm/psx_cpu.c core/src/bm/psx_gte.c core/src/bm/psx_bus.c core/src/bm/psx_gpu.c core/src/bm/psx_cd.c core/src/bm/psx_spu.c core/src/bm/psx_jit.c core/src/bm/snes_bus.c core/src/bm/snes_sa1.c core/src/bm/snes_sa1_cpu.c core/src/bm/snes_gsu.c core/src/bm/snes_dsp77.c core/src/bm/snes_cpu.c core/src/bm/snes_ppu.c core/src/bm/snes_spc.c core/src/bm/snes_dsp.c core/src/bm/api_sound.c core/src/bm/api_light.c core/src/bm/api_map.c core/src/bm/api_vm.c core/src/bm/bmvm.c core/src/bm/bmlc.c core/src/bm/bmnative.c core/src/bm/api_collmap.c core/src/bm/api_prompts.c core/src/bm/api_keys.c core/src/bm/api_web.c core/src/bm/api_video.c core/src/bm/api_perm.c core/src/bm/api_editor.c core/src/bm/api_text.c core/src/bm/api_debug.c core/src/bm/api_math.c core/src/bm/phys2d.c core/src/bm/api_phys2d.c core/src/bm/tokens.c core/src/bm/gfx16.c core/src/bm/r3d.c core/src/bm/world3d.c core/src/bm/map3d.c core/src/bm/format.c core/src/bm/meshcap.c core/src/bm/vfont.c \
             core/src/kernel/syskeys.c core/src/kernel/controls.c \
             core/src/bm/require.c core/src/kernel/prompts.c core/src/gfx/font8x16.c core/src/gfx/font8x14.c core/src/gfx/font_inter8x16.c core/src/gfx/font_bmf.c \
             core/src/gfx/font_bm12x32.c core/src/gfx/font_bm18x48.c core/src/bm/api_font.c \
             core/src/gfx/font6x12.c core/src/lib/printf.c core/src/lib/crc32.c core/src/audio/audio.c core/src/audio/synth.c \
             core/src/audio/player.c core/src/audio/presets.c core/src/audio/lua_tone.c core/src/ai/net.c core/src/ai/nn.c \
             core/src/bm/decimate.c core/src/bm/cutout.c \
             core/src/bm/glb.c core/src/bm/json.c core/src/bm/png.c core/src/bm/jpeg.c core/src/bm/vid.c core/src/bm/loading.c core/src/bm/loading_logo.c \
             core/src/bm/profile.c core/src/bm/project.c core/src/bm/shot.c core/src/bm/webp.c core/src/bm/webimg.c core/src/bm/webpolicy.c core/src/bm/aichat.c
BMHOST_LUA := $(filter-out core/third_party/lua/lua.c core/third_party/lua/luac.c,$(LUA_SRCS))
BMHOST_OBJS := $(patsubst %,$(BUILD)/host/bmhost/%.o,$(BMHOST_RT) $(BMHOST_LUA))
$(BUILD)/host/bmhost/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=c11 -O2 -g -w -Itests/host/shim -Icore/src -Icore/src/bm -Icore/third_party -Icore/third_party/lua -c -o $@ $<
$(BUILD)/host/bmhost/runtime-deps: $(wildcard core/src/bm/*.h core/src/audio/*.h core/src/kernel/*.h core/src/usb/hid.h)
	@mkdir -p $(dir $@) && touch $@
$(BMHOST_OBJS): $(BUILD)/host/bmhost/runtime-deps
$(BUILD)/host/bmhost-bin: tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/host.h tests/audio/wavout.h tests/host/libs.S \
                          $(BMHOST_OBJS) core/src/ai/assist.lua core/src/script/bm3d.lua core/src/script/bmlib.lua core/src/script/bmnet.lua core/src/ai/predict.lua $(BUILD)/words.lua core/src/ai/padtype.lua \
                          core/src/script/riff.lua core/src/script/bmui.lua core/src/script/snes_asm.lua
	$(HOSTCC) -O2 -g -Wall -Wextra -D_DEFAULT_SOURCE -Itests/host/shim -Icore/src -Icore/src/bm -Icore/third_party/lua -I$(BUILD) \
	    -o $@ tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/libs.S $(BMHOST_OBJS) -lm
bmhost: $(BUILD)/host/bmhost-bin

# bm web player stage 2 (docs/design/bm-web-player.md): the bmhost runner built to
# WebAssembly with Emscripten (source ~/emsdk/emsdk_env.sh first), run under Node.
# libs.S cannot be assembled for wasm: core/scripts/mkweblibs.py writes the same data as C.
EMCC ?= emcc
$(BUILD)/web/libs.c: core/scripts/mkweblibs.py $(BUILD)/words.lua core/src/ai/assist.lua core/src/script/bm3d.lua core/src/script/bmlib.lua core/src/script/bmnet.lua                      core/src/ai/predict.lua core/src/ai/padtype.lua core/src/script/riff.lua core/src/script/bmui.lua core/src/script/snes_asm.lua
	$(PYTHON) core/scripts/mkweblibs.py -o $@ --words $(BUILD)/words.lua
$(BUILD)/web/bmhost.js: tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/host.h tests/audio/wavout.h                         $(BMHOST_RT) $(BMHOST_LUA) $(BUILD)/web/libs.c
	$(EMCC) -std=gnu11 -O2 -w -D_DEFAULT_SOURCE -Itests/host/shim -Icore/src -Icore/src/bm -Icore/third_party/lua -I$(BUILD) 	    -sNODERAWFS -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=2097152 	    -o $@ 	    tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c $(BUILD)/web/libs.c $(BMHOST_RT) $(BMHOST_LUA) -lm
bmhost-wasm: $(BUILD)/web/bmhost.js

# bm web player stage 3: the browser runner (web/runner/bmweb.c, Asyncify) and the page. make web-player
# writes build/web/player/ (index.html, player.js, bmweb.js, bmweb.wasm): serve it with any static server.
WEB_PLAYER := $(BUILD)/web/player
$(WEB_PLAYER)/bmweb.js: web/runner/bmweb.c tests/host/stubs.c tests/host/hostnet.c tests/host/host.h $(BMHOST_RT) $(BMHOST_LUA) $(BUILD)/web/libs.c
	@mkdir -p $(WEB_PLAYER)
	$(EMCC) -std=gnu11 -O2 -w -D_DEFAULT_SOURCE -Itests/host -Itests/host/shim -Icore/src -Icore/src/bm -Icore/third_party/lua -I$(BUILD) 	    -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sENVIRONMENT=web,node -sMODULARIZE -sEXPORT_NAME=createBmWeb 	    -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=2097152 	    -sEXPORTED_FUNCTIONS=_malloc,_free,_bmweb_start,_bmweb_input,_bmweb_quit -lidbfs.js -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32,FS,IDBFS 	    -o $@ web/runner/bmweb.c tests/host/stubs.c tests/host/hostnet.c $(BUILD)/web/libs.c $(BMHOST_RT) $(BMHOST_LUA) -lm
web-player: $(WEB_PLAYER)/bmweb.js $(wildcard web/*.html web/*.js web/*.css web/assets/* web/models/*) $(BUILD)/bm/kitchen.bm $(BUILD)/bm/hero.bm
	cp web/index.html web/style.css web/app.js web/bmcart.js web/bmring.js web/player.js $(WEB_PLAYER)/
	cp -r web/assets $(WEB_PLAYER)/
	rm -rf $(WEB_PLAYER)/models && mkdir -p $(WEB_PLAYER)/models
	cp web/models/*.html web/models/*.bmm web/models/*.png web/models/robots.txt $(WEB_PLAYER)/models/
	cp $(BUILD)/bm/kitchen.bm $(WEB_PLAYER)/kitchen.bm
	cp $(BUILD)/bm/hero.bm $(WEB_PLAYER)/hero.bm
# a 3D cartridge (the ARM rasterizer): its frames in wasm equal the PC build's (Hero Stress, no input script)
test-wasm-3d: $(BUILD)/web/bmhost.js $(BUILD)/host/bmhost-bin $(BUILD)/bm/hero.bm
	sh tests/webplayer/compare3d.sh $(BUILD) $(BUILD)/bm/hero.bm "" 10
test-wasm: $(BUILD)/web/bmhost.js $(BUILD)/web/player/bmweb.js
	$(PYTHON) tests/webplayer/run_wasm.py $(BUILD)

# bmhost-gpu: the same with the GPU's 3D (core/src/gpu/gpu3d.c) on the V3D
# emulator of the tests (tests/gpu/v3d_emu.c): the frames as the GPU draws
# them, to set them beside the ARM's (slower: the emulator is plain C)
$(BUILD)/host/bmhost-gpu: tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/host.h tests/audio/wavout.h tests/host/libs.S \
                          $(BMHOST_OBJS) core/src/ai/assist.lua core/src/script/bm3d.lua core/src/script/bmlib.lua core/src/script/bmnet.lua core/src/ai/predict.lua $(BUILD)/words.lua core/src/ai/padtype.lua \
                          core/src/gpu/gpu3d.c core/src/gpu/v3d_cl.c \
                          core/src/gpu/shaders.h tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h
	$(HOSTCC) -std=c11 -O2 -g -w -Icore/src -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free \
	    -c core/src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -g -Wall -Wextra -D_DEFAULT_SOURCE -DBMHOST_GPU -Itests/host/shim -Icore/src -Icore/src/bm -Itests/gpu \
	    -Icore/third_party/lua -I$(BUILD) -o $@ tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/libs.S \
	    $(BMHOST_OBJS) $@-gpu3d.o core/src/gpu/v3d_cl.c core/src/gpu/etc1.c tests/gpu/v3d_emu.c -lm
bmhost-gpu: $(BUILD)/host/bmhost-gpu

# bmhost-ai: the same with the real assistant (the `ai` table: core/src/ai/lua_ai.c and the
# C of the assistant), so bm Code's colours of the API words, the assistant panel (F6)
# and the tools that ask it can be run and recorded on the PC
BMHOST_AI_SRCS = core/src/ai/lua_ai.c core/src/ai/lua_music.c $(filter-out core/src/ai/nn.c,$(AI_SRCS))
$(BUILD)/host/bmhost-ai: tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/host.h tests/audio/wavout.h tests/host/libs.S \
                         $(BMHOST_OBJS) core/src/ai/assist.lua core/src/script/bm3d.lua core/src/script/bmlib.lua core/src/script/bmnet.lua core/src/ai/predict.lua $(BUILD)/words.lua core/src/ai/padtype.lua \
                         core/src/script/riff.lua $(BMHOST_AI_SRCS) core/src/ai/*.h $(BUILD)/assist.bin
	$(HOSTCC) -O2 -g -Wall -Wextra -D_DEFAULT_SOURCE -DBMHOST_AI -DBM_HOST_TEST -Itests/host/shim -Icore/src -Icore/src/bm -Icore/third_party/lua -I$(BUILD) \
	    -o $@ tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/libs.S $(BMHOST_OBJS) $(BMHOST_AI_SRCS) -lm
bmhost-ai: $(BUILD)/host/bmhost-ai

# the frame queue's 2D (M35): frames with the queue off and on, the same
test-queue2d: $(BUILD)/host/bmhost-gpu
	$(PYTHON) tests/gpu/queue2d.py $(BUILD)

# gpu2d_only (bm3d 7.16): the 2D of a game with no 3D on the V3D emulated, the same pictures as the ARM's
test-gpu2d-only: $(BUILD)/host/bmhost-gpu $(BUILD)/bm/yharnam.bm
	$(PYTHON) tests/gpu/gpu2d_only.py $(BUILD)

# gpu3d_light (bm3d 7.17): the light of dark_end by the V3D emulated, against the model it implements
test-gpu3d-light: $(BUILD)/host/bmhost-gpu
	$(PYTHON) tests/gpu/gpu3d_light.py $(BUILD)

# gpu3d_opt() and gpu3d_stats() (B3D-1, B3D-2) on the V3D emulator
$(BUILD)/gpu3d-opt-test.bm: tests/gpu3d_opt/cart.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "gpu3d opt test" --author tests

test-gpu3d-opt: $(BUILD)/host/bmhost-gpu $(BUILD)/gpu3d-opt-test.bm
	$< $(BUILD)/gpu3d-opt-test.bm --seconds 4 2>&1 | tee $(BUILD)/gpu3d-opt-test.log | grep "^gpu3d_opt"
	grep -q "^gpu3d_opt: \([0-9]*\)/\1 checks passed" $(BUILD)/gpu3d-opt-test.log

# The Lua edition of the 3D Bench (bm/bench3d) on the V3D emulator (the keys of bm/config.txt come from
# BMHOST_CONFIG; the list separator is `/`), --clock-scale 1 so the ms are real. test-bench3d is the fast part:
# 8 tests of the groups (3D, heroes, cel, 2D, herotris) on ARM and GPU+VS+Q, about 10 s of run. test-bench3d-all
# runs every test on every profile (ARM, GPU+VS+Q, GPU+VS+Q2): about 1 minute; it is not in HOST_TESTS.
B3D_FAST := spheres/heroes/quad_flat/cel_lit/cel/map8/sprites/herotris
test-bench3d: $(BUILD)/host/bmhost-gpu $(BUILD)/bm/bench3d.bm tests/bench3d/check.py
	BMHOST_CONFIG="3d_tests=$(B3D_FAST),3d_nowait=1" \
	    $< $(BUILD)/bm/bench3d.bm --seconds 100 --clock-scale 1 > $(BUILD)/bench3d-test.log 2>&1
	$(PYTHON) tests/bench3d/check.py $(BUILD)/bench3d-test.log --tests $(B3D_FAST)

test-bench3d-all: $(BUILD)/host/bmhost-gpu $(BUILD)/bm/bench3d.bm tests/bench3d/check.py
	BMHOST_CONFIG="3d_profiles=all,3d_nowait=1" \
	    $< $(BUILD)/bm/bench3d.bm --seconds 300 --clock-scale 1 > $(BUILD)/bench3d-all.log 2>&1
	$(PYTHON) tests/bench3d/check.py $(BUILD)/bench3d-all.log --profiles ARM/VSQ/VSQ2

.DEFAULT_GOAL := all
.PHONY: FORCE test-snes-m7rom test-snes-m7rom-arm test-snes-asm test-snes-gfx test-snes-code test-snes-run test-snes-learn test-snes-audio-ref test-snes-dsp77 test-snes-dsp77-arm test-snes-gsu test-snes-gsu-diff test-snes-gsu-arm test-snes-smw2 test-snes-smw2-arm bench-snes-gsu bench-snes-cpu test-snes-g2 test-typed test-bml bench-bml test-snes test-nes test-psx test-psx-cart test-tape test-snes-ppu test-snes-ppu-arm bench-snes-ppu test-islandbench test-darknight test-bmm test-netfetch test-web test-web-bmm test-agent test-agent-relay test-webplayer test-wasm test-wasm-3d bmhost-wasm web-player test-cel test-hero test-controls test-keymap test-gamekit test-game-trace test-gameapi test-screen-modes test-layout test-write test-shot test-sheet-png test-sheet-gif test-shot test-profile test-online test-bmnet test-loading test-smp test-qpu test-qpurun test-qpucheck test-etc1 test-gpu3d test-gpu3d-opt test-bench3d test-bench3d-all test-impostor test-queue2d test-gpu2d-only test-gpu3d-light test-b3d test-v3d bench3d count-insns all clean firmware image \
        image-pi1 sdcard install sdcard-chainloader qemu qemu7 qemu-screenshot \
        run-serial test test-map3d test-jobs test-bm test-res test-ai test-img2mesh ai-model test-predict test-padtype predict-bench syllables test-usb test-battery-curve test-audio \
        test-music music-model \
        test-fat test-fat-queue test-sound test-nano8 test-net test-http test-https test-release \
        release disasm wav test-studio test-prompts test-menu-bg test-docs test-api test-crumbs test-klog test-monitor-cmd test-shell-code test-netfetch test-hyp test-zero2 \
        showreel bmhost bmhost-gpu bmhost-ai overbit-reel overbit-reel-heroes overbit-reel-match yharnam-video yharnam-map test-yharnam test-yharnam-autoplay \
        test-catalog test-github test-lan market-seed test-host test-anim-ease test-qemu test-vid

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

# SNES instruction-cache switches (docs/progress/snes-icache.md). SNES_HOT=0: the SNES code stays where the linker puts it (no block, core/src/bm/snes_hot.ld not used).
# SNES_SPC_OS=1: the SPC700 file is built at -Os. After a change of either, delete build/k/core/src/bm/snes_spc.c.o (the flag is not a prerequisite).
SNES_HOT ?= 1
SNES_SPC_OS ?= 0
ifeq ($(SNES_SPC_OS),1)
$(BUILD)/k/core/src/bm/snes_spc.c.o: CFLAGS += -DSNES_SPC_OS
endif
$(BUILD)/snes_hot_sel.ld: FORCE
	@mkdir -p $(BUILD)
	@if [ "$(SNES_HOT)" = 1 ]; then cp core/src/bm/snes_hot.ld $@.new; else echo "/* SNES_HOT=0: no hot block */" > $@.new; fi; if cmp -s $@.new $@; then rm $@.new; else mv $@.new $@; fi

$(BUILD)/kernel.elf: $(KERNEL_OBJS) core/linker.ld $(BUILD)/snes_hot_sel.ld
	$(CC) $(LDFLAGS) -Wl,-L$(BUILD) -T core/linker.ld -Wl,-Map=$(BUILD)/kernel.map $(KERNEL_OBJS) $(LDLIBS) -o $@

$(BUILD)/kernel7.elf: $(KERNEL7_OBJS) core/linker.ld $(BUILD)/snes_hot_sel.ld
	$(CC) $(LDFLAGS) -Wl,-L$(BUILD) -T core/linker.ld -Wl,-Map=$(BUILD)/kernel7.map $(KERNEL7_OBJS) $(LDLIBS) -o $@

$(BUILD)/chainloader.elf: $(LOADER_OBJS) core/chainloader/linker.ld
	$(CC) $(LDFLAGS) -T core/chainloader/linker.ld -Wl,-Map=$(BUILD)/chainloader.map $(LOADER_OBJS) $(LLDLIBS) -o $@

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
# Cartridges go to bm/ (the menu also looks in the root directory; it does not read bm/ any more).
# Both kernels go on the card (kernel7.img with ZERO2=1): config.txt makes
# the firmware start kernel7.img on a Pi Zero 2 W and kernel.img on the
# other boards, so one card works in every Pi bm runs on.
KERNEL ?= kernel
# The kernel that ships (card, image, release) is built clean (CLEAN_KERNEL=1) in its own
# folder: the bm suite and the Assistant come from the Market. The kernel for the tests and
# for the QEMU runs stays complete in $(BUILD). SHIP_KERNEL=build/kernel.img ships the complete one.
SHIP_BUILD  := build-clean
SHIP_KERNEL ?= $(SHIP_BUILD)/kernel.img
SHIP_IMG    := $(if $(filter kernel,$(KERNEL)),$(SHIP_KERNEL),$(BUILD)/$(KERNEL).img)
ifeq ($(BUILD),build)
$(SHIP_BUILD)/kernel.img: FORCE
	$(MAKE) BUILD=$(SHIP_BUILD) CLEAN_KERNEL=1 $(SHIP_BUILD)/kernel.img
endif
SD_CARTS := $(GAME_CARTS)
# the WiFi and Bluetooth chips' firmware (make firmware), in bm/: the Zero
# W's BCM43438, then the Zero 2 W's CYW43436 (two versions of the chip)
RADIO_FW := BCM43430A1.hcd brcmfmac43430-sdio.bin brcmfmac43430-sdio.txt brcmfmac43430-sdio.clm_blob \
            SYN43430A1.hcd SYN43430B0.hcd brcmfmac43436-sdio.bin brcmfmac43436-sdio.txt \
            brcmfmac43436-sdio.clm_blob brcmfmac43436s-sdio.bin brcmfmac43436s-sdio.txt
sdcard: $(SHIP_IMG) $(K7) $(SD_CARTS)
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)/bm
	cp $(FW_DIR)/bootcode.bin $(FW_DIR)/start.elf $(FW_DIR)/fixup.dat $(DIST)/
	cp core/boot/config.txt $(DIST)/
	cp $(SHIP_IMG) $(DIST)/kernel.img
	$(if $(K7),cp $(K7) $(DIST)/kernel7.img,rm -f $(DIST)/kernel7.img)
	rm -f $(DIST)/bm/*.bm
	cp $(SD_CARTS) $(DIST)/bm/
	mkdir -p $(DIST)/bm/nano8 && cp $(NANO8_ROMS) $(DIST)/bm/nano8/
	mkdir -p $(DIST)/bm && cp core/boot/ca.pem $(DIST)/bm/ca.pem
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
              $(FW_DIR)/fixup.dat=fixup.dat core/boot/config.txt=config.txt \
              $(SHIP_KERNEL)=kernel.img \
              $(foreach c,$(SD_CARTS),$(c)=bm/$(notdir $(c))) \
              $(foreach r,$(NANO8_ROMS),$(r)=bm/nano8/$(notdir $(r))) \
              core/boot/ca.pem=bm/ca.pem
image: $(SHIP_KERNEL) $(K7) $(SD_CARTS)
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)
	$(PYTHON) core/scripts/mksd.py $(DIST)/bm.img --size-mib 64 --label BM $(IMAGE_FILES) \
	    $(if $(K7),$(K7)=kernel7.img) \
	    $(foreach f,$(RADIO_FW),$(if $(wildcard $(FW_DIR)/$(f)),$(FW_DIR)/$(f)=bm/$(f)))

image-pi1: $(SHIP_KERNEL) $(SD_CARTS)
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)
	$(PYTHON) core/scripts/mksd.py $(DIST)/bm-pi1.img --size-mib 64 --label BM $(IMAGE_FILES)

# The files of a release (M19), in $(DIST)/release: kernel.img and
# kernel7.img (the Pi Zero 2 W's, ZERO2=1), the games, bm/ca.pem and manifest.txt
# with where each goes, its size and SHA-256,
# signed (manifest.sig) with the key in BM_RELEASE_KEY (the environment;
# on GitHub the repository's secret). CI on a tag v*:
#   make release VERSION=v0.1.0 RELEASE_FLAGS=--require-key
# The signature must match core/keys/release-pub.pem, the key in the kernel.
RELEASE_DIR := $(DIST)/release
RELEASE_PUB ?= core/keys/release-pub.pem
# RGB30_KERNEL=build/rgb30/kernel8.img (made first with make TARGET=rgb30):
# the RGB30's manifest-rgb30 (kernel8.img, bm/ca.pem) in the same release,
# for its System > Updates
RGB30_KERNEL ?=
# The release ships the kernels bare: no games and, since 26 Oct 2026, no bm suite (they are in the Market)
release: $(SHIP_KERNEL) $(K7)
	rm -rf $(RELEASE_DIR)
	$(PYTHON) core/scripts/mkrelease.py $(RELEASE_DIR) --version $(VERSION) --commit $$(git rev-parse HEAD) \
	    --file $(SHIP_KERNEL):/kernel.img $(if $(K7),--file $(K7):/kernel7.img) \
	    --file core/boot/ca.pem:/bm/ca.pem --pub $(RELEASE_PUB) $(RELEASE_FLAGS)
	$(if $(RGB30_KERNEL),$(PYTHON) core/scripts/mkrelease.py $(RELEASE_DIR) --manifest manifest-rgb30 \
	    --version $(VERSION) --commit $$(git rev-parse HEAD) --file $(RGB30_KERNEL):/kernel8.img \
	    --file core/boot/ca.pem:/bm/ca.pem --pub $(RELEASE_PUB) $(RELEASE_FLAGS))

# Copies what make sdcard prepared onto a mounted SD card (SD=/mnt/d by
# default): the kernels, boot files, config.txt, cartridges and the chip
# firmware in bm/. Settings and saves (bm/config.txt, bm/save) are
# never touched. Uses sudo when the card is not writable (WSL).
SD ?= /mnt/d

install: sdcard
	@test -d $(SD) || { echo "$(SD) is not mounted (sudo mount -t drvfs D: $(SD))"; exit 1; }
	@S=; [ -w $(SD) ] || S=sudo; \
	$$S mkdir -p $(SD)/bm && \
	$$S cp $(DIST)/bootcode.bin $(DIST)/start.elf $(DIST)/fixup.dat $(DIST)/config.txt $(DIST)/kernel.img \
	    $(if $(K7),$(DIST)/kernel7.img) $(SD)/ && \
	$$S cp -r $(DIST)/bm/* $(SD)/bm/ && \
	sync && echo "installed on $(SD): kernel $$(git describe --tags --always --dirty), bm/ firmware" && \
	ls $(SD)/bm

sdcard-chainloader:
	$(MAKE) sdcard KERNEL=chainloader

# Upload the kernel to a Pi running the chainloader, then open a terminal.
# Rebooting the Pi (monitor command 'r') re-sends the current kernel.img.
run-serial: $(BUILD)/kernel.img
	$(PYTHON) core/tools/bm_load.py --baud $(BAUD) $(PORT) $<

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
# test-nano8 stays a target to run by hand; it is not in the CI list
HOST_TESTS := test-jobs test-bm test-res test-compat test-usb test-fat test-fat-queue test-audio test-music test-riff test-sound test-net test-http test-https test-img3d \
      test-catalog test-github test-lan test-controls test-keymap test-gamekit test-game-trace test-gameapi test-screen-modes test-layout test-anim-ease test-write test-shot test-reserved test-spec test-consistency test-profile test-online test-bmnet test-bmz test-loading \
      test-release test-smp test-qpu test-qpurun test-qpucheck test-snes-g2 test-etc1 test-gpu3d test-gpu3d-opt test-bench3d test-impostor test-cel test-queue2d test-gpu2d-only test-gpu3d-light test-b3d test-v3d test-ai test-predict test-padtype \
	test-studio test-prompts test-menu-bg test-docs test-api test-webp test-web test-web-bmm test-webapi test-agent test-agent-relay test-crumbs test-klog test-monitor-cmd test-shell-code test-vid $(if $(K7),test-hyp)
# what the QEMU tests read besides `all` (made by the PC's tests too)
QEMU_DEPS := $(BUILD)/host/meshview $(BUILD)/bm/hero.bm $(BUILD)/bm/mapkit.bm $(BUILD)/bm/testkit.bm
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

$(BUILD)/host/test_bm: tests/bm/test_bm.c core/src/bm/gfx16.c core/src/bm/vfont.c core/src/bm/r3d.c core/src/bm/format.c core/src/lib/crc32.c core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -Icore/third_party -o $@ tests/bm/test_bm.c core/src/bm/gfx16.c core/src/bm/vfont.c core/src/bm/r3d.c core/src/bm/format.c core/src/lib/crc32.c -lm

test-fat: $(BUILD)/host/test_fat
	$(PYTHON) tests/fs/run.py $<

$(BUILD)/host/test_fat: tests/fs/test_fat.c core/src/fs/fat.c core/src/fs/fat.h core/src/drivers/sd.h core/src/kernel/sdmap_core.c core/src/kernel/sdmap.h core/src/bm/project.c core/src/bm/project.h core/src/bm/format.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/fs/test_fat.c core/src/fs/fat.c core/src/kernel/sdmap_core.c core/src/bm/project.c core/src/bm/format.c core/src/lib/crc32.c

# the steps of a queued file_write: sectors between two ticks of a 2048 KB write
test-fat-queue: $(BUILD)/host/test_fat_queue
	$(PYTHON) tests/fs/run_queue.py $<

$(BUILD)/host/test_fat_queue: tests/fs/test_fat_queue.c core/src/fs/fat.c core/src/fs/fat.h core/src/drivers/sd.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/fs/test_fat_queue.c core/src/fs/fat.c

# Network console on lwIP's loopback interface (the WiFi chip is not in QEMU),
# then lwIP on the Pi 1 B's Ethernet with a simulated LAN9512 and a DHCP peer
test-net: $(BUILD)/host/test_netcon $(BUILD)/host/test_ethnet $(BUILD)/host/test_config
	$(BUILD)/host/test_netcon
	$(BUILD)/host/test_netcon menu
	$(BUILD)/host/test_netcon gone
	$(BUILD)/host/test_ethnet
	$(BUILD)/host/test_config
	$(PYTHON) tests/net/check_bm_net_config.py

# .bmz (core/src/bm/bmz.c): the C reader on files made by core/scripts/bmz.py, good and broken
$(BUILD)/host/test_bmz: tests/bmz/test_bmz.c core/src/bm/bmz.c core/src/bm/bmz.h core/src/bm/png.c core/src/bm/png.h core/src/lib/crc32.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -o $@ tests/bmz/test_bmz.c core/src/bm/bmz.c core/src/bm/png.c core/src/lib/crc32.c

# the console's extraction of /bmz on a FAT image (core/src/kernel/bmz_install.c), checked with fsck.vfat and mtools
$(BUILD)/host/test_install: tests/bmz/test_install.c core/src/kernel/bmz_install.c core/src/kernel/bmz_install.h core/src/bm/bmz.c core/src/bm/bmz.h                             core/src/bm/png.c core/src/fs/fat.c core/src/fs/fat.h core/src/kernel/sdmap_core.c core/src/bm/project.c core/src/bm/format.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/bmz/test_install.c core/src/kernel/bmz_install.c core/src/bm/bmz.c core/src/bm/png.c 		core/src/fs/fat.c core/src/kernel/sdmap_core.c core/src/bm/project.c core/src/bm/format.c core/src/lib/crc32.c

test-bmz: $(BUILD)/host/test_bmz $(BUILD)/host/test_install
	$(PYTHON) tests/bmz/run_bmz_test.py $(BUILD)/host/test_bmz
	$(PYTHON) tests/bmz/run_install_test.py $(BUILD)/host/test_install
.PHONY: test-bmz

# bm/config.txt (core/src/kernel/config.c): the file, and the settings from the PC
# (netxfer's C, bm_net.py --config, easy_install.sh)
$(BUILD)/host/test_config: tests/kernel/test_config.c core/src/kernel/config.c core/src/kernel/config.h core/src/lib/printf.c core/src/fs/sdlock.c core/src/fs/sdlock.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -o $@ tests/kernel/test_config.c core/src/kernel/config.c core/src/lib/printf.c core/src/fs/sdlock.c

$(BUILD)/host/test_ethnet: tests/kernel/crumbs_stub.c tests/net/test_ethnet.c core/src/net/net.c core/src/net/net.h core/src/net/cartnet.c core/src/net/cartnet.h \
                           core/src/kernel/fiber.h core/src/usb/smsc95xx.c core/src/usb/smsc95xx.h tests/usb/lan9512_sim.c \
                           tests/usb/lan9512_sim.h $(LWIP_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Icore/src -Icore/src/net -Icore/third_party/lwip/src/include -o $@ \
		tests/net/test_ethnet.c core/src/net/net.c core/src/net/cartnet.c core/src/usb/smsc95xx.c tests/usb/lan9512_sim.c tests/kernel/crumbs_stub.c $(LWIP_SRCS)

$(BUILD)/host/test_netcon: tests/net/test_netcon.c core/src/net/netcon.c core/src/net/netxfer.c core/src/net/stream.c core/src/net/*.h core/src/lib/crc32.c \
                          core/src/kernel/fiber.h core/src/kernel/notice.h $(LWIP_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Icore/src -Icore/src/net -Icore/third_party/lwip/src/include -o $@ \
		tests/net/test_netcon.c core/src/net/netcon.c core/src/net/netxfer.c core/src/net/stream.c core/src/lib/crc32.c $(LWIP_SRCS)

# HTTPS: http.c + tls.c + mbedTLS (the kernel's configuration) over POSIX
# sockets, against local TLS servers with a test CA made by openssl
test-https: $(BUILD)/host/test_https
	$(PYTHON) tests/net/run_https_test.py $(BUILD)/host/test_https

$(BUILD)/host/test_https: tests/net/test_https.c tests/net/stream_posix.c core/src/net/tls.c core/src/net/http.c \
                          core/src/net/http_kernel.c core/src/net/aipair.c core/src/net/*.h $(MBEDTLS_SRCS) core/boot/ca.pem core/tools/bm_agent_relay.py
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Icore/src -Icore/src/net -Icore/third_party/mbedtls/include \
		-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -DHTTP_USER_AGENT='"test"' -o $@ \
		tests/net/test_https.c tests/net/stream_posix.c core/src/net/tls.c core/src/net/http.c \
		core/src/net/http_kernel.c core/src/net/aipair.c $(MBEDTLS_SRCS)

# Release manifests (M19): core/scripts/mkrelease.py signs with a test key made
# by openssl, core/src/net/release.c checks signature, lines and files
test-release: $(BUILD)/host/test_release
	$(PYTHON) tests/net/run_release_test.py $(BUILD)/host/test_release

$(BUILD)/host/test_release: tests/net/test_release.c core/src/net/release.c core/src/net/release.h core/src/kernel/offload.c $(MBEDTLS_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Icore/src -Icore/src/net -Icore/third_party/mbedtls/include \
		-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/net/test_release.c core/src/net/release.c core/src/kernel/offload.c $(MBEDTLS_SRCS)

# The Market's catalog (M25): core/scripts/mkmarket.py checks and signs a folder
# of games with a test key, core/src/net/catalog.c checks signature, records and
# files, the kernel's PNG reader decodes the covers
test-catalog: $(BUILD)/host/test_catalog
	$(PYTHON) tests/net/run_catalog_test.py $(BUILD)/host/test_catalog

$(BUILD)/host/test_catalog: tests/net/test_catalog.c core/src/net/catalog.c core/src/net/catalog.h \
                            core/src/bm/png.c core/src/bm/png.h $(MBEDTLS_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Icore/src -Icore/src/net -Icore/third_party/mbedtls/include \
		-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/net/test_catalog.c core/src/net/catalog.c core/src/kernel/offload.c \
		core/src/bm/png.c $(MBEDTLS_SRCS) -lm

# Games between consoles on the home network (M24): core/src/net/lan.c on lwIP's
# loopback, sender and receiver in one process
test-lan: $(BUILD)/host/test_lan
	$(BUILD)/host/test_lan

$(BUILD)/host/test_lan: tests/net/test_lan.c core/src/net/lan.c core/src/net/lan.h core/src/net/stream.c $(LWIP_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Icore/src -Icore/src/net -Icore/third_party/lwip/src/include -Icore/third_party/mbedtls/include \
		-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/net/test_lan.c core/src/net/lan.c core/src/net/stream.c \
		core/third_party/mbedtls/library/sha256.c core/third_party/mbedtls/library/platform_util.c $(LWIP_SRCS)

# Publishing to the Market from the console (M25): core/src/net/github.c over
# POSIX sockets against a fake GitHub API (branch or fork, files, pull request);
# and the reports (core/src/kernel/reports.c): their names, a file on a branch
test-github: $(BUILD)/host/test_github $(BUILD)/host/test_report $(BUILD)/host/test_reports
	$(BUILD)/host/test_report
	$(BUILD)/host/test_reports
	$(PYTHON) tests/net/run_github_test.py $(BUILD)/host/test_github

# the reports waiting that go by themselves (core/src/kernel/reports.c), with a
# fake SD card, GitHub, network and clock
$(BUILD)/host/test_reports: tests/kernel/test_reports.c core/src/kernel/reports.c core/src/kernel/reports.h core/src/net/report.c \
                            core/src/lib/printf.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -o $@ tests/kernel/test_reports.c core/src/kernel/reports.c core/src/net/report.c \
		core/src/lib/printf.c

# the kernel log keeps its head and its last lines (core/src/lib/printf.c)
test-klog: $(BUILD)/host/test_klog
	$(BUILD)/host/test_klog

$(BUILD)/host/test_klog: tests/kernel/test_klog.c core/src/lib/printf.c core/src/lib/printf.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -o $@ tests/kernel/test_klog.c core/src/lib/printf.c

$(BUILD)/host/test_report: tests/net/test_report.c core/src/net/report.c core/src/net/report.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -o $@ tests/net/test_report.c core/src/net/report.c

$(BUILD)/host/test_github: tests/net/test_github.c core/src/net/github.c core/src/net/github.h core/src/net/http.c core/src/net/http.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -Icore/src/net -DHTTP_USER_AGENT='"test"' -o $@ tests/net/test_github.c \
		core/src/net/github.c core/src/net/http.c

# The market's repository (M25): its template (market/) and the games of
# the project, built here, into MARKET (a clone of f-accomando/bm-market).
# A game's version is the day its bytes changed.
MARKET ?= ../bm-market
MARKET_VERSION := $(shell date -u +%Y.%m.%d)
# The Market has its own list of games: GAMES minus MARKET_APPS_OF_GAMES.
# typing, write and bench3d are apps: they go to the bm/apps/ folder of the Market.
MARKET_APPS_OF_GAMES := typing write bench3d font
MARKET_GAMES := $(filter-out $(MARKET_APPS_OF_GAMES),$(GAMES))
# The apps (the bm editors): each one is a built-in cartridge, built as
# build/<id>.bm and seeded in the bm/apps/ folder of the Market. The RGB30 gets
# the RGB30's SDK is not in the Market for now (the new SDK .bm for both kernels comes later).
MARKET_APPS := editor code pixel studio animator mesh sound assistant snes_sdk
MARKET_APP_CARTS := $(patsubst %,$(BUILD)/%.bm,$(MARKET_APPS))
MKMARKET_ADD = $(PYTHON) core/scripts/mkmarket.py $(MARKET)/bm/games --apps $(MARKET)/bm/apps --add
market-seed: $(GAME_CARTS) $(MARKET_APP_CARTS)
	@test -d $(MARKET) || { echo "MARKET=$(MARKET): clone f-accomando/bm-market there first"; exit 1; }
	mkdir -p $(MARKET)/.github/workflows $(MARKET)/bm/games $(MARKET)/bm/apps
	cp market/README.md market/.gitignore $(MARKET)/
	cp market/.github/workflows/market.yml $(MARKET)/.github/workflows/
	for g in $(MARKET_GAMES); do \
		$(MKMARKET_ADD) $(BUILD)/bm/$$g.bm --id $$g --version $(MARKET_VERSION) --about-file market/about.txt || exit 1; \
	done
	for a in $(MARKET_APPS_OF_GAMES); do \
		$(MKMARKET_ADD) $(BUILD)/bm/$$a.bm --id $$a --kind app --version $(MARKET_VERSION) --about-file market/about.txt || exit 1; \
	done
	for a in $(MARKET_APPS); do \
		$(MKMARKET_ADD) $(BUILD)/$$a.bm --id $$a --kind app --version $(MARKET_VERSION) --about-file market/about.txt || exit 1; \
	done
	$(PYTHON) core/scripts/mkmarket.py $(MARKET)/bm/games --apps $(MARKET)/bm/apps --check
# kernel7.img's start in Hyp mode, as the Pi Zero 2 W's firmware does it:
# start.S and vectors.S in QEMU's virt machine with the virtualization
# extensions (raspi2b starts in SVC), tests/boot/hyp_main.c on its PL011
HYP_DIR := $(BUILD)/hyp
$(HYP_DIR)/hyp.elf: tests/boot/hyp_main.c $(BUILD)/k7/core/src/boot/start.S.o $(BUILD)/k7/core/src/kernel/vectors.S.o core/linker.ld $(BUILD)/snes_hot_sel.ld
	@mkdir -p $(dir $@)
	sed 's/^    \. = 0x8000;/    . = 0x40008000;/' core/linker.ld > $(HYP_DIR)/linker.ld
	$(CC) $(ARCH7) -DBM_ZERO2 -O2 -Wall -Wextra -Icore/src/kernel -ffreestanding -nostdlib -nostartfiles \
	    -Wl,-L$(BUILD) -T $(HYP_DIR)/linker.ld tests/boot/hyp_main.c $(BUILD)/k7/core/src/boot/start.S.o \
	    $(BUILD)/k7/core/src/kernel/vectors.S.o -lgcc -o $@

test-hyp: $(HYP_DIR)/hyp.elf
	$(PYTHON) tests/boot/run_hyp.py $(QEMU) $<

# Bluetooth LE pairing cryptography (SMP), against the spec's sample data
test-smp: $(BUILD)/host/test_smp
	$(BUILD)/host/test_smp

$(BUILD)/host/test_smp: tests/bt/smp_test.c core/src/bt/smp_crypto.c core/src/bt/smp_crypto.h $(MBEDTLS_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -Icore/src -Icore/src/net -Icore/third_party/mbedtls/include \
		-DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' -o $@ tests/bt/smp_test.c core/src/bt/smp_crypto.c $(MBEDTLS_SRCS)

# HTTP client over POSIX sockets, against a local Python server
# the picture-to-model flow (core/src/net/img3d.c) against a fake service, with the .glb reader
test-img3d: $(BUILD)/host/test_img3d
	$(PYTHON) tests/net/run_img3d_test.py $(BUILD)/host/test_img3d $(BUILD)/img3d

$(BUILD)/host/test_img3d: tests/net/test_img3d.c core/src/net/img3d.c core/src/net/http.c core/src/net/*.h $(GLB_SRCS) core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -Icore/src/bm -DHTTP_USER_AGENT='"test"' -o $@ tests/net/test_img3d.c core/src/net/img3d.c core/src/net/http.c $(GLB_SRCS) -lm

# the WebP decoder (core/src/bm/webp.c): lossy and lossless pictures against libwebp's decode, then truncated and
# damaged files, under the address and undefined-behaviour sanitizers
test-webp: $(BUILD)/host/test_webp
	$(PYTHON) tests/webp/run_webp_test.py $(BUILD)/host/test_webp

$(BUILD)/host/test_webp: tests/webp/test_webp.c core/src/bm/webp.c core/src/bm/webp.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=c11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -Icore/src/bm -o $@ tests/webp/test_webp.c core/src/bm/webp.c

# the web API for cartridges (web_get, image_load, file_write): the rules (core/src/bm/webpolicy.c), the request in a
# fiber (webget.c, ucontext fibers here, plain sockets for the transport) and the picture decoders
# (webimg.c) against a local server
test-webapi: $(BUILD)/host/test_webget
	$(PYTHON) tests/web/run_webget_test.py $(BUILD)/host/test_webget

# bm Agent (bm/agent, ai_ask in docs/api/system.md): the cart in bmhost with a fake model before its
# main.lua (tests/agent/prelude.lua), and core/tools/bm_agent_relay.py over TLS with a fake program (needs openssl).
# The kernel side of ai_ask is in test-webapi (the JSON, the POST) and test-https (the relay's certificate).
test-agent: $(BUILD)/host/bmhost-bin tests/agent/run_agent_test.py tests/agent/prelude.lua bm/agent/main.lua bm/agent/bm.info
	$(PYTHON) tests/agent/run_agent_test.py $(BUILD)/host/bmhost-bin core/scripts/mkbm.py $(BUILD)/agent-test
test-agent-relay:
	$(PYTHON) tests/tools/test_bm_agent_relay.py

# file_write of a .bmm (docs/api/system.md): the model goes to /bmm/NAME.bmm after the kernel's own check of the
# bytes; broken files, other names and the .bm/.bme/.b16 rule on the host card. The committed models of
# web/models: web/models/build.py --check (the same bytes as the tools make) and the BMRES validator (bmres.py)
$(BUILD)/web-bmm-test.bm: tests/web/bmm_cart.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "web bmm test" --author tests
test-web-bmm: $(BUILD)/host/bmhost-bin $(BUILD)/web-bmm-test.bm tests/web/run_bmm_test.py tests/web/test_models.py \
              $(wildcard web/models/*)
	$(PYTHON) tests/web/test_models.py
	$(PYTHON) tests/web/run_bmm_test.py $(BUILD)/host/bmhost-bin $(BUILD)/web-bmm-test.bm $(BUILD)/web-bmm

$(BUILD)/host/test_webget: tests/web/test_webget.c tests/web/run_webget_test.py core/src/bm/webget.c core/src/bm/webget.h core/src/bm/aichat.c core/src/bm/aichat.h core/src/bm/webfile.c core/src/bm/webfile.h core/src/fs/sdlock.c core/src/fs/sdlock.h core/src/bm/webpolicy.c                            core/src/bm/webpolicy.h core/src/bm/webimg.c core/src/bm/webimg.h core/src/bm/jpeg.c core/src/bm/png.c core/src/bm/webp.c                            core/src/net/http.c core/src/net/http.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -DBM_HOST_TEST 		-DHTTP_USER_AGENT='"test"' -Icore/src -Icore/src/bm -Icore/src/net -Icore/third_party/lua -o $@ tests/web/test_webget.c core/src/bm/webget.c core/src/bm/webfile.c core/src/fs/sdlock.c core/src/bm/webpolicy.c core/src/bm/aichat.c core/src/bm/json.c 		core/src/bm/webimg.c core/src/bm/jpeg.c core/src/bm/png.c core/src/bm/webp.c core/src/net/http.c -lm

# the .bmv reader (core/src/bm/vid.c), jpeg_decode565 and core/scripts/mkvid.py
test-vid: $(BUILD)/host/test_vid core/scripts/mkvid.py tests/video/run_vid_test.py
	$(PYTHON) tests/video/run_vid_test.py $(BUILD)/host/test_vid $(BUILD)/vid

$(BUILD)/host/test_vid: tests/video/test_vid.c tests/video/vidframes.c tests/video/vidframes.h core/src/bm/vid.c core/src/bm/jpeg.c core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -Icore/src/bm -Itests/video -o $@ tests/video/test_vid.c tests/video/vidframes.c core/src/bm/vid.c core/src/bm/jpeg.c -lm

test-http: $(BUILD)/host/test_http
	$(PYTHON) tests/net/run_http_test.py $(BUILD)/host/test_http

$(BUILD)/host/test_http: tests/net/test_http.c core/src/net/http.c core/src/net/http.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -DHTTP_USER_AGENT='"test"' -o $@ tests/net/test_http.c core/src/net/http.c

test-audio: $(BUILD)/host/test_audio $(BUILD)/host/bmrender tests/audio/test_bmaudio.py core/scripts/bmaudio.py
	$<
	$(PYTHON) tests/audio/test_bmaudio.py $(BUILD)/host/bmrender $(BUILD)/audio

# The music assistant (core/src/ai/music.c): the melody network against the
# Python reference, every recipe, the words of the requests. `make
# music-model` trains the network again (numpy) from core/src/ai/melodies.txt:
# then commit core/src/ai/music_net.c. MUSIC_WAV=dir also writes examples.
test-music: $(BUILD)/host/test_music
	$(PYTHON) core/scripts/trainmusic.py --check
	$< $(MUSIC_WAV)

$(BUILD)/host/test_music: tests/ai/test_music.c core/src/ai/music.c core/src/ai/music_net.c core/src/ai/nn.c core/src/audio/presets.c \
                          core/src/audio/player.c core/src/audio/synth.c core/src/ai/*.h core/src/audio/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/ai/test_music.c core/src/ai/music.c core/src/ai/music_net.c core/src/ai/nn.c \
	    core/src/audio/presets.c core/src/audio/player.c core/src/audio/synth.c -lm

music-model:
	$(PYTHON) core/scripts/trainmusic.py

$(BUILD)/host/test_audio: tests/audio/test_audio.c core/src/audio/synth.c core/src/audio/player.c core/src/audio/presets.c \
                          core/src/audio/iec958.c core/src/audio/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/audio/test_audio.c core/src/audio/synth.c core/src/audio/player.c \
	    core/src/audio/presets.c core/src/audio/iec958.c -lm

# A song (or SFX=n) of a sound bank as a WAV file, made on the PC by the
# console's synthesizer: make wav BANK=bm/sound/demo.json SONG=0
# BITS=24 (or 32, f32: IEEE floats) for a deeper file than the 16 bits
BANK ?= bm/sound/demo.json
SONG ?= 0
wav: $(BUILD)/host/bmrender
	$(PYTHON) core/scripts/bmaudio.py pack $(BANK) -o $(BUILD)/wav.bmau
	$< $(if $(BITS),--bits $(BITS)) $(BUILD)/wav.bmau $(BUILD)/$(if $(SFX),sfx$(SFX),song$(SONG)).wav \
	    $(if $(SFX),sfx $(SFX),song $(SONG)) $(SECONDS)

$(BUILD)/host/bmrender: tests/audio/render.c tests/audio/wavout.h core/src/audio/synth.c core/src/audio/player.c core/src/audio/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/audio/render.c core/src/audio/synth.c core/src/audio/player.c -lm

test-usb: $(BUILD)/host/test_hid $(BUILD)/host/test_eth $(BUILD)/host/test_board $(BUILD)/host/test_board7
	$(BUILD)/host/test_hid
	$(BUILD)/host/test_eth
	$(BUILD)/host/test_board
	$(BUILD)/host/test_board7

$(BUILD)/host/test_hid: tests/usb/test_hid.c core/src/usb/hid.c core/src/usb/hid.h core/src/usb/usb.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/usb/test_hid.c core/src/usb/hid.c

# Ethernet of the Pi 1 B (LAN951x) against a simulated chip
$(BUILD)/host/test_eth: tests/usb/test_eth.c tests/usb/lan9512_sim.c tests/usb/lan9512_sim.h \
                        core/src/usb/smsc95xx.c core/src/usb/smsc95xx.h core/src/usb/usb.h core/src/usb/dwc2.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Wno-format -Icore/src -o $@ tests/usb/test_eth.c tests/usb/lan9512_sim.c core/src/usb/smsc95xx.c

$(BUILD)/host/test_board: tests/usb/test_board.c core/src/drivers/board.c core/src/drivers/board.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/usb/test_board.c core/src/drivers/board.c

$(BUILD)/host/test_board7: tests/usb/test_board.c core/src/drivers/board.c core/src/drivers/board.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -DBM_ZERO2 -Icore/src -o $@ tests/usb/test_board.c core/src/drivers/board.c

# the meshes a cartridge builds in its code (cart_meshes(), bm Mesh)
MESHCAP_SRCS := tests/bm/test_meshcap.c core/src/bm/meshcap.c core/src/bm/format.c core/src/bm/r3d.c core/src/bm/gfx16.c core/src/lib/crc32.c
$(BUILD)/host/test_meshcap: $(MESHCAP_SRCS) core/src/bm/*.h $(LUA_SRCS) tests/host/libs.S core/src/script/bmlib.lua core/src/script/bmnet.lua \
                            $(BUILD)/words.lua
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src/bm -Icore/src -Icore/third_party/lua -I$(BUILD) -o $@ $(MESHCAP_SRCS) tests/host/libs.S \
	    $(LUA_SRCS) -lm

# the dev kit's token counter (stat(11), code_tokens(), the SDK)
$(BUILD)/host/test_tokens: tests/bm/test_tokens.c core/src/bm/tokens.c core/src/bm/tokens.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src/bm -o $@ tests/bm/test_tokens.c core/src/bm/tokens.c

# the polygon reducer (bm Studio's reduce, core/tools/bmreduce.py)
DECIMATE_SRCS := tests/bm/test_decimate.c core/src/bm/decimate.c core/src/bm/format.c core/src/lib/crc32.c
$(BUILD)/host/test_decimate: $(DECIMATE_SRCS) core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src/bm -Icore/src -o $@ $(DECIMATE_SRCS) -lm

# the .glb reader (the image-to-3D services' files on the console)
GLB_SRCS := core/src/bm/glb.c core/src/bm/json.c core/src/bm/jpeg.c core/src/bm/decimate.c core/src/bm/png.c
# bm_res_build: the file the console's res_write makes in /bmm, byte for byte as core/scripts/bmres.py
$(BUILD)/host/test_resbuild: tests/bm/test_resbuild.c core/src/bm/format.c core/src/lib/crc32.c core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/bm/test_resbuild.c core/src/bm/format.c core/src/lib/crc32.c -lm

$(BUILD)/host/test_glb: tests/bm/test_glb.c $(GLB_SRCS) core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src/bm -Icore/src -o $@ tests/bm/test_glb.c $(GLB_SRCS) -lm

# the outline maker (a model from a picture without any network, cutout.c)
CUTOUT_SRCS := core/src/bm/cutout.c $(GLB_SRCS)
$(BUILD)/host/test_cutout: tests/bm/test_cutout.c $(CUTOUT_SRCS) core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src/bm -Icore/src -o $@ tests/bm/test_cutout.c $(CUTOUT_SRCS) -lm

# the outline maker as a shared library (ctypes: core/scripts/bmcutout.py, core/tools/cutout2mesh.py)
$(BUILD)/host/libbmcutout.so: $(CUTOUT_SRCS) core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -shared -fPIC -o $@ $(CUTOUT_SRCS) -lm

# the reducer as a shared library for the PC tools (ctypes: core/scripts/bmdecimate.py)
$(BUILD)/host/libbmdecimate.so: core/src/bm/decimate.c core/src/bm/decimate.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -shared -fPIC -o $@ core/src/bm/decimate.c -lm

$(BUILD)/meshcap-test.bm: tests/bm/meshcap_cart.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "meshcap test" --author tests

# the 3D map engine (map3d.c, AGY-01)
MAP3D_TEST_SRCS := core/src/bm/map3d.c core/src/bm/r3d.c core/src/bm/gfx16.c
$(BUILD)/host/test_map3d: tests/bm/test_map3d.c $(MAP3D_TEST_SRCS) $(BMHOST_LUA) core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src/bm -Icore/src -Icore/third_party/lua -o $@ tests/bm/test_map3d.c $(MAP3D_TEST_SRCS) $(BMHOST_LUA) -lm

test-map3d: $(BUILD)/host/test_map3d
	$(BUILD)/host/test_map3d

# V2: the pure jobs of kernel/jobs.c (grid_path, grid_flood) with no worker: the answers are known
$(BUILD)/host/test_jobs: tests/bm/test_jobs.c core/src/kernel/jobs.c core/src/kernel/jobs.h core/src/kernel/offload.c core/src/kernel/offload.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/bm/test_jobs.c core/src/kernel/jobs.c core/src/kernel/offload.c

test-jobs: $(BUILD)/host/test_jobs
	$(BUILD)/host/test_jobs

test-bm: $(BUILD)/host/test_bm $(BUILD)/bm/mapkit.bm $(BUILD)/host/test_meshcap $(BUILD)/bm/astrowing.bm \
         $(BUILD)/bm/kitchen.bm $(BUILD)/meshcap-test.bm \
         $(BUILD)/host/test_decimate $(BUILD)/bm/testkit.bm $(BUILD)/host/libbmdecimate.so \
         $(BUILD)/host/test_glb $(BUILD)/host/test_cutout $(BUILD)/host/libbmcutout.so $(BUILD)/host/test_tokens \
         $(BUILD)/host/test_resbuild
	$< $(BUILD)/bm/mapkit.bm
	$(BUILD)/host/test_tokens bm/editor/main.lua bm/pong/main.lua
	$(BUILD)/host/test_decimate $(BUILD)/bm/testkit.bm $(BUILD)/bm/kitchen.bm
	$(PYTHON) tests/bm/run_glb_test.py $(BUILD)/host/test_glb $(BUILD)/glb
	$(PYTHON) tests/bm/run_resbuild_test.py $(BUILD)/host/test_resbuild $(BUILD)/resbuild
	$(PYTHON) tests/bm/run_cutout_test.py $(BUILD)/host/test_cutout $(BUILD)/cutout
	$(PYTHON) core/tools/cutout2mesh.py $(BUILD)/cutout/lolli_alpha.png -o $(BUILD)/cutout/tool.bm
	$(PYTHON) core/tools/cutout2mesh.py $(BUILD)/cutout/lolli_white.png -o $(BUILD)/cutout/tool.bm --lathe --name vase
	$(PYTHON) core/tools/bmreduce.py $(BUILD)/bm/testkit.bm --ratio 0.5 -o $(BUILD)/testkit-half.bm
	$(BUILD)/host/test_meshcap core/src/bm/runtime.c \
	    $(BUILD)/meshcap-test.bm '!stop here,wheel:1,crate:12,cars1_body:1,gem:2' \
	    $(BUILD)/bm/astrowing.bm ship:32,dart,tower,gate,ring:120,laser,bolt,debris,debris2,mark,core,core_hot,turret \
	    $(BUILD)/bm/kitchen.bm chef_classic1_body,chef1_body,plate,dplate
	$(PYTHON) tests/bm/test_type.py

# The button prompts (bm-ui): every one checked, and both sets drawn 3x as
# on a TV into build/prompts/prompts.png (the menu) and chips.png (the apps)
$(BUILD)/host/test_prompts: tests/ui/test_prompts.c core/src/kernel/prompts.c core/src/kernel/prompts.h \
                            core/src/gfx/font8x16.c core/src/gfx/font6x12.c core/src/lib/crc32.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/ui/test_prompts.c core/src/kernel/prompts.c \
		core/src/gfx/font8x16.c core/src/gfx/font6x12.c core/src/lib/crc32.c -lm

test-prompts: $(BUILD)/host/test_prompts
	@mkdir -p $(BUILD)/prompts
	$< $(BUILD)/prompts

# The menu background, made a band of rows per frame (core/src/kernel/menu_bg.c)
$(BUILD)/host/test_menu_bg: tests/ui/test_menu_bg.c core/src/kernel/menu_bg.c core/src/kernel/menu_bg.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/ui/test_menu_bg.c core/src/kernel/menu_bg.c -lm

test-menu-bg: $(BUILD)/host/test_menu_bg
	$<

# The crash record (core/src/kernel/crumbs.c): the old record is not written before crumbs_boot()
test-crumbs: $(BUILD)/host/test_crumbs $(BUILD)/host/test_wdguard
	$(BUILD)/host/test_crumbs
	$(BUILD)/host/test_wdguard

# The Monitor's command core (core/src/kernel/monitor_cmd.c): tokenizer, dispatcher, help, Tab, editor, history
test-monitor-cmd: $(BUILD)/host/test_monitor_cmd
	$(BUILD)/host/test_monitor_cmd

# "net fetch" (core/src/kernel/netfetch.c): the address, the gh: short form, the name and the folder
test-netfetch: $(BUILD)/host/test_netfetch
	$(BUILD)/host/test_netfetch

$(BUILD)/host/test_netfetch: tests/kernel/test_netfetch.c core/src/kernel/netfetch.c core/src/kernel/netfetch.h core/src/bm/webpolicy.c core/src/bm/webpolicy.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -o $@ tests/kernel/test_netfetch.c core/src/kernel/netfetch.c core/src/bm/webpolicy.c

$(BUILD)/host/test_monitor_cmd: tests/kernel/test_monitor_cmd.c core/src/kernel/monitor_cmd.c core/src/kernel/monitor_cmd.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -o $@ tests/kernel/test_monitor_cmd.c core/src/kernel/monitor_cmd.c

test-shell-code: $(BUILD)/host/test_shell_code
	$(BUILD)/host/test_shell_code

$(BUILD)/host/test_shell_code: tests/kernel/test_shell_code.c core/src/kernel/shell_code.c core/src/kernel/shell_code.h core/src/bm/bmlc.c core/src/bm/bmnative.c $(LUA_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -DSHELL_CODE_HOST_TEST -Icore/src -Icore/src/bm -Icore/third_party/lua -o $@ tests/kernel/test_shell_code.c core/src/kernel/shell_code.c core/src/bm/bmlc.c core/src/bm/bmnative.c $(LUA_SRCS) -lm

# The freeze guard: the timer IRQ pets the watchdog only while the main code makes progress (SYS-2)
$(BUILD)/host/test_wdguard: tests/kernel/test_wdguard.c core/src/drivers/watchdog.h core/src/drivers/mbox.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -o $@ tests/kernel/test_wdguard.c

$(BUILD)/host/test_crumbs: tests/kernel/test_crumbs.c core/src/kernel/crumbs.c core/src/kernel/crumbs.h core/src/lib/printf.c core/src/lib/printf.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Icore/src -o $@ tests/kernel/test_crumbs.c core/src/lib/printf.c

# The API list of the code editor: api.tsv is made from docs/api, the lint and
# the generator have unit tests
test-api:
	$(PYTHON) core/scripts/mkapi.py --check
	$(PYTHON) tests/tools/test_api_lint.py
	$(PYTHON) tests/tools/test_mkapi.py

test-docs:
	$(PYTHON) core/scripts/docs_audit.py score
	$(PYTHON) -m unittest tests.tools.test_claude_glossary
	$(PYTHON) -m unittest discover -s tests/tools -p "test_*.py"

# The assistant (M30): C features and network against the Python reference,
# answers to the held-out questions, sprite generator
AI_SRCS := core/src/ai/assist.c core/src/ai/nn.c core/src/ai/text.c core/src/ai/sprite.c core/src/ai/mesh.c core/src/ai/mesh_chars.c core/src/ai/mesh_skin.c core/src/ai/mesh_script.c \
           core/src/ai/music.c core/src/ai/music_net.c core/src/ai/lisp.c core/src/ai/synth.c
$(BUILD)/host/test_ai: tests/ai/test_ai.c $(AI_SRCS) core/src/ai/*.h core/src/lib/crc32.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/ai/test_ai.c $(AI_SRCS) core/src/lib/crc32.c -lm

# Lua for the PC with the `ai` table: the panel's tests
$(BUILD)/host/luaai: tests/ai/luaai.c core/src/ai/lua_ai.c core/src/ai/lua_music.c $(AI_SRCS) core/src/ai/*.h core/src/lib/crc32.c $(LUA_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -w -DBM_HOST_TEST -Icore/src -Icore/third_party/lua -o $@ tests/ai/luaai.c core/src/ai/lua_ai.c core/src/ai/lua_music.c \
		$(AI_SRCS) core/src/lib/crc32.c $(LUA_SRCS) -lm

# The 3D recipes drawn on the PC: build/ai/meshes.ppm (every recipe) and
# `meshview one mech out.ppm` (one, from four sides and in its poses)
$(BUILD)/host/meshview: tests/ai/meshview.c $(AI_SRCS) core/src/ai/*.h core/src/lib/crc32.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/ai/meshview.c $(AI_SRCS) core/src/lib/crc32.c -lm

# The word completion on the PC: words, contexts, suggestions, and the
# benchmark texts typed again with Tab
test-predict: $(BUILD)/host/luahost $(BUILD)/words.lua
	$(BUILD)/host/luahost tests/predict/predict_test.lua $(BUILD)

# Typing with the pad (docs/PADTYPE.md): the rules of compose and of the
# on-screen keyboard, every practice text written to the end by padtype's
# coach (the presses a character), then Pad Typing in bmhost copying the
# first text of each language to its result (shots in build/padtype/)
test-padtype: $(BUILD)/host/luahost $(BUILD)/words.lua $(BUILD)/host/bmhost-bin $(BUILD)/bm/typing.bm
	$(BUILD)/host/luahost tests/padtype/pad_test.lua $(BUILD)
	for l in it en lua; do \
	  mkdir -p $(BUILD)/padtype/$$l && \
	  $(BUILD)/host/luahost tests/padtype/script.lua $(BUILD) $$l 1 $(BUILD)/padtype/$$l.txt && \
	  $(BUILD)/host/bmhost-bin $(BUILD)/bm/typing.bm --input $(BUILD)/padtype/$$l.txt \
	      --shots $(BUILD)/padtype/$$l --seconds 300 > $(BUILD)/padtype/$$l.log 2>&1 && \
	  grep "typing: done" $(BUILD)/padtype/$$l.log || { tail -20 $(BUILD)/padtype/$$l.log; exit 1; }; \
	done

# Its benchmark (docs/PREDICT.md): the keys saved on the texts of 100
# characters, then the corpus with each group left out of the dictionary
WORD_FOLDS := giochi informativi lettere narrativa quotidiano tecnica
predict-bench: $(BUILD)/host/luahost $(BUILD)/words.lua
	@mkdir -p $(BUILD)/predict
	for g in $(WORD_FOLDS); do $(PYTHON) core/scripts/mkwords.py --skip it_$$g -o $(BUILD)/predict/fold_$$g.lua || exit 1; done
	$(BUILD)/host/luahost tests/predict/bench.lua $(BUILD) --folds

# The syllables of Italian, counted on the same texts (docs/PREDICT.md)
syllables:
	$(PYTHON) core/scripts/syllables.py

test-ai: $(BUILD)/host/test_ai $(BUILD)/assist.bin $(BUILD)/host/luahost $(BUILD)/host/luaai $(BUILD)/host/meshview $(BUILD)/words.lua
	$< $(BUILD)/assist.bin $(BUILD)/ai/ref.txt
	$(BUILD)/host/meshview sheet $(BUILD)/ai/meshes.ppm > /dev/null
	$(BUILD)/host/luahost tests/ai/check_snippets.lua $(BUILD)/ai/snippets.txt
	$(BUILD)/host/luaai $(BUILD)/assist.bin tests/ai/panel_test.lua $(BUILD)
	$(BUILD)/host/luaai $(BUILD)/assist.bin tests/ai/act_test.lua
	$(BUILD)/host/luaai $(BUILD)/assist.bin tests/ai/synth_test.lua
	$(MAKE) test-img2mesh

# core/tools/img2mesh.py offline: the recorded replies (the mech in the part
# language) become a .bm with the model, then one more model in it;
# core/tools/meshy2mesh.py on a .glb made by the test (no call to Meshy)
test-img2mesh: $(BUILD)/host/meshview
	rm -rf $(BUILD)/img2mesh/test $(BUILD)/img2mesh/test.bm && mkdir -p $(BUILD)/img2mesh
	$< one knight $(BUILD)/img2mesh/knight.ppm > /dev/null
	$(PYTHON) core/tools/img2mesh.py $(BUILD)/img2mesh/knight.ppm -o $(BUILD)/img2mesh/test.bm --name mech \
	    --replay tests/ai/img2mesh/replay --work $(BUILD)/img2mesh/test --rounds 1
	$(PYTHON) core/tools/img2mesh.py $(BUILD)/img2mesh/knight.ppm -o $(BUILD)/img2mesh/test.bm --name mech2 \
	    --replay tests/ai/img2mesh/replay --work $(BUILD)/img2mesh/test2 --rounds 0
	$(PYTHON) tests/ai/check_img2mesh.py $(BUILD)/img2mesh/test.bm
	$(PYTHON) tests/ai/check_meshy.py $(BUILD)
	$(PYTHON) tests/ai/check_local2mesh.py $(BUILD)/local2mesh

# The console's bm Studio, bm Animator, bm Mesh, bm Pixel and the SDK on the
# PC (luahost), then the files they wrote read by the Python of the build
# (tests/studio/check_files.py) and by the kernel's parser.
test-studio: $(BUILD)/host/test_bm $(BUILD)/bm/mapkit.bm $(BUILD)/host/luahost $(BUILD)/bm/testkit.bm \
             $(BUILD)/bm/astrowing.bm $(BUILD)/host/test_meshcap $(BUILD)/pixel/main.lua $(BUILD)/snes_sdk/main.lua
	$(BUILD)/host/luahost tests/studio/bmui_host.lua .
	$(BUILD)/host/luahost tests/studio/layout_host.lua .
	$(BUILD)/host/luahost tests/studio/code_templates_host.lua .
	$(BUILD)/host/luahost tests/studio/write_host.lua .
	$(BUILD)/host/luahost tests/studio/write_pdf_host.lua .
	$(BUILD)/host/luahost tests/studio/import_host.lua .
	rm -rf $(BUILD)/studio3d-sd && mkdir -p $(BUILD)/studio3d-sd/bm $(BUILD)/studio3d-sd/bme
	cp $(BUILD)/bm/testkit.bm $(BUILD)/studio3d-sd/bm/
	$(BUILD)/host/luahost tests/studio/tools3d_host.lua . $(BUILD)/studio3d-sd
	rm -rf $(BUILD)/mesh-sd && mkdir -p $(BUILD)/mesh-sd/bm $(BUILD)/mesh-sd/bme
	cp $(BUILD)/bm/testkit.bm $(BUILD)/bm/astrowing.bm $(BUILD)/mesh-sd/bm/
	$(BUILD)/host/luahost tests/studio/mesh_host.lua . $(BUILD)/mesh-sd
	$(BUILD)/host/test_meshcap core/src/bm/runtime.c $(BUILD)/mesh-sd/bme/astrowin.bme ship \
	    $(BUILD)/mesh-sd/bme/testkit.bme "" $(BUILD)/mesh-sd/bme/meshcopy.bme ""
	$(PYTHON) core/scripts/bmmesh.py $(BUILD)/mesh-sd/bme/astrowin.bme $(BUILD)/mesh-sd/bme/testkit.bme >/dev/null
	rm -rf $(BUILD)/sdk-sd && mkdir -p $(BUILD)/sdk-sd/bm $(BUILD)/sdk-sd/bm $(BUILD)/sdk-sd/bme
	cp $(BUILD)/bm/testkit.bm $(BUILD)/sdk-sd/bm/
	$(BUILD)/host/luahost tests/studio/sdk_host.lua . $(BUILD)/sdk-sd
	rm -rf $(BUILD)/pixel-sd && mkdir -p $(BUILD)/pixel-sd/bm $(BUILD)/pixel-sd/bme
	cp $(BUILD)/bm/testkit.bm $(BUILD)/bm/mapkit.bm $(BUILD)/pixel-sd/bm/
	$(BUILD)/host/luahost tests/studio/pixel_host.lua . $(BUILD)/pixel-sd
	rm -rf $(BUILD)/pixel-sd && mkdir -p $(BUILD)/pixel-sd/bm $(BUILD)/pixel-sd/bme
	cp $(BUILD)/bm/testkit.bm $(BUILD)/bm/mapkit.bm $(BUILD)/pixel-sd/bm/
	$(BUILD)/host/luahost tests/studio/pixel_host.lua . $(BUILD)/pixel-sd $(BUILD)/pixel/main.lua
	rm -rf $(BUILD)/snes-sdk-sd && mkdir -p $(BUILD)/snes-sdk-sd/bm $(BUILD)/snes-sdk-sd/bme
	$(BUILD)/host/luahost tests/studio/snes_sdk_host.lua . $(BUILD)/snes-sdk-sd $(BUILD)/snes_sdk/main.lua
	$(BUILD)/host/test_meshcap core/src/bm/runtime.c $(BUILD)/pixel-sd/bme/testkit.bme "" $(BUILD)/pixel-sd/bme/newspr.bme ""
	rm -rf $(BUILD)/humanoid-sd && mkdir -p $(BUILD)/humanoid-sd/bm $(BUILD)/humanoid-sd/bme
	$(BUILD)/host/luahost tests/studio/humanoid_host.lua . $(BUILD)/humanoid-sd
	$(BUILD)/host/test_meshcap core/src/bm/runtime.c $(BUILD)/humanoid-sd/bme/humanoid.bme ""
	$(PYTHON) tests/studio/check_files.py $(BUILD)/studio3d-sd $(BUILD)/mesh-sd $(BUILD)/pixel-sd $(BUILD)/bm/testkit.bm $(BUILD)/humanoid-sd
	$(PYTHON) tests/studio/anim_figure.py $(BUILD)/anim-figure.bm
	$(BUILD)/host/test_bm $(BUILD)/bm/mapkit.bm $(BUILD)/anim-figure.bm $(BUILD)/studio3d-sd/bme/blocks.bme

# The showreel at the top of the README (docs/showreel.gif and .mp4): the
# walker of the test kit in a new cartridge, the map in the SDK, the
# code in bm Code with the assistant and the game, on the console in QEMU;
# the cards drawn by Chromium (Playwright), ffmpeg puts it together
# (core/tools/showreel/)
SHOWREEL := $(BUILD)/showreel
showreel: $(BUILD)/kernel.img
	$(PYTHON) core/tools/showreel/console.py $(BUILD) $(SHOWREEL)/console
	node core/tools/showreel/cards.js $(SHOWREEL)/cards
	$(PYTHON) core/tools/showreel/assemble.py $(SHOWREEL) docs/showreel.mp4 docs/showreel.gif

# Rasterizer bench (M33): checksums of fixed 3D scenes (a change to r3d.c
# that should not change the picture must keep them), and with
# count-insns the ARM instructions per pixel and per triangle (needs
# gcc-arm-linux-gnueabihf and qemu-user).
$(BUILD)/host/bench3d: tests/bm/bench3d.c core/src/bm/gfx16.c core/src/bm/r3d.c core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/bm/bench3d.c core/src/bm/gfx16.c core/src/bm/r3d.c -lm

bench3d: $(BUILD)/host/bench3d
	$<

count-insns:
	$(PYTHON) tests/bm/count_insns.py

# The GPU backend of the 3D (M33) on a V3D emulator, against the software
# rasterizer, for the four byte orders its probe must find
$(BUILD)/host/test_gpu3d: tests/gpu/test_gpu3d.c tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h core/src/gpu/gpu3d.c \
                          core/src/gpu/gpu3d.h core/src/gpu/v3d_cl.c core/src/gpu/v3d.h core/src/gpu/shaders.h core/src/bm/r3d.c \
                          core/src/bm/gfx16.c core/src/bm/*.h core/src/gpu/etc1.c core/src/gpu/etc1.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free \
	    -c core/src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -Itests/gpu -o $@ tests/gpu/test_gpu3d.c tests/gpu/v3d_emu.c \
	    core/src/gpu/v3d_cl.c core/src/gpu/etc1.c core/src/bm/r3d.c core/src/bm/gfx16.c $@-gpu3d.o -lm

# ETC1 blocks (M39, bm3d 7.3): the decoder against hand-made blocks, the
# encoder's error and its bits always the same
$(BUILD)/host/test_etc1: tests/gpu/test_etc1.c core/src/gpu/etc1.c core/src/gpu/etc1.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -o $@ tests/gpu/test_etc1.c core/src/gpu/etc1.c -lm

test-etc1: $(BUILD)/host/test_etc1
	$<

# Impostors (far small meshes as one quad, off by default): off the picture
# of before, on fewer triangles and a bounded difference, the same choice on
# the ARM and the GPU (V3D emulator)
$(BUILD)/host/test_impostor: tests/bm/test_impostor.c tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h core/src/gpu/gpu3d.c                              core/src/gpu/gpu3d.h core/src/gpu/v3d_cl.c core/src/gpu/v3d.h core/src/gpu/shaders.h core/src/bm/r3d.c                              core/src/bm/gfx16.c core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free 	    -c core/src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Icore/src -Itests/gpu -o $@ tests/bm/test_impostor.c tests/gpu/v3d_emu.c 	    core/src/gpu/v3d_cl.c core/src/gpu/etc1.c core/src/bm/r3d.c core/src/bm/gfx16.c $@-gpu3d.o -lm

test-impostor: $(BUILD)/host/test_impostor
	$<

# The heroes of bm/hero through the vertex shader's mesh path (V3D emulator)
$(BUILD)/host/test_hero_mesh: tests/bm/test_hero_mesh.c tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h core/src/gpu/gpu3d.c \
                                  core/src/gpu/gpu3d.h core/src/gpu/v3d_cl.c core/src/gpu/v3d.h core/src/gpu/shaders.h core/src/bm/r3d.c \
                                  core/src/bm/gfx16.c core/src/bm/format.c core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free \
	    -c core/src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Icore/src -Itests/gpu -o $@ tests/bm/test_hero_mesh.c tests/gpu/v3d_emu.c \
	    core/src/gpu/v3d_cl.c core/src/gpu/etc1.c core/src/bm/r3d.c core/src/bm/gfx16.c core/src/bm/format.c core/src/lib/crc32.c $@-gpu3d.o -lm

# Cel light on the ARM raster (cel3d, draw3d flag 256, dither3d): the table, the bands on a sphere,
# off = the picture of before, the ARM dither switch
$(BUILD)/host/test_cel: tests/bm/test_cel.c core/src/bm/r3d.c core/src/bm/r3d.h core/src/bm/gfx16.c core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Icore/src -o $@ tests/bm/test_cel.c core/src/bm/r3d.c core/src/bm/gfx16.c -lm

test-cel: $(BUILD)/host/test_cel
	$<

# The 3D Bench (core/src/bm/b3d.c) on the PC: the V3D emulated, quick mode,
# twice (the second run reads the first's report); pages in build/b3d
$(BUILD)/host/b3d_host: tests/bm/b3d_host.c core/src/bm/b3d.c core/src/bm/b3d.h tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h \
                        core/src/gpu/gpu3d.c core/src/gpu/gpu3d.h core/src/gpu/v3d_cl.c core/src/gpu/v3d.h core/src/gpu/shaders.h \
                        core/src/gpu/version3d.h core/src/gpu/etc1.c core/src/gpu/etc1.h core/src/bm/r3d.c core/src/bm/gfx16.c core/src/gfx/font8x16.c \
                        core/src/gfx/font6x12.c core/src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free \
	    -c core/src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -Wall -Wextra -Icore/src -Itests/gpu -o $@ tests/bm/b3d_host.c core/src/bm/b3d.c tests/gpu/v3d_emu.c \
	    core/src/gpu/v3d_cl.c core/src/gpu/etc1.c core/src/bm/r3d.c core/src/bm/gfx16.c core/src/gfx/font8x16.c core/src/gfx/font6x12.c $@-gpu3d.o -lm

test-b3d: $(BUILD)/host/b3d_host
	$< --selftest
	rm -rf $(BUILD)/b3d-stop && mkdir -p $(BUILD)/b3d-stop
	$< $(BUILD)/b3d-stop --stop=5
	rm -rf $(BUILD)/b3d && mkdir -p $(BUILD)/b3d
	$< $(BUILD)/b3d
	$< $(BUILD)/b3d
	$(PYTHON) tests/bm/check_b3d.py $(BUILD)/b3d
	rm -rf $(BUILD)/b3d2d && mkdir -p $(BUILD)/b3d2d
	$< $(BUILD)/b3d2d --tests=map8,sprites,stack,fade,light,herotris,cel,cel_flat_lit,cel_flat,stress,htmap
	$(PYTHON) tests/bm/check_b3d.py $(BUILD)/b3d2d --2d
	rm -rf $(BUILD)/b3dq && mkdir -p $(BUILD)/b3dq
	$< $(BUILD)/b3dq --tests=fade --profiles=ARM,QPU --qpufade
	$(PYTHON) tests/bm/check_b3d.py $(BUILD)/b3dq --qpufade

# The V3D driver's job runner against a model of the V3D's registers
$(BUILD)/host/test_v3d: tests/gpu/test_v3d.c tests/gpu/mock/drivers/mmio.h core/src/gpu/v3d.c core/src/gpu/v3d.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Wno-format -Itests/gpu/mock -Icore/src -o $@ tests/gpu/test_v3d.c core/src/gpu/v3d.c

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
	EMU_T16_ALPHA=1 $< 0 1 1 0 0 0
	EMU_T16_ALPHA=2 $< 1 0 0 0 0 0
	EMU_T16_LOSSY=1 $< 1 0 0 0 0 0
	EMU_T16_LOSSY=1 EMU_T16_ALPHA=1 $< 0 1 1 0 0 0
	EMU_ETC1=1 $< 0 1 1 0 0 0
	EMU_ETC1=2 $< 1 0 0 0 0 0
	EMU_ETC1=3 $< 1 0 0 0 0 0

# QPU shaders (M33): the assembler against shaders run on a Pi, and
# core/src/gpu/shaders.h up to date with the sources in core/tools/qpuasm.py
test-qpu:
	$(PYTHON) core/tools/qpuasm.py -o $(BUILD)/shaders.h
	cmp $(BUILD)/shaders.h core/src/gpu/shaders.h
	$(PYTHON) core/tools/qpuprogs.py -o $(BUILD)/qpuprog.h
	cmp $(BUILD)/qpuprog.h core/src/gpu/qpuprog.h

# bm3d 7.x: the QPU programs (core/src/gpu/qpu.c) on the V3D emulator, three ways: they work,
# the V3D never answers, the V3D stores wrong data
$(BUILD)/host/test_qpurun: tests/gpu/test_qpu.c tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h core/src/gpu/qpu.c core/src/gpu/qpu.h                            core/src/gpu/qpuprog.h core/src/gpu/v3d.h core/src/bm/gfx16.c core/src/bm/gfx16.h
	mkdir -p $(dir $@)
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Icore/src -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free 	    -c core/src/gpu/qpu.c -o $@-qpu.o
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Icore/src -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free -include v3d_emu.h -c core/src/bm/gfx16.c -o $@-g16.o
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Icore/src -Itests/gpu -o $@ tests/gpu/test_qpu.c tests/gpu/v3d_emu.c 	    core/src/gpu/v3d_cl.c core/src/gpu/etc1.c $@-g16.o $@-qpu.o -lm

test-qpurun: $(BUILD)/host/test_qpurun
	$< 0
	$< 1
	$< 2
	$< 3

# bm3d 7.x: the QPU check (core/src/gpu/qpucheck.c, config key gpu3d_qpu) on the V3D emulator: PASS, SKIP (no answer), SKIP (probe off)
$(BUILD)/host/test_qpucheck: tests/gpu/test_qpucheck.c tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h core/src/gpu/qpu.c core/src/gpu/qpu.h core/src/gpu/qpucheck.c core/src/gpu/qpucheck.h core/src/gpu/qpuprog.h core/src/gpu/v3d.h core/src/bm/gfx16.c core/src/bm/gfx16.h
	mkdir -p $(dir $@)
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Icore/src -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free -c core/src/gpu/qpu.c -o $@-qpu.o
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Icore/src -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free -c core/src/gpu/qpucheck.c -o $@-chk.o
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Icore/src -Itests/gpu -o $@ tests/gpu/test_qpucheck.c tests/gpu/v3d_emu.c core/src/gpu/v3d_cl.c core/src/gpu/etc1.c core/src/bm/gfx16.c $@-qpu.o $@-chk.o -lm

test-qpucheck: $(BUILD)/host/test_qpucheck
	$< 0
	$< 1
	$< 2

# G2 (docs/progress/snes-gpu.md): the Mode 7 layer of the SNES PPU on the QPUs, on the V3D emulator, against the reference PPU byte for byte
$(BUILD)/qpu_snes.h: core/tools/qpusnes.py core/tools/qpuprogs.py core/tools/qpuasm.py
	@mkdir -p $(BUILD)
	$(PYTHON) core/tools/qpusnes.py -o $@
$(BUILD)/host/test_snes_m7: tests/gpu/test_snes_m7.c tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h $(BUILD)/qpu_snes.h tests/snes/ppu_ref.c core/src/bm/snes_ppu.c $(SNES_PPU_SRCS) core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O2 -g -w $(SNES_REF_DEFS) -Icore/src/bm -c -o $(BUILD)/host/snes_m7_ref.o tests/snes/ppu_ref.c
	$(HOSTCC) -O2 -g -Wall -Wextra -Icore/src -Icore/src/bm -Itests/gpu -I$(BUILD) -o $@ tests/gpu/test_snes_m7.c tests/gpu/v3d_emu.c core/src/gpu/v3d_cl.c core/src/gpu/etc1.c 	    $(BUILD)/host/snes_m7_ref.o core/src/bm/snes_ppu.c $(SNES_PPU_SRCS) -lm

test-snes-g2: $(BUILD)/host/test_snes_m7
	$<
	$< cost

# The Mode 7 test ROM (tests/snes/m7test/main.s, built by tests/snes/build_m7test.sh): the five variants in the core, the QPU path on the
# V3D emulator against the ARM picture byte for byte, and the reject counters (tests/snes/m7test_run.c).
$(BUILD)/host/snes_m7test_run: tests/snes/m7test_run.c tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h core/src/gpu/qpu_snes.c core/src/gpu/qpu_snes.h core/src/gpu/qpuprog_snes.h core/src/bm/snes_ppu.c $(SNES_PPU_SRCS) core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/host
	$(HOSTCC) -O2 -g -Wall -Wextra -Icore/src -Icore/src/bm -Itests/gpu -DQPU_SNES_SCRATCH_ALWAYS -o $@ tests/snes/m7test_run.c core/src/gpu/qpu_snes.c tests/gpu/v3d_emu.c core/src/gpu/v3d_cl.c core/src/gpu/etc1.c core/src/bm/snes_ppu.c $(SNES_PPU_SRCS) -lm
test-snes-m7rom: $(BUILD)/host/luahost $(BUILD)/host/snes_m7test_run core/src/script/snes_asm.lua tests/snes/asm_cli.lua tests/snes/build_m7test.sh tests/snes/m7test/main.s tests/snes/m7test/sintab.inc
	tests/snes/build_m7test.sh $(BUILD)/m7test.sfc
	$(BUILD)/host/snes_m7test_run $(BUILD)/m7test.sfc

# The same ROM on the ARM1176 in QEMU (game_run_arm.c): the pictures of the five variants equal the host ones.
test-snes-m7rom-arm: $(BUILD)/host/luahost $(BUILD)/host/snes_game_run tests/snes/game_run_arm.c tests/snes/game_run.c tests/snes/m7test_arm.py tests/snes/m7test/main.s core/src/bm/snes_ppu_arm.S core/src/bm/snes_cpu_arm.S core/src/bm/snes_gsu_arm.S core/src/bm/snes_int.h
	@mkdir -p $(BUILD)/arm
	tests/snes/build_m7test.sh $(BUILD)/m7test.sfc
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -c core/src/bm/snes_ppu_arm.S -o $(BUILD)/arm/ppuarm.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -c core/src/bm/snes_cpu_arm.S -o $(BUILD)/arm/cpuarm.o
	arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -Icore/src/bm -c core/src/bm/snes_gsu_arm.S -o $(BUILD)/arm/gsuarm.o
	$(SNES_ARM_CC) -DSNES_TRACE -DGR_ROM='"rom.bin"' -DGR_FW='"-"' -DGR_FRAMES='"70"' -DGR_SCRIPT='"s.txt"' -DGR_OUT='"p"' -specs=rdimon.specs -Wl,-Ttext=0x10000 -o $(BUILD)/arm/m7test.elf tests/snes/game_run_arm.c core/src/bm/snes_cpu.c $(SNES_ARM_SRCS) $(BUILD)/arm/ppuarm.o $(BUILD)/arm/cpuarm.o $(BUILD)/arm/gsuarm.o -lm
	$(PYTHON) tests/snes/m7test_arm.py $(BUILD)/m7test.sfc $(BUILD)/host/snes_game_run $(BUILD)/arm/m7test.elf

HOSTCC ?= cc

clean:
	rm -rf build $(DIST)

-include $(KERNEL_OBJS:.o=.d) $(KERNEL7_OBJS:.o=.d) $(LOADER_OBJS:.o=.d)

endif   # TARGET

# Screenshots (core/src/bm/shot.c, shot() and the system's key; docs/SCREENSHOTS.md): a cart asks for
# them in bmhost, tests/shot/check.py reads the PNGs it finds in the SD's bm/shots
$(BUILD)/shot-test.bm: tests/shot/cart.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "shot test" --author tests
test-shot: $(BUILD)/host/bmhost-bin $(BUILD)/shot-test.bm tests/shot/check.py tests/shot/key.txt
	rm -rf $(BUILD)/shot-sd && mkdir -p $(BUILD)/shot-sd
	$< $(BUILD)/shot-test.bm --sd $(BUILD)/shot-sd --input tests/shot/key.txt --seconds 40 2>&1 | tee $(BUILD)/shot-test.log | grep "shot:"
	$(PYTHON) tests/shot/check.py $(BUILD)/shot-sd

# sheet_png (core/src/bm/runtime.c l_sheet_png, shot_png_rgba): PNG bytes with alpha of sheet rectangles
$(BUILD)/sheet-png.bm: tests/shot/sheet_png.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "sheet png" --author tests
test-sheet-png: $(BUILD)/host/bmhost-bin $(BUILD)/sheet-png.bm tests/shot/check_sheet_png.py
	rm -rf $(BUILD)/sheet-png-sd && mkdir -p $(BUILD)/sheet-png-sd
	$< $(BUILD)/sheet-png.bm --sd $(BUILD)/sheet-png-sd --seconds 20 > $(BUILD)/sheet-png.log 2>&1
	$(PYTHON) tests/shot/check_sheet_png.py $(BUILD)/sheet-png.log

# sheet_gif (core/src/bm/runtime.c l_sheet_gif, shot_gif): the animated GIF of sheet rectangles
$(BUILD)/sheet-gif.bm: tests/shot/sheet_gif.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "sheet gif" --author tests
test-sheet-gif: $(BUILD)/host/bmhost-bin $(BUILD)/sheet-gif.bm tests/shot/check_sheet_gif.py
	rm -rf $(BUILD)/sheet-gif-sd && mkdir -p $(BUILD)/sheet-gif-sd
	$< $(BUILD)/sheet-gif.bm --sd $(BUILD)/sheet-gif-sd --seconds 20 > $(BUILD)/sheet-gif.log 2>&1
	$(PYTHON) tests/shot/check_sheet_gif.py $(BUILD)/sheet-gif.log

# The PDF manuals (docs/MANUALS.md): en and it, theme A; test-manuals says if one language is behind
manuals:
	$(PYTHON) core/scripts/manuals.py build -o manuals/pdf
test-manuals:
	$(PYTHON) core/scripts/manuals.py check
.PHONY: manuals test-manuals

# A game cannot create a file named as one of bm's tools (core/src/bm/runtime.c reserved_tool_name)
$(BUILD)/reserved-test.bm: tests/tools/reserved.lua core/scripts/mkbm.py
	$(PYTHON) core/scripts/mkbm.py -o $@ --lua $< --title "reserved test" --author tests
test-reserved: $(BUILD)/host/bmhost-bin $(BUILD)/reserved-test.bm
	rm -rf $(BUILD)/reserved-sd && mkdir -p $(BUILD)/reserved-sd/bm
	$< $(BUILD)/reserved-test.bm --sd $(BUILD)/reserved-sd --seconds 1 2>&1 | tee $(BUILD)/reserved-test.log | grep "^reserved"
	@for n in pixel.bm SDK.BM assistan.bm code.bm Studio.bm; do \
	    grep -q "^reserved: $$n false .*kept for one of bm's tools" $(BUILD)/reserved-test.log || { echo "$$n was not refused"; exit 1; }; done
	@for n in ok.bm pixels.bm; do grep -q "^reserved: $$n true" $(BUILD)/reserved-test.log || { echo "$$n was refused"; exit 1; }; done
	@echo "reserved: 7/7 checks passed"
.PHONY: test-reserved

# A cartridge that needs a newer revision of the specification (INFO "spec", core/src/bm/bm.h BM_SPEC_REV) is refused with a
# message; one that needs the kernel's revision or none runs
test-spec: $(BUILD)/host/bmhost-bin tests/spec/cart.lua core/scripts/mkbm.py
	@mkdir -p $(BUILD)/spec
	@printf 'name: spec test\nspec: 1\n' > $(BUILD)/spec/ok.info
	@printf 'name: spec test\nspec: 999\n' > $(BUILD)/spec/new.info
	@printf 'name: spec test\nspec: 6\n' > $(BUILD)/spec/six.info
	@printf 'name: spec test\nspec: 7\n' > $(BUILD)/spec/seven.info
	@printf 'name: spec test\nspec: 8\n' > $(BUILD)/spec/eight.info
	@$(PYTHON) core/scripts/mkbm.py -o $(BUILD)/spec/six.bm --lua tests/spec/cart.lua --info $(BUILD)/spec/six.info > /dev/null
	@$(PYTHON) core/scripts/mkbm.py -o $(BUILD)/spec/seven.bm --lua tests/spec/cart.lua --info $(BUILD)/spec/seven.info > /dev/null
	@$(PYTHON) core/scripts/mkbm.py -o $(BUILD)/spec/eight.bm --lua tests/spec/cart.lua --info $(BUILD)/spec/eight.info > /dev/null
	@$(PYTHON) core/scripts/mkbm.py -o $(BUILD)/spec/ok.bm --lua tests/spec/cart.lua --info $(BUILD)/spec/ok.info > /dev/null
	@$(PYTHON) core/scripts/mkbm.py -o $(BUILD)/spec/none.bm --lua tests/spec/cart.lua --title none > /dev/null
	@$(PYTHON) core/scripts/mkbm.py -o $(BUILD)/spec/new.bm --lua tests/spec/cart.lua --info $(BUILD)/spec/new.info > /dev/null
	@$(BUILD)/host/bmhost-bin $(BUILD)/spec/ok.bm --seconds 1 2>&1 | grep -q "spec: ran" || { echo "spec 1 did not run"; exit 1; }
	@$(BUILD)/host/bmhost-bin $(BUILD)/spec/none.bm --seconds 1 2>&1 | grep -q "spec: ran" || { echo "no spec did not run"; exit 1; }
	@$(BUILD)/host/bmhost-bin $(BUILD)/spec/new.bm --seconds 1 2>&1 | grep -q "needs revision 999" || { echo "spec 999 was not refused"; exit 1; }
	@$(BUILD)/host/bmhost-bin $(BUILD)/spec/six.bm --seconds 1 2>&1 | grep -q "spec: ran" || { echo "spec 6 did not run"; exit 1; }
	@$(BUILD)/host/bmhost-bin $(BUILD)/spec/seven.bm --seconds 1 2>&1 | grep -q "spec: ran" || { echo "spec 7 did not run"; exit 1; }
	@$(BUILD)/host/bmhost-bin $(BUILD)/spec/eight.bm --seconds 1 2>&1 | grep -q "needs revision 8" || { echo "spec 8 was not refused"; exit 1; }
	@echo "spec: 6/6 checks passed"
# A cartridge's code is Lua text: a section that starts with the bytecode signature is refused (mode "t" in lua_load)
test-luatext: $(BUILD)/host/bmhost-bin core/scripts/mkbm.py
	@mkdir -p $(BUILD)/luatext
	@$(PYTHON) -c "open('$(BUILD)/luatext/code.luac','wb').write(b'\x1bLua\x55\x00\x19\x93\r\n\x1a\n junk')"
	@$(PYTHON) core/scripts/mkbm.py -o $(BUILD)/luatext/bytecode.bm --lua $(BUILD)/luatext/code.luac --title luac > /dev/null
	@$(BUILD)/host/bmhost-bin $(BUILD)/luatext/bytecode.bm --seconds 1 2>&1 | tee $(BUILD)/luatext/bytecode.log | grep -q "attempt to load a binary chunk" || { echo "bytecode was not refused"; cat $(BUILD)/luatext/bytecode.log; exit 1; }
	@echo "luatext: 1/1 checks passed"
.PHONY: test-luatext

# luavm_run (kernel scripts) loads text only: a binary chunk is refused (mode "t")
test-luavm-text: tests/script/luavm_text_host.c core/src/script/luavm.c $(LUA_SRCS)
	@mkdir -p $(BUILD)/host
	@$(HOSTCC) -O1 -w -Icore/third_party/lua -Icore/src -o $(BUILD)/host/luavm_text_host tests/script/luavm_text_host.c core/src/script/luavm.c \
	    $(filter-out core/third_party/lua/lua.c core/third_party/lua/luac.c,$(LUA_SRCS)) -lm
	@$(BUILD)/host/luavm_text_host
.PHONY: test-luavm-text

test-consistency:
	$(PYTHON) core/scripts/consistency.py check
.PHONY: test-spec test-consistency

# Tropic Trail video series (docs/video/PIANO.md)
-include video/3d/video3d.mk

dbg-gsu-arm:
	$(SNES_ARM_CC) $(DBGFLAGS) -specs=rdimon.specs -Wl,-Ttext=0x10000 -o $(BUILD)/arm/dbg.elf $(DBGSRC) tests/snes/gsu_ref.c core/src/bm/snes_cpu.c $(SNES_ARM_SRCS) $(BUILD)/arm/ppuarm.o $(BUILD)/arm/cpuarm.o $(BUILD)/arm/gsuarm.o -lm
	timeout 20 qemu-system-arm -M versatilepb -cpu arm1176 -m 128 -semihosting -nographic -audio none -kernel $(BUILD)/arm/dbg.elf

# Our 65C816 and Super FX against the ares emulator (ISC licence), one instruction at a time (tests/snes/ref_diff.cpp).
# OPTIONAL: set ARES to a checkout of github.com/ares-emulator/ares (kept OUTSIDE this repository; the harness #includes its processor modules at build
# time and copies nothing). Without ARES the target prints a message and succeeds. Needs g++ with C++20. `make test-snes-ref ARES=/path/to/ares`
SNES_REF_FLAGS := -Icore/src/bm -Itests/snes -DSNES_TRACE
test-snes-ref:
ifeq ($(ARES),)
	@echo "test-snes-ref: skipped (set ARES=/path/to/ares to compare with the ares emulator)"
else
	@mkdir -p $(BUILD)/host/ref
	$(HOSTCC) -O2 -g -w $(SNES_REF_FLAGS) -c -o $(BUILD)/host/ref/ours.o tests/snes/ref_diff_ours.c
	$(HOSTCC) -O2 -g -w $(SNES_REF_FLAGS) -c -o $(BUILD)/host/ref/cpu.o core/src/bm/snes_cpu.c
	for f in snes_bus snes_gsu snes_sa1 snes_sa1_cpu snes_dsp77 snes_spc snes_dsp snes_ppu; do $(HOSTCC) -O2 -g -w $(SNES_REF_FLAGS) -c -o $(BUILD)/host/ref/$$f.o core/src/bm/$$f.c || exit 1; done
	g++ -std=c++20 -O2 -w -Itests/snes/ref_shim -Itests/snes -I$(ARES)/ares -I$(ARES)/nall -I$(ARES) -I$(ARES)/thirdparty -c -o $(BUILD)/host/ref/ref_diff.o tests/snes/ref_diff.cpp
	g++ -std=c++20 -O2 -w -Itests/snes/ref_shim -Itests/snes -I$(ARES)/ares -I$(ARES)/nall -I$(ARES) -I$(ARES)/thirdparty -c -o $(BUILD)/host/ref/ref_diff_gsu.o tests/snes/ref_diff_gsu.cpp
	g++ -o $(BUILD)/host/snes_ref_diff $(BUILD)/host/ref/ref_diff.o $(BUILD)/host/ref/ours.o $(BUILD)/host/ref/cpu.o $(foreach f,snes_bus snes_gsu snes_sa1 snes_sa1_cpu snes_dsp77 snes_spc snes_dsp snes_ppu,$(BUILD)/host/ref/$(f).o) -lm
	g++ -o $(BUILD)/host/snes_ref_diff_gsu $(BUILD)/host/ref/ref_diff_gsu.o $(BUILD)/host/ref/ours.o $(BUILD)/host/ref/cpu.o $(foreach f,snes_bus snes_gsu snes_sa1 snes_sa1_cpu snes_dsp77 snes_spc snes_dsp snes_ppu,$(BUILD)/host/ref/$(f).o) -lm
	$(BUILD)/host/snes_ref_diff $(REF_ARGS)
	@echo "the Super FX differences are listed by build/host/snes_ref_diff_gsu (it exits 1 while some are open: docs/progress/snes-superfx.md, 'Checked against ares')"
	-$(BUILD)/host/snes_ref_diff_gsu $(REF_GSU_ARGS)
endif
