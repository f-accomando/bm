# bm (BareMetal) — note per chi lavora su questo repository

Kernel bare metal per Raspberry Pi Zero W (BCM2835, ARM1176JZF-S): C + assembly +
Lua 5.4 embedded. Documentazione: `README.md`, `docs/ROADMAP.md`, `docs/HARDWARE.md`.

## Build e test

- `make` → `build/kernel.img` e `build/chainloader.img` (toolchain `arm-none-eabi-gcc`).
- `make test` → test sul PC (grafica, FAT, USB, audio, rete, giochi) + test end-to-end in
  QEMU (`-M raspi0`).
- L'utente prova sul Pi reale copiando `dist/kernel.img` sulla SD (WSL, `/mnt/d`),
  senza cavo seriale: tutto ciò che deve verificare va mostrato sullo schermo.

## Nome

- Il progetto si chiama **bm** (BareMetal); cartucce `.bm`, cartella `bm/` sulla SD.
  Il vecchio nome sopravvive solo dove serve alla compatibilità (la cartella della SD e
  l'intestazione delle cartucce di prima, lette ancora; il tag di rete per i kernel
  vecchi in `tools/bm_net.py`). Il repository GitHub è `f-accomando/bm`.

## s32 (lua32): rimosso

- Decisione dell'utente (2026-09-30): bm **non esegue più le cartucce s32** di
  `f-accomando/lua32`. Nessuna cartuccia s32 (`.cart`) né l'interprete entrano nelle
  build, nel kernel o nell'immagine SD; il menu legge solo i `.bm`. Non reintrodurli
  senza una richiesta esplicita.
- Resta solo la disposizione dei registri dell'audio (il sintetizzatore e `apu()` dei
  giochi `.bm`), nata dall'APU della s32.

## GPU (M30)

- Il 3D dei giochi lo disegna la **GPU** (backend V3D `src/gpu/gpu3d.c`, sotto
  `src/bm/r3d.c`), verificata sul Pi il 2026-10-01. Il rasterizzatore software di r3d
  resta: con `gpu3d=0` in `bm/config.txt`, in QEMU (che non ha la V3D) e da solo se la
  GPU non risponde. Sul PC la GPU si prova con l'emulatore `tests/gpu/v3d_emu.c`
  (`make test-gpu3d`) e il driver con `make test-v3d`; sul Pi con il test `g` del
  monitor e le righe GPU dello stress test.
- Gli shader QPU si scrivono in `tools/qpuasm.py`, che genera `src/gpu/shaders.h`
  (`make test-qpu` controlla che sia aggiornato).
- M31 (in corso): cose della V3D non documentate o non usate da Mesa (layout T-format
  delle texture, load della pagina in un tile MSAA) le **impara la prova all'avvio** di
  `gpu3d.c` e, se non tornano, si spengono da sole; l'emulatore ne ha le varianti
  (`make test-gpu3d` le prova tutte). L'MSAA è spento di default (`gpu3d_aa=1`).

## Comunicazione con l'utente

- Riportare la **lista delle milestone** solo quando una milestone è completata per
  intero (non per i singoli passi): una lista puntata (niente tabelle, niente icone),
  una descrizione breve e lo stato di ciascuna; le milestone completate barrate
  (`~~M12 — ...~~`).
