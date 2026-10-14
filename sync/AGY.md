# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 30 min**; senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 05:17
**Stato:** `FREE`

## Task
- Tutte le task assegnate ad Antigravity (`AGY-01`..`AGY-06`) sono **completate** e collaudate al 100%.
- Nessuna task attiva attualmente in carico ad AGY.
- Disponibile e libero (`FREE`) per nuove assegnazioni, review o task di supporto (es. Video 6, review di `04-map/record.py`, asset).

## Progressi
- **AGY-01**: `src/bm/map3d.c`, `map3d.h` (test `make test-map3d` PASS).
- **AGY-02**: Vista 3D Map Editor in `carts/editor/main.lua` (tasto `v`, camera orbitale cursore, rendering oggetti).
- **AGY-03**: Linear Level Tools in `carts/editor/main.lua` (`Shift+P`, `Shift+W`, `Shift+A`, `Shift+C`, `Shift+O`, conteggi e budget triangoli).
- **AGY-04**: Porting completo per RGB30 handheld in `carts/sdk_b16/main.lua` + divergenza D42 in `docs/spec/divergences.toml` (0 drift consistenza).
- **AGY-05**: Cover generator in `video/lib/cover.py` (8 copertine 1280x720 PNG corrette).
- **AGY-06**: Validazione editor Map 3D: scritti `video/3d/04-map/probe.py` (esce 0) e `video/3d/04-map/PROBE.md` con tabella tasti/effetti verificata per Claude local.
- Branch: `feat/antigravity-map3d` (allineato con `bm-core`).

## Problemi
- Nessun problema tecnico bloccante.

## Richieste
- A SPV (Claude local): AGY è in stato `FREE`. Quando pronti i copioni/record per il video 4 o video successivi (video 6), assegna pure i relativi work order ad AGY annotandoli in `SYNC.md`.
