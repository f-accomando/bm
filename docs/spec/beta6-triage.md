# Bozza AGY, 2026-10-15, letta su bm-core@e76900f0: classi proposte, decidono SPV e l'utente

| ID | area | status attuale | prova Pi (file:riga) | prova RGB30 (file:riga) | classe proposta | motivo (≤200 caratteri) |
| D01 | input / system keys | untested | src/kernel/input.c:145 (ritorna INPUT_HOME su HID_QUIT_PS) | src/rgb30/ui.c:1084 (tab=TAB_GAMES su HID_QUIT_PS); src/rgb30/bm_input.c:92 (inoltra hid_quit_pressed) | needs-hardware-test | status untested; toml stale nei campi pi/rgb30 (ma note descrive stato attuale: bm_input.c e ui.c leggono PS/Esc); attende test su HW |
| D02 | bluetooth | untested | src/kernel/main.c:280 (bt_start al boot se bt_paired) | src/rgb30/main.c:333 (bt_start al boot se bt_paired) | needs-hardware-test | status untested; toml stale nei campi pi/rgb30 (ma note descrive stato attuale: bt_start al boot attivo se paired); attende test su HW |
| D03 | players | suspended | src/kernel/settings.c:378 (loop Player 1..4 in Settings) | src/kernel/settings.c:371 (mostra solo riga fissa Player 1) | accept-proposed | toml: sospesa dall'utente il 2026-10-13 (cause=unknown); accept-proposed e' tirata perche' players riguarda input (BT pads); classe lasciata a SPV |
| D04 | menu keys | open | src/kernel/carts.c:1433 (gestione Tab, 1-5, U da tastiera) | src/rgb30/ui.c:1084 (gestisce solo HID_QUIT_PS su tastiera BT); src/rgb30/bm_input.c:92 | accept-hardware | accept-hardware e' contestabile: toml ha cause=history e action "align Start and Tab for BT keyboards" (Start conferma su Pi, inattivo su RGB30); classe lasciata a SPV |
| D05 | menu pointer | suspended | src/kernel/carts.c:1507 (pointer_update: hover, click, rotella, tasto destro) | - (nessun puntatore mouse in src/rgb30/ui.c, solo pad) | accept-hardware | discende da capability pad-only e no-keyboard; toml: sospeso dall'utente il 2026-10-13 in attesa di sistema stick/tastiera |
| D06 | launch | untested | src/kernel/carts.c:1673 (ask = ASK_SWITCH se gioco sospeso) | src/rgb30/ui.c:1159 (asking = ASK_SWITCH per gioco sospeso) | needs-hardware-test | status untested; toml stale nei campi pi/rgb30 (ma note descrive stato attuale: ASK_SWITCH presente in ui.c:1159); attende prova su console |
| D07 | frozen game | untested | src/kernel/carts.c:1236 (.running = is_suspended) | src/rgb30/ui.c:984 (.running = is_suspended) | needs-hardware-test | status untested; toml stale nei campi pi/rgb30 (ma note descrive stato attuale: badge running e Close presenti); attende prova su console |
| D08 | delete a game | untested | src/kernel/carts.c:1032 (controlli netxfer_updating e is_suspended) | src/rgb30/ui.c:818,822 (controlli netxfer_updating e is_suspended) | needs-hardware-test | status untested; toml stale nei campi pi/rgb30 (ma note descrive stato attuale: chiusura suspended e check download presenti); attende prova |
| D09 | game list | open | src/kernel/carts.c:156 (scansione .bme per Dev); 426 (scansione /carts) | src/rgb30/ui.c:248 (scansione /bm); 259 (ignora .bme) | fix-before-beta | toml: /carts accettato per storage-path; note toml dichiara gia' RGB30 ordinato per titolo; resta scansione .bme per Dev |
| D10 | covers | open | src/kernel/carts.c:268 (read_cover in fiber background) | src/rgb30/ui.c:272 (load_cover sincrono nello scan) | fix-before-beta | tocca Games (caricamento sincrono delle cover blocca la UI fino a 64 giochi su RGB30); cause != hardware |
| D11 | game options (X / C) | open | src/kernel/carts.c:929 (copia .bme), 934 (publish), 937 (send) | src/rgb30/ui.c:731 (enum opzioni prive di copia, publish, send) | fix-before-beta | tocca menu opzioni giochi (mancano copia modificabile, publish e send su menu RGB30); cause != hardware |
| D12 | cover details | open | src/kernel/carts.c:1260 (mostra autore, tipo, KiB e percorso) | src/rgb30/ui.c:1020 (mostra solo riga suggerimento Start+Select) | fix-before-beta | tocca Games e menu (dettagli autore, dimensione cartuccia e nota prestazioni mancanti su RGB30); cause != hardware |
| D13 | Dev tab and Market Apps | untested | src/kernel/carts.c:402 (chiamata bm_tool_id_of_file in tool_id_of) | src/rgb30/ui.c:261 (chiamata bm_tool_id_of_file filtra i tool in Games) | needs-hardware-test | status untested; toml stale nei campi pi/rgb30 (ma note descrive stato attuale: bm_tool_id_of_file esclude tool da Games); attende prova |
| D28 | a game built in /carts and the Games tab | open | src/kernel/carts.c:426 (scansione /carts) | src/rgb30/ui.c:248 (scansiona solo bm; corretta su claude/integrazione-ottobre@32c4ec4b) | fixed-on-branch | corretta su claude/integrazione-ottobre@32c4ec4b; toml stale sul campo pi (carts.c:426 scansiona gia' /carts); evidence toml tools.md:127 fuori tema |
| D30 | screen | open | src/kernel/menu_ui.c:1189 (sw=640, scale con menu_scale) | src/kernel/menu_ui.c:1184 (sw=360, scale=1 per display 720x720) | accept-hardware | discende da capability square-panel (pannello nativo quadrato 720x720 su RGB30 vs uscita TV HDMI 16:9 su Pi) |
| D31 | game screen | open | src/bm/runtime.c:7107 (letterboxing 16:9 per giochi quadrati) | src/bm/runtime.c:7099 (boxed=0, riempie il pannello 1:1) | accept-hardware | discende da capability square-panel (pannello quadrato nativo 720x720 su RGB30 senza letterboxing 16:9) |
| D32 | confirm / back | open | src/kernel/input.c:243 (input_ok_bit: A conferma, B annulla); src/kernel/input.c:50 | src/rgb30/pad.c:10 (B conferma, A annulla di default; confirm=a) | accept-hardware | discende da capability pad-only (layout Nintendo B conferma / A annulla di default su RGB30 vs Pi A conferma / B annulla fisso, con switch confirm=a) |
| D33 | game buttons | open | src/usb/hid.c:743 (croce DS4 mappata per posizione a HID_A) | src/rgb30/bm_input.c:30 (pad mappato per lettere serigrafate) | accept-hardware | discende da capability pad-only (pad integrato mappato sulle lettere serigrafate anziche' DS4 per posizione) |
| D34 | system keys | open | src/bm/runtime.c:5082 (F11 fullscreen, F12 aiuto tasti); src/bm/runtime.c:5065 | src/rgb30/bm_input.c:96 (combo uscita Start+Select); src/bm/runtime.c:5065 (combo screenshot) | accept-hardware | F11/F12 tasti di sistema non inoltrati (no tastiera su RGB30); separatamente lo screenshot usa Select+L1+R1 e l'uscita Start+Select su pad |
| D35 | text input | open | src/usb/hid.c:262 (gestione tastiera fisica USB/BT) | src/rgb30/bm_port.c:185,187 (usb_info: porta USB non pilotata su RGB30, nessun mouse o pad USB) | accept-hardware | discende da capability no-keyboard e usb_info vuoto (nessun host USB pilotato su RGB30); tastiera a schermo affidata a TODO |
| D36 | storage paths | open | src/kernel/market.c:27 (#define GAMES_DIR "/carts") | src/kernel/market.c:25 (#define GAMES_DIR "/bm"); src/net/netxfer.c:182 | accept-hardware | discende da capability storage-path (su RGB30 tutti i file cartucce e dati risiedono nella cartella unica /bm) |

## Conteggio per classe
- accept-hardware: 9
- fix-before-beta: 4
- fixed-on-branch: 1
- needs-hardware-test: 6
- accept-proposed: 1
- Totale voci analizzate: 21

## Lista fix-before-beta ordinata per priorità
1. D09 (priority 4) — game list (scansione .bme per Dev; toml accetta /carts per capability storage-path)
2. D10 (priority 5) — covers (caricamento asincrono cover in background su RGB30)
3. D11 (priority 6) — game options (X / C) (allineamento opzioni gioco: copia modificabile, publish, send)
4. D12 (priority 6) — cover details (dettagli autore, dimensione cartuccia e nota prestazioni)

## Righe citate per verifica SPV
- src/bm/runtime.c:5065
- src/bm/runtime.c:5082
- src/bm/runtime.c:7099
- src/bm/runtime.c:7107
- src/kernel/carts.c:156
- src/kernel/carts.c:402
- src/kernel/carts.c:426
- src/kernel/carts.c:929
- src/kernel/carts.c:1032
- src/kernel/carts.c:1236
- src/kernel/carts.c:1260
- src/kernel/carts.c:1433
- src/kernel/carts.c:1507
- src/kernel/carts.c:1673
- src/kernel/input.c:50
- src/kernel/input.c:145
- src/kernel/input.c:243
- src/kernel/main.c:280
- src/kernel/market.c:25
- src/kernel/market.c:27
- src/kernel/menu_ui.c:1184
- src/kernel/menu_ui.c:1189
- src/kernel/settings.c:371
- src/kernel/settings.c:378
- src/net/netxfer.c:182
- src/rgb30/bm_input.c:30
- src/rgb30/bm_input.c:92
- src/rgb30/bm_input.c:96
- src/rgb30/bm_port.c:185
- src/rgb30/bm_port.c:187
- src/rgb30/main.c:333
- src/rgb30/pad.c:10
- src/rgb30/ui.c:248
- src/rgb30/ui.c:259
- src/rgb30/ui.c:261
- src/rgb30/ui.c:272
- src/rgb30/ui.c:731
- src/rgb30/ui.c:818
- src/rgb30/ui.c:822
- src/rgb30/ui.c:984
- src/rgb30/ui.c:1020
- src/rgb30/ui.c:1084
- src/rgb30/ui.c:1159
- src/usb/hid.c:262
- src/usb/hid.c:743

## Righe cambiate (AGY-9)
- D01: toml stale specificato sui campi pi/rgb30 (note gia' aggiornato).
- D02: toml stale specificato sui campi pi/rgb30 (note gia' aggiornato).
- D03: specificato che accept-proposed e' tirata perche' players tocca input (BT pads); decisione classe a SPV.
- D04: prova RGB30 aggiornata a src/rgb30/ui.c:1084 (PS button) e src/rgb30/bm_input.c:92 (tastiera BT gia' letta); toml ha Start che conferma su Pi e inattivo su RGB30; accept-hardware contestabile (cause=history nel toml), decisione classe a SPV.
- D06: toml stale specificato sui campi pi/rgb30 (note gia' aggiornato su ASK_SWITCH).
- D07: toml stale specificato sui campi pi/rgb30 (note gia' aggiornato su badge running).
- D08: toml stale specificato sui campi pi/rgb30 (note gia' aggiornato su controlli cancellazione).
- D09: citato che la note del toml dichiara gia' l'elenco RGB30 ordinato per titolo.
- D13: prova Pi precisata a src/kernel/carts.c:402 (chiamata bm_tool_id_of_file in tool_id_of); toml stale sui campi pi/rgb30 (note descrive stato attuale).
- D28: toml stale sul campo pi (src/kernel/carts.c:426 scansiona gia' /carts); evidence del toml (tools.md:127) annotata come fuori tema.
- D32: sostituito src/kernel/settings.c:428 (#else) con src/kernel/input.c:243 (input_ok_bit) e src/kernel/input.c:50 (p & HID_A return '\r') come prova che sul Pi conferma A.
- D34: separati i due fatti nel motivo: F11/F12 tasti di sistema non inoltrati a cartuccia (nessuna tastiera su RGB30); Select+L1+R1 e Start+Select combo su pad.
- D35: sostituito rgb30.mk:66 con src/rgb30/bm_port.c:185,187 (usb_info ritorna &none, porta USB non pilotata su RGB30).
