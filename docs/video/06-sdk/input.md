# Video 6 — automazione dell'input

Descrive, non implementa. Da realizzare in CLD-03: `video/3d/06-sdk/{record.py,merge.py,preview.lua,input-r1.txt,input-r3.txt,verify.py}`.
Libreria: `video/lib/bmvideo.py` (`Script`, `encode`, `card_cmd`, `concat_cmd`), come `video/03-sound/record.py`.

> **BLOCCATO** per la registrazione finale finche' il video 4 non e' registrato (`docs/video/STATUS.md`): servono `chain/04-map.bm` (editor mappa 3D, `SYNC.md` AGY-01..03) e le API `map3d_*`.
> **Prove senza l'editor mappa:** una mappa fatta a script (`scripts/mkbm.py` o `scripts/bmres.py`, layer `floor/height/walls/objects` di `SYNC.md` §2) in un `04-map.bm` provvisorio, solo per sbloccare 6-8 nelle prove. **Mai nel video finale.**

## Il merge deterministico `start.bm` = `04-map.bm` + `05-sound.bm`

`video/3d/06-sdk/merge.py` costruisce `video/3d/06-sdk/start.bm` senza intervento a mano (stesso input, stesso file, stesso hash):

1. Legge `video/3d/chain/04-map.bm` con `scripts/bmres.py` (`read`): la base. Ha tutto quello che viene da 03 (MESH con `PICO`, `APPLE`, `CRATE`; ANIM con le 5 clip; SHEET con le texture) piu' le sezioni LAYERS (`floor`, `height`, `walls`, `objects`) e FLAGS.
2. Legge `video/3d/chain/05-sound.bm` e ne prende **solo** la sezione AUDIO (`SEC_AUDIO = 6`, magic `BMAU`).
3. `File.put(SEC_AUDIO, body)` sulla base (sostituisce o aggiunge in coda, nell'ordine del file), poi `File.put(SEC_LUA, preview.lua)`: il codice di anteprima (sotto).
4. Scrive il risultato con `bmres.write`. Non tocca nessun'altra sezione: LAYERS, FLAGS, MESH, ANIM, SHEET restano byte per byte quelle di `04-map.bm`.

Controllo del merge (nel `record.py`, prima di registrare; exit diverso da 0 se manca qualcosa):

| Sezione di `start.bm` | Deve essere |
|---|---|
| MESH, ANIM, SHEET | identiche a `04-map.bm` (hash SHA-256 uguali; e a `03-animator.bm` se la mappa non ha aggiunto tessere) |
| LAYERS, FLAGS | identiche a `04-map.bm`; i quattro nomi di layer `floor`, `height`, `walls`, `objects` presenti |
| AUDIO | identica a `05-sound.bm`; contiene i suoni `APPLE CRATE JUMP STEP SPIN HIT` e la canzone `JUNGLE` |
| LUA | `preview.lua` |

`python scripts/bmres.py list video/3d/06-sdk/start.bm` mostra le sezioni presenti (stesso strumento che usa il `verify.py` del 2D per la scheda). `[DA VERIFICARE: se la risoluzione scelta con `r` sta nelle 128 byte dell'intestazione (`File.header`) o in INFO; serve al controllo della riga "SCREEN" qui sotto]`.

## Il codice di partenza `preview.lua` (bozza)

E' il template **3D with models** (struttura `list`, `pick`, `_update`, `_draw` di `carts/editor/main.lua`, righe 568-610) adattato al livello: carica la mappa, mette Pico che corre da solo e lascia **quattro marcatori**
dove il video scrive le righe. Senza i marcatori `-- [sky]` ecc. la scena e' piatta e senza nebbia.

```lua
local pico, apple, crate, w
local lamps, apples = {}, {}
local px, pz, t = 4.5, 2, 0

function _init()
  pico, apple, crate = model("PICO"), model("APPLE"), model("CRATE")
  w = map3d_build()
  lamps, apples = map3d_objects(w, "LAMP"), map3d_objects(w, "APPLE")
  -- [shadow]
  -- [sky]
  -- [fog]
end

function _update()
  t = t + 1 / 60
  pz = pz + 3 / 60
end

function _draw()
  local py = map3d_height(w, px, pz) or 0
  cls(0xA8D0C0)
  camera3d(px, py + 2.4, pz - 5, 0, -0.32, 60)
  light3d(-0.4, 0.8, -0.5, 0.45)
  zclear()
  map3d_draw(w)
  -- [lamp]
  animate(pico, "run", t)
  -- [shadow-draw]
  draw3d(pico, px, py, pz)
  for _, o in ipairs(apples) do
    draw3d(apple, o.x, o.y + 0.6, o.z, 0, t * 2, 0, 1)
  end
end
```

Le sei righe scritte nel video (shot list, righe 17-23): `sky3d(0xFFE8B0, 0x80B8E8, 0x486038)` sotto `-- [sky]`; `fog3d(0xA8D0C0, 12, 40)` sotto `-- [fog]`;
`local l = lamps[1]` e `if l then lamp3d(1, l.x, l.y + 1.2, l.z, 4, 0.8, 0xFFC888) end` sotto `-- [lamp]`; `shadow3d(1)` sotto `-- [shadow]`;
`draw3d(pico, px, py, pz, 0, 0, 0, 1, 8)` sotto `-- [shadow-draw]`. Le funzioni `map3d_*` sono il contratto di `SYNC.md` §2 (non ancora implementate). La camera e la rotazione di Pico (`[DA VERIFICARE]` in `script.md`)
si tarano in prova. `px = 4.5` e' il centro del sentiero largo 9; `pz = 2` la partenza.

## Come si registra: tre registrazioni

`F5` termina la corsa di `bmhost` (la cartuccia provata e' un'altra registrazione): il video ha quindi **tre pezzi** che `record.py` monta con `concat_cmd`. Non e' mai la stessa SD: R1 e R3 condividono la SD.

| Pezzo | Cartuccia lanciata | Cosa fa | Durata | Ordine di ripresa | Ordine nel video |
|---|---|---|---|---|---|
| R1 | `build/editor.bm` (bm SDK) con `--tool` | apre `TROPIC.BM`, pagine, template, `r`, le sei righe, *Save as*, `F5` | 112 s (0:10-2:02) | 1 | 1 |
| R3 | `build/editor.bm` con `--tool`, stessa SD (l'SDK ricorda file e pagina) | dev kit, `Ctrl+B` | 38 s (2:14-2:52) | 2 | 3 |
| R2 | il `.bm` costruito da R3 (`TROPIC.BM`, in `out/sd/carts/`) | anteprima da sola, nessun tasto | 12 s (2:02-2:14) | 3 | 2 |

Si riprende R3 prima di R2 perche' R2 ha bisogno del `.bm` che produce `Ctrl+B` di R3. `[DA VERIFICARE: Ctrl+B e F5 sotto bmhost --tool, e dove si scrive il .bm; alternativa: costruire il .bm con scripts/mkbm.py da TROPIC.BME]`.

- Runtime: `build/host/bmhost-bin` (`make bmhost`), orologio virtuale (60 fps esatti), **software 3D** (bmhost non ha GPU ne' il menu del kernel). Schermo registrato: 640x360 per l'editor (R1, R3); 480x270 per la scena (R2), riportata a 1920x1080 con scala 4x (`overlay.md`).
- SD **pulita**: `out/sd/carts/TROPIC.BM` = copia di `start.bm` all'inizio di R1. R3 non ripulisce.
- Opzioni di R1: `--tool --sd out/sd --seconds 112 --input input-r1.txt --video <raw/fifo> --quiet`. R3: stesso con `--seconds 38 --input input-r3.txt`. R2: `--sd out/sd --seconds 12 --video <raw> --shots out/shots` senza `--tool` e senza input.
- Frame di ancoraggio di R1 (frame = (t_video - 10 s) x 60): `Ctrl+O` 0; `F1` 240; `F4` 420; `Ctrl+N` 720; `r` 1560; inizio blocco `sky3d` 2160; blocco `fog3d` 3240; blocco `lamp3d` 4200; blocco ombra 5160; `Ctrl+Shift+S` 6240; `F5` 6600 (fine registrazione a 6720).
  R3: `F1` al frame 0 (2:14); secondo `F1` 240; `Ctrl+B` 960 (2:30).

### Il file dei tasti

Formato `FRAME type CODICI` (righe in ordine di frame, spazio come `\x20`, frecce `\xf0..\xf3`, `F1..F5 = \xf9..\xfd`, `Ctrl+O = \x0f`, `Ctrl+N = \x0e`, `Ctrl+B = \x02`, `Ctrl+Shift+S = \xee\x13`):

```
0 type \x0f
120 type \n
240 type \xf9
420 type \xfc
```

(`Ctrl+O` a 0:10, `Enter` a 0:12, `F1` a 0:14, `F4` a 0:17). I `↓ xN` verso i marcatori li calcola `record.py` contando le righe di `preview.lua`.

## `verify.py` (nessuna immagine)

Dopo R1 e R3, sulla SD (`out/sd/carts/`):

1. **Sezioni** (`scripts/bmres.py list`/`read` sul `.BM` costruito e sul `.BME`): LUA, MESH, ANIM, SHEET, LAYERS, FLAGS, AUDIO presenti; MESH/ANIM/SHEET/LAYERS/FLAGS/AUDIO con lo stesso hash di `start.bm` (l'SDK non li tocca).
2. **Layer**: i quattro nomi `floor`, `height`, `walls`, `objects` e il numero di celle `1025` (APPLE), `1026` (CRATE), `1029` (LAMP) uguali a quelli di `04-map.bm` (letti con le funzioni dei layer di `bmres.py`).
3. **Codice**: nel LUA di `TROPIC.BM` ci sono le sei righe di `script.md` (stringhe esatte), ciascuna subito sotto il suo marcatore; i marcatori `-- [..]` restano (sono commenti).
4. **Schermo**: la risoluzione e' `480x270` (nell'intestazione della cartuccia o in INFO, vedi sopra).
5. **Anteprima** (`--shots`): tre fotogrammi di R2 (secondi 1, 5, 10): il pixel in alto al centro (cielo/orizzonte) ha il colore della nebbia `0xA8D0C0` entro +-6 per canale; nel fotogramma 2 la media di luminanza in un cerchio di 20 px attorno alla lanterna e' maggiore di quella del fotogramma senza la riga `lamp3d` (confronto con una corsa di controllo con `lamp3d` tolto); sotto i piedi di Pico c'e' un'area piu' scura (confronto con una corsa senza flag 8).
6. **Costo** (solo con la console o QEMU, **non** bmhost): `LAST TRY` con meno di 10 frame sopra 16.7 ms su 600 e triangoli <= 6300 (Pico 300 + terreno 6000). `[DA VERIFICARE]`.
7. **Catena**: `chain/06-sdk.bm` = il `.bm` costruito con `Ctrl+B` (`TROPIC.BM` dopo R3), con gli stessi dati di `start.bm` e il codice nuovo.

Dopo: `ffprobe` sulla durata del montaggio (~180 s). Il codice di anteprima non suona nulla (il suono e' nel banco ma lo usa il video 7): l'audio e' solo la voce fuori campo.

## Cosa NON si fa

Nessuna mappa fatta a script nel video finale; nessuna GPU (si dice solo che sul televisore la GPU puo' disegnare le stesse chiamate: `docs/api/light3d.md`, "3D on the GPU"); nessun altro editor oltre a bm SDK.
