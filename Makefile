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

BUILD   := build
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
CLEAN_KERNEL ?= 0
CLEAN_DEFS := $(if $(filter 1,$(CLEAN_KERNEL)),-DBM_CLEAN_KERNEL)
K7      := $(if $(filter 1,$(ZERO2)),$(BUILD)/kernel7.img)
COMMON  = $(ARCH) $(SOC) -std=c11 -O2 -Wall -Wextra -g -Isrc \
          -ffunction-sections -fdata-sections \
          -DUART_BAUD=$(BAUD) $(CLEAN_DEFS)
# Kernel: hosted C on top of newlib (libc, libm), see src/lib/syscalls.c.
CFLAGS  = $(COMMON) -D_DEFAULT_SOURCE -Ithird_party/lua \
          -Ithird_party/lwip/src/include -Isrc/net \
          -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"bm_mbedtls.h"' $(WARN)
# PI_O3_FILES: source files built with -O3 instead of -O2 (default empty: every file -O2,
# the output does not change). Example: make PI_O3_FILES="src/bm/gfx16.c src/bm/r3d.c".
# Paths as in KERNEL_SRCS. The flags are not a prerequisite: after you change the
# variable, run make clean (or delete the .o files) so the files are built again.
# Off for now; the accept rule is in docs/progress/graphics.md (Open work).
PI_O3_FILES ?=
ifneq ($(strip $(PI_O3_FILES)),)
$(patsubst %,$(BUILD)/k/%.o,$(PI_O3_FILES)): CFLAGS += -O3
endif
# Chainloader: freestanding, no libc.
LCFLAGS := $(COMMON) -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns
ASFLAGS = $(ARCH) $(SOC) $(CLEAN_DEFS) -g -Isrc -Isrc/kernel -Wa,-I$(BUILD)
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
EMBED_TOOLS := $(if $(filter 1,$(CLEAN_KERNEL)),,$(BUILD)/editor.bm $(BUILD)/sound.bm \
               $(BUILD)/studio.bm $(BUILD)/animator.bm $(BUILD)/mesh.bm $(BUILD)/pixel.bm \
               $(BUILD)/assistant.bm $(BUILD)/code.bm)
# Lua scripts embedded with .incbin
$(BUILD)/k/src/script/embed.S.o $(BUILD)/k7/src/script/embed.S.o: $(wildcard src/script/*.lua) keys/release-pub.pem keys/market-pub.pem \
                                 $(EMBED_TOOLS) \
                                 $(BUILD)/assist.bin src/ai/assist.lua \
                                 src/ai/predict.lua $(BUILD)/words.lua \
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
WORDS_SRC := $(wildcard src/ai/words/*.txt) $(wildcard carts/*/main.lua carts/kitchen/src/*.lua carts/titan/src/*.lua) src/ai/api.tsv scripts/api_lint_allow.txt \
             $(AI_KB) scripts/mkwords.py
$(BUILD)/words.lua: $(WORDS_SRC)
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkwords.py -o $@

# The built-in tools (code, assistant, editor, sound, studio, animator, mesh, pixel) are built as
# $(BUILD)/<tool>.bm by the cart rule below (TOOL_BMS).

# ---- Cartridges: one data-driven recipe (how to add a cart: docs/PROJECT-LAYOUT.md) ----
# A cart <k> needs only carts/<k>/main.lua. The recipe adds each optional file that exists:
#   bm.info, cover.png, sheet.png, map.csv, map_<layer>.csv (layers_<k>), flags.csv, sprites.txt,
#   models.bm or models.glb; in carts/<k>/assets/ or in carts/<k>/.
# Per-cart variables, only where a cart differs (all optional):
#   cdir_<k>    folder of the cart (default carts/<k>)
#   lua_<k>     the Lua script (default <cdir>/main.lua; a joined one: $(BUILD)/<k>/main.lua)
#   bminfo_<k>  the bm.info file;  noinfo_<k> := 1 builds with --title/--author/--res (title_<k>, res_<k>)
#   cover_<k>, sheet_<k>, models_<k>, audio_<k>   a file other than the default
#   sheet8_<k> := 1            the sheet with a palette and runs (up to 256 colours, much smaller)
#   layers_<k> := name ...     the map layers, files map_<name>.csv, in this order
#   extra_<k>   more mkbm.py flags, at the end
# The key <k> is the cart name; a second edition has its own key (overbit_b16).
c_dir    = $(or $(cdir_$(1)),carts/$(1))
c_asset  = $(firstword $(wildcard $(call c_dir,$(1))/assets/$(2) $(call c_dir,$(1))/$(2)))
c_lua    = $(or $(lua_$(1)),$(call c_dir,$(1))/main.lua)
c_info   = $(if $(noinfo_$(1)),,$(or $(bminfo_$(1)),$(wildcard $(call c_dir,$(1))/bm.info)))
c_cover  = $(or $(cover_$(1)),$(wildcard $(call c_dir,$(1))/cover.png))
c_sheet  = $(or $(sheet_$(1)),$(call c_asset,$(1),sheet.png))
c_map    = $(call c_asset,$(1),map.csv)
c_layer  = $(call c_asset,$(1),map_$(2).csv)
c_flags  = $(call c_asset,$(1),flags.csv)
c_sprite = $(call c_asset,$(1),sprites.txt)
c_models = $(or $(models_$(1)),$(call c_asset,$(1),models.bm),$(call c_asset,$(1),models.glb))
# the files a cart is built from: the prerequisites of its rule
c_deps   = $(call c_info,$(1)) $(call c_cover,$(1)) $(call c_sheet,$(1)) $(call c_map,$(1)) \
           $(foreach l,$(layers_$(1)),$(call c_layer,$(1),$(l))) $(call c_flags,$(1)) \
           $(call c_sprite,$(1)) $(call c_models,$(1)) $(audio_$(1)) $(if $(audio_$(1)),scripts/bmaudio.py)

define CART_RECIPE
@mkdir -p $(dir $@)
$(PYTHON) scripts/mkbm.py -o $@ --lua $< \
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
# The built-in tools: $(BUILD)/<tool>.bm. They print carts/<tool>/icon.png on the cartridge.
TOOL_BMS := code assistant editor sound studio animator mesh pixel
$(foreach t,$(TOOL_BMS),$(eval cover_$(t) := carts/$(t)/icon.png))
cover_assistant := carts/code/icon.png
audio_sound := carts/sound/demo.json
sheet8_studio := 1
$(TOOL_BMS:%=$(BUILD)/%.bm): $(BUILD)/%.bm: $$(call c_lua,$$*) scripts/mkbm.py $$(call c_deps,$$*)
	$(call CART_RECIPE,$*)

# Demo games (Lua only, sprites drawn in code): build/carts/<name>.bm
GAMES := pong snake shooter astrowing kitchen titan nano8 overbit yharnam typing write ko2 bench3d
GAME_CARTS := $(patsubst %,$(BUILD)/carts/%.bm,$(GAMES))
sheet8_yharnam := 1
sheet8_ko2 := 1
# The 3D Bench (Lua edition): its models with bones and baked light, its sprite sheet and the 8 map layers
# of the test map8 are made by carts/bench3d/build/mkassets.py (no binary in the repository)
models_bench3d := $(BUILD)/bench3d/models.bm
extra_bench3d := $(foreach l,1 2 3 4 5 6 7 8,--map l$(l)=$(BUILD)/bench3d/map_l$(l).csv)
$(BUILD)/bench3d/models.bm: carts/bench3d/build/mkassets.py scripts/bmmesh.py scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) carts/bench3d/build/mkassets.py $(dir $@)
# Yharnam's map drawn by hand (the SDK's map page): carts/yharnam/assets/map_ground.csv, map_overlay.csv and
# map_objects.csv (the placeholders of the houses, trees, lamps... it places)
# (carts/yharnam/build/mkmap.py makes the first one, and takes it back from the console)
layers_yharnam := ground overlay objects
# Test fixture, not a game: the test kit, a cartridge with 8 models, a skeleton and a sheet, made by
# tests/fixtures/mktestkit.py (no binary in the repository). The studio, resource and QEMU tests open
# it. It is not in GAMES, the SD card or the Market. mapkit.bm (below) is the sample with a map.
$(BUILD)/carts/testkit.bm: tests/fixtures/mktestkit.py scripts/mkbm.py scripts/bmmesh.py
	@mkdir -p $(dir $@)
	$(PYTHON) tests/fixtures/mktestkit.py $@

# The map kit: a second fixture, with a true-colour sheet and a map of 4 layers and no model (mktestkit.py --map)
$(BUILD)/carts/mapkit.bm: tests/fixtures/mktestkit.py scripts/mkbm.py
	@mkdir -p $(dir $@)
	$(PYTHON) tests/fixtures/mktestkit.py $@ --map

$(BUILD)/carts/%.bm: $$(call c_lua,$$*) scripts/mkbm.py scripts/bmmesh.py $$(call c_deps,$$*)
	$(call CART_RECIPE,$*)

# The projects (.bme) of the carts the Dev tab edits: the same cartridge, built the same way
BME_CARTS := kitchen bmweb
$(BME_CARTS:%=$(BUILD)/carts/%.bme): $(BUILD)/carts/%.bme: $$(call c_lua,$$*) scripts/mkbm.py $$(call c_deps,$$*)
	$(call CART_RECIPE,$*)

# Chaos Kitchen (M17) is written in several Lua files, joined by its build.py
KITCHEN_SRC := $(sort $(wildcard carts/kitchen/src/*.lua))
$(BUILD)/kitchen/main.lua: $(KITCHEN_SRC) carts/kitchen/build/build.py
	$(PYTHON) carts/kitchen/build/build.py $@ --map $(BUILD)/kitchen/main.map

lua_kitchen := $(BUILD)/kitchen/main.lua

# bm Web (carts/bmweb): the modules in src/ and main.lua are joined by build/build.py. The project
# (.bme) is the source the Dev tab edits; the .bm is the same cartridge to play. Not in GAMES.
BMWEB_SRC := $(sort $(wildcard carts/bmweb/src/*.lua)) carts/bmweb/main.lua
$(BUILD)/bmweb/main.lua: $(BMWEB_SRC) carts/bmweb/build/build.py
	$(PYTHON) carts/bmweb/build/build.py $@

lua_bmweb := $(BUILD)/bmweb/main.lua

# Kratos Build (carts/kratos): a mesh built part by part by a virtual pointer. One Lua file whose
# PARTS table is made by carts/kratos/build/glb2parts.py. The .bme is the source, the .bm the cartridge.
$(BUILD)/carts/kratos.bm $(BUILD)/carts/kratos.bme: $(BUILD)/carts/kratos.%: carts/kratos/main.lua scripts/mkbm.py carts/kratos/bm.info
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --info carts/kratos/bm.info

# Titan Clash (M20): several Lua files too; its sheet has more colours than
# fit in RGBA sections, so it goes as SHEET8 (a palette and RLE)
TITAN_SRC := $(sort $(wildcard carts/titan/src/*.lua))
$(BUILD)/titan/main.lua: $(TITAN_SRC) carts/titan/build/build.py
	$(PYTHON) carts/titan/build/build.py $@ --map $(BUILD)/titan/main.map

lua_titan := $(BUILD)/titan/main.lua
sheet8_titan := 1

# Titan Clash EX (RGB30, .b16 only): the same game's second edition, 360x360 (1:1),
# with its own sources in carts/titan_ex/ (the Pi's .bm stays in carts/titan/)
TITAN_EX_SRC := $(sort $(wildcard carts/titan_ex/src/*.lua))
$(BUILD)/titan_ex/main.lua: $(TITAN_EX_SRC) carts/titan_ex/build/build.py
	$(PYTHON) carts/titan_ex/build/build.py $@ --map $(BUILD)/titan_ex/main.map

cdir_titanex := carts/titan_ex
lua_titanex := $(BUILD)/titan_ex/main.lua
sheet_titanex := carts/titan/assets/sheet.png
sheet8_titanex := 1

# bm SDK (RGB30): the SDK's second edition, 360x360 and the pad, with its own
# sources in carts/sdk_b16/ (layout.lua + main.lua, joined by its build.py). A tool
# built into the RGB30's kernel (rgb30.mk, its Dev tab), not into the Pi's: here
# only its script, for test-sdk-b16. The Pi's SDK stays carts/editor.
SDK_B16_RES ?= 360x360
SDK_B16_SRC := carts/sdk_b16/layout.lua carts/sdk_b16/main.lua carts/sdk_b16/build/build.py
$(BUILD)/sdk_b16/main.lua: $(SDK_B16_SRC)
	$(PYTHON) carts/sdk_b16/build/build.py $@

lua_sdk_b16 := $(BUILD)/sdk_b16/main.lua
cover_sdk_b16 := carts/editor/icon.png
extra_sdk_b16 = --res $(SDK_B16_RES)

# nano8: plays .p8 / .p8.png carts (machine in src/bm/n8*.c); several Lua
# files too. The carts it ships with (carts/nano8/roms, licenses in
# CREDITS.md) go to carts/nano8/ on the SD card.
NANO8_SRC := $(sort $(wildcard carts/nano8/src/*.lua))
NANO8_ROMS := $(sort $(wildcard carts/nano8/roms/*.p8 carts/nano8/roms/*.p8.png))
$(BUILD)/nano8/main.lua: $(NANO8_SRC) carts/nano8/build/build.py
	$(PYTHON) carts/nano8/build/build.py $@ --map $(BUILD)/nano8/main.map

lua_nano8 := $(BUILD)/nano8/main.lua

# Hero Stress (docs/design/hero-stress-cart-and-cel-shader.md, stage 1): a plaza with walking heroes, to
# compare the 3D options on the Pi. Not in GAMES: it does not go in dist/bm or the Market.
# The models and the plaza are committed (carts/herocart/assets/, made by build/mkheroes.py and
# build/mkplaza.py); the sources are carts/herocart/src/*.lua joined by build/build.py.
HEROCART_SRC := $(sort $(wildcard carts/herocart/src/*.lua))
HEROCART_ASSETS := $(addprefix carts/herocart/assets/,sheet.png models.bm map_floor.csv map_height.csv map_walls.csv)
$(BUILD)/herocart/main.lua: $(HEROCART_SRC) carts/herocart/build/build.py
	$(PYTHON) carts/herocart/build/build.py $@ --map $(BUILD)/herocart/main.map

lua_herocart := $(BUILD)/herocart/main.lua
layers_herocart := floor height walls

# Overbit (M38): a hero shooter in 3D. Several Lua files joined by its
# build.py; the heroes' models and animations (MESH, ANIM) made by
# carts/overbit/art/models.py, the sounds by art/sounds.py.
OVERBIT_SRC := $(sort $(wildcard carts/overbit/src/*.lua))
OVERBIT_ART := $(wildcard carts/overbit/art/*.py carts/overbit/art/heroes/*.py carts/overbit/art/meshy/*.mesh \
                          carts/overbit/art/meshy/*.png carts/overbit/art/meshy/*.rig)
# The heroes' bodies are made of primitives (art/heroes/*.py). OVERBIT_MESHY=1: the Meshy
# figures of art/meshy instead (set aside: their arms and guns fused in the poses)
OVERBIT_MODELS_FLAGS := $(if $(OVERBIT_MESHY),--meshy)
title_overbit := Overbit
# its screen: 480x270, each pixel 4x4 on a 1080p TV (it was 320x180)
OVERBIT_RES := 480x270
# the .bm (the Pi's, in the Market next to the .b16): three screens on a TV,
# 640x360 on the ARM, 640x360, 1280x720 and 1920x1080 on the GPU (85_quality); the
# .b16 has not the flag and keeps its own (640x360 and 1080p)
OVERBIT_BM_DEFS := --define OVERBIT_TV_BM=true
$(BUILD)/overbit/main.lua: $(OVERBIT_SRC) carts/overbit/build/build.py $(BUILD)/overbit/models.bm
	$(PYTHON) carts/overbit/build/build.py $@ --map $(BUILD)/overbit/main.map --extra $(BUILD)/overbit/21_map.lua \
	    $(OVERBIT_BM_DEFS)

$(BUILD)/overbit/models.bm: $(OVERBIT_ART) scripts/bmmesh.py scripts/mkbm.py $(BUILD)/host/mappvs
	@mkdir -p $(dir $@)
	$(PYTHON) carts/overbit/art/models.py $@ --map $(BUILD)/overbit/21_map.lua --pvs $(BUILD)/host/mappvs \
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
	  $(PYTHON) scripts/bmres.py extract $< -o $(BUILD)/overbit/heroes/$${h%%:*}.bmm \
	    $$(for m in $$(echo $${h#*:} | tr , ' '); do printf -- '--model %s ' $$m; done) || exit 1; \
	done

# what can be seen from where on the maps (carts/overbit/art/mapbake.py)
$(BUILD)/host/mappvs: tools/mappvs.c src/bm/r3d.c src/bm/gfx16.c src/bm/format.c src/lib/crc32.c src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -std=c11 -Wall -Isrc -o $@ tools/mappvs.c src/bm/r3d.c src/bm/gfx16.c src/bm/format.c \
	    src/lib/crc32.c -lm

$(BUILD)/overbit/sounds.json: carts/overbit/art/sounds.py
	@mkdir -p $(dir $@)
	$(PYTHON) carts/overbit/art/sounds.py $@

lua_overbit := $(BUILD)/overbit/main.lua
models_overbit := $(BUILD)/overbit/models.bm
audio_overbit := $(BUILD)/overbit/sounds.json

# the optimized release, the .b16 of the Market for the Pi and the RGB30 (the
# same container, docs/B16.md §0): the script without comment
# and blank lines, 360x360 in the header (a .b16's size). The game sets its own
# screen, quality and renderer (85_quality): on the RGB30 360x360 or 720x720,
# on the Pi's TV 1080p on the GPU or 640x360 (the square page left at the
# start); the title's RESOLUTION and RENDERER, saved; BENCHMARK recalibrates.
OVERBIT_B16_RES := 360x360
$(BUILD)/overbit/main.min.lua: $(OVERBIT_SRC) carts/overbit/build/build.py $(BUILD)/overbit/models.bm
	$(PYTHON) carts/overbit/build/build.py $@ --minify --extra $(BUILD)/overbit/21_map.lua

cdir_overbit_b16 := carts/overbit
lua_overbit_b16 := $(BUILD)/overbit/main.min.lua
noinfo_overbit_b16 := 1
title_overbit_b16 := $(title_overbit)
res_overbit_b16 := $(OVERBIT_B16_RES)
models_overbit_b16 := $(BUILD)/overbit/models.bm
audio_overbit_b16 := $(BUILD)/overbit/sounds.json
$(BUILD)/carts/overbit.b16: $$(call c_lua,overbit_b16) scripts/mkbm.py $$(call c_deps,overbit_b16)
	$(call CART_RECIPE,overbit_b16)

# the .b16 that is the .bm's very bytes (docs/B16.md §0: the same container): a
# 360x360 or 720x720 game, built as its .bm (Yharnam: 360x360,
# its menus on "ok" / "back", the RGB30's B and A)
B16_AS_BM := yharnam titanex
$(patsubst %,$(BUILD)/carts/%.b16,$(B16_AS_BM)): $(BUILD)/carts/%.b16: $(BUILD)/carts/%.bm
	$(PYTHON) scripts/mkbm.py --from $< --b16 -o $@

# variants that start in a mode: the reel (for docs/img) and the benchmark
$(BUILD)/overbit/%.bm: $(OVERBIT_SRC) carts/overbit/build/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build/build.py $(BUILD)/overbit/$*.lua --start $* --extra $(BUILD)/overbit/21_map.lua
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/$*.lua --title "Overbit $*" --author bm --res $(OVERBIT_RES) \
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
$(BUILD)/overbit/reel-heroes.bm: $(OVERBIT_SRC) carts/overbit/build/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build/build.py $(BUILD)/overbit/reel-heroes.lua --start reel --hero $(OVERBIT_REEL_HEROES) \
	    --extra $(BUILD)/overbit/21_map.lua
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/overbit/reel-heroes.lua --title "Overbit reel" --author bm --res $(OVERBIT_RES) \
	    --models $(BUILD)/overbit/models.bm --audio $(BUILD)/overbit/sounds.json
overbit-reel-heroes: $(BUILD)/host/bmhost-bin $(BUILD)/overbit/reel-heroes.bm
	$(PYTHON) carts/overbit/build/reel.py $(BUILD) $(BUILD)/overbit/reel-heroes.bm 232 docs/img/overbit-reel-heroes.mp4 \
	    --size 960x540 --res $(OVERBIT_RES) --crf 30 --gif docs/img/overbit-reel-heroes.gif --gif-shots ULTIMATE

# a match of ten bots on Partenope, filmed round the point, behind and in the
# eyes of the heroes (docs/img/overbit-match.mp4 and a GIF of 20 s)
$(BUILD)/overbit/match-film.bm: $(OVERBIT_SRC) carts/overbit/build/build.py $(BUILD)/overbit/models.bm $(BUILD)/overbit/sounds.json
	$(PYTHON) carts/overbit/build/build.py $(BUILD)/overbit/match-film.lua --start match --extra $(BUILD)/overbit/21_map.lua \
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

# Yharnam's first drawn map, from its street plan (carts/yharnam/build/mkmap.py: it
# overwrites map_ground.csv, map_overlay.csv and map_objects.csv, the map
# drawn by hand)
yharnam-map: $(BUILD)/host/luahost
	$(PYTHON) carts/yharnam/build/mkmap.py --luahost $<

# The Sound editor in a fake bm: its banks are the console's format, byte for byte
$(BUILD)/demo.bmau: carts/sound/demo.json scripts/bmaudio.py
	@mkdir -p $(dir $@)
	$(PYTHON) scripts/bmaudio.py pack $< -o $@

# The games' buttons by name (2026-10-04): keymap(), btn("ok"), controller(),
# prompt() for a player, in bmhost with an input script (players on a
# keyboard, a DS4, an Xbox pad)
$(BUILD)/keymap-test.bm: tests/keymap/cart.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "keymap test"

$(BUILD)/controls-test.bm: tests/controls/cart.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "controls test"

# The gameplay kit (lib.world and the states, input, tween, record of bmlib) used by one small game
$(BUILD)/gamekit-test.bm: tests/gamekit/cart.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "gate kit test" --author tests

# The games' loading screen (src/bm/loading.c, 2026-10-04): the retro intro,
# its jingle, the console's page then the game's; the frames in build/loading/
$(BUILD)/host/test_loading: tests/bm/test_loading.c src/bm/loading.c src/bm/loading_logo.c src/bm/loading.h \
                            src/bm/gfx16.c src/gfx/font6x12.c src/lib/printf.c src/audio/presets.c src/audio/synth.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Isrc/bm -Ithird_party/lua -o $@ tests/bm/test_loading.c src/bm/loading.c \
	    src/bm/loading_logo.c src/bm/gfx16.c src/gfx/font6x12.c src/lib/printf.c src/audio/presets.c src/audio/synth.c -lm

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

# Hero Stress (stage 1): the models' counts, the printed line, the keys and the sweep, in bmhost
test-herocart: $(BUILD)/host/bmhost-bin $(BUILD)/carts/herocart.bm tests/herocart/check.py carts/herocart/build/mkheroes.py
	$(PYTHON) tests/herocart/check.py $< $(BUILD)/carts/herocart.bm $(BUILD)/herocart/test
	$(MAKE) $(BUILD)/host/test_herocart_mesh
	$(BUILD)/host/test_herocart_mesh carts/herocart/assets/models.bm

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
$(BUILD)/pong-trace.bm: carts/pong/main.lua tests/gamekit/trace_tail.lua scripts/mkbm.py
	cat carts/pong/main.lua tests/gamekit/trace_tail.lua > $(BUILD)/pong-trace.lua
	$(PYTHON) scripts/mkbm.py -o $@ --lua $(BUILD)/pong-trace.lua --title "pong trace" --author tests

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
$(BUILD)/gameapi-test.bm: $(GAMEAPI_SRC) $(BUILD)/gameapi/sheet.png scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua tests/gameapi/cart.lua --sheet $(BUILD)/gameapi/sheet.png \
	    --map tests/gameapi/map.csv --map front=tests/gameapi/map_front.csv --flags tests/gameapi/flags.csv \
	    --sprites tests/gameapi/sprites.txt --title "game api test" --author tests
test-gameapi: $(BUILD)/host/bmhost-bin $(BUILD)/gameapi-test.bm tests/gameapi/input.txt
	@mkdir -p $(BUILD)/gameapi
	$< $(BUILD)/gameapi-test.bm --input tests/gameapi/input.txt --seconds 8 --shots $(BUILD)/gameapi 2>&1 \
	    | tee $(BUILD)/gameapi-test.log | grep "^gameapi"
	grep -q "^gameapi: \([0-9]*\)/\1 checks passed" $(BUILD)/gameapi-test.log

# screen(): the modes of a square panel (bmhost --square: 360x360, 720x720, 720x405) and of a TV
# (bmhost), the size after a change, safe_area() and aspect() (W2, W3 of docs/design/b16-removal.md)
$(BUILD)/screen-modes.bm: tests/host/screen_modes.lua scripts/mkbm.py
	@mkdir -p $(BUILD)
	@printf 'name: screen modes\nspec: 5\n' > $(BUILD)/screen-modes.info
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --res 360x360 --title "screen modes" --author tests --info $(BUILD)/screen-modes.info
test-screen-modes: $(BUILD)/host/bmhost-bin $(BUILD)/screen-modes.bm
	$< $(BUILD)/screen-modes.bm --square --seconds 4 2>&1 | tee $(BUILD)/screen-modes-square.log | grep "^screen_modes"
	grep -q "^screen_modes: \([0-9]*\)/\1 checks passed" $(BUILD)/screen-modes-square.log
	$< $(BUILD)/screen-modes.bm --seconds 4 2>&1 | tee $(BUILD)/screen-modes-tv.log | grep "^screen_modes"
	grep -q "^screen_modes: \([0-9]*\)/\1 checks passed" $(BUILD)/screen-modes-tv.log

# Clip modes (APP-30): the kernel's pose_at through animate() and bone3d(), one clip a mode
$(BUILD)/anim-ease.bm: tests/anim_ease_cart.py tests/anim_ease.lua tests/studio/anim_figure.py scripts/mkbm.py
	$(PYTHON) tests/anim_ease_cart.py $@
test-anim-ease: $(BUILD)/host/bmhost-bin $(BUILD)/anim-ease.bm
	$< $(BUILD)/anim-ease.bm --seconds 2 2>&1 | tee $(BUILD)/anim-ease.log | grep "^anim-ease"
	grep -q "^anim-ease: \([0-9]*\)/\1 checks passed" $(BUILD)/anim-ease.log

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

# bmweb (carts/bmweb/src): HTML, CSS, layout and URL in pure Lua. No build
# dir needed: run.sh uses build/host/luahost, a Lua in the PATH, or builds a
# private one in a temp folder.
test-web:
	sh tests/web/run.sh

# bm web player stage 1 (web/bmcart.js): the .bm reader in Node; skipped when Node is missing
test-webplayer:
	@if command -v node >/dev/null 2>&1; then node tests/webplayer/test_bmcart.js && node tests/webplayer/test_bmring.js; else echo "test-webplayer: skipped (no node)"; fi

test-sound: $(BUILD)/host/luahost $(BUILD)/demo.bmau carts/sound/main.lua
	$< tests/sound/sim.lua carts/sound/main.lua $(BUILD)/demo.bmau

# Resource files (docs/RISORSE.md, scripts/bmres.py): models, images, sounds,
# maps and palettes out of a .bm and back in, conversions, broken files
test-res: $(BUILD)/carts/testkit.bm $(BUILD)/testkit-before.bm $(BUILD)/carts/mapkit.bm $(BUILD)/sound.bm scripts/bmres.py $(BUILD)/host/test_res
	rm -rf $(BUILD)/res && $(PYTHON) tests/res/test_bmres.py $(BUILD)/carts/testkit.bm $(BUILD)/carts/mapkit.bm \
	    $(BUILD)/sound.bm $(BUILD)/res
	$(BUILD)/host/test_res $(BUILD)/res $(BUILD)/testkit-before.bm

# a game of before the INFO section: the test kit without INFO and cover (test_res)
$(BUILD)/testkit-before.bm: tests/fixtures/mktestkit.py scripts/mkbm.py scripts/bmmesh.py
	@mkdir -p $(dir $@)
	$(PYTHON) tests/fixtures/mktestkit.py $@ --plain

$(BUILD)/host/test_res: tests/bm/test_res.c src/bm/format.c src/lib/crc32.c src/bm/bm.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/bm/test_res.c src/bm/format.c src/lib/crc32.c

# The compatibility policy: the C table is made from docs/spec/compat-policy.toml (mkcompat --check); the C test
# checks every rule; the grid of facts must give the same verdicts as the toml (scripts/bm_weight.py classify)
test-compat: $(BUILD)/host/test_compat docs/spec/compat-policy.toml
	$(PYTHON) scripts/mkcompat.py --check
	$(BUILD)/host/test_compat
	$(PYTHON) tests/tools/test_compat_policy.py $(BUILD)/host/test_compat

test-lint: $(BUILD)/host/test_lint docs/spec/lint-rules.toml
	$(PYTHON) scripts/mklint.py --check
	$(PYTHON) tests/tools/test_mklint.py $(BUILD)/host/test_lint

$(BUILD)/host/test_lint: tests/bm/test_lint.c src/bm/lint.c src/bm/lint.h src/bm/lint_table.h src/bm/bm.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Isrc/bm -o $@ tests/bm/test_lint.c src/bm/lint.c

$(BUILD)/host/test_compat: tests/bm/test_compat.c src/bm/format.c src/bm/compat_table.h src/lib/crc32.c src/bm/bm.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/bm/test_compat.c src/bm/format.c src/lib/crc32.c

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
BMHOST_RT := src/bm/runtime.c src/bm/api_world.c src/bm/api_anim.c src/bm/api_net.c src/bm/api_save.c src/bm/api_shot.c src/bm/api_docs.c src/bm/api_sound.c src/bm/api_light.c src/bm/api_map.c src/bm/api_prompts.c src/bm/api_keys.c src/bm/api_web.c src/bm/api_video.c src/bm/api_perm.c src/bm/api_editor.c src/bm/api_text.c src/bm/api_debug.c src/bm/tokens.c src/bm/gfx16.c src/bm/r3d.c src/bm/world3d.c src/bm/map3d.c src/bm/format.c src/bm/meshcap.c \
             src/kernel/syskeys.c src/kernel/controls.c \
             src/bm/require.c src/kernel/prompts.c src/gfx/font8x16.c src/gfx/font8x14.c \
             src/gfx/font6x12.c src/lib/printf.c src/lib/crc32.c src/audio/audio.c src/audio/synth.c \
             src/audio/player.c src/audio/presets.c src/audio/lua_tone.c src/ai/net.c src/ai/nn.c \
             src/bm/decimate.c src/bm/cutout.c \
             src/bm/glb.c src/bm/json.c src/bm/png.c src/bm/jpeg.c src/bm/vid.c src/bm/loading.c src/bm/loading_logo.c \
             src/bm/profile.c src/bm/project.c src/bm/lint.c src/bm/shot.c src/bm/webp.c src/bm/webimg.c src/bm/webpolicy.c
BMHOST_LUA := $(filter-out third_party/lua/lua.c third_party/lua/luac.c,$(LUA_SRCS))
BMHOST_OBJS := $(patsubst %,$(BUILD)/host/bmhost/%.o,$(BMHOST_RT) $(BMHOST_LUA))
$(BUILD)/host/bmhost/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=c11 -O2 -g -w -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -c -o $@ $<
$(BUILD)/host/bmhost/runtime-deps: $(wildcard src/bm/*.h src/audio/*.h src/kernel/*.h src/usb/hid.h)
	@mkdir -p $(dir $@) && touch $@
$(BMHOST_OBJS): $(BUILD)/host/bmhost/runtime-deps
$(BUILD)/host/bmhost-bin: tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/host.h tests/audio/wavout.h tests/host/libs.S \
                          $(BMHOST_OBJS) src/ai/assist.lua src/script/bm3d.lua src/script/bmlib.lua src/script/bmnet.lua src/ai/predict.lua $(BUILD)/words.lua src/ai/padtype.lua \
                          src/script/riff.lua src/script/bmui.lua
	$(HOSTCC) -O2 -g -Wall -Wextra -D_DEFAULT_SOURCE -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -I$(BUILD) \
	    -o $@ tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/libs.S $(BMHOST_OBJS) -lm
bmhost: $(BUILD)/host/bmhost-bin

# bm web player stage 2 (docs/design/bm-web-player.md): the bmhost runner built to
# WebAssembly with Emscripten (source ~/emsdk/emsdk_env.sh first), run under Node.
# libs.S cannot be assembled for wasm: scripts/mkweblibs.py writes the same data as C.
EMCC ?= emcc
$(BUILD)/web/libs.c: scripts/mkweblibs.py $(BUILD)/words.lua src/ai/assist.lua src/script/bm3d.lua src/script/bmlib.lua src/script/bmnet.lua                      src/ai/predict.lua src/ai/padtype.lua src/script/riff.lua src/script/bmui.lua
	$(PYTHON) scripts/mkweblibs.py -o $@ --words $(BUILD)/words.lua
$(BUILD)/web/bmhost.js: tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/host.h tests/audio/wavout.h                         $(BMHOST_RT) $(BMHOST_LUA) $(BUILD)/web/libs.c
	$(EMCC) -std=gnu11 -O2 -w -D_DEFAULT_SOURCE -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -I$(BUILD) 	    -sNODERAWFS -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=2097152 	    -o $@ 	    tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c $(BUILD)/web/libs.c $(BMHOST_RT) $(BMHOST_LUA) -lm
bmhost-wasm: $(BUILD)/web/bmhost.js

# bm web player stage 3: the browser runner (web/runner/bmweb.c, Asyncify) and the page. make web-player
# writes build/web/player/ (index.html, player.js, bmweb.js, bmweb.wasm): serve it with any static server.
WEB_PLAYER := $(BUILD)/web/player
$(WEB_PLAYER)/bmweb.js: web/runner/bmweb.c tests/host/stubs.c tests/host/hostnet.c tests/host/host.h $(BMHOST_RT) $(BMHOST_LUA) $(BUILD)/web/libs.c
	@mkdir -p $(WEB_PLAYER)
	$(EMCC) -std=gnu11 -O2 -w -D_DEFAULT_SOURCE -Itests/host -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -I$(BUILD) 	    -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sENVIRONMENT=web,node -sMODULARIZE -sEXPORT_NAME=createBmWeb 	    -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=2097152 	    -sEXPORTED_FUNCTIONS=_malloc,_free,_bmweb_start,_bmweb_input,_bmweb_quit -lidbfs.js -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32,FS,IDBFS 	    -o $@ web/runner/bmweb.c tests/host/stubs.c tests/host/hostnet.c $(BUILD)/web/libs.c $(BMHOST_RT) $(BMHOST_LUA) -lm
web-player: $(WEB_PLAYER)/bmweb.js $(wildcard web/*.html web/*.js web/*.css web/assets/* web/models/*) $(BUILD)/carts/kitchen.bm $(BUILD)/carts/herocart.bm
	cp web/index.html web/style.css web/app.js web/bmcart.js web/bmring.js web/player.js $(WEB_PLAYER)/
	cp -r web/assets $(WEB_PLAYER)/
	rm -rf $(WEB_PLAYER)/models && mkdir -p $(WEB_PLAYER)/models
	cp web/models/*.html web/models/*.bmm web/models/*.png web/models/robots.txt $(WEB_PLAYER)/models/
	cp $(BUILD)/carts/kitchen.bm $(WEB_PLAYER)/kitchen.bm
	cp $(BUILD)/carts/herocart.bm $(WEB_PLAYER)/herocart.bm
# a 3D cartridge (the ARM rasterizer): its frames in wasm equal the PC build's (Hero Stress, no input script)
test-wasm-3d: $(BUILD)/web/bmhost.js $(BUILD)/host/bmhost-bin $(BUILD)/carts/herocart.bm
	sh tests/webplayer/compare3d.sh $(BUILD) $(BUILD)/carts/herocart.bm "" 10
test-wasm: $(BUILD)/web/bmhost.js $(BUILD)/web/player/bmweb.js
	$(PYTHON) tests/webplayer/run_wasm.py $(BUILD)

# bmhost-gpu: the same with the GPU's 3D (src/gpu/gpu3d.c) on the V3D
# emulator of the tests (tests/gpu/v3d_emu.c): the frames as the GPU draws
# them, to set them beside the ARM's (slower: the emulator is plain C)
$(BUILD)/host/bmhost-gpu: tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/host.h tests/audio/wavout.h tests/host/libs.S \
                          $(BMHOST_OBJS) src/ai/assist.lua src/script/bm3d.lua src/script/bmlib.lua src/script/bmnet.lua src/ai/predict.lua $(BUILD)/words.lua src/ai/padtype.lua \
                          src/gpu/gpu3d.c src/gpu/v3d_cl.c \
                          src/gpu/shaders.h tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h
	$(HOSTCC) -std=c11 -O2 -g -w -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free \
	    -c src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -g -Wall -Wextra -D_DEFAULT_SOURCE -DBMHOST_GPU -Itests/host/shim -Isrc -Isrc/bm -Itests/gpu \
	    -Ithird_party/lua -I$(BUILD) -o $@ tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/libs.S \
	    $(BMHOST_OBJS) $@-gpu3d.o src/gpu/v3d_cl.c src/gpu/etc1.c tests/gpu/v3d_emu.c -lm
bmhost-gpu: $(BUILD)/host/bmhost-gpu

# bmhost-ai: the same with the real assistant (the `ai` table: src/ai/lua_ai.c and the
# C of the assistant), so bm Code's colours of the API words, the assistant panel (F6)
# and the tools that ask it can be run and recorded on the PC
BMHOST_AI_SRCS = src/ai/lua_ai.c src/ai/lua_music.c $(filter-out src/ai/nn.c,$(AI_SRCS))
$(BUILD)/host/bmhost-ai: tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/host.h tests/audio/wavout.h tests/host/libs.S \
                         $(BMHOST_OBJS) src/ai/assist.lua src/script/bm3d.lua src/script/bmlib.lua src/script/bmnet.lua src/ai/predict.lua $(BUILD)/words.lua src/ai/padtype.lua \
                         src/script/riff.lua $(BMHOST_AI_SRCS) src/ai/*.h $(BUILD)/assist.bin
	$(HOSTCC) -O2 -g -Wall -Wextra -D_DEFAULT_SOURCE -DBMHOST_AI -DBM_HOST_TEST -Itests/host/shim -Isrc -Isrc/bm -Ithird_party/lua -I$(BUILD) \
	    -o $@ tests/host/bmhost.c tests/host/stubs.c tests/host/hostnet.c tests/host/libs.S $(BMHOST_OBJS) $(BMHOST_AI_SRCS) -lm
bmhost-ai: $(BUILD)/host/bmhost-ai

# the frame queue's 2D (M35): frames with the queue off and on, the same
test-queue2d: $(BUILD)/host/bmhost-gpu
	$(PYTHON) tests/gpu/queue2d.py $(BUILD)

# gpu3d_opt() and gpu3d_stats() (B3D-1, B3D-2) on the V3D emulator
$(BUILD)/gpu3d-opt-test.bm: tests/gpu3d_opt/cart.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "gpu3d opt test" --author tests

test-gpu3d-opt: $(BUILD)/host/bmhost-gpu $(BUILD)/gpu3d-opt-test.bm
	$< $(BUILD)/gpu3d-opt-test.bm --seconds 4 2>&1 | tee $(BUILD)/gpu3d-opt-test.log | grep "^gpu3d_opt"
	grep -q "^gpu3d_opt: \([0-9]*\)/\1 checks passed" $(BUILD)/gpu3d-opt-test.log

# The Lua edition of the 3D Bench (carts/bench3d) on the V3D emulator (the keys of bm/config.txt come from
# BMHOST_CONFIG; the list separator is `/`), --clock-scale 1 so the ms are real. test-bench3d is the fast part:
# 8 tests of the groups (3D, heroes, cel, 2D, herotris) on ARM and GPU+VS+Q, about 10 s of run. test-bench3d-all
# runs every test on every profile (ARM, GPU+VS+Q, GPU+VS+Q2): about 1 minute; it is not in HOST_TESTS.
B3D_FAST := spheres/heroes/quad_flat/cel_lit/cel/map8/sprites/herotris
test-bench3d: $(BUILD)/host/bmhost-gpu $(BUILD)/carts/bench3d.bm tests/bench3d/check.py
	BMHOST_CONFIG="3d_tests=$(B3D_FAST),3d_nowait=1" \
	    $< $(BUILD)/carts/bench3d.bm --seconds 100 --clock-scale 1 > $(BUILD)/bench3d-test.log 2>&1
	$(PYTHON) tests/bench3d/check.py $(BUILD)/bench3d-test.log --tests $(B3D_FAST)

test-bench3d-all: $(BUILD)/host/bmhost-gpu $(BUILD)/carts/bench3d.bm tests/bench3d/check.py
	BMHOST_CONFIG="3d_profiles=all,3d_nowait=1" \
	    $< $(BUILD)/carts/bench3d.bm --seconds 300 --clock-scale 1 > $(BUILD)/bench3d-all.log 2>&1
	$(PYTHON) tests/bench3d/check.py $(BUILD)/bench3d-all.log --profiles ARM/VSQ/VSQ2

.DEFAULT_GOAL := all
.PHONY: FORCE test-web test-web-bmm test-webplayer test-wasm test-wasm-3d bmhost-wasm web-player test-cel test-herocart test-controls test-keymap test-gamekit test-game-trace test-gameapi test-screen-modes test-write test-shot test-sheet-png test-sheet-gif test-shot test-profile test-online test-bmnet test-loading test-smp test-qpu test-qpurun test-qpucheck test-etc1 test-gpu3d test-gpu3d-opt test-bench3d test-bench3d-all test-impostor test-queue2d test-b3d test-v3d bench3d count-insns all clean firmware image \
        image-pi1 sdcard install sdcard-chainloader qemu qemu7 qemu-screenshot \
        run-serial test test-map3d test-bm test-res test-ai test-img2mesh ai-model test-predict test-padtype predict-bench syllables test-usb test-audio \
        test-music music-model \
        test-fat test-fat-queue test-sdk-b16 test-sound test-nano8 test-net test-http test-https test-release \
        release disasm wav test-studio test-prompts test-menu-bg test-docs test-api test-crumbs test-monitor-cmd test-hyp test-zero2 \
        showreel bmhost bmhost-gpu bmhost-ai overbit-reel overbit-reel-heroes overbit-reel-match yharnam-video yharnam-map \
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
# The release ships the kernels bare: no games (D20; the bm editor suite is inside kernel.img)
release: $(BUILD)/kernel.img $(K7)
	rm -rf $(RELEASE_DIR)
	$(PYTHON) scripts/mkrelease.py $(RELEASE_DIR) --version $(VERSION) --commit $$(git rev-parse HEAD) \
	    --file $(BUILD)/kernel.img:/kernel.img $(if $(K7),--file $(K7):/kernel7.img) \
	    --file boot/ca.pem:/bm/ca.pem --pub $(RELEASE_PUB) $(RELEASE_FLAGS)
	$(if $(RGB30_KERNEL),$(PYTHON) scripts/mkrelease.py $(RELEASE_DIR) --manifest manifest-rgb30 \
	    --version $(VERSION) --commit $$(git rev-parse HEAD) --file $(RGB30_KERNEL):/kernel8.img \
	    --file boot/ca.pem:/bm/ca.pem --pub $(RELEASE_PUB) $(RELEASE_FLAGS))

# Copies what make sdcard prepared onto a mounted SD card (SD=/mnt/d by
# default): the kernels, boot files, config.txt, cartridges and the chip
# firmware in bm/. Settings and saves (bm/config.txt, bm/save) are
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
# test-nano8 stays a target to run by hand; it is not in the CI list
HOST_TESTS := test-bm test-res test-compat test-lint test-usb test-fat test-fat-queue test-audio test-music test-riff test-sound test-net test-http test-https test-img3d \
      test-catalog test-github test-lan test-controls test-keymap test-gamekit test-game-trace test-gameapi test-screen-modes test-anim-ease test-write test-shot test-reserved test-spec test-consistency test-profile test-online test-bmnet test-loading \
      test-release test-smp test-qpu test-qpurun test-qpucheck test-etc1 test-gpu3d test-gpu3d-opt test-bench3d test-impostor test-cel test-queue2d test-b3d test-v3d test-ai test-predict test-padtype \
	test-studio test-prompts test-menu-bg test-docs test-api test-webp test-web test-web-bmm test-webapi test-crumbs test-monitor-cmd test-vid $(if $(K7),test-hyp)
# what the QEMU tests read besides `all` (made by the PC's tests too)
QEMU_DEPS := $(BUILD)/host/meshview $(BUILD)/carts/herocart.bm $(BUILD)/carts/mapkit.bm $(BUILD)/carts/testkit.bm
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

$(BUILD)/host/test_fat: tests/fs/test_fat.c src/fs/fat.c src/fs/fat.h src/drivers/sd.h src/kernel/sdmap_core.c src/kernel/sdmap.h src/bm/project.c src/bm/project.h src/bm/lint.c src/bm/lint.h src/bm/lint_table.h src/bm/format.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/fs/test_fat.c src/fs/fat.c src/kernel/sdmap_core.c src/bm/project.c src/bm/lint.c src/bm/format.c src/lib/crc32.c

# the steps of a queued file_write: sectors between two ticks of a 2048 KB write
test-fat-queue: $(BUILD)/host/test_fat_queue
	$(PYTHON) tests/fs/run_queue.py $<

$(BUILD)/host/test_fat_queue: tests/fs/test_fat_queue.c src/fs/fat.c src/fs/fat.h src/drivers/sd.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/fs/test_fat_queue.c src/fs/fat.c

# Network console on lwIP's loopback interface (the WiFi chip is not in QEMU),
# then lwIP on the Pi 1 B's Ethernet with a simulated LAN9512 and a DHCP peer
test-net: $(BUILD)/host/test_netcon $(BUILD)/host/test_ethnet $(BUILD)/host/test_config
	$(BUILD)/host/test_netcon
	$(BUILD)/host/test_netcon menu
	$(BUILD)/host/test_netcon gone
	$(BUILD)/host/test_ethnet
	$(BUILD)/host/test_config
	$(PYTHON) tests/net/check_bm_net_config.py

# bm/config.txt (src/kernel/config.c): the file, and the settings from the PC
# (netxfer's C, bm_net.py --config, easy_install.sh)
$(BUILD)/host/test_config: tests/kernel/test_config.c src/kernel/config.c src/kernel/config.h src/lib/printf.c src/fs/sdlock.c src/fs/sdlock.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -o $@ tests/kernel/test_config.c src/kernel/config.c src/lib/printf.c src/fs/sdlock.c

$(BUILD)/host/test_ethnet: tests/net/test_ethnet.c src/net/net.c src/net/net.h src/net/cartnet.c src/net/cartnet.h \
                           src/kernel/fiber.h src/usb/smsc95xx.c src/usb/smsc95xx.h tests/usb/lan9512_sim.c \
                           tests/usb/lan9512_sim.h $(LWIP_SRCS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DBM_HOST_TEST -Isrc -Isrc/net -Ithird_party/lwip/src/include -o $@ \
		tests/net/test_ethnet.c src/net/net.c src/net/cartnet.c src/usb/smsc95xx.c tests/usb/lan9512_sim.c $(LWIP_SRCS)

$(BUILD)/host/test_netcon: tests/net/test_netcon.c src/net/netcon.c src/net/netxfer.c src/net/stream.c src/net/*.h src/lib/crc32.c \
                          src/kernel/fiber.h src/kernel/notice.h $(LWIP_SRCS)
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
# the games that go to the Market as .b16 (the optimized release, if there is a rule);
# Overbit has a .bm too (the Pi's), Yharnam and Titan Clash EX only their .b16
# (MARKET_NO_BM: no .bm in the Market); the last one is not in GAMES (no .bm for the
# Pi's card): MARKET_ONLY. The tools are in the kernels, not here.
MARKET_B16 := overbit yharnam titanex editor
MARKET_NO_BM := yharnam titanex
MARKET_ONLY := titanex
# The Market has its own list of games: GAMES minus MARKET_APPS_OF_GAMES.
# typing, write and bench3d are apps: they go to the apps/ folder of the Market.
MARKET_APPS_OF_GAMES := typing write bench3d
MARKET_GAMES := $(filter-out $(MARKET_APPS_OF_GAMES),$(GAMES))
# The apps (the bm editors): each one is a built-in cartridge, built as
# build/<id>.bm and seeded in the apps/ folder of the Market. The RGB30 gets
# the SDK as .b16 (carts/sdk_b16, its own sources) under the id "editor".
MARKET_APPS := editor code pixel studio animator mesh sound
MARKET_APP_CARTS := $(patsubst %,$(BUILD)/%.bm,$(MARKET_APPS))
$(BUILD)/carts/editor.b16: $(BUILD)/carts/sdk_b16.bm
	$(PYTHON) scripts/mkbm.py --from $< --b16 -o $@
market-seed: $(GAME_CARTS) $(MARKET_APP_CARTS) $(patsubst %,$(BUILD)/carts/%.b16,$(MARKET_B16))
	@test -d $(MARKET) || { echo "MARKET=$(MARKET): clone f-accomando/bm-market there first"; exit 1; }
	mkdir -p $(MARKET)/.github/workflows $(MARKET)/games $(MARKET)/apps
	cp market/README.md market/.gitignore $(MARKET)/
	cp market/.github/workflows/market.yml $(MARKET)/.github/workflows/
	for g in $(MARKET_GAMES) $(MARKET_ONLY); do 		case " $(MARKET_NO_BM) " in *" $$g "*) ;; *) $(PYTHON) scripts/mkmarket.py $(MARKET)/games --apps $(MARKET)/apps --add $(BUILD)/carts/$$g.bm --id $$g 			--version $(MARKET_VERSION) --about-file market/about.txt || exit 1;; esac; 		case " $(MARKET_B16) " in *" $$g "*) $(PYTHON) scripts/mkmarket.py $(MARKET)/games --apps $(MARKET)/apps --add $(BUILD)/carts/$$g.b16 --id $$g 			--version $(MARKET_VERSION) --about-file market/about.txt || exit 1;; esac; 	done
	for a in $(MARKET_APPS_OF_GAMES); do 		$(PYTHON) scripts/mkmarket.py $(MARKET)/games --apps $(MARKET)/apps --add $(BUILD)/carts/$$a.bm --id $$a --kind app 			--version $(MARKET_VERSION) --about-file market/about.txt || exit 1; 	done
	for a in $(MARKET_APPS); do 		$(PYTHON) scripts/mkmarket.py $(MARKET)/games --apps $(MARKET)/apps --add $(BUILD)/$$a.bm --id $$a --kind app 			--version $(MARKET_VERSION) --about-file market/about.txt || exit 1; 	done
	$(PYTHON) scripts/mkmarket.py $(MARKET)/games --apps $(MARKET)/apps --add $(BUILD)/carts/editor.b16 --id editor --kind app 		--version $(MARKET_VERSION) --about-file market/about.txt
	$(PYTHON) scripts/mkmarket.py $(MARKET)/games --apps $(MARKET)/apps --check
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

# the WebP decoder (src/bm/webp.c): lossy and lossless pictures against libwebp's decode, then truncated and
# damaged files, under the address and undefined-behaviour sanitizers
test-webp: $(BUILD)/host/test_webp
	$(PYTHON) tests/webp/run_webp_test.py $(BUILD)/host/test_webp

$(BUILD)/host/test_webp: tests/webp/test_webp.c src/bm/webp.c src/bm/webp.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=c11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -Isrc/bm -o $@ tests/webp/test_webp.c src/bm/webp.c

# the web API for cartridges (web_get, image_load, file_write): the rules (src/bm/webpolicy.c), the request in a
# fiber (webget.c, ucontext fibers here, plain sockets for the transport) and the picture decoders
# (webimg.c) against a local server
test-webapi: $(BUILD)/host/test_webget
	$(PYTHON) tests/web/run_webget_test.py $(BUILD)/host/test_webget

# file_write of a .bmm (docs/api/system.md): the model goes to /bmm/NAME.bmm after the kernel's own check of the
# bytes; broken files, other names and the .bm/.bme/.b16 rule on the host card. The committed models of
# web/models: web/models/build.py --check (the same bytes as the tools make) and the BMRES validator (bmres.py)
$(BUILD)/web-bmm-test.bm: tests/web/bmm_cart.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "web bmm test" --author tests
test-web-bmm: $(BUILD)/host/bmhost-bin $(BUILD)/web-bmm-test.bm tests/web/run_bmm_test.py tests/web/test_models.py \
              $(wildcard web/models/*)
	$(PYTHON) tests/web/test_models.py
	$(PYTHON) tests/web/run_bmm_test.py $(BUILD)/host/bmhost-bin $(BUILD)/web-bmm-test.bm $(BUILD)/web-bmm

$(BUILD)/host/test_webget: tests/web/test_webget.c tests/web/run_webget_test.py src/bm/webget.c src/bm/webget.h src/bm/webfile.c src/bm/webfile.h src/fs/sdlock.c src/fs/sdlock.h src/bm/webpolicy.c                            src/bm/webpolicy.h src/bm/webimg.c src/bm/webimg.h src/bm/jpeg.c src/bm/png.c src/bm/webp.c                            src/net/http.c src/net/http.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -DBM_HOST_TEST 		-DHTTP_USER_AGENT='"test"' -Isrc -Isrc/bm -Isrc/net -Ithird_party/lua -o $@ tests/web/test_webget.c src/bm/webget.c src/bm/webfile.c src/fs/sdlock.c src/bm/webpolicy.c 		src/bm/webimg.c src/bm/jpeg.c src/bm/png.c src/bm/webp.c src/net/http.c -lm

# the .bmv reader (src/bm/vid.c), jpeg_decode565 and scripts/mkvid.py
test-vid: $(BUILD)/host/test_vid scripts/mkvid.py tests/video/run_vid_test.py
	$(PYTHON) tests/video/run_vid_test.py $(BUILD)/host/test_vid $(BUILD)/vid

$(BUILD)/host/test_vid: tests/video/test_vid.c tests/video/vidframes.c tests/video/vidframes.h src/bm/vid.c src/bm/jpeg.c src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -Isrc/bm -Itests/video -o $@ tests/video/test_vid.c tests/video/vidframes.c src/bm/vid.c src/bm/jpeg.c -lm

test-http: $(BUILD)/host/test_http
	$(PYTHON) tests/net/run_http_test.py $(BUILD)/host/test_http

$(BUILD)/host/test_http: tests/net/test_http.c src/net/http.c src/net/http.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -DHTTP_USER_AGENT='"test"' -o $@ tests/net/test_http.c src/net/http.c

test-audio: $(BUILD)/host/test_audio $(BUILD)/host/bmrender tests/audio/test_bmaudio.py scripts/bmaudio.py
	$<
	$(PYTHON) tests/audio/test_bmaudio.py $(BUILD)/host/bmrender $(BUILD)/audio

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

$(BUILD)/host/test_audio: tests/audio/test_audio.c src/audio/synth.c src/audio/player.c src/audio/presets.c \
                          src/audio/iec958.c src/audio/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/audio/test_audio.c src/audio/synth.c src/audio/player.c \
	    src/audio/presets.c src/audio/iec958.c -lm

# A song (or SFX=n) of a sound bank as a WAV file, made on the PC by the
# console's synthesizer: make wav BANK=carts/sound/demo.json SONG=0
# BITS=24 (or 32, f32: IEEE floats) for a deeper file than the 16 bits
BANK ?= carts/sound/demo.json
SONG ?= 0
wav: $(BUILD)/host/bmrender
	$(PYTHON) scripts/bmaudio.py pack $(BANK) -o $(BUILD)/wav.bmau
	$< $(if $(BITS),--bits $(BITS)) $(BUILD)/wav.bmau $(BUILD)/$(if $(SFX),sfx$(SFX),song$(SONG)).wav \
	    $(if $(SFX),sfx $(SFX),song $(SONG)) $(SECONDS)

$(BUILD)/host/bmrender: tests/audio/render.c tests/audio/wavout.h src/audio/synth.c src/audio/player.c src/audio/*.h
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
MESHCAP_SRCS := tests/bm/test_meshcap.c src/bm/meshcap.c src/bm/format.c src/bm/r3d.c src/bm/gfx16.c src/lib/crc32.c
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
# bm_res_build: the file the console's res_write makes in /bmm, byte for byte as scripts/bmres.py
$(BUILD)/host/test_resbuild: tests/bm/test_resbuild.c src/bm/format.c src/lib/crc32.c src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/bm/test_resbuild.c src/bm/format.c src/lib/crc32.c -lm

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

# the 3D map engine (map3d.c, AGY-01)
MAP3D_TEST_SRCS := src/bm/map3d.c src/bm/r3d.c src/bm/gfx16.c
$(BUILD)/host/test_map3d: tests/bm/test_map3d.c $(MAP3D_TEST_SRCS) $(BMHOST_LUA) src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc/bm -Isrc -Ithird_party/lua -o $@ tests/bm/test_map3d.c $(MAP3D_TEST_SRCS) $(BMHOST_LUA) -lm

test-map3d: $(BUILD)/host/test_map3d
	$(BUILD)/host/test_map3d

test-bm: $(BUILD)/host/test_bm $(BUILD)/carts/mapkit.bm $(BUILD)/host/test_meshcap $(BUILD)/carts/astrowing.bm \
         $(BUILD)/carts/kitchen.bm $(BUILD)/meshcap-test.bm \
         $(BUILD)/host/test_decimate $(BUILD)/carts/testkit.bm $(BUILD)/host/libbmdecimate.so \
         $(BUILD)/host/test_glb $(BUILD)/host/test_cutout $(BUILD)/host/libbmcutout.so $(BUILD)/host/test_tokens \
         $(BUILD)/host/test_resbuild
	$< $(BUILD)/carts/mapkit.bm
	$(BUILD)/host/test_tokens carts/editor/main.lua carts/pong/main.lua
	$(BUILD)/host/test_decimate $(BUILD)/carts/testkit.bm $(BUILD)/carts/kitchen.bm
	$(PYTHON) tests/bm/run_glb_test.py $(BUILD)/host/test_glb $(BUILD)/glb
	$(PYTHON) tests/bm/run_resbuild_test.py $(BUILD)/host/test_resbuild $(BUILD)/resbuild
	$(PYTHON) tests/bm/run_cutout_test.py $(BUILD)/host/test_cutout $(BUILD)/cutout
	$(PYTHON) tools/cutout2mesh.py $(BUILD)/cutout/lolli_alpha.png -o $(BUILD)/cutout/tool.bm
	$(PYTHON) tools/cutout2mesh.py $(BUILD)/cutout/lolli_white.png -o $(BUILD)/cutout/tool.bm --lathe --name vase
	$(PYTHON) tools/bmreduce.py $(BUILD)/carts/testkit.bm --ratio 0.5 -o $(BUILD)/testkit-half.bm
	$(BUILD)/host/test_meshcap src/bm/runtime.c \
	    $(BUILD)/meshcap-test.bm '!stop here,wheel:1,crate:12,cars1_body:1,gem:2' \
	    $(BUILD)/carts/astrowing.bm ship:32,dart,tower,gate,ring:120,laser,bolt,debris,debris2,mark,core,core_hot,turret \
	    $(BUILD)/carts/kitchen.bm chef_classic1_body,chef1_body,plate,dplate
	$(PYTHON) tests/bm/test_type.py

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

# The menu background, made a band of rows per frame (src/kernel/menu_bg.c)
$(BUILD)/host/test_menu_bg: tests/ui/test_menu_bg.c src/kernel/menu_bg.c src/kernel/menu_bg.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/ui/test_menu_bg.c src/kernel/menu_bg.c -lm

test-menu-bg: $(BUILD)/host/test_menu_bg
	$<

# The crash record (src/kernel/crumbs.c): the old record is not written before crumbs_boot()
test-crumbs: $(BUILD)/host/test_crumbs $(BUILD)/host/test_wdguard
	$(BUILD)/host/test_crumbs
	$(BUILD)/host/test_wdguard

# The Monitor's command core (src/kernel/monitor_cmd.c): tokenizer, dispatcher, help, Tab, editor, history
test-monitor-cmd: $(BUILD)/host/test_monitor_cmd
	$(BUILD)/host/test_monitor_cmd

$(BUILD)/host/test_monitor_cmd: tests/kernel/test_monitor_cmd.c src/kernel/monitor_cmd.c src/kernel/monitor_cmd.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -o $@ tests/kernel/test_monitor_cmd.c src/kernel/monitor_cmd.c

# The freeze guard: the timer IRQ pets the watchdog only while the main code makes progress (SYS-2)
$(BUILD)/host/test_wdguard: tests/kernel/test_wdguard.c src/drivers/watchdog.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -o $@ tests/kernel/test_wdguard.c

$(BUILD)/host/test_crumbs: tests/kernel/test_crumbs.c src/kernel/crumbs.c src/kernel/crumbs.h src/lib/printf.c src/lib/printf.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -Wall -Wextra -Isrc -o $@ tests/kernel/test_crumbs.c src/lib/printf.c

# The API list of the code editor: api.tsv is made from docs/api, the lint and
# the generator have unit tests
test-api:
	$(PYTHON) scripts/mkapi.py --check
	$(PYTHON) tests/tools/test_api_lint.py
	$(PYTHON) tests/tools/test_mkapi.py

test-docs:
	$(PYTHON) scripts/docs_audit.py score
	$(PYTHON) -m unittest tests.tools.test_claude_glossary
	$(PYTHON) -m unittest discover -s tests/tools -p "test_*.py"

# The assistant (M30): C features and network against the Python reference,
# answers to the held-out questions, sprite generator
AI_SRCS := src/ai/assist.c src/ai/nn.c src/ai/text.c src/ai/sprite.c src/ai/mesh.c src/ai/mesh_chars.c src/ai/mesh_skin.c src/ai/mesh_script.c \
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
test-studio: $(BUILD)/host/test_bm $(BUILD)/carts/mapkit.bm $(BUILD)/host/luahost $(BUILD)/carts/testkit.bm \
             $(BUILD)/carts/astrowing.bm $(BUILD)/host/test_meshcap
	$(BUILD)/host/luahost tests/studio/bmui_host.lua .
	$(BUILD)/host/luahost tests/studio/layout_host.lua .
	$(BUILD)/host/luahost tests/studio/code_templates_host.lua .
	$(BUILD)/host/luahost tests/studio/write_host.lua .
	$(BUILD)/host/luahost tests/studio/write_pdf_host.lua .
	$(BUILD)/host/luahost tests/studio/import_host.lua .
	rm -rf $(BUILD)/studio3d-sd && mkdir -p $(BUILD)/studio3d-sd/carts $(BUILD)/studio3d-sd/bme
	cp $(BUILD)/carts/testkit.bm $(BUILD)/studio3d-sd/carts/
	$(BUILD)/host/luahost tests/studio/tools3d_host.lua . $(BUILD)/studio3d-sd
	rm -rf $(BUILD)/mesh-sd && mkdir -p $(BUILD)/mesh-sd/carts $(BUILD)/mesh-sd/bme
	cp $(BUILD)/carts/testkit.bm $(BUILD)/carts/astrowing.bm $(BUILD)/mesh-sd/carts/
	$(BUILD)/host/luahost tests/studio/mesh_host.lua . $(BUILD)/mesh-sd
	$(BUILD)/host/test_meshcap src/bm/runtime.c $(BUILD)/mesh-sd/bme/astrowin.bme ship \
	    $(BUILD)/mesh-sd/bme/testkit.bme "" $(BUILD)/mesh-sd/bme/meshcopy.bme ""
	$(PYTHON) scripts/bmmesh.py $(BUILD)/mesh-sd/bme/astrowin.bme $(BUILD)/mesh-sd/bme/testkit.bme >/dev/null
	rm -rf $(BUILD)/sdk-sd && mkdir -p $(BUILD)/sdk-sd/carts $(BUILD)/sdk-sd/bme
	cp $(BUILD)/carts/testkit.bm $(BUILD)/sdk-sd/carts/
	$(BUILD)/host/luahost tests/studio/sdk_host.lua . $(BUILD)/sdk-sd
	rm -rf $(BUILD)/pixel-sd && mkdir -p $(BUILD)/pixel-sd/carts $(BUILD)/pixel-sd/bme
	cp $(BUILD)/carts/testkit.bm $(BUILD)/carts/mapkit.bm $(BUILD)/pixel-sd/carts/
	$(BUILD)/host/luahost tests/studio/pixel_host.lua . $(BUILD)/pixel-sd
	$(BUILD)/host/test_meshcap src/bm/runtime.c $(BUILD)/pixel-sd/bme/testkit.bme "" $(BUILD)/pixel-sd/bme/newspr.bme ""
	rm -rf $(BUILD)/humanoid-sd && mkdir -p $(BUILD)/humanoid-sd/carts $(BUILD)/humanoid-sd/bme
	$(BUILD)/host/luahost tests/studio/humanoid_host.lua . $(BUILD)/humanoid-sd
	$(BUILD)/host/test_meshcap src/bm/runtime.c $(BUILD)/humanoid-sd/bme/humanoid.bme ""
	$(PYTHON) tests/studio/check_files.py $(BUILD)/studio3d-sd $(BUILD)/mesh-sd $(BUILD)/pixel-sd $(BUILD)/carts/testkit.bm $(BUILD)/humanoid-sd
	$(PYTHON) tests/studio/anim_figure.py $(BUILD)/anim-figure.bm
	$(BUILD)/host/test_bm $(BUILD)/carts/mapkit.bm $(BUILD)/anim-figure.bm $(BUILD)/studio3d-sd/bme/blocks.bme

# bm SDK (RGB30), the .b16 edition, on the PC: its layout, every frame inside
# 360x360 on the 8x16 grid, the RGB30's pad on every page and action, the saves,
# build and try of the RGB30's kernel tool, none of the Pi's other programs. Not in
# HOST_TESTS (the Pi's): run it when the edition changes.
test-sdk-b16: $(BUILD)/host/luahost $(BUILD)/sdk_b16/main.lua $(BUILD)/carts/testkit.bm
	rm -rf $(BUILD)/sdk-b16-sd && mkdir -p $(BUILD)/sdk-b16-sd/carts $(BUILD)/sdk-b16-sd/bme $(BUILD)/sdk-b16-sd/bm
	cp $(BUILD)/carts/testkit.bm $(BUILD)/sdk-b16-sd/bm/testkit.b16
	$(BUILD)/host/luahost tests/studio/sdk_b16_host.lua . $(BUILD)/sdk-b16-sd $(BUILD)/sdk_b16/main.lua

# The showreel at the top of the README (docs/showreel.gif and .mp4): the
# walker of the test kit in a new cartridge, the map in the SDK, the
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
                          src/bm/gfx16.c src/bm/*.h src/gpu/etc1.c src/gpu/etc1.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free \
	    -c src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Itests/gpu -o $@ tests/gpu/test_gpu3d.c tests/gpu/v3d_emu.c \
	    src/gpu/v3d_cl.c src/gpu/etc1.c src/bm/r3d.c src/bm/gfx16.c $@-gpu3d.o -lm

# ETC1 blocks (M39, bm3d 7.3): the decoder against hand-made blocks, the
# encoder's error and its bits always the same
$(BUILD)/host/test_etc1: tests/gpu/test_etc1.c src/gpu/etc1.c src/gpu/etc1.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -o $@ tests/gpu/test_etc1.c src/gpu/etc1.c -lm

test-etc1: $(BUILD)/host/test_etc1
	$<

# Impostors (far small meshes as one quad, off by default): off the picture
# of before, on fewer triangles and a bounded difference, the same choice on
# the ARM and the GPU (V3D emulator)
$(BUILD)/host/test_impostor: tests/bm/test_impostor.c tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h src/gpu/gpu3d.c                              src/gpu/gpu3d.h src/gpu/v3d_cl.c src/gpu/v3d.h src/gpu/shaders.h src/bm/r3d.c                              src/bm/gfx16.c src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free 	    -c src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Isrc -Itests/gpu -o $@ tests/bm/test_impostor.c tests/gpu/v3d_emu.c 	    src/gpu/v3d_cl.c src/gpu/etc1.c src/bm/r3d.c src/bm/gfx16.c $@-gpu3d.o -lm

test-impostor: $(BUILD)/host/test_impostor
	$<

# The heroes of carts/herocart through the vertex shader's mesh path (V3D emulator)
$(BUILD)/host/test_herocart_mesh: tests/bm/test_herocart_mesh.c tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h src/gpu/gpu3d.c \
                                  src/gpu/gpu3d.h src/gpu/v3d_cl.c src/gpu/v3d.h src/gpu/shaders.h src/bm/r3d.c \
                                  src/bm/gfx16.c src/bm/format.c src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free \
	    -c src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Isrc -Itests/gpu -o $@ tests/bm/test_herocart_mesh.c tests/gpu/v3d_emu.c \
	    src/gpu/v3d_cl.c src/gpu/etc1.c src/bm/r3d.c src/bm/gfx16.c src/bm/format.c src/lib/crc32.c $@-gpu3d.o -lm

# Cel light on the ARM raster (cel3d, draw3d flag 256, dither3d): the table, the bands on a sphere,
# off = the picture of before, the ARM dither switch
$(BUILD)/host/test_cel: tests/bm/test_cel.c src/bm/r3d.c src/bm/r3d.h src/bm/gfx16.c src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Isrc -o $@ tests/bm/test_cel.c src/bm/r3d.c src/bm/gfx16.c -lm

test-cel: $(BUILD)/host/test_cel
	$<

# The 3D Bench (src/bm/b3d.c) on the PC: the V3D emulated, quick mode,
# twice (the second run reads the first's report); pages in build/b3d
$(BUILD)/host/b3d_host: tests/bm/b3d_host.c src/bm/b3d.c src/bm/b3d.h tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h \
                        src/gpu/gpu3d.c src/gpu/gpu3d.h src/gpu/v3d_cl.c src/gpu/v3d.h src/gpu/shaders.h \
                        src/gpu/version3d.h src/gpu/etc1.c src/gpu/etc1.h src/bm/r3d.c src/bm/gfx16.c src/gfx/font8x16.c \
                        src/gfx/font6x12.c src/bm/*.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free \
	    -c src/gpu/gpu3d.c -o $@-gpu3d.o
	$(HOSTCC) -O2 -Wall -Wextra -Isrc -Itests/gpu -o $@ tests/bm/b3d_host.c src/bm/b3d.c tests/gpu/v3d_emu.c \
	    src/gpu/v3d_cl.c src/gpu/etc1.c src/bm/r3d.c src/bm/gfx16.c src/gfx/font8x16.c src/gfx/font6x12.c $@-gpu3d.o -lm

test-b3d: $(BUILD)/host/b3d_host
	$< --selftest
	rm -rf $(BUILD)/b3d-stop && mkdir -p $(BUILD)/b3d-stop
	$< $(BUILD)/b3d-stop --stop=5
	rm -rf $(BUILD)/b3d && mkdir -p $(BUILD)/b3d
	$< $(BUILD)/b3d
	$< $(BUILD)/b3d
	$(PYTHON) tests/bm/check_b3d.py $(BUILD)/b3d
	rm -rf $(BUILD)/b3d2d && mkdir -p $(BUILD)/b3d2d
	$< $(BUILD)/b3d2d --tests=map8,sprites,stack,fade,light,herotris,cel,stress,htmap
	$(PYTHON) tests/bm/check_b3d.py $(BUILD)/b3d2d --2d
	rm -rf $(BUILD)/b3dq && mkdir -p $(BUILD)/b3dq
	$< $(BUILD)/b3dq --tests=fade --profiles=ARM,QPU --qpufade
	$(PYTHON) tests/bm/check_b3d.py $(BUILD)/b3dq --qpufade

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
	EMU_T16_ALPHA=1 $< 0 1 1 0 0 0
	EMU_T16_ALPHA=2 $< 1 0 0 0 0 0
	EMU_T16_LOSSY=1 $< 1 0 0 0 0 0
	EMU_T16_LOSSY=1 EMU_T16_ALPHA=1 $< 0 1 1 0 0 0
	EMU_ETC1=1 $< 0 1 1 0 0 0
	EMU_ETC1=2 $< 1 0 0 0 0 0
	EMU_ETC1=3 $< 1 0 0 0 0 0

# QPU shaders (M33): the assembler against shaders run on a Pi, and
# src/gpu/shaders.h up to date with the sources in tools/qpuasm.py
test-qpu:
	$(PYTHON) tools/qpuasm.py -o $(BUILD)/shaders.h
	cmp $(BUILD)/shaders.h src/gpu/shaders.h
	$(PYTHON) tools/qpuprogs.py -o $(BUILD)/qpuprog.h
	cmp $(BUILD)/qpuprog.h src/gpu/qpuprog.h

# bm3d 7.x: the QPU programs (src/gpu/qpu.c) on the V3D emulator, three ways: they work,
# the V3D never answers, the V3D stores wrong data
$(BUILD)/host/test_qpurun: tests/gpu/test_qpu.c tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h src/gpu/qpu.c src/gpu/qpu.h                            src/gpu/qpuprog.h src/gpu/v3d.h src/bm/gfx16.c src/bm/gfx16.h
	mkdir -p $(dir $@)
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free 	    -c src/gpu/qpu.c -o $@-qpu.o
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free -include v3d_emu.h -c src/bm/gfx16.c -o $@-g16.o
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Isrc -Itests/gpu -o $@ tests/gpu/test_qpu.c tests/gpu/v3d_emu.c 	    src/gpu/v3d_cl.c src/gpu/etc1.c $@-g16.o $@-qpu.o -lm

test-qpurun: $(BUILD)/host/test_qpurun
	$< 0
	$< 1
	$< 2
	$< 3

# bm3d 7.x: the QPU check (src/gpu/qpucheck.c, config key gpu3d_qpu) on the V3D emulator: PASS, SKIP (no answer), SKIP (probe off)
$(BUILD)/host/test_qpucheck: tests/gpu/test_qpucheck.c tests/gpu/v3d_emu.c tests/gpu/v3d_emu.h src/gpu/qpu.c src/gpu/qpu.h src/gpu/qpucheck.c src/gpu/qpucheck.h src/gpu/qpuprog.h src/gpu/v3d.h src/bm/gfx16.c src/bm/gfx16.h
	mkdir -p $(dir $@)
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free -c src/gpu/qpu.c -o $@-qpu.o
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Isrc -Itests/gpu -Daligned_alloc=test_aligned_alloc -Dfree=test_free -c src/gpu/qpucheck.c -o $@-chk.o
	$(HOSTCC) -O2 -ffp-contract=off -Wall -Wextra -Isrc -Itests/gpu -o $@ tests/gpu/test_qpucheck.c tests/gpu/v3d_emu.c src/gpu/v3d_cl.c src/gpu/etc1.c src/bm/gfx16.c $@-qpu.o $@-chk.o -lm

test-qpucheck: $(BUILD)/host/test_qpucheck
	$< 0
	$< 1
	$< 2

HOSTCC ?= cc

clean:
	rm -rf build $(DIST)

-include $(KERNEL_OBJS:.o=.d) $(KERNEL7_OBJS:.o=.d) $(LOADER_OBJS:.o=.d)

endif   # TARGET

# Screenshots (src/bm/shot.c, shot() and the system's key; docs/SCREENSHOTS.md): a cart asks for
# them in bmhost, tests/shot/check.py reads the PNGs it finds in the SD's bm/shots
$(BUILD)/shot-test.bm: tests/shot/cart.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "shot test" --author tests
test-shot: $(BUILD)/host/bmhost-bin $(BUILD)/shot-test.bm tests/shot/check.py tests/shot/key.txt
	rm -rf $(BUILD)/shot-sd && mkdir -p $(BUILD)/shot-sd
	$< $(BUILD)/shot-test.bm --sd $(BUILD)/shot-sd --input tests/shot/key.txt --seconds 40 2>&1 | tee $(BUILD)/shot-test.log | grep "shot:"
	$(PYTHON) tests/shot/check.py $(BUILD)/shot-sd

# sheet_png (src/bm/runtime.c l_sheet_png, shot_png_rgba): PNG bytes with alpha of sheet rectangles
$(BUILD)/sheet-png.bm: tests/shot/sheet_png.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "sheet png" --author tests
test-sheet-png: $(BUILD)/host/bmhost-bin $(BUILD)/sheet-png.bm tests/shot/check_sheet_png.py
	rm -rf $(BUILD)/sheet-png-sd && mkdir -p $(BUILD)/sheet-png-sd
	$< $(BUILD)/sheet-png.bm --sd $(BUILD)/sheet-png-sd --seconds 20 > $(BUILD)/sheet-png.log 2>&1
	$(PYTHON) tests/shot/check_sheet_png.py $(BUILD)/sheet-png.log

# sheet_gif (src/bm/runtime.c l_sheet_gif, shot_gif): the animated GIF of sheet rectangles
$(BUILD)/sheet-gif.bm: tests/shot/sheet_gif.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "sheet gif" --author tests
test-sheet-gif: $(BUILD)/host/bmhost-bin $(BUILD)/sheet-gif.bm tests/shot/check_sheet_gif.py
	rm -rf $(BUILD)/sheet-gif-sd && mkdir -p $(BUILD)/sheet-gif-sd
	$< $(BUILD)/sheet-gif.bm --sd $(BUILD)/sheet-gif-sd --seconds 20 > $(BUILD)/sheet-gif.log 2>&1
	$(PYTHON) tests/shot/check_sheet_gif.py $(BUILD)/sheet-gif.log

# The PDF manuals (docs/MANUALS.md): en and it, theme A; test-manuals says if one language is behind
manuals:
	$(PYTHON) scripts/manuals.py build -o manuals/pdf
test-manuals:
	$(PYTHON) scripts/manuals.py check
.PHONY: manuals test-manuals

# A game cannot create a file named as one of bm's tools (src/bm/runtime.c reserved_tool_name)
$(BUILD)/reserved-test.bm: tests/tools/reserved.lua scripts/mkbm.py
	$(PYTHON) scripts/mkbm.py -o $@ --lua $< --title "reserved test" --author tests
test-reserved: $(BUILD)/host/bmhost-bin $(BUILD)/reserved-test.bm
	rm -rf $(BUILD)/reserved-sd && mkdir -p $(BUILD)/reserved-sd/carts
	$< $(BUILD)/reserved-test.bm --sd $(BUILD)/reserved-sd --seconds 1 2>&1 | tee $(BUILD)/reserved-test.log | grep "^reserved"
	@for n in pixel.bm SDK.BM assistan.bm code.bm Studio.bm sdk_b16.bm; do \
	    grep -q "^reserved: $$n false .*kept for one of bm's tools" $(BUILD)/reserved-test.log || { echo "$$n was not refused"; exit 1; }; done
	@for n in ok.bm pixels.bm; do grep -q "^reserved: $$n true" $(BUILD)/reserved-test.log || { echo "$$n was refused"; exit 1; }; done
	@echo "reserved: 8/8 checks passed"
.PHONY: test-reserved

# A cartridge that needs a newer revision of the specification (INFO "spec", src/bm/bm.h BM_SPEC_REV) is refused with a
# message; one that needs the kernel's revision or none runs
test-spec: $(BUILD)/host/bmhost-bin tests/spec/cart.lua scripts/mkbm.py
	@mkdir -p $(BUILD)/spec
	@printf 'name: spec test\nspec: 1\n' > $(BUILD)/spec/ok.info
	@printf 'name: spec test\nspec: 999\n' > $(BUILD)/spec/new.info
	@printf 'name: spec test\nspec: 5\n' > $(BUILD)/spec/five.info
	@printf 'name: spec test\nspec: 6\n' > $(BUILD)/spec/six.info
	@$(PYTHON) scripts/mkbm.py -o $(BUILD)/spec/five.bm --lua tests/spec/cart.lua --info $(BUILD)/spec/five.info > /dev/null
	@$(PYTHON) scripts/mkbm.py -o $(BUILD)/spec/six.bm --lua tests/spec/cart.lua --info $(BUILD)/spec/six.info > /dev/null
	@$(PYTHON) scripts/mkbm.py -o $(BUILD)/spec/ok.bm --lua tests/spec/cart.lua --info $(BUILD)/spec/ok.info > /dev/null
	@$(PYTHON) scripts/mkbm.py -o $(BUILD)/spec/none.bm --lua tests/spec/cart.lua --title none > /dev/null
	@$(PYTHON) scripts/mkbm.py -o $(BUILD)/spec/new.bm --lua tests/spec/cart.lua --info $(BUILD)/spec/new.info > /dev/null
	@$(BUILD)/host/bmhost-bin $(BUILD)/spec/ok.bm --seconds 1 2>&1 | grep -q "spec: ran" || { echo "spec 1 did not run"; exit 1; }
	@$(BUILD)/host/bmhost-bin $(BUILD)/spec/none.bm --seconds 1 2>&1 | grep -q "spec: ran" || { echo "no spec did not run"; exit 1; }
	@$(BUILD)/host/bmhost-bin $(BUILD)/spec/new.bm --seconds 1 2>&1 | grep -q "needs revision 999" || { echo "spec 999 was not refused"; exit 1; }
	@$(BUILD)/host/bmhost-bin $(BUILD)/spec/five.bm --seconds 1 2>&1 | grep -q "spec: ran" || { echo "spec 5 did not run"; exit 1; }
	@$(BUILD)/host/bmhost-bin $(BUILD)/spec/six.bm --seconds 1 2>&1 | grep -q "needs revision 6" || { echo "spec 6 was not refused"; exit 1; }
	@echo "spec: 5/5 checks passed"
# A cartridge's code is Lua text: a section that starts with the bytecode signature is refused (mode "t" in lua_load)
test-luatext: $(BUILD)/host/bmhost-bin scripts/mkbm.py
	@mkdir -p $(BUILD)/luatext
	@$(PYTHON) -c "open('$(BUILD)/luatext/code.luac','wb').write(b'\x1bLua\x55\x00\x19\x93\r\n\x1a\n junk')"
	@$(PYTHON) scripts/mkbm.py -o $(BUILD)/luatext/bytecode.bm --lua $(BUILD)/luatext/code.luac --title luac > /dev/null
	@$(BUILD)/host/bmhost-bin $(BUILD)/luatext/bytecode.bm --seconds 1 2>&1 | tee $(BUILD)/luatext/bytecode.log | grep -q "attempt to load a binary chunk" || { echo "bytecode was not refused"; cat $(BUILD)/luatext/bytecode.log; exit 1; }
	@echo "luatext: 1/1 checks passed"
.PHONY: test-luatext

# luavm_run (kernel scripts) loads text only: a binary chunk is refused (mode "t")
test-luavm-text: tests/script/luavm_text_host.c src/script/luavm.c $(LUA_SRCS)
	@mkdir -p $(BUILD)/host
	@$(HOSTCC) -O1 -w -Ithird_party/lua -Isrc -o $(BUILD)/host/luavm_text_host tests/script/luavm_text_host.c src/script/luavm.c \
	    $(filter-out third_party/lua/lua.c third_party/lua/luac.c,$(LUA_SRCS)) -lm
	@$(BUILD)/host/luavm_text_host
.PHONY: test-luavm-text

test-consistency:
	$(PYTHON) scripts/consistency.py check
.PHONY: test-spec test-consistency

# Tropic Trail video series (docs/video/PIANO.md)
-include video/3d/video3d.mk
