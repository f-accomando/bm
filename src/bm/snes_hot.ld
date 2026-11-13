/* The hot code of the SNES core in one block (src/bm/snes_hot.ld, included by linker.ld inside .text).
 * The ARM1176 has a 16 KB instruction cache (4 ways, 32-byte lines). The CPU loop, the SPC700, the S-DSP and the PPU run in turn in every line and
 * evict each other, so where a function sits modulo 4 KB decides which ones fight for the same cache sets. The order and the small gaps below were
 * found by tests/snes/icache_layout.c on traces of SMW and SMW2 (see docs/progress/snes-cpu.md, "I-cache work"): do not reorder by hand and do not
 * add a line without a new search. After a change of the size of any of these sections run scripts/icache_sim.py again (the search is stale then).
 * One line per section: ld puts the sections of one pattern in the order of the files, not in the order of the list. */
/* made by tests/snes/icache_layout.c: the order and the shifts that gave the fewest misses on the traces (70 sections) */
. = ALIGN(4096);
__snes_hot_start = .;
. = . + 192;
*snes_cpu_arm.S.o(.text.snes_cpu_hot)
*snes_gsu.c.o(.text.gsu_c_romb)
*snes_cpu.c.o(.text.snes_cpu_attn)
*snes_spc.c.o(.text.rel)
. = . + 16;
*snes_gsu.c.o(.text.rom_buffer)
*snes_cpu.c.o(.text.snes_cpu_fast)
. = . + 52;
*snes_gsu.c.o(.text.gsu_ram_mask)
*snes_spc.c.o(.text.cmp)
*snes_bus.c.o(.text.snes_read)
. = . + 216;
*snes_bus.c.o(.text.raw_read)
. = . + 8;
*snes_bus.c.o(.text.snes_ppu_write)
. = . + 100;
*snes_bus.c.o(.text.hdma_init_ch.part.0)
*snes_bus.c.o(.text.snes_ppu_read)
*snes_bus.c.o(.text.snes_hdma_line)
. = . + 32;
*snes_spc.c.o(.text.spc_run)
*snes_spc.c.o(.text.rd)
*libc.a:libc_a-memset.o(.text)
*snes_dsp.c.o(.text.dsp_tick)
*snes_bus.c.o(.text.run_dma)
*snes_bus.c.o(.text.gsu_map_ptr)
*snes_spc.c.o(.text.adc)
. = . + 124;
*snes_bus.c.o(.text.cpu_reg_read)
*snes_dsp.c.o(.text.snes_audio_mix)
*snes_dsp.c.o(.text.voice_run)
. = . + 4;
*snes_cpu.c.o(.text.cpu_run)
*snes_cpu.c.o(.text.snes_cpu_cc)
*snes_bus.c.o(.text.spc_sync_pos)
*snes_spc.c.o(.text.f8)
*snes_spc.c.o(.text.timers)
*snes_bus.c.o(.text.gsu_tab_update)
. = . + 12;
*snes_bus.c.o(.text.raw_write)
. = . + 136;
*snes_dsp.c.o(.text.dsp_write)
*snes_spc.c.o(.text.alu)
*snes_spc.c.o(.text.push)
. = . + 28;
*snes_spc.c.o(.text.dp16)
. = . + 40;
*snes_spc.c.o(.text.wr)
. = . + 200;
*snes_cpu.c.o(.text.snes_cpu_sr)
*snes_spc.c.o(.text.pop)
*snes_bus.c.o(.text.irq_time)
*snes_bus.c.o(.text.snes_frame_break)
*snes_bus.c.o(.text.snes_write)
. = . + 28;
*snes_spc.c.o(.text.nz)
. = . + 4;
*snes_ppu.c.o(.text.ppu_cgram_write)
. = . + 32;
*snes_bus.c.o(.text.vwrite)
. = . + 8;
*snes_bus.c.o(.text.vprefetch)
*snes_spc.c.o(.text.unary)
*snes_ppu.c.o(.text.build_list)
*snes_bus.c.o(.text.map_ptr)
*snes_ppu.c.o(.text.win_row)
. = . + 36;
*snes_ppu.c.o(.text.draw_bg)
. = . + 16;
*snes_ppu.c.o(.text.ppu_line)
. = . + 132;
*snes_ppu.c.o(.text.compose)
. = . + 136;
*snes_spc.c.o(.text.f16)
. = . + 160;
*snes_ppu.c.o(.text.ppu_vram_dirty)
*snes_ppu.c.o(.text.window_mask)
. = . + 4;
*snes_gsu.c.o(.text.gsu_c_stop)
. = . + 76;
*snes_gsu.c.o(.text.gsu_c_getb)
. = . + 8;
*snes_ppu_arm.S.o(.text)
. = . + 204;
*snes_gsu.c.o(.text.gsu_read)
. = . + 176;
*snes_gsu.c.o(.text.gsu_go)
. = . + 128;
*snes_spc.c.o(.text.step_c)
. = . + 68;
*snes_gsu.c.o(.text.gsu_write)
. = . + 176;
*libc.a:libc_a-memcpy-stub.o(.text)
. = . + 164;
*snes_gsu.c.o(.text.pix_flush)
. = . + 112;
*snes_spc.c.o(.text.spc_port_write)
. = . + 204;
*snes_cpu.c.o(.text.snes_cpu_sw)
. = . + 108;
*snes_bus.c.o(.text.gsu_sync_pos)
. = . + 52;
*snes_gsu.c.o(.text.gsu_fetch_slow)
. = . + 12;
*snes_gsu.c.o(.text.gsu_run)
. = . + 140;
*snes_gsu_arm.S.o(.text.snes_gsu_hot)
. = ALIGN(32);
__snes_hot_end = .;
