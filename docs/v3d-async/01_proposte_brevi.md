# Proposte di miglioramento per bm3d

Breve elenco delle cose da fare e file da controllare.

## 1. Rendere v3d_run asincrono
**Cosa**: Togliere il wait sincrono. Sottomettere binning, tornare subito, avviare rendering su FLDONE, segnalare fine su FRDONE.  
**File da controllare**: `src/v3d.c` (funzione `v3d_run` e gestione registri CT0/CT1), `docs/M33-PRIMA-DOPO.md` (tempi attuali).

## 2. Triple buffering dei job
**Cosa**: 3 set di vertex buffer + control list. ARM prepara N+1 mentre GPU lavora su N.  
**File da controllare**: `src/v3d.c`, `src/r3d.c` (dove si preparano i vertici), gestione memoria in `src/`.

## 3. Gestione interrupt V3D
**Cosa**: Abilitare e gestire `FLDONE`, `FRDONE`, `OUTOMEM`.  
**File da controllare**: interrupt handler esistente (cerca IRQ / interrupt nel kernel), registri `V3D_INTCTL/ENA/DIS`.

## 4. Cache coherency
**Cosa**: Clean prima del submit, invalidate dopo se serve leggere il framebuffer.  
**File da controllare**: dove si fa `cache_clean` / `cache_invalidate` oggi (cerca in `src/`).

## 5. Gestione OUTOMEM del binner
**Cosa**: Pool iniziale + overflow su interrupt.  
**File da controllare**: `src/v3d.c` (allocazione binning pool), `docs/PRESTAZIONI.md`.

## 6. Vertex shader (futuro)
**Cosa**: Spostare parte del transform sulla GPU.  
**File da controllare**: roadmap in `docs/`, tentativi precedenti menzionati in `M33-PRIMA-DOPO.md`.

## 7. Impostor / billboard per oggetti lontani
**Cosa**: Sostituire mesh piccole con 1-2 triangoli grandi.  
**File da controllare**: `bm Animator`, strumenti di bake esistenti, `docs/STRESS.md`.
