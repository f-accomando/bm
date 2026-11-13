/* The hot code of the SNES core in one block (src/bm/snes_hot.ld, included by linker.ld inside .text).
 * The ARM1176 has a 16 KB instruction cache (4 ways, 32-byte lines). The CPU loop, the SPC700, the S-DSP and the PPU run in turn in every line and
 * evict each other, so where a function sits modulo 4 KB decides which ones fight for the same cache sets. The order and the small gaps below were
 * found by tests/snes/icache_layout.c on traces of SMW and SMW2 (see docs/progress/snes-cpu.md, "I-cache work"): do not reorder by hand and do not
 * add a line without a new search. After a change of the size of any of these sections run scripts/icache_sim.py again (the search is stale then).
 * One line per section: ld puts the sections of one pattern in the order of the files, not in the order of the list. */
/* made by tests/snes/icache_layout.c: the order and the shifts that gave the fewest misses on the traces (71 sections) */
. = ALIGN(4096);
__snes_hot_start = .;
. = . + 184;
*snes_gsu.c.o(.text.pix_flush)
. = . + 224;
*snes_bus.c.o(.text.raw_read)
. = . + 248;
*snes_cpu.c.o(.text.snes_cpu_fast)
*snes_bus.c.o(.text.gsu_map_ptr)
. = . + 36;
*snes_gsu.c.o(.text.gsu_run)
. = . + 180;
*snes_cpu.c.o(.text.snes_cpu_sr)
*snes_spc.c.o(.text.adc)
. = . + 184;
*snes_bus.c.o(.text.gsu_tab_update)
. = . + 120;
*snes_bus.c.o(.text.snes_read)
. = . + 220;
*snes_bus.c.o(.text.snes_ppu_write)
*snes_cpu_arm.S.o(.text.snes_cpu_hot)
*snes_ppu.c.o(.text.win_row)
*snes_cpu.c.o(.text.snes_cpu_cc)
*snes_bus.c.o(.text.run_dma)
*snes_bus.c.o(.text.snes_frame_break)
*snes_ppu_arm.S.o(.text)
*snes_ppu.c.o(.text.ppu_line)
*libc.a:libc_a-memset.o(.text)
. = . + 12;
*snes_spc.c.o(.text.timers)
*snes_bus.c.o(.text.map_ptr)
*libc.a:libc_a-memcpy-stub.o(.text)
*snes_bus.c.o(.text.spc_sync_pos)
. = . + 4;
*snes_bus.c.o(.text.snes_hdma_line)
*snes_bus.c.o(.text.snes_write)
. = . + 116;
*snes_gsu.c.o(.text.gsu_go)
*snes_bus.c.o(.text.hdma_init_ch.part.0)
. = . + 104;
*snes_spc.c.o(.text.f8)
*snes_spc.c.o(.text.rd)
. = . + 4;
*snes_bus.c.o(.text.vprefetch)
. = . + 40;
*snes_gsu.c.o(.text.gsu_write)
. = . + 48;
*snes_gsu.c.o(.text.gsu_c_getb)
*snes_dsp.c.o(.text.dsp_write)
. = . + 36;
*snes_spc.c.o(.text.pop)
*snes_spc.c.o(.text.spc_port_write)
*snes_dsp.c.o(.text.snes_audio_mix)
*snes_gsu.c.o(.text.gsu_read)
*snes_bus.c.o(.text.irq_time)
. = . + 20;
*snes_ppu.c.o(.text.build_list)
. = . + 20;
*snes_bus.c.o(.text.cpu_reg_read)
*snes_spc.c.o(.text.unary)
*snes_spc.c.o(.text.step_c)
*snes_spc.c.o(.text.f16)
*snes_spc.c.o(.text.nz)
*snes_spc.c.o(.text.push)
*snes_dsp.c.o(.text.voice_run)
. = . + 136;
*snes_bus.c.o(.text.gsu_sync_pos)
. = . + 44;
*snes_spc.c.o(.text.cmp)
*snes_spc.c.o(.text.spc_run)
*snes_ppu.c.o(.text.ppu_vram_dirty)
*snes_ppu.c.o(.text.draw_bg)
. = . + 140;
*snes_cpu.c.o(.text.snes_cpu_sw)
. = . + 60;
*snes_ppu.c.o(.text.lut_update)
. = . + 32;
*snes_dsp.c.o(.text.dsp_tick)
. = . + 116;
*snes_gsu.c.o(.text.gsu_fetch_slow)
. = . + 20;
*snes_spc.c.o(.text.dp16)
. = . + 60;
*snes_ppu.c.o(.text.window_mask)
*snes_ppu.c.o(.text.ppu_cgram_write)
. = . + 68;
*snes_ppu.c.o(.text.compose)
. = . + 160;
*snes_gsu.c.o(.text.gsu_c_stop)
. = . + 12;
*snes_gsu.c.o(.text.gsu_ram_mask)
. = . + 144;
*snes_bus.c.o(.text.vwrite)
. = . + 164;
*snes_bus.c.o(.text.snes_ppu_read)
. = . + 56;
*snes_gsu.c.o(.text.rom_buffer)
. = . + 140;
*snes_gsu.c.o(.text.gsu_c_romb)
. = . + 212;
*snes_spc.c.o(.text.wr)
*snes_spc.c.o(.text.alu)
. = . + 224;
*snes_bus.c.o(.text.raw_write)
. = . + 84;
*snes_cpu.c.o(.text.snes_cpu_attn)
. = . + 128;
*snes_spc.c.o(.text.rel)
. = . + 200;
*snes_cpu.c.o(.text.cpu_run)
*snes_gsu_arm.S.o(.text.snes_gsu_hot)
. = ALIGN(32);
__snes_hot_end = .;
