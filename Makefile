# bm33 - bare metal console for Raspberry Pi Zero / Zero W (BCM2835)

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
QEMU    ?= qemu-system-arm

BUILD   := build
DIST    := dist
FW_DIR  := firmware

ARCH    := -mcpu=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard
CFLAGS  := $(ARCH) -std=c11 -O2 -Wall -Wextra -ffreestanding -nostdlib \
           -fno-builtin -fno-tree-loop-distribute-patterns -g
ASFLAGS := $(ARCH) -g
LDFLAGS := $(ARCH) -nostdlib -nostartfiles -T linker.ld -Wl,--gc-sections \
           -Wl,-Map=$(BUILD)/kernel.map
LDLIBS  := -lgcc

SRCS_C  := $(shell find src -name '*.c')
SRCS_S  := $(shell find src -name '*.S')
OBJS    := $(patsubst src/%,$(BUILD)/%.o,$(SRCS_S) $(SRCS_C))

.PHONY: all clean firmware sdcard qemu qemu-screenshot disasm

all: $(BUILD)/kernel.img

$(BUILD)/%.S.o: src/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/%.c.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/kernel.elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(BUILD)/kernel.img: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@echo "kernel.img: $$(stat -c %s $@) bytes"

disasm: $(BUILD)/kernel.elf
	$(OBJDUMP) -d $< > $(BUILD)/kernel.lst

firmware:
	./scripts/fetch-firmware.sh $(FW_DIR)

# Everything that goes on the FAT32 boot partition of the SD card.
sdcard: $(BUILD)/kernel.img
	@test -f $(FW_DIR)/start.elf || { echo "Run 'make firmware' first"; exit 1; }
	@mkdir -p $(DIST)
	cp $(FW_DIR)/bootcode.bin $(FW_DIR)/start.elf $(FW_DIR)/fixup.dat $(DIST)/
	cp boot/config.txt $(BUILD)/kernel.img $(DIST)/
	@echo "Copy the contents of $(DIST)/ to the root of a FAT32 SD card."

qemu: $(BUILD)/kernel.elf
	$(QEMU) -M raspi0 -kernel $<

# Headless run: boots for a few seconds and saves the screen to build/screen.png
qemu-screenshot: $(BUILD)/kernel.elf
	./scripts/qemu-screenshot.sh $< $(BUILD)/screen.png

clean:
	rm -rf $(BUILD) $(DIST)

-include $(OBJS:.o=.d)
