# I driver 3D di bm: le versioni

Il 3D di bm passa da due driver: **r3d** (`src/bm/r3d.c`: scena, trasformazioni, luce,
il rasterizzatore dell'ARM) e **gpu3d** (`src/gpu/gpu3d.c` con gli shader di
`tools/qpuasm.py`: il backend della V3D). Hanno una versione sola, **bm3d X.Y**: X è il
blocco di sviluppo (una milestone), Y il passo dentro il blocco. La versione è in
`src/gpu/version3d.h` (`BM3D_VERSION`, la tabella `bm3d_versions()`).

Dove si vede: la riga di stato della GPU (test `g`, log), *Impostazioni > System > 3D
driver*, il log all'avvio di un gioco ("the 3D is drawn by the GPU as bm3d 3.0"), lo
stress test, il 3D Bench, il benchmark di Overbit e il quarto valore di `gpu3d()`.

## Le versioni

- **0.1** (fino a settembre 2026, M7–M32): solo ARM, il primo rasterizzatore di r3d (lo
  stress test di settembre: 31 sfere a 60 fps).
- **0.2** (2026-10-01, M33): solo ARM, bordi in virgola fissa, cicli delle texture
  specializzati, z azzerato con il DMA (69 sfere a 60 fps sul Pi).
- **1.0** (2026-10-01, M33): la GPU disegna i triangoli (shader NV), l'ARM li mette e li
  illumina; lo z conservato tra un lavoro e l'altro.
- **2.0** (2026-10-01, M34): pagine pulite senza load, texture in T-format, MSAA 4×, meno
  istruzioni dell'ARM per triangolo (182 sfere a 60 fps sul Pi).
- **2.1** (2026-10-03, M34): Overbit sulla GPU: facce a retino, luce RGB precalcolata e
  nebbia sugli angoli, ombre, effetti 3D.
- **3.0** (2026-10-03, M36): il vertex shader della GPU mette lo scenario (modelli spenti
  o con la luce agli angoli); la GPU taglia sul piano vicino e sulla guard band.
- **3.1** (2026-10-03, M36): il vertex shader anche per i modelli illuminati dal sole, con
  le ossa (gli eroi).
- **3.2** (2026-10-03, M36): il vertex shader anche per le ombre e il primo piano.

## Le modalità: versioni vecchie sul codice di oggi

Le impostazioni riproducono le versioni precedenti, così si confrontano sullo stesso Pi:

- 3D sull'ARM (`gpu3d=0`): **0.2**;
- GPU senza vertex shader (`gpu3d_vs=0`): **2.1** (con `gpu3d_aa=1` anche l'MSAA);
- GPU con il vertex shader per lo scenario (`gpu3d_vs=1`): **3.0**;
- GPU con il vertex shader per tutto (`gpu3d_vs=2`): **3.2**.

0.1 e 1.0 non girano più: i loro numeri sono quelli misurati sul Pi allora
(`docs/M33-PRIMA-DOPO.md`).

## La regola

Ogni blocco nuovo (una milestone del 3D) alza X, ogni passo che cambia quello che il
driver sa fare alza Y: si cambia `BM3D_VERSION`, si aggiunge una riga alla tabella di
`version3d.h` e a questa pagina, e se il passo si può spegnere, una modalità.
