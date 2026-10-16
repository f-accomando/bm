# APP-6: sviluppare un gioco senza PC sull'RGB30 (risorse e progetti sul dispositivo) (APP-6, BETA-2)
Assegnato a B16 (16 ott), dopo APP-4. Decisioni adottate: D1-D6 come raccomandate (D1 = B: solo Lib dei suoni, 6h inclusa). Base: il branch di APP-4 `claude/sdk-stages-template` (branch impilato, nessun merge su bm-core); rebase quando APP-4 sara' integrato.
Inizia dopo: DELIVERED APP-4 **e** integrazione di APP-4 su `bm-core` fatta da SPV (i due WO toccano gli stessi file: `TEMPLATES` e menu in `carts/sdk_b16/main.lua`, `tests/rgb30/qemu_test.py`).
Base: `bm-core` aggiornato dopo APP-4. Branch `claude/sdk-b16-dev-without-pc`. Mai push su `bm-core`.
Perimetro: `carts/sdk_b16/main.lua`, `tests/rgb30/qemu_test.py`. Se SPV approva D4: anche `tests/studio/sdk_b16_host.lua`, `docs/progress/tools.md`, `docs/spec/divergences.toml`.
NON toccare: `carts/editor/*`, `carts/sound/*` (sola lettura, il codice si copia), `carts/sdk_b16/layout.lua`, `src/*` (`runtime.c`, `rgb30/ui.c`, `kernel/lib.c`), `rgb30.mk`, `Makefile`, `SYNC.md`, `sync/*`.
Regole: un `.b16` non sostituisce mai il `.bm`. Gli editor modificano solo i `.bme` (un `.bm` aperto si salva come copia modificabile, `write_target`). "Build the game .bm" fa come Ctrl+B (`build_project()`). Mai `pad_ok`/`pad_back` a mano: nella cartuccia `btn("ok")`/`btn("back")`.

## Stato reale (verificato dal planner; righe di bm-core di oggi: dopo APP-4 cercare per nome di funzione)
- Già usabili col solo pad: sprite `P.sprite.pad()` (main.lua:1880-1889: ok disegna, L2/R2 cambiano colore); mappa `P.map.pad()` (2845-2869: ok piazza la tile, parte da 1); codice: Select attiva padtype (`type_cycle()` 1472-1483; sull'RGB30 il primo modo è `keyboard` perché `words` è vuoto, 1466-1470); salva con nome `save_as()` 844-852 (`D.ask`, nome proposto `MYGAME.BME` in `/carts`, Start accetta); build ed export `build_project()` 864-879, voci menu Start 3427-3439. Attenzione: `msel` (3414) non torna a 1 quando il menu si riapre, solo quando la cartuccia riparte.
- **Manca: suono.** `PAGES` (3543) è `project, code, d2, d3, menu`: nessuna pagina suoni. `SUITE = {}` (147), quindi "bm Sound" (899) risponde "is on the Pi" (`open_in()` 902-909).
- **Manca: banco suoni salvabile.** `cart_save` scrive `proj_audio`, impostato solo da `cart_load`/`cart_new` (runtime.c:5612, 5880, 5961). `cart_put_audio` (runtime.c:6788) non lo aggiorna: un `cart_save` dopo un `cart_put_audio` riscrive il banco vecchio.
- **Manca: Lib.** Le schede dell'RGB30 sono solo Market/Games/Dev (src/rgb30/ui.c:666-667); la Lib del Pi sta in `src/kernel/lib.c` e `libview.c`. Da Lua: `cart_audio` usa `bm_parse` e non `bm_parse_any` (runtime.c:6725), quindi un `.bms` (BMRES) non si legge; non c'è API per importare sheet o mappa da un altro file senza sostituire quelli del progetto (`cart_load`, docs/api/tools.md:26).
- **Manca: test QEMU solo pad.** `test_sdk_tool` (qemu_test.py:752) usa F5 e Ctrl+\ (tastiera). Seriale→pad (src/rgb30/pad.c:50-71): `w s a d` croce, `\r` ok, `\x7f` back, `x y l r L R`, `\t` Select, spazio Start. Lettura della SD dopo il test: `pmemsave` + mtools (qemu_test.py:312-328). Audio: riga seriale `audio: heard N Hz` (1044).
- **Manca: stub nel test host.** In `tests/studio/sdk_b16_host.lua` non ci sono stub per `cart_put_audio`, `audio_bank`, `audio_play` (`cart_audio` restituisce `false`, riga 621). Tabella delle pagine alle righe 815-822.

## Readiness Gate
| Sezione | File | Determinismo | Verifica | ≤3 file | Nessun blocco |
|---|---|---|---|---|---|
| 6a-6c, 6g | SÌ | SÌ | SÌ | SÌ | SÌ (dopo APP-4) |
| 6d | SÌ | SÌ | SÌ | SÌ | NO: D3, D4, D6 |
| 6e-6f | SÌ | SÌ | SÌ | SÌ | NO: D3, D4 |
| 6h | SÌ | SÌ | SÌ | SÌ | NO: D1 |
| 6i | SÌ | SÌ | SÌ | SÌ | NO: D4, D5 |
Ordine: 6a → 6b → 6c → (D3/D4/D6) 6d → 6e → 6f → 6g → (D1) 6h → (D5) 6i.
Comando di build comune ai test QEMU, sotto `QV`: `make -f rgb30.mk PLAT=virt all && python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k <nome>`.
Regola di fallimento: se un test di 6a-6c o 6g fallisce per un difetto di `main.lua`, si chiude con `BLOCKED: <test> + output`, senza correggere a intuito.

## APP-6a: aiuti QEMU + sprite col pad (bm-light, 45 min)
- File: `tests/rgb30/qemu_test.py`. Funzioni nuove: `sdk_session`, `sdk_open`, `sdk_menu`, `sdk_new_empty`, `sdk_save_default`, `sdk_exit_dump`, `sd_read`, `cart_secs`, `test_sdk_pad_sprite` (aiuti prima di `test_battery_icon`).
- `sdk_session(tmp, files)` → `(q, size)`, SD con `{"bm/config.txt": b"game_intro=0\n", **files}`. `sdk_open(q)`: `boot`, `keys(q,"r")`, `keys(q,"ss")`, `\r`, `q.expect("bm SDK\r\n")` e schermo con `PROJECT` (sequenza di `test_sdk_tool` 792-799). `sdk_menu(q, st, label)`: righe da main.lua:3428-3439, mappa `{"New project...":2,"Save":4,"Build the game .bm":6,"Export .b16":7,"Try the game":8,"Exit bm SDK":10}`; da `st["msel"]` (all'inizio 1 per ogni sessione) si muove con `s`/`w` senza giro, poi `\r` e aggiorna `st`; si chiama con la pagina menu già aperta (spazio). `sdk_new_empty(q, st)`: spazio, "New project...", `\r` (riga 1 = Empty 2D), schermo con `new project (Empty 2D)`. `sdk_save_default(q, st)`: spazio, "Save", spazio (Start accetta `MYGAME.BME`), schermo con `saved /carts/MYGAME.BME`. `sdk_exit_dump(q, st, tmp, size)`: "Exit bm SDK", `in_menu`, `pmemsave` come alle righe 312-315; restituisce il percorso del dump. `sd_read(dump, path)` → bytes con `mcopy -n -i {dump}@@1M ::{path} <tmp>` (con `MTOOLS_SKIP_CHECK=1`). `cart_secs(data)` → `{tipo: body}` con `mkbm.bmmesh.cart_sections`.
- Test `test_sdk_pad_sprite`: `sdk_new_empty`, `rr` (pagina 2D sprite), `\r` (pixel (0,0) della cella 0, bianco), `sdk_save_default`, `sdk_exit_dump`. Asserzione: la sezione 2 (SHEET) di `/carts/MYGAME.BME` ha `body[4:8] == b"\xff\xff\xff\xff"`.
- Verifica: `QV -k sdk_pad_sprite` → `PASS test_sdk_pad_sprite`, exit 0; `-k sdk_tool` resta PASS.

## APP-6b: mappa col pad (bm-coder, 20 min)
- `tests/rgb30/qemu_test.py`, nuova `test_sdk_pad_map`: `sdk_new_empty`, `rr`, `q.send("ys")` (Y+giù: vista mappa, schermo con `map`), `\r` (tile 1 nella cella (0,0), layer 1), save, exit, dump. Asserzione: sezione 3 (MAP), `struct.unpack_from("<H", body, 4)[0] == 1` (formato in runtime.c, ramo `i == 3` di `l_cart_save`).
- Verifica: `QV -k sdk_pad_map` → PASS.

## APP-6c: codice col pad (bm-coder, 30 min)
- `tests/rgb30/qemu_test.py`, nuova `test_sdk_pad_code`: `sdk_new_empty`, `r` (code), `\t` (schermo con `pad typing: keyboard`). Scrivere `do end ` (7 caratteri: tasti della pagina 1 di `KB` e `space` di `KB_SPECIAL`, src/ai/padtype.lua:93-97) muovendo il cursore della tastiera solo con `w s a d` e confermando con `\r` (sequenza da ricavare dal codice della tastiera in padtype.lua). Poi spazio (Start: menu), "Save" (riga attuale di `msel` 1), spazio, exit, dump. Asserzione: la sezione 1 (LUA) inizia con `b"do end -- my game"`.
- Verifica: `QV -k sdk_pad_code` → PASS.

## APP-6d: modello del banco suoni e salvataggio (bm-coder, 40 min; dopo D3, D4, D6)
- File: `carts/sdk_b16/main.lua`, `tests/studio/sdk_b16_host.lua`.
- Nuovo blocco `do … end` subito dopo `new_project()`, esportato in `P.bank` (nessun nuovo local di primo livello). Si copiano da carts/sound/main.lua: `WAVES` (44), `B.TONE`/`tone_default` (95-101), `B.sound`/`B.sfx` (104-108), `sound_empty`/`sfx_empty` (132-145), `name8`, `PSOUND`/`PSFX` (163-167); la parte di `B.pack`/`B.parse` per suoni ed effetti (170-189, 215-240).
- `P.bank.parse(data)` → `{ver, sounds[1..32], sfx[1..64], np, ng, nsm, tail}`; `tail = data:sub(pos)` subito dopo gli effetti (pattern, canzoni e campioni restano byte per byte); se la lettura fallisce → `nil, msg`. `P.bank.pack(b)`: `"BMAU"` + `pack("<BBBBBBxxxxxx", b.nsm>0 and 3 or 2, ns, nx, b.np, b.ng, b.nsm)` + suoni + effetti + `b.tail`. `P.bank.new()`: banco vuoto, `tail=""`, `np/ng/nsm=0`. `P.bank.get()`: se `S.snd == nil`, `local d = S.proj.path and cart_audio and cart_audio(S.proj.path)`; `S.snd` = `parse(d)` se `d` è stringa, altrimenti `new()`; se la lettura fallisce `S.snd = false` e `say("sound bank not readable: " .. msg, C.ERR)`; restituisce `S.snd`. `P.bank.edited()`: `S.snd_edited = true; touched()`.
- `load_project()` e `new_project()`: aggiungere `S.snd, S.snd_edited = nil, false`. `save_project()`, dopo il `cart_save` riuscito e l'aggiornamento di `S.proj.save`, prima di `store_target()`: se `S.snd and S.snd_edited and cart_put_audio` → `cart_put_audio(S.proj.save, P.bank.pack(S.snd))`; se fallisce `say("sounds not saved: "..e, C.ERR, 400)` e `return false`; a ogni salvataggio dopo la prima modifica (rimedio a D6).
- Test host: stub in `new_env`: `E.cart_put_audio(path, bank)` scrive `audio_files[path:upper()] = bank` e restituisce `true`; `E.cart_audio(path)` restituisce `audio_files[...]` o `false`; `E.audio_bank`/`E.audio_play` registrano l'ultima chiamata; check: pack/parse di un banco con suono 0 `wave=4` → stessi byte; un `tail` arbitrario si conserva; `save_project` con `S.snd_edited` scrive il banco.
- Verifica: `make test-sdk-b16` → exit 0, nessun check fallito.

## APP-6e: pagina suoni, vista Sounds (bm-light, 45 min; dopo 6d)
- File: `carts/sdk_b16/main.lua`, `tests/studio/sdk_b16_host.lua`, `tests/rgb30/qemu_test.py`.
- Registrazione: `PAGES` = `project, code, d2, d3, snd, menu`; `TAB_KEYS.snd = "f7"`; `global_key`: `f7` (come `f3`: se già su `snd`, `other_view()`); `S.sndview = "sounds"`; `other_view()` alterna `sounds`/`sfx` su `snd`; `page_of()` → `P.snd`; `page_key` → `P.snd.key(k)`; `tab_label` → `snd` o `sfx`; `page_name` → `the sounds` / `the effects`; `remember()` salva `t.sndview`, `_init` lo ripristina; `HELP.snd` e nome `sounds` in `sdk_keyhelp`.
- Nuovo blocco `P.snd` (un solo `do … end`, meno di 200 local, righe da 16 px): strip `SOUNDS`; a sinistra l'elenco degli slot 0-31 (nome o forma d'onda, `(empty)`), a destra i campi `wave, vol, duty, a, d, s, r`. Pad: su/giù campo; sinistra/destra (`PAD.rp`) valore ±1, ±8 se `PAD.held[dir] > 60`; `wave` gira tra 0 e 13, gli altri campi 0..255; L2/R2 slot ±1 (0..31); ok: `audio_bank(P.bank.pack(S.snd)); audio_play(0, slot, 69, 255, 0, 350)`; back nessuna azione. Tastiera: frecce, `[`/`]`, invio. `menu_items`: `{"Play A4","\n"}`, `{"Clear the sound","c"}` (riporta lo slot a `B.sound()`). Ogni modifica chiama `P.bank.edited()`. Con `S.snd == false` la pagina mostra solo l'errore. `status()`: `sound N  WAVE  vol V`.
- Test host: voce `{ "sounds", "the sounds", r1 ×4 }` nella tabella delle pagine (815-822).
- Test QEMU `test_sdk_pad_sound`: `sdk_new_empty`, `rrrr` (schermo con `SOUNDS`), `dddd` (schermo con `SINE`), `q.buf=b""`, `\r`; una riga `audio: heard N Hz` con 438≤N≤442 (regex di 1044); poi save, exit, dump: `bmaudio.unpack(sezione 6)["sounds"][0]["wave"] == "sine"`.
- Verifica: `make test-sdk-b16` → exit 0; `QV -k sdk_pad_sound` → PASS.

## APP-6f: vista Effects (bm-light, 40 min; dopo 6e)
- Stessi file di 6e. Vista: strip `EFFECTS`; slot 0-63 con L2/R2; 32 passi su 2 righe da 16; cursore `k` con sinistra/destra (0..31). Passo: `st = note | sound<<8 | vol<<16 | fx<<24`, come `mk`/`s_*` di carts/sound/main.lua:86-89.
- Pad: ok su un passo vuoto → `note = last_note` (all'inizio 69), `sound` = slot della vista Sounds, `vol = 255`; se `k ≥ len` allora `len = k+1` (come carts/sound 1175); ok su qualsiasi passo → anteprima `audio_play(0, s_sound, s_note, s_vol, s_fx, 350)`; su/giù → nota ±1 nel passo pieno (1..127), `last_note` = nota; back → passo = 0. `menu_items`: `{"Speed +10 ms","+"}`, `{"Speed -10 ms","-"}` (`ms` 10..500), `{"Play the effect","p"}` (`audio_bank(pack); sfx(slot)`).
- Test host: voce `{ "sfx", "the effects", r1 ×4 + with("y","down") }`. Test QEMU `test_sdk_pad_sfx`: `rrrr`, `q.send("ys")` (schermo con `EFFECTS`), `\r`, save, exit, dump; asserzione `bmaudio.unpack(...)["sfx"][0]["steps"][0] == bmaudio.step_text(69, 0, 255, 0)`.
- Verifica: `make test-sdk-b16` → exit 0; `QV -k sdk_pad_sfx` → PASS.

## APP-6g: build completa col solo pad, test di accettazione (bm-coder, 35 min; dopo 6f)
- `tests/rgb30/qemu_test.py`, nuova `test_sdk_pad_build`. Sequenza unica: passi di 6a, 6b, 6c, 6e (pixel, tile, `do end `, `SINE`), `sdk_save_default`, poi "Build the game .bm" (schermo con `built /carts/MYGAME.BM`), "Export .b16" (schermo con `.B16`), exit, dump.
- Asserzioni: in `MYGAME.BM` le sezioni 1, 2, 3 e 6 sono uguali a quelle di `MYGAME.BME`; `MYGAME.BM[104:112] != 8 zeri` (nome del progetto, `cart_build`); `mdir ::/bm` contiene `MYGAME.B16`, con `u16@18 & 2`.
- Verifica: `QV -k sdk_pad` → tutti i `test_sdk_pad_*` PASS; `-k sdk_tool` PASS.

## APP-6h: Lib dei suoni nell'SDK (bm-light, 45 min; solo se D1 = B)
- Stessi file di 6e. Azione X della vista Sounds `{"Sounds from a file (Lib)...", "i"}`: sorgenti `list_files()` più `ls("/bm/lib")`, solo `.bm`/`.bme`, escluso `S.proj.save`, con `cart_audio(path)` di tipo stringa; si sceglie un file con `D.choose`, poi un suono non vuoto del suo banco; il suono (copia profonda, `tone` compreso) va nel primo slot vuoto di `S.snd`, poi `P.bank.edited()`; senza slot vuoti `say("no empty sound slot", C.ERR)`.
- Test QEMU `test_sdk_pad_lib`: SD con `bm/lib/kit.bm` = `mkbm.pack(b"", title="Kit", audio=bmaudio.pack(<banco con sound 0 "BELL" wave metal>))`. Import col pad, save, dump; asserzione `sounds[0]["name"] == "BELL"`.
- Verifica: `make test-sdk-b16` → exit 0; `QV -k sdk_pad_lib` → PASS.

## APP-6i: documenti (bm-mechanic, 15 min; dopo D5)
- `docs/progress/tools.md`: in "bm SDK (RGB30)" una riga per la pagina `snd` (Sounds/Effects, pad) e i test `test_sdk_pad_*`; in Open work, sostituire la riga "bm SDK (RGB30)" con i passi sul dispositivo qui sotto. `docs/spec/divergences.toml`: voce nuova come da D5, ID successivo all'ultimo.
- Verifica: `python scripts/consistency.py check` senza errori nuovi; `python scripts/docs_audit.py score` senza link rotti né orfani.

## Prova sul dispositivo (la fa l'utente, dopo l'integrazione e `./easy_install.sh`)
1. Dev > bm SDK. Start > New project... > Empty 2D.
2. R1 ×2: disegnare con ok. Y+giù: piazzare una tile con ok.
3. R1 indietro fino a code. Select: scrivere `do end ` con la tastiera del pad.
4. R1 fino a `snd`: wave SINE, ok si sente. Y+giù: una nota in Effects.
5. Start > Save (Start accetta `MYGAME.BME`) > Build the game .bm > Export .b16.
6. Start+Select per uscire. In Games: `MYGAME.B16` c'è. `MYGAME.BM` in `/carts` compare solo dopo D28 di AGY (R41).
7. Avviare il gioco. Riaprire il `.bme` dall'SDK: sprite, mappa, codice e suoni ci sono ancora.

## Decisioni per SPV
- **D1, Lib.** (A) scheda Lib nel menu dell'RGB30: porting di `lib.c`/`libview.c` in `src/rgb30/ui.c` e `menu_ui.c` (fuori perimetro, lavoro di kernel, serve comunque un'API per importare da BMRES); (B) Lib dei soli suoni dentro l'SDK (6h). Raccomando B per la Beta: l'import di sprite, mappe e `.bms` richiede API nuove nel runtime (`bm_parse_any` in `cart_audio`, un import di sheet/mappa senza sostituire il progetto), in un WO di sistema a parte.
- **D2, Pi.** La ROADMAP dice "RGB30 e Pi". Sul Pi c'è bm Sound (con supporto pad, `edit_step` mod A/B/Y) aperto dall'SDK. Raccomando niente lavoro sul Pi in APP-6; solo un passo di prova dell'utente sul Pi in `docs/progress/tools.md` (M47).
- **D3, dove vanno i suoni.** Pagina nuova `snd` (6e) oppure terza vista di `d2`. Raccomando la pagina `snd`: la barra delle schede ci sta (circa 324 px su 360); il nome del file sparisce quando non c'è spazio, come fa `draw_tabs`.
- **D4, perimetro.** Aggiungere `tests/studio/sdk_b16_host.lua` (la regola di tools.md impone `make test-sdk-b16`), `docs/progress/tools.md` e `docs/spec/divergences.toml`. Raccomando sì.
- **D5, divergenza.** Sul Pi i suoni si fanno in bm Sound; sull'RGB30 nella pagina `snd` dell'SDK. Raccomando una voce con `cause = "hardware"` (schermo 360×360, solo pad, nessun programma della suite nel kernel) e `status = "open"`: non blocca il gate.
- **D6, banco vecchio in `cart_save`** (runtime.c:5961 contro 6788). Raccomando il rimedio in Lua di 6d adesso, più un WO di sistema a parte perché `cart_put_audio` sul progetto caricato aggiorni `proj_audio` (vale per Pi e RGB30).

Consegna: riga DELIVERED standard per ogni sezione o gruppo (6a-6c insieme, poi 6d-6g, 6h, 6i), poi FREE.
Note per SPV: le righe di `main.lua` citate sono quelle di `bm-core` di oggi (dopo APP-4 ritrovarle per nome di funzione). Il WO B16-D28FU (op-b16) cambia il messaggio di `build_project()`: il test 6g controlla solo `built /carts/MYGAME.BM`, quindi regge nei due casi. `test_sdk_pad_sound` presuppone che il sink audio del build virt registri anche le anteprime `audio_play`, come per `test_sound`: non verificato; se non succede, BLOCKED.
