# Proposta aggiornamento Dev Kit (F11 + F5)

## Obiettivo
Rendere più chiaro dove va il tempo del frame, sia in gioco (F11) sia nel SDK dopo un test (F5).

## A schermo — Overlay F11 (pulito)

Due barre principali, una per CPU e una per GPU. Dentro ogni barra i segmenti colorati mostrano a cosa è dedicato il tempo.

```
60 fps    8.2 ms

CPU  [ Logic 37% | Pre-render 29% | 2D 5% | Audio 1% | idle ]   62%
GPU  [ Binning + Render 41% | idle ]                           41%
```

### Categorie proposte

**CPU:**
- Logic (update Lua, fisica, AI)
- Pre-render (setup vertici, costruzione control list sull'ARM)
- 2D / HUD
- Audio
- Idle (tempo rimanente nel frame)

**GPU:**
- Binning + Render (tempo reale misurato dalla V3D)
- Idle

### Comportamento
- Overlay semi-trasparente in basso.
- Colori: verde < 50% del budget, giallo 50-80%, rosso > 80%.
- Numeri essenziali sempre visibili: fps, ms totali, % CPU, % GPU.
- Toggle con F11 (come oggi).

## Nel SDK — Dopo F5 (più dettagliato)

Mantenere e arricchire i numeri attuali:

- fps, ms medi / max di _update + _draw
- Frame oltre 16.7 ms
- RAM max (Lua + data)
- Istruzioni del frame più pesante
- Triangoli e chi ha disegnato il 3D

**Aggiungere:**
- Breakdown medio delle categorie (Logic, Pre-render, 2D, Audio, GPU)
- Percentuali e ms per categoria
- Numero di job V3D sottomessi
- Eventuale contatore OUTOMEM del binner

## A log (opzionale, più verboso)

Ogni N frame o su richiesta (es. F11 tenuto o comando):

```
FRAME 1247 | 8.4 ms | CPU 58% GPU 35%
  logic: 3.1 ms (37%)
  pre-render: 2.4 ms (29%)
  2d: 0.4 ms
  audio: 0.1 ms
  gpu-bin: 0.8 ms
  gpu-render: 1.6 ms
  idle: 0.0 ms
  tris: 1840 | jobs: 1
```

## Implementazione suggerita

1. Aggiungere punti di misura (timestamp) attorno a:
   - Inizio/fine _update (Logic)
   - Setup vertici / costruzione CL (Pre-render)
   - Disegno 2D
   - Audio
   - Submit e completamento V3D (GPU)
2. Accumula i tempi per frame.
3. Overlay F11 legge i valori accumulati e disegna le barre.
4. Il SDK (F5) mostra le medie e i massimi.

## Note
- Le misure devono essere leggere (solo pochi timestamp per frame).
- In modalità asincrona il tempo GPU non si somma più al tempo ARM: le barre lo mostreranno correttamente.
- Compatibile con il codice attuale di F11 e con i numeri di stat().
