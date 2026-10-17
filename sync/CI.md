---
schema: 1
id: CI
state: WORKING
task: D05-STICK-b
waiting_on: 
progress: 0
eta: 2026-10-17T18:00+02:00
wakeup: no
ack: 2026-10-17T16:29+02:00
updated: 2026-10-17T16:40+02:00
---
# CI — operatore CI

> **Scrive solo CI** (gli altri leggono). Il supervisore (SPV) legge questo file e aggiorna la tabella §1b di `SYNC.md`.
> Aggiornare il blocco con `python tools/sync_hdr.py CI --state ... --task ... [--waiting-on] [--progress] [--eta]`, poi commit e
> `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`. Almeno ogni 15 min e a ogni task conclusa.

## Profilo
- Dove gira: Claude Code desktop locale, Windows 11, worktree git; build/test in WSL (`bm-wsl`). Rotture: sessione chiusa dall'utente, niente timer.
- `wakeup no`: non mi risveglio da solo; lavoro a passi con `eta` nel blocco.
- Modelli: Sonnet 5.5 (principale); subagent sì: bm-coder/bm-mechanic/bm-light, bm-ops (comandi). Niente bm-deep.
- Competenze: CI GitHub Actions, test QEMU, Python. Limiti: la CI vera si vede solo dopo un push del branch di lavoro; nessun Pi/RGB30.
- Strumenti: python 3.14.7, git 2.53, gh autenticato (f-accomando), WSL con make 4.4.1, arm-none-eabi-gcc 14.2.1, aarch64-linux-gnu-gcc 15.2.0, qemu 10.2.1.

## Requisiti
- python 3.x nel PATH: SÌ (3.14.7)
- WSL con toolchain per make e QEMU: SÌ
- git con push su origin: SÌ
- Nessun Pi/RGB30: confermato
- `gh` autenticato: SÌ
- QEMU 8.2 (versione della CI): PARZIALE. In WSL è 10.2.1 (arm e aarch64): i test che dipendono dalla versione (screendump, timing: D-SCREEN, D-RGB) possono comportarsi diversamente dalla CI; il confronto va fatto col log CI dopo un push.

## Task

## Consegne
DELIVERED CI-1 @ claude/ci-repair@3486018c · verifica: parse YAML in WSL (push ignora sync/**, concurrency, timeout 5/30/45/30/30) → OK; effetto reale visibile solo nella CI · 2026-10-15T10:55+02:00
(una riga per work order finito: `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: {comando} → {risultato} · {ISO}`)
DELIVERED D-MON @ claude/ci-repair@9fe1f325 · verifica: make test selettivo -k test_monitor_line (WSL, qemu 10.2.1) → 1/1 passed · 2026-10-15T11:20+02:00
DELIVERED D-RES @ claude/ci-repair@2c0a91b9 · verifica: make test-res (WSL) → 34/34 (prima 33/34) · 2026-10-15T11:35+02:00
DELIVERED D-LOAD @ claude/ci-repair@c8e2cf22 · verifica: tests/qemu_test.py -k test_projects (WSL) → 1/1 passed · 2026-10-15T12:00+02:00
DELIVERED D-SCREEN @ claude/ci-repair@a170e9e4 · verifica: tests/qemu_test.py -k test_publish e -k test_studio_animator (WSL) → 1/1 passed ciascuno · 2026-10-15T12:35+02:00
DELIVERED D-RGB @ claude/ci-repair@baffa299 · verifica: tests/rgb30/qemu_test.py --build build/rgb30-virt -k bm_cartridge (WSL) → PASS · 2026-10-15T13:20+02:00
DELIVERED D-RGB2 @ claude/ci-repair@8da839c7 · verifica: make -f rgb30.mk PLAT=virt all + tests/rgb30/qemu_test.py -k el2 (WSL) → PASS, make TARGET=rgb30 → OK; prova su console vera (easy_install.sh) all'utente · 2026-10-15T14:05+02:00
DELIVERED CI-AUDIT @ claude/ci-repair@08d2c670 · verifica: python3 scripts/ci_audit.py → exit 0; con ci.yml senza un timeout-minutes → exit 1; docs_audit.py score → 6255, 0 penalità · 2026-10-15T11:57+02:00
DELIVERED D-HOST @ claude/ci-repair@792554f5 · verifica: make test-loading (WSL) → exit 0, 20/20 checks · 2026-10-15T12:10+02:00
DELIVERED D-RUN2 @ claude/ci-repair@6b2d8727 · verifica: tests/qemu_test.py -k editor_mouse (WSL) → 1/1 passed; make test-impostor (WSL) → exit 0 · 2026-10-15T12:18+02:00
DELIVERED CI-25 @ claude/integrazione-ottobre@0ba07260 · verifica: gh run 39030418735 → success (cherry-pick R-NET9, nessun rerun) · 2026-10-17T14:40+02:00
DELIVERED DIV-1 @ claude/spec-divergences-d03-d05-d11@008af2a1 · verifica: python scripts/consistency.py check -> exit 0 · 1 file · non verificato su hardware · 2026-10-17T16:45+02:00
DELIVERED D05-STICK-a @ claude/rgb30-stick-pointer@d80e1dfd · verifica: make test-stickptr -> ok; make e make TARGET=rgb30 -> exit 0; consistency.py check -> exit 0 · 7 files (rgb30.mk in piu: stickptr.c va nella lista del kernel RGB30) · non verificato su hardware · 2026-10-17T17:20+02:00

## Progressi

## Problemi
- CI-25: ci.yml gia con branches-ignore sync/** su bm-core e sync (nessun ramo ci-sync-ignore); nessuna run annullata (nessuna superata).
- D-SCREEN parziale: test_editor_mouse passa in locale, da confermare in CI. D-RGB2: ho aggiunto un ASSERT in src/rgb30/kernel.ld (fuori perimetro del WO) perche' il .if di gas non valuta la differenza fra etichette; copy_up_end e' globale.
- D-RGB test_bm_cartridge: `q` ora sospende il gioco (dal 2026-10-12, "bm: \"Pi cart\" suspended"), le statistiche frame arrivano solo alla chiusura. Il test ora aspetta "suspended"; il controllo frames>100 è tolto.
- D-RGB test_el2_image_relocation, DIAGNOSI (serve decisione SPV, è start.S): riprodotta in locale, nessun output seriale. Con `-kernel` QEMU carica l'immagine a 0x40200000 (link 0x40080000): start.S copia in avanti (copy_up) ~1,86 MB sopra se stesso. Dopo 0x180000 byte (1,5 MiB) la destinazione raggiunge il cuore del ciclo di copia (0x40200070) e lo sovrascrive: `Undefined Instruction` a ELR 0x40200074. Ipotesi: l'immagine ha superato 1,5 MiB (kernel8.img 1.859.152 B) e prima il ciclo non veniva toccato. Fix possibili (decide SPV): (a) copiare con un ciclo già spostato/copiato in un luogo sicuro, o in blocchi da una posizione che non si sovrappone al codice; (b) text_offset/KERNEL_BASE di virt diversi, ma il test non proverebbe più la rilocazione. Non l'ho toccato (fuori perimetro, WO: fermarsi). Vale anche per U-Boot reale se carica a più di 1,5 MiB sopra il link (da verificare con l'utente).
- test_editor_mouse: passa in locale; da confermare col run CI di claude/ci-repair (a170e9e4 ora in corso, baffa299 dopo).
- D-SCREEN: NON causa comune. test_publish: la riga "Export .b16" (carts.c:926, 2026-10-12) ha spostato "Publish to the Market" di una posizione (servono 10 "s", non 9). test_studio_animator: il messaggio dell'Animator ora è "N sprites and M boxes put in sheet" (carts/animator/main.lua:1685). test_editor_mouse: passa in locale (qemu 10.2.1): sospetto timing/versione QEMU, da confermare col run CI.
- D-LOAD: causa = non Makefile:195 né overbit. Dal 2026-10-10 (carts.c, aeb0a14b) la suite non mostra lo splash, quindi "bm: loaded in" non arriva mai per un progetto aperto nell'SDK; falliva anche in locale. Tolta l'attesa, il test controlla lo schermo. Lo stesso vale per altri test che aspettino lo splash dopo l'apertura di un tool (nessun altro caso in qemu_test.py).
- D-MON: la diagnosi del WO (timeout 10 s troppo corto) non basta: alzare a 40 s falliva ancora. "back to the monitor" è stampato solo da carts.c:2037 (uscita dal menu), mai dalla riga b3d; l'attesa a tests/qemu_test.py:5155 era obsoleta e l'ho tolta (le attese successive coprono lo stesso flusso). Da rivedere in CI.
- Overbit Makefile:195/:645: fuori perimetro (decisione 2), da segnalare a SPV quando D-LOAD lo conferma.
- CI host (run baffa299): `make build/host/test_loading` non linka, `undefined reference to synth_kit_find` (presets.c usa synth.c, che il Makefile:464 non include). Non è in nessun WO, rotto da prima; rimedio probabile: aggiungere src/audio/synth.c alle dipendenze e alla riga di link. Serve un tuo OK o un WO D-HOST.
- D-RUN2 qemu 3/4: test_editor_mouse falliva nella parte SDK ("'code' not on screen", riga 0 vuota): leggeva un secondo screendump dopo il look; ora usa il testo dello stesso look ("OPEN IN" + "code"). Locale 1/1; da confermare in CI.- D-RUN2 host: make test-impostor, solo 'crowd' (eroi in posa): in CI gli checksum ARM 9926f79d e GPU f32adb17 differiscono da 12a000fd e d0191095, in locale (gcc 15.2, WSL) coincidono; le altre scene passano. Sospetto: posa con sin/cos/FMA diversi fra gcc 13 della CI e il mio. NON corretto: cambiare gli hash o il controllo e' una decisione (tests/bm/test_impostor.c:280,287). Proposta: per 'crowd' sostituire l'uguaglianza esatta con il confronto ARM/GPU gia' presente (<=410 px) o un WO D-IMP con gli hash della CI.
- D-IMP run 38719633913 (8e182236): passo 1 NON basta, crowd ancora 9926f79d/f32adb17 in CI (in locale invariati: -ffp-contract=off non cambia nulla su x86). Passo 2 applicato in 76a2b136: per crowd gli hash non sono piu' asseriti (stampati come informazione), resta il confronto ARM/GPU; indebolimento voluto, copertura residua = ARM/GPU (<=410 px) e le altre scene. Locale: make test-impostor exit 0.- qemu 1/4 test_mesh (rosso solo in quel run, verde nel precedente): lo screendump catturato a meta' ridisegno (riga d'intestazione '??? list?') passava le parole attese ma non la stringa asserita dopo. Fix in 78e4062e: la stringa e' ora fra le attese di screen(). Locale -k mesh 3/3. Flake da dump a meta' redraw, come test_editor_mouse.
- host run 38737001683 (78e4062e): rosso solo test-studio (sdk_host.lua:575, carts/editor/main.lua:1614 'attempt to index a nil value'). Causa PREESISTENTE su bm-core: step_zoom usa ZOOM_STEPS mai definito nell'editor (esiste solo in carts/sdk_b16/main.lua:1916); '-' / '+' della mappa crashavano anche per l'utente. Fix di una riga in c38271f7 (local ZOOM_STEPS = {1,2,4,8,16} prima di step_zoom), fuori perimetro CI: dichiarato. Locale: make test-studio exit 0. Crowd e test_mesh non sono piu' rossi nel run.
- host run 38750609431 (c38271f7): test-prompts, 9 FAIL '(null)'. Causa PREESISTENTE (d0e819f0, chip del mouse): tests/ui/test_prompts.c non aveva i nomi dei 3 PROMPT_MOUSE* in names[] e pretendeva larghezza >=16 / >=altezza, ma il mouse e' 11 px (9 nel chip piccolo) per scelta. Fix solo test in 065f102b: nomi, controllo dedicato del mouse (11x16, angoli liberi), minw 9 nei chip. Locale: make test-prompts ok. Non verificato: altri test host dopo prompts (si vede dal run).
- 2026-10-16 ci-repair pushato (8de0384d, merge di bm-core). Ultimo run verde su claude/ci-repair: 38867262799 (6e0b5b40). Run rossi sui 5 rami: ledger-add, skills-review-ci-triage, translate-skill, beta6-triage, net-bridge-durable mostrano SOLO i fallimenti preesistenti (test_res 'a game of before', test_monitor_line, test_studio_animator, test_publish, rgb30 el2/ready, qemu 1/3/4): nessuno per colpa propria, sono rami su base bm-core senza ci-repair. Run sul sync: 98 rossi su 100 (push di soli file di sync). ci-repair ha branches-ignore sync/**, ma un push su sync/* usa il ci.yml del branch sync stesso: serve portare quella riga anche li' (proposta a SPV, non applicata).
- 2026-10-16 esito: merge 8de0384d VERDE in CI (run 38869985415, come 6e0b5b40). Rami con ultimo run rosso e senza ci-repair (tutti su base precedente): verificati nei log (solo fallimenti preesistenti): ledger-add, skills-review-ci-triage, translate-skill, beta6-triage, net-bridge-durable. NON verificati (gh in timeout TLS): agy/agent-audit-baseline, api-lint, api-source, beta-quickstart, sync-perimeter-quarantine, claude/integrazione-ottobre, sdk-stages-template; stesso sospetto, da riverificare dopo il merge con ci-repair. Candidate obsolete (proposta, non cancellato): rami remoti gia' in bm-core: claude/docs-english, exciting-galileo-9gevjm, organic-recipes, remove-meshy-workflows, roadmap-restructure; run rossi di rami senza piu' remoto: agy/d28-games-carts, freeze-reports, sdlog-prev-fix, claude/net-keys, net-logs, sdk-games-message. bm-core ha l'ultimo run rosso (38760262933, 15 ott) ma precede ci-repair.

## Richieste
