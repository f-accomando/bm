/* The hot code of the SNES core in one block (core/src/bm/snes_hot.ld, included by core/linker.ld inside .text).
 * The ARM1176 has a 16 KB instruction cache (4 ways, 32-byte lines). The CPU loop, the SPC700, the S-DSP and the PPU run in turn in every line and
 * evict each other, so where a function sits modulo 4 KB decides which ones fight for the same cache sets. The order and the small gaps below were
 * found by tests/snes/icache_layout.c on traces of SMW and SMW2 (see docs/progress/snes-cpu.md, "I-cache work"): do not reorder by hand and do not
 * add a line without a new search. After a change of the size of any of these sections run core/scripts/icache_sim.py again (the search is stale then).
 * One line per section: ld puts the sections of one pattern in the order of the files, not in the order of the list. */
/* made by tests/snes/icache_layout.c: the order and the shifts that gave the fewest misses on the traces (67 sections) */
. = ALIGN(4096);
__snes_hot_start = .;
. = . + 160;
*snes_cpu.c.o(.text.snes_cpu_fast)
*snes_gsu.c.o(.text.gsu_c_stop)
*snes_gsu.c.o(.text.gsu_ram_mask)
*snes_spc.c.o(.text.adc)
. = . + 52;
*snes_gsu.c.o(.text.rom_buffer)
. = . + 232;
*snes_cpu.c.o(.text.cpu_run)
. = . + 12;
*snes_ppu.c.o(.text.compose)
*snes_bus.c.o(.text.raw_read)
. = . + 28;
*snes_gsu.c.o(.text.pix_flush)
. = . + 124;
*snes_cpu_arm.S.o(.text.snes_cpu_hot)
*snes_bus.c.o(.text.run_dma)
. = . + 4;
*snes_gsu.c.o(.text.gsu_read)
. = . + 80;
*snes_bus.c.o(.text.snes_frame_break)
*snes_bus.c.o(.text.hdma_init_ch.part.0)
. = . + 68;
*snes_bus.c.o(.text.snes_ppu_write)
*snes_spc.c.o(.text.alu)
*snes_bus.c.o(.text.gsu_sync_pos)
*snes_spc.c.o(.text.unary)
*snes_bus.c.o(.text.irq_time)
. = . + 128;
*libc.a:libc_a-memset.o(.text)
. = . + 80;
*snes_ppu.c.o(.text.lut_update)
*snes_spc.c.o(.text.spc_port_write)
. = . + 4;
*snes_spc.c.o(.text.f8)
. = . + 224;
*snes_bus.c.o(.text.gsu_tab_update)
*snes_bus.c.o(.text.spc_sync_pos)
. = . + 12;
*snes_dsp.c.o(.text.dsp_tick)
. = . + 64;
*snes_spc.c.o(.text.timers)
*snes_ppu.c.o(.text.window_mask)
*snes_bus.c.o(.text.snes_hdma_line)
*snes_spc.c.o(.text.f16)
*snes_dsp.c.o(.text.snes_audio_mix)
*snes_spc.c.o(.text.rel)
*snes_bus.c.o(.text.snes_write)
*snes_cpu.c.o(.text.snes_cpu_sw)
*snes_cpu.c.o(.text.snes_cpu_sr)
*snes_spc.c.o(.text.spc_run)
. = . + 20;
*snes_dsp.c.o(.text.dsp_write)
*snes_gsu.c.o(.text.gsu_fetch_slow)
*snes_gsu.c.o(.text.gsu_write)
*snes_spc.c.o(.text.wr)
*snes_bus.c.o(.text.raw_write)
*snes_bus.c.o(.text.vwrite)
*snes_spc.c.o(.text.dp16)
. = . + 4;
*snes_bus.c.o(.text.map_ptr)
*snes_dsp.c.o(.text.voice_run)
*snes_ppu.c.o(.text.draw_bg)
*snes_ppu.c.o(.text.ppu_line)
*snes_bus.c.o(.text.snes_ppu_read)
. = . + 128;
*snes_gsu_arm.S.o(.text.snes_gsu_hot)
. = . + 96;
*snes_cpu.c.o(.text.snes_cpu_cc)
. = . + 8;
*snes_gsu.c.o(.text.gsu_go)
*snes_gsu.c.o(.text.gsu_c_getb)
. = . + 40;
*snes_spc.c.o(.text.step)
. = . + 44;
*snes_bus.c.o(.text.vprefetch)
. = . + 16;
*snes_gsu.c.o(.text.gsu_c_romb)
. = . + 140;
*snes_ppu.c.o(.text.build_list)
. = . + 84;
*libc.a:libc_a-memcpy-stub.o(.text)
. = . + 120;
*snes_ppu.c.o(.text.ppu_vram_dirty)
. = . + 12;
*snes_bus.c.o(.text.cpu_reg_read)
. = . + 112;
*snes_ppu.c.o(.text.ppu_cgram_write)
. = . + 196;
*snes_ppu_arm.S.o(.text)
. = . + 24;
*snes_spc.c.o(.text.rd)
. = . + 252;
*snes_ppu.c.o(.text.win_row)
. = . + 252;
*snes_cpu.c.o(.text.snes_cpu_attn)
. = . + 104;
*snes_bus.c.o(.text.snes_read)
. = . + 112;
*snes_gsu.c.o(.text.gsu_run)
. = . + 216;
*snes_bus.c.o(.text.gsu_map_ptr)
. = ALIGN(32);
__snes_hot_end = .;
