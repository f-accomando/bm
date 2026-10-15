# Bozza AGY, 2026-10-15, letta su bm-core@e76900f0: classi proposte, decidono SPV e l'utente

| ID | area | status attuale | prova Pi (file:riga) | prova RGB30 (file:riga) | classe proposta | motivo (≤200 caratteri) |
| D01 | input / system keys | untested | src/kernel/input.c:145 (ritorna INPUT_HOME su HID_QUIT_PS) | src/rgb30/ui.c:1084 (tab=TAB_GAMES su HID_QUIT_PS); src/rgb30/bm_input.c:92 (inoltra hid_quit_pressed) | needs-hardware-test | status untested; toml stale (sostiene che PS/Esc non siano mai letti su RGB30, ma il codice li legge gia'); attende test su hardware |
| D02 | bluetooth | untested | src/kernel/main.c:280 (bt_start al boot se bt_paired) | src/rgb30/main.c:333 (bt_start al boot se bt_paired) | needs-hardware-test | status untested; toml stale (sostiene che BT parta solo aprendo la riga di pairing, ma parte gia' al boot); attende test su hardware |
| D03 | players | suspended | src/kernel/settings.c:378 (loop Player 1..4 in Settings) | src/kernel/settings.c:371 (mostra solo riga fissa Player 1) | accept-proposed | toml: sospeso dall'utente il 2026-10-13 in attesa di tastiera a schermo; accept-proposed rispetta la decisione lasciando multiplayer a dopo |
| D04 | menu keys | open | src/kernel/carts.c:1433 (gestione Tab, 1-5, U da tastiera) | src/rgb30/ui.c:1083 (legge solo pad_pressed, nessun tasto tastiera) | accept-hardware | discende da capability no-keyboard e pad-only (interfaccia guidata solo da pad fisico, senza tasti funzione/lettere ASCII) |
| D05 | menu pointer | suspended | src/kernel/carts.c:1507 (pointer_update: hover, click, rotella, tasto destro) | - (nessun puntatore mouse in src/rgb30/ui.c, solo pad) | accept-hardware | discende da capability pad-only e no-keyboard; toml: sospeso dall'utente il 2026-10-13 in attesa di sistema stick/tastiera |
| D06 | launch | untested | src/kernel/carts.c:1673 (ask = ASK_SWITCH se gioco sospeso) | src/rgb30/ui.c:1159 (asking = ASK_SWITCH per gioco sospeso) | needs-hardware-test | status untested; toml stale nella descrizione (dice chiusura silente, ma ASK_SWITCH e' implementato); attende prova su console |
| D07 | frozen game | untested | src/kernel/carts.c:1236 (.running = is_suspended) | src/rgb30/ui.c:984 (.running = is_suspended) | needs-hardware-test | status untested; toml stale nella descrizione (dice no badge, ma .running e' presente in entrambi); attende prova su console |
| D08 | delete a game | untested | src/kernel/carts.c:1032 (controlli netxfer_updating e is_suspended) | src/rgb30/ui.c:818,822 (controlli netxfer_updating e is_suspended) | needs-hardware-test | status untested; toml stale nella descrizione (dice does neither, ma controlli implementati); attende prova su console |
| D09 | game list | open | src/kernel/carts.c:156 (scansione .bme per Dev); 426 (scansione /carts) | src/rgb30/ui.c:248 (scansione /bm); 259 (ignora .bme) | fix-before-beta | toml: /carts accettato per capability storage-path (2026-10-13); fix-before-beta rispetta la decisione richiedendo solo scansione .bme per Dev |
| D10 | covers | open | src/kernel/carts.c:268 (read_cover in fiber background) | src/rgb30/ui.c:272 (load_cover sincrono nello scan) | fix-before-beta | tocca Games (caricamento sincrono delle cover blocca la UI fino a 64 giochi su RGB30); cause != hardware |
| D11 | game options (X / C) | open | src/kernel/carts.c:929 (copia .bme), 934 (publish), 937 (send) | src/rgb30/ui.c:731 (enum opzioni prive di copia, publish, send) | fix-before-beta | tocca menu opzioni giochi (mancano copia modificabile, publish e send su menu RGB30); cause != hardware |
| D12 | cover details | open | src/kernel/carts.c:1260 (mostra autore, tipo, KiB e percorso) | src/rgb30/ui.c:1020 (mostra solo riga suggerimento Start+Select) | fix-before-beta | tocca Games e menu (dettagli autore, dimensione cartuccia e nota prestazioni mancanti su RGB30); cause != hardware |
| D13 | Dev tab and Market Apps | untested | src/kernel/carts.c:402 (bm_tool_id_of_file filtra i tool) | src/rgb30/ui.c:261 (bm_tool_id_of_file filtra i tool) | needs-hardware-test | status untested; toml stale (sostiene che Market App non sia riconosciuta, ma bm_tool_id_of_file e' attivo); attende prova |
| D28 | a game built in /carts and the Games tab | open | src/kernel/carts.c:426 (scansione /carts) | src/rgb30/ui.c:248 (scansiona solo bm; corretta su claude/integrazione-ottobre@32c4ec4b) | fixed-on-branch | corretta su claude/integrazione-ottobre@32c4ec4b (scansione unificata di /bm e /carts per cartucce compilate) |
| D30 | screen | open | src/kernel/menu_ui.c:1189 (sw=640, scale con menu_scale) | src/kernel/menu_ui.c:1184 (sw=360, scale=1 per display 720x720) | accept-hardware | discende da capability square-panel (pannello nativo quadrato 720x720 su RGB30 vs uscita TV HDMI 16:9 su Pi) |
| D31 | game screen | open | src/bm/runtime.c:7107 (letterboxing 16:9 per giochi quadrati) | src/bm/runtime.c:7099 (boxed=0, riempie il pannello 1:1) | accept-hardware | discende da capability square-panel (pannello quadrato nativo 720x720 su RGB30 senza letterboxing 16:9) |
| D32 | confirm / back | open | src/kernel/settings.c:428 (A conferma, B annulla fisso) | src/rgb30/pad.c:10 (B conferma, A annulla di default; confirm=a) | accept-hardware | discende da capability pad-only (layout Nintendo B conferma / A annulla di default, con switch confirm=a) |
| D33 | game buttons | open | src/usb/hid.c:743 (croce DS4 mappata per posizione a HID_A) | src/rgb30/bm_input.c:30 (pad mappato per lettere serigrafate) | accept-hardware | discende da capability pad-only (pad integrato mappato sulle lettere serigrafate anziche' DS4 per posizione) |
| D34 | system keys | open | src/bm/runtime.c:5082 (F11 fullscreen, F12 aiuto tasti) | src/rgb30/bm_input.c:96 (combo uscita); src/bm/runtime.c:5065 (combo screenshot) | accept-hardware | discende da capability no-keyboard e pad-only (combo Select+L1+R1 e Start+Select su pad anziche' tasti funzione F11/F12) |
| D35 | text input | open | src/usb/hid.c:262 (gestione tastiera fisica USB/BT) | rgb30.mk:66 (nessun host USB; solo BT, no tastiera a schermo) | accept-hardware | discende da capability no-keyboard e causa hardware nel toml; accept-hardware la rispetta (tastiera a schermo affidata a TODO) |
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
4. D12 (priority 6) — cover details (dettagli autore, tipo, dimensione cartuccia e nota prestazioni)

## Righe citate per verifica SPV
- rgb30.mk:66
- src/bm/runtime.c:5065
- src/bm/runtime.c:5082
- src/bm/runtime.c:7099
- src/bm/runtime.c:7107
- src/kernel/carts.c:156
- src/kernel/carts.c:402
- src/kernel/carts.c:426
- src/kernel/carts.c:929
- src/kernel/carts.c:1032
- src/kernel/carts.c:1035
- src/kernel/carts.c:1236
- src/kernel/carts.c:1260
- src/kernel/carts.c:1433
- src/kernel/carts.c:1507
- src/kernel/carts.c:1673
- src/kernel/main.c:280
- src/kernel/market.c:25
- src/kernel/market.c:27
- src/kernel/menu_ui.c:1184
- src/kernel/menu_ui.c:1189
- src/kernel/settings.c:371
- src/kernel/settings.c:378
- src/kernel/settings.c:428
- src/net/netxfer.c:182
- src/rgb30/bm_input.c:30
- src/rgb30/bm_input.c:92
- src/rgb30/bm_input.c:96
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
- src/rgb30/ui.c:1083
- src/rgb30/ui.c:1084
- src/rgb30/ui.c:1159
- src/usb/hid.c:262
- src/usb/hid.c:743
