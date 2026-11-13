/* The hot code of the SNES core in one block (src/bm/snes_hot.ld, included by linker.ld inside .text).
 * The ARM1176 has a 16 KB instruction cache (4 ways, 32-byte lines). The CPU loop, the SPC700, the S-DSP and the PPU run in turn in every line and
 * evict each other, so where a function sits modulo 4 KB decides which ones fight for the same cache sets. The order and the small gaps below were
 * found by tests/snes/icache_layout.c on traces of SMW and SMW2 (see docs/progress/snes-cpu.md, "I-cache work"): do not reorder by hand and do not
 * add a line without a new search. After a change of the size of any of these sections run scripts/icache_sim.py again (the search is stale then).
 * One line per section: ld puts the sections of one pattern in the order of the files, not in the order of the list. */
/* made by tests/snes/icache_layout.c: the order and the shifts that gave the fewest misses on the traces (64 sections) */
. = ALIGN(4096);
__snes_hot_start = .;
. = . + 232;
*snes_ppu.c.o(.text.ppu_cgram_write)
. = . + 52;
*snes_cpu.c.o(.text.snes_cpu_cc)
*snes_bus.c.o(.text.gsu_sync_pos)
. = . + 140;
*snes_cpu_arm.S.o(.text.snes_cpu_hot)
*snes_bus.c.o(.text.irq_time)
. = . + 12;
*snes_bus.c.o(.text.cpu_reg_read)
. = . + 64;
*snes_bus.c.o(.text.raw_read)
. = . + 88;
*snes_dsp.c.o(.text.voice_run)
*snes_gsu.c.o(.text.gsu_read)
. = . + 148;
*snes_spc.c.o(.text.adc)
*snes_bus.c.o(.text.snes_read)
. = . + 4;
*snes_gsu.c.o(.text.gsu_fetch_slow)
*snes_spc.c.o(.text.unary)
*snes_ppu.c.o(.text.build_list)
*snes_bus.c.o(.text.snes_ppu_write)
*snes_spc.c.o(.text.spc_run)
. = . + 16;
*snes_bus.c.o(.text.spc_sync_pos)
. = . + 144;
*snes_gsu.c.o(.text.gsu_ram_mask)
. = . + 20;
*snes_ppu.c.o(.text.ppu_vram_dirty)
*snes_spc.c.o(.text.rel)
*snes_bus.c.o(.text.raw_write)
*snes_gsu_arm.S.o(.text.snes_gsu_hot)
. = . + 124;
*snes_spc.c.o(.text.dp16)
. = . + 128;
*snes_spc.c.o(.text.spc_port_write)
*snes_spc.c.o(.text.rd)
*snes_bus.c.o(.text.hdma_init_ch.part.0)
*snes_dsp.c.o(.text.dsp_write)
. = . + 12;
*snes_gsu.c.o(.text.pix_flush)
. = . + 68;
*snes_bus.c.o(.text.vwrite)
*snes_bus.c.o(.text.gsu_tab_update)
*snes_bus.c.o(.text.snes_ppu_read)
. = . + 128;
*snes_bus.c.o(.text.snes_write)
*snes_bus.c.o(.text.snes_frame_break)
*snes_gsu.c.o(.text.gsu_c_stop)
. = . + 48;
*snes_dsp.c.o(.text.snes_audio_mix)
. = . + 120;
*snes_spc.c.o(.text.timers)
. = . + 52;
*snes_spc.c.o(.text.alu)
*snes_bus.c.o(.text.map_ptr)
. = . + 160;
*snes_dsp.c.o(.text.dsp_tick)
*libc.a:libc_a-memcpy-stub.o(.text)
. = . + 252;
*snes_cpu.c.o(.text.snes_cpu_sw)
. = . + 52;
*libc.a:libc_a-memset.o(.text)
*snes_gsu.c.o(.text.rom_buffer)
. = . + 32;
*snes_ppu.c.o(.text.draw_bg)
*snes_ppu.c.o(.text.ppu_line)
*snes_spc.c.o(.text.step)
*snes_ppu.c.o(.text.compose)
. = . + 136;
*snes_gsu.c.o(.text.gsu_c_romb)
. = . + 240;
*snes_gsu.c.o(.text.gsu_c_getb)
. = . + 92;
*snes_bus.c.o(.text.snes_hdma_line)
. = . + 168;
*snes_spc.c.o(.text.wr)
. = . + 12;
*snes_bus.c.o(.text.gsu_map_ptr)
. = . + 140;
*snes_bus.c.o(.text.vprefetch)
. = . + 236;
*snes_bus.c.o(.text.run_dma)
. = . + 232;
*snes_gsu.c.o(.text.gsu_run)
*snes_ppu.c.o(.text.window_mask)
. = . + 16;
*snes_gsu.c.o(.text.gsu_write)
. = . + 148;
*snes_spc.c.o(.text.f8)
. = . + 148;
*snes_cpu.c.o(.text.cpu_run)
. = . + 244;
*snes_ppu_arm.S.o(.text)
. = . + 52;
*snes_cpu.c.o(.text.snes_cpu_sr)
*snes_cpu.c.o(.text.snes_cpu_attn)
. = . + 116;
*snes_spc.c.o(.text.f16)
. = . + 64;
*snes_gsu.c.o(.text.gsu_go)
. = ALIGN(32);
__snes_hot_end = .;
