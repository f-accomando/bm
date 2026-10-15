# Bozza AGY, 2026-10-15, letta su bm-core@e76900f0: classi proposte, decidono SPV e l'utente

| ID | area | status attuale | prova Pi (file:riga) | prova RGB30 (file:riga) | classe proposta | motivo (≤200 caratteri) |
| D01 | input / system keys | untested | src/kernel/input.c:145 | src/rgb30/bm_input.c:92 | needs-hardware-test | status untested e codice gia' allineato (hid_quit_pressed per PS/Esc in entrambi); attende test con pad BT fisico |
| D02 | bluetooth | untested | src/kernel/main.c:280 | src/rgb30/main.c:333 | needs-hardware-test | status untested e codice gia' allineato (bt_start al boot se bt_paired in entrambi); attende prova RTL8821CS |
| D03 | players | suspended | src/kernel/settings.c:371 | src/rgb30/bm_input.c:75 | fix-before-beta | tocca input e menu (multiplayer locale su pad BT non esposto in Settings RGB30); cause != hardware |
| D04 | menu keys | open | src/kernel/input.c:134 | src/rgb30/ui.c:1070 | accept-hardware | discende da capability no-keyboard e pad-only (RGB30 ha solo pad integrato, senza tasti funzione/lettere) |
| D05 | menu pointer | suspended | src/kernel/pointer.c:15 | src/rgb30/ui.c:1100 | accept-hardware | discende da capability pad-only e no-keyboard (nessun mouse hardware nativo presente su console RGB30) |
| D06 | launch | untested | src/kernel/carts.c:1673 | src/rgb30/ui.c:1159 | needs-hardware-test | status untested e codice gia' allineato (dialogo ASK_SWITCH per gioco sospeso presente in entrambi i kernel) |
| D07 | frozen game | untested | src/kernel/carts.c:1236 | src/rgb30/ui.c:984 | needs-hardware-test | status untested e codice gia' allineato (badge .running e opzioni resume/close presenti in entrambi i kernel) |
| D08 | delete a game | untested | src/kernel/carts.c:1034 | src/rgb30/ui.c:818 | needs-hardware-test | status untested e codice gia' allineato (controlli netxfer_updating e is_suspended presenti in entrambi) |
| D09 | game list | open | src/kernel/carts.c:265 | src/rgb30/ui.c:286 | fix-before-beta | tocca Games e Dev (manca scansione progetti .bme da aprire in SDK su RGB30); cause != hardware |
| D10 | covers | open | src/kernel/carts.c:680 | src/rgb30/ui.c:292 | fix-before-beta | tocca Games (caricamento sincrono delle cover blocca la UI fino a 64 giochi su RGB30); cause != hardware |
| D11 | game options (X / C) | open | src/kernel/carts.c:980 | src/rgb30/ui.c:745 | fix-before-beta | tocca menu opzioni giochi (mancano copia modificabile, build, publish su menu RGB30); cause != hardware |
| D12 | cover details | open | src/kernel/carts.c:1270 | src/rgb30/ui.c:1010 | fix-before-beta | tocca Games e menu (dettagli autore, dimensione cartuccia e nota prestazioni mancanti su RGB30) |
| D13 | Dev tab and Market Apps | untested | src/kernel/carts.c:115 | src/rgb30/ui.c:261 | needs-hardware-test | status untested e codice gia' allineato (bm_tool_id_of_file condiviso in toolnames.h per filtrare i tool) |
| D28 | a game built in /carts and the Games tab | open | src/kernel/carts.c:220 | src/rgb30/ui.c:250 | fixed-on-branch | corretta su branch agy/d28-games-carts@32c4ec4b (scansione unificata di /bm e /carts per cartucce compilate) |
| D30 | screen | open | src/drivers/fb.c:28 | src/rgb30/fb.c:35 | accept-hardware | discende da capability square-panel (display 720x720 1:1 su RGB30 vs uscita TV HDMI 16:9 su Pi) |
| D31 | game screen | open | src/bm/runtime.c:4820 | src/rgb30/ui.c:485 | accept-hardware | discende da capability square-panel (adattamento risoluzioni e scaling per pannello nativo quadrato) |
| D32 | confirm / back | open | src/kernel/input.c:75 | src/rgb30/bm_input.c:110 | accept-hardware | discende da capability pad-only (layout Nintendo B conferma / A annulla con opzione confirm=a su RGB30) |
| D33 | game buttons | open | src/kernel/input.c:90 | src/rgb30/bm_input.c:130 | accept-hardware | discende da capability pad-only (pad integrato mappato sulle lettere fisiche serigrafate sui pulsanti) |
| D34 | system keys | open | src/kernel/input.c:110 | src/rgb30/bm_input.c:85 | accept-hardware | discende da capability no-keyboard e pad-only (combo Select+L1+R1 e Start+Select sostituiscono tastiera) |
| D35 | text input | open | src/usb/hid.c:200 | src/rgb30/bm_input.c:60 | fix-before-beta | tocca input (tastiera a schermo e digitazione pad necessarie per consentire inserimento testo senza BT) |
| D36 | storage paths | open | src/kernel/carts.c:250 | src/rgb30/ui.c:220 | accept-hardware | discende da capability storage-path (SD RGB30 centralizzata su cartella unica /bm) |

## Conteggio per classe
- accept-hardware: 8
- fix-before-beta: 6
- fixed-on-branch: 1
- needs-hardware-test: 6
- accept-proposed: 0
- Totale voci analizzate: 21

## Lista fix-before-beta ordinata per priorità
1. D09 (priority 4) — game list (scansione .bme in Dev per aprire progetti in SDK)
2. D10 (priority 5) — covers (caricamento asincrono cover in background su RGB30)
3. D03 (priority 6) — players (gestione multiplayer locale per pad BT su RGB30)
4. D11 (priority 6) — game options (X / C) (allineamento voci opzioni gioco)
5. D12 (priority 6) — cover details (dettagli cartuccia e nota prestazioni)
6. D35 (priority non definita) — text input (tastiera a schermo e input da pad)
