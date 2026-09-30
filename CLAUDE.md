# bm (BareMetal) — note per chi lavora su questo repository

Kernel bare metal per Raspberry Pi Zero W (BCM2835, ARM1176JZF-S): C + assembly +
Lua 5.4 embedded. Documentazione: `README.md`, `docs/ROADMAP.md`, `docs/HARDWARE.md`.

## Build e test

- `make` → `build/kernel.img` e `build/chainloader.img` (toolchain `arm-none-eabi-gcc`).
- `make test` → conformità s32 (host) + test end-to-end in QEMU (`-M raspi0`).
- `make test-s32-arm` → conformità s32 del codice ARM1176 in `qemu-arm`.
- L'utente prova sul Pi reale copiando `dist/kernel.img` sulla SD (WSL, `/mnt/d`),
  senza cavo seriale: tutto ciò che deve verificare va mostrato sullo schermo.

## Nome

- Il progetto si chiama **bm** (BareMetal); cartucce `.bm`, cartella `bm/` sulla SD.
  Il vecchio nome sopravvive solo dove serve alla compatibilità (la cartella della SD e
  l'intestazione delle cartucce di prima, lette ancora; il tag di rete per i kernel
  vecchi in `tools/bm_net.py`) e nel file di canale di lua32 (`s32-bm33.md`), che
  appartiene a lua32. Il repository GitHub è `f-accomando/bm`.

## Compatibilità con s32 (lua32)

- bm implementa in C la macchina **s32** del progetto `f-accomando/lua32`.
  Il contratto è `spec/s32/s32-spec.md` con i vettori in `spec/s32/conformance/`,
  copiati da lua32 con `scripts/sync-s32-spec.sh <checkout di lua32>`.
  In caso di dubbio decide lua32 (s32 è la guida).
- **Canale di comunicazione con lua32: `docs/spec/s32-bm33.md` nel repository lua32**
  (copia in `spec/s32/s32-bm33.md`). È il punto di riferimento per richieste e
  chiarimenti su s32: le decisioni prese stanno lì.
- Se serve qualcosa da lua32 (una decisione, un chiarimento, un nuovo vettore):
  aggiungere la domanda nella sezione **"Aperto"** di `docs/spec/s32-bm33.md` in lua32
  (con una pull request) e **dire all'utente: "si richiede aggiornamento su
  s32-bm33.md"**. Non cambiare il comportamento di bm su punti ancora aperti.
- Dopo ogni aggiornamento di lua32: `scripts/sync-s32-spec.sh`, poi `make test-s32`.

## Comunicazione con l'utente

- Riportare la **lista delle milestone** solo quando una milestone è completata per
  intero (non per i singoli passi): una lista puntata (niente tabelle, niente icone),
  una descrizione breve e lo stato di ciascuna; le milestone completate barrate
  (`~~M12 — ...~~`).
