---
schema: 1
id: CI
state: WAITING
task: D-RGB el2 (start.S)
waiting_on: SPV
progress: 0
eta: 2026-10-15T13:15+02:00
wakeup: no
ack: 2026-10-15T10:25+02:00
updated: 2026-10-15T11:17+02:00
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
DELIVERED D-SCREEN @ claude/ci-repair@a170e9e4 (parziale: test_publish e test_studio_animator; test_editor_mouse passa già in locale, da confermare in CI) · verifica: -k test_publish, -k test_studio_animator (WSL) → 1/1 passed ciascuno · 2026-10-15T12:35+02:00
DELIVERED D-RGB (parziale: solo test_bm_cartridge) @ claude/ci-repair@baffa299 · verifica: tests/rgb30/qemu_test.py --build build/rgb30-virt -k bm_cartridge (WSL) → PASS · 2026-10-15T13:20+02:00

## Progressi

## Problemi
- D-RGB test_bm_cartridge: `q` ora sospende il gioco (dal 2026-10-12, "bm: \"Pi cart\" suspended"), le statistiche frame arrivano solo alla chiusura. Il test ora aspetta "suspended"; il controllo frames>100 è tolto.
- D-RGB test_el2_image_relocation, DIAGNOSI (serve decisione SPV, è start.S): riprodotta in locale, nessun output seriale. Con `-kernel` QEMU carica l'immagine a 0x40200000 (link 0x40080000): start.S copia in avanti (copy_up) ~1,86 MB sopra se stesso. Dopo 0x180000 byte (1,5 MiB) la destinazione raggiunge il cuore del ciclo di copia (0x40200070) e lo sovrascrive: `Undefined Instruction` a ELR 0x40200074. Ipotesi: l'immagine ha superato 1,5 MiB (kernel8.img 1.859.152 B) e prima il ciclo non veniva toccato. Fix possibili (decide SPV): (a) copiare con un ciclo già spostato/copiato in un luogo sicuro, o in blocchi da una posizione che non si sovrappone al codice; (b) text_offset/KERNEL_BASE di virt diversi, ma il test non proverebbe più la rilocazione. Non l'ho toccato (fuori perimetro, WO: fermarsi). Vale anche per U-Boot reale se carica a più di 1,5 MiB sopra il link (da verificare con l'utente).
- test_editor_mouse: passa in locale; da confermare col run CI di claude/ci-repair (a170e9e4 ora in corso, baffa299 dopo).
- D-SCREEN: NON causa comune. test_publish: la riga "Export .b16" (carts.c:926, 2026-10-12) ha spostato "Publish to the Market" di una posizione (servono 10 "s", non 9). test_studio_animator: il messaggio dell'Animator ora è "N sprites and M boxes put in sheet" (carts/animator/main.lua:1685). test_editor_mouse: passa in locale (qemu 10.2.1): sospetto timing/versione QEMU, da confermare col run CI.
- D-LOAD: causa = non Makefile:195 né overbit. Dal 2026-10-10 (carts.c, aeb0a14b) la suite non mostra lo splash, quindi "bm: loaded in" non arriva mai per un progetto aperto nell'SDK; falliva anche in locale. Tolta l'attesa, il test controlla lo schermo. Lo stesso vale per altri test che aspettino lo splash dopo l'apertura di un tool (nessun altro caso in qemu_test.py).
- D-MON: la diagnosi del WO (timeout 10 s troppo corto) non basta: alzare a 40 s falliva ancora. "back to the monitor" è stampato solo da carts.c:2037 (uscita dal menu), mai dalla riga b3d; l'attesa a tests/qemu_test.py:5155 era obsoleta e l'ho tolta (le attese successive coprono lo stesso flusso). Da rivedere in CI.
- Overbit Makefile:195/:645: fuori perimetro (decisione 2), da segnalare a SPV quando D-LOAD lo conferma.

## Richieste
