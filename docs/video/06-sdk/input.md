# Video 6 — automazione dell'input

Realizzato in `video/3d/06-sdk/{record.py,merge.py,preview.lua,verify.py}` (genera `out/input-r1.txt`, `out/input-r3.txt`, gli overlay e la SD).
Libreria: `video/lib/bmvideo.py` (`Script`, `encode`, `card_cmd`, `concat_cmd`), come `video/03-sound/record.py`.

> **Stato: provato** (`verify.py` ALL OK). La mappa 3D esiste (`map3d_*` implementate); `start.bm` = mappa del video 4 + AUDIO del video 5 + `preview.lua`.
> Nessuna mappa fatta a script nel video finale.

## Il merge deterministico `start.bm` = `04-map.bm` + `05-sound.bm`

`video/3d/06-sdk/merge.py` costruisce `out/start.bm` senza intervento a mano (stesso input, stesso file):

1. Legge `video/3d/chain/04-map.bm` (se manca: `chain-delivered/04-map.bm`, poi `video/3d/04-map/out/sd/carts/TROPIC.BM`) con `scripts/bmres.py` (`read`): la base. Ha tutto quello che viene da 03 (MESH con `PICO`, `APPLE`, `CRATE`; ANIM; SHEET) piu' LAYERS e FLAGS.
2. Legge `video/3d/chain/05-sound.bm` (o `chain-delivered/`) e ne prende **solo** la sezione AUDIO (`SEC_AUDIO = 6`, magic `BMAU`).
3. `put(SEC_AUDIO, ...)` sulla base, poi `put(SEC_LUA, preview.lua)`.
4. Scrive `out/start.bm` con `bmres.write`, poi la stessa cartuccia con il flag progetto (`header[18] | 1`) come `out/start.bme`. Nessun'altra sezione viene toccata.

Il progetto si apre come **`TROPIC.BME`** (`start.bme`), non come gioco: un `.bm` al salvataggio chiede "Save an editable copy: TROPIC.BME? Enter Copy / Esc No" e questo dialogo del kernel blocca `bmhost`.

`python scripts/bmres.py list video/3d/06-sdk/out/start.bm` mostra le sezioni presenti.

## Il codice di partenza `preview.lua`

E' il template **3D with models** adattato al livello: carica la mappa, mette Pico che corre da solo (corridoio del video 4 largo 3 unita', `px = 7.5`, `pz` da 2 a 19 e ricomincia)
e lascia **cinque marcatori** dove il video scrive le righe. Senza di essi la scena e' piatta e senza nebbia.

```lua
local pico, apple, crate, w
local lamps, apples = {}, {}
local px, pz, t = 7.5, 2, 0

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
  if pz > 19 then pz = 2 end
end

function _draw()
  local py = map3d_height(w, px, pz) or 0
  cls(0xA8D0C0)
  camera3d(px, py + 3.2, pz - 6, 0, -0.35, 60)
  light3d(-0.4, 0.8, -0.5, 0.45)
  zclear()
  map3d_draw(w)
  -- [lamp]
  animate(pico, "run", t)
  -- [shadow-draw]
  draw3d(pico, px - 0.75, py, pz, 0, 0, 0, 0.5)
  for _, o in ipairs(apples) do
    draw3d(apple, o.x, o.y + 0.4, o.z, 0, t * 2, 0, 0.5)
  end
end
```

Le sei righe scritte nel video (`record.py`, `LINES`), ciascuna sotto il suo marcatore:

| Marcatore | Riga digitata |
|---|---|
| `-- [sky]` | `sky3d(0xFFE8B0, 0x80B8E8, 0x486038)` |
| `-- [fog]` | `fog3d(0xA8D0C0, 12, 40)` |
| `-- [lamp]` | `local l = lamps[1]` e `if l then lamp3d(1, l.x, l.y + 1.2, l.z, 4, 0.8, 0xFFC888) end` |
| `-- [shadow]` | `shadow3d(1)` |
| `-- [shadow-draw]` | `draw3d(pico, px - 0.75, py, pz, 0, 0, 0, 0.5, 8)` |

Pico e le mele sono disegnati a scala 0.5; Pico e' spostato di 0.75 (mezza larghezza) perche' il corridoio del video 4 e' largo 3 unita'.
Il cursore raggiunge ogni marcatore **contando le righe** (`record.py`, `move()`: tanti `↓`/`↑` quanti ne servono dalla posizione corrente), poi `End`, `Enter` e la riga digitata (`type_text`, `per_key=4`).
Ordine nel tempo: sky, fog, lamp, shadow, shadow-draw.

## Come si registra: tre registrazioni

`F5` termina la corsa di `bmhost` (la cartuccia provata e' un'altra registrazione) e `Ctrl+B` non costruisce sotto `bmhost --tool`: il video ha quindi **tre pezzi** montati con `concat_cmd`.

| Pezzo | Cartuccia lanciata | Cosa fa | Durata | Ordine nel video |
|---|---|---|---|---|
| R1 | `out/bin/editor.bm` (bm SDK) con `--tool` | apre `TROPIC.BME`, pagine, template, `r`, le sei righe, `Ctrl+S`, overlay `F5` | 112 s (0:10-2:02) | 1 |
| R3 | `out/bin/editor.bm` con `--tool`, stessa SD | `F1`, `F1` (dev kit), `Ctrl+B` a 16 s (registra il tasto e la pagina dev kit) | 38 s (2:14-2:52) | 3 |
| R2 | `out/06-sdk.bm` (anche in `out/sd/carts/TROPIC.BM`) | la cartuccia in esecuzione, nessun tasto | 12 s (2:02-2:14) | 2 |

Il gioco `06-sdk.bm` e' lo stesso file del progetto senza il flag progetto, scritto da `record.py` dopo R1 (`bmres`, `header[18] & ~1`). `F5` e' solo una sovrimpressione.

- Runtime: `build/host/bmhost-bin` (`make bmhost`), orologio virtuale (60 fps esatti), **software 3D** (nessuna GPU ne' menu del kernel). Schermo registrato: 640x360 per l'editor (R1, R3); 480x270 per la scena (R2), riportata a 1920x1080 con scala 4x (`overlay.md`).
- SD **pulita** (`lib3d.fresh_sd`): `out/sd/carts/TROPIC.BME` = `start.bme` all'inizio di R1. R3 non ripulisce.
- `python3 video/3d/06-sdk/record.py [--build-only] [--shots]`: esegue `merge.py`, scrive i file dei tasti e gli overlay, costruisce l'editor con `scripts/mkbm.py`, lancia R1, R3 e R2 (R2 con tre `shot`: `r2a`, `r2b`, `r2c` in `out/shots/`).
- Ancore di R1 (secondi dall'inizio di R1; video = +10 s): `Ctrl+O` 0; `F1` 4; `F4` 7; `F3 F3 + l x3` 11; `Ctrl+N` 14 (elenco, `↓` x7, `Esc`); `F2` 20; `F1` 25; `r` 27; `F2` 36;
  sky 40-44; fog 54-58; lamp 68-72; shadow 84-88; shadow-draw 94-98; `Ctrl+S` ~104; overlay `F5` ~110; fine 112 s.
  R3: `F1`, `F1` (dev kit), `Ctrl+B` a 16 s; fine 38 s.

### Il file dei tasti

Formato `FRAME type CODICI` (frecce `\xf0..\xf3`, `F1..F5 = \xf9..\xfd`, `Ctrl+O = \x0f`, `Ctrl+N = \x0e`, `Ctrl+S = \x13`, `Ctrl+B = \x02`):

```
0 type \x0f
120 type \n
240 type \xf9
420 type \xfc
```

(`Ctrl+O` a 0:10, `Enter` poco dopo, `F1` a 0:14, `F4` a 0:17: i frame esatti li scrive `record.py`.)

## `verify.py` (nessuna immagine guardata)

`python3 video/3d/06-sdk/verify.py [SD_DIR]` controlla `out/sd/carts/TROPIC.BME`, `out/06-sdk.bm` e `out/shots/r2*.png`; exit 0 solo se tutto passa (`RESULT: ALL OK`):

1. **Progetto**: `TROPIC.BME` ha il flag progetto.
2. **Sezioni**: MESH, ANIM, LAYERS, AUDIO e SHEET identiche byte per byte a quelle di `out/start.bm` (l'SDK non le tocca).
3. **Codice**: nel LUA le sei righe (stringhe esatte) stanno ciascuna sotto il suo marcatore, senza altri marcatori in mezzo; i marcatori `-- [..]` restano.
4. **R2**: un fotogramma e' `480x270`; in `r2a`, `r2b`, `r2c` il pixel in alto a sinistra ha il colore della nebbia `0xA8D0C0` (+-6 per canale) e nella meta' inferiore ci sono pixel scuri (piu' di 30) dell'ombra retinata.
5. **Catena**: `06-sdk.bm` ha il flag progetto spento e le stesse sezioni di `TROPIC.BME`.

Dopo: `ffprobe` sulla durata del montaggio (~180 s). Il codice di anteprima non suona nulla (il suono e' nel banco ma lo usa il video 7): l'audio e' solo la voce fuori campo.
Il costo reale (`LAST TRY`, frame sopra 16.7 ms) si misura solo sulla console o in QEMU, **non** con `bmhost`.

## Cosa NON si fa

Nessuna mappa fatta a script nel video finale; nessuna GPU (si dice solo che sul televisore la GPU puo' disegnare le stesse chiamate: `docs/api/light3d.md`, "3D on the GPU"); nessun altro editor oltre a bm SDK.
