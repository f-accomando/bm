# Proposta overlay 3D

## Obiettivo
Aggiungere informazioni specifiche sul 3D all'overlay (F11) e ai dati del SDK, senza appesantire la visualizzazione normale.

## Overlay F11 — sezione 3D

Quando c'è rendering 3D attivo, mostrare una riga o un piccolo blocco aggiuntivo sotto le barre CPU/GPU:

```
3D  1840 tri   setup 2.4 ms   bin 0.8 ms   render 1.6 ms   GPU 41%
```

Oppure, se preferisci restare sulle barre:

- La barra GPU è già dedicata al 3D.
- Aggiungere sotto di essa:
  - Numero triangoli
  - Tempo di setup ARM (pre-render)
  - Tempo binning
  - Tempo rendering

### Informazioni utili da mostrare

| Dato | Dove | Note |
|------|------|------|
| Triangoli | Overlay + SDK | Totale sottomessi nel frame |
| Setup ARM | Overlay + SDK | Tempo di trasformazione/lighting/clipping |
| Binning | Overlay + SDK | Tempo CT0 |
| Rendering | Overlay + SDK | Tempo CT1 |
| Job attivi | SDK | Quanti job sono in volo (utile con async) |
| OUTOMEM | SDK / log | Quante volte il binner ha chiesto memoria extra |

## Comportamento

- Se non c'è 3D nel frame, la sezione 3D non appare.
- Con GPU disabilitata (`gpu3d=0`) mostra i tempi del rasterizer software.
- I numeri possono essere mediati su pochi frame per stabilità.

## Nel SDK (dopo F5)

Aggiungere al report esistente:

- Triangoli medi / max per frame
- Tempo medio di setup ARM
- Tempo medio di binning e rendering
- Percentuale di frame in cui la GPU è stata il collo di bottiglia
- Eventuali contatori di overflow del binner

## Esempio di log dettagliato

```
FRAME 1247 | 8.4 ms
  3D: 1840 tri
    setup: 2.4 ms
    binning: 0.8 ms
    rendering: 1.6 ms
    jobs in flight: 1
    outomem: 0
```

## Note di implementazione

- I tempi GPU si possono leggere dai registri o misurare tra submit e `FRDONE`.
- Il numero di triangoli è già disponibile (usato nei benchmark attuali).
- In modalità asincrona è importante distinguere "tempo GPU" da "tempo in cui l'ARM aspetta".
- L'overlay deve restare leggero: pochi numeri, aggiornati una volta per frame.
