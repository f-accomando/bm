# AGX-6 - BUDGET-MAP-CLEAN: `carts/budget/map.csv` senza le 23 righe vuote (il `.bm` non cambia)
Assegnato ad AGX (16 ott). Il file `map.csv` della cartuccia `budget` ha una riga vuota ogni due righe di dati: la riga vuota non serve (lo script la salta) e confonde chi legge il file.
Base: `origin/claude/perf-budget-cart` @ `c18e881d` (`git fetch origin` poi, dal worktree di AGX: `git worktree add -b claude/budget-map-clean C:\Users\Utente\Documents\GitHub\bm33-agx6 origin/claude/perf-budget-cart`); branch `claude/budget-map-clean`. Mai push su bm-core; push solo di questo branch. (Stessa base di AGY-16, file diversi: nessun conflitto.)
Perimetro: solo `carts/budget/map.csv` (23 righe tolte).
NON toccare: `carts/budget/main.lua` (usa `r % 46` e resta valido), `scripts/mkbm.py`, `Makefile`, `docs/*`, `sync/*`, `SYNC.md`, i numeri e l'ordine delle 46 righe di dati.
Vincoli: nessuna prova su Pi o RGB30 (`not verified on hardware`: solo host). Il tuo worktree sta sotto `C:\Users\Utente\Documents\GitHub\` e NON è `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120` (coda di almeno 120 righe); da PowerShell il comando va fra APICI SINGOLI (così `$` non viene espanso da PowerShell; un apice singolo interno si raddoppia), da Git Bash fra apici doppi con `\$`: gli esempi qui sotto sono scritti come li vede bash dentro WSL. Il `/tmp` di WSL è condiviso e piccolo (tmpfs da 3,9 GB, a volte pieno): non cancellare ciò che non hai creato tu; per i test host lancia con `TMPDIR=$PWD/build/tmp` (prima `mkdir -p build/tmp`), per i test QEMU lascia `TMPDIR` nel `/tmp` di WSL. I numeri di riga qui sotto sono stati aperti dal planner ma possono essere spostati (il catalogo li prendeva da copie diverse): PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi come descritto: scrivilo `non verificato` e fermati con `BLOCKED`, non indovinare.
Obiettivo: `carts/budget/map.csv` ha 46 righe, nessuna vuota, e `build/carts/budget.bm` ha lo STESSO sha256 di prima.

## Stato reale (verificato dal planner su `origin/claude/perf-budget-cart` @ `c18e881d`, copia fuori dal repo, in WSL)
- `carts/budget/` contiene solo `main.lua` e `map.csv` (`git ls-tree -r origin/claude/perf-budget-cart carts/budget`). `map.csv` ha 69 righe: 46 di dati (81 celle ciascuna, `1,1,1,...`) e 23 righe vuote alle righe 3, 6, 9, ..., 69 (`awk 'NF==0{print NR}' carts/budget/map.csv`).
- `scripts/mkbm.py`, `read_map` (righe 120-125 su `bm-core`): `for line in open(path) if line.strip() and not line.startswith("#")` salta le righe vuote, quindi togliere quelle righe non cambia la mappa. `carts/budget/main.lua` riga 32: `map(0, r % 46, ...)` (46 righe di dati) resta valido.
- Attenzione al fine riga: nel repo il file è LF, ma in un worktree Windows (`core.autocrlf=true`) è CRLF. Con CRLF una riga "vuota" è `\r` e `grep -c "^$"` dà 0 ANCHE PRIMA della modifica: il controllo giusto è `grep -c "^[[:space:]]*$"` (il catalogo diceva `grep -c '^$'`: corretto qui). Togli le righe vuote lasciando agli altri righe il loro fine riga.
- Prova del planner: `make build/carts/budget.bm` PRIMA e DOPO aver tolto le righe vuote (`sed -i '/^[[:space:]]*$/d'`, poi `make -B build/carts/budget.bm`): in entrambi i casi `a59a5e4065d9a83c706460eb25b1e21686e3f3718e298393264f63391a1db3fa  build/carts/budget.bm` (14452 byte). Dopo: `grep -c '^[[:space:]]*$'` = 0, `wc -l` = 46.

## AGX-6a - la modifica (bm-mechanic o bm-coder, 10 min)
Passo 0: dalla radice del worktree: `scripts/wsl-run.ps1 'make build/carts/budget.bm && sha256sum build/carts/budget.bm' -Tail 120` e ANNOTA lo sha256 (atteso `a59a5e40...1db3fa`; se è diverso va bene lo stesso, conta il confronto prima/dopo), poi `scripts/wsl-run.ps1 'grep -c "^[[:space:]]*$" carts/budget/map.csv; wc -l carts/budget/map.csv' -Tail 120` (atteso `23` e `69`; se non sono questi: `BLOCKED`, la base è cambiata).
Passo 1: togli le righe vuote da `carts/budget/map.csv`. Comando, dalla radice del worktree: `scripts/wsl-run.ps1 'sed -i "/^[[:space:]]*$/d" carts/budget/map.csv' -Tail 120` (modifica solo quel file; mantiene il fine riga delle altre righe). In alternativa a mano: cancella le righe 3, 6, ..., 69 (tutte vuote) dal fondo verso l'inizio.
Passo 2: verifica.
- `scripts/wsl-run.ps1 'grep -c "^[[:space:]]*$" carts/budget/map.csv; wc -l carts/budget/map.csv' -Tail 120` -> `0` e `46 carts/budget/map.csv`.
- `scripts/wsl-run.ps1 'make -B build/carts/budget.bm && sha256sum build/carts/budget.bm' -Tail 120` -> lo STESSO sha256 del passo 0. Se è diverso: `git checkout -- carts/budget/map.csv` e `BLOCKED` con i due sha256.
- `git diff --stat`: solo `carts/budget/map.csv`, 23 righe tolte (`| 23 ----------------------`); nessuna riga aggiunta. Se dice 69 righe cambiate hai alterato i fine riga: `git checkout -- carts/budget/map.csv` e rifai col `sed`.
- Le righe di dati NON devono cambiare: `git diff -U0 -- carts/budget/map.csv | grep '^+[^+]' | wc -l` -> 0.

Criterio di fine: 46 righe, 0 vuote, sha256 del `.bm` identico, `git diff --stat` = solo `carts/budget/map.csv`, 23 righe tolte.
Commit: un solo commit sul branch `claude/budget-map-clean`, messaggio `budget cart: map.csv without the blank lines`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-6 --repo C:\Users\Utente\Documents\GitHub\bm33-agx6 --branch claude/budget-map-clean --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGX-6 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGX.md` aggiungi la riga standard `DELIVERED AGX-6 @ claude/budget-map-clean@<sha> · verifica: sha256 di build/carts/budget.bm uguale prima/dopo (<sha>); map.csv 46 righe, 0 vuote · 1 file, 23 righe tolte · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-6 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-mechanic (testo esatto, un comando).
Stima: 10 minuti (la compilazione del `.bm` dura pochi secondi).

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Si tolgono le righe vuote invece di insegnare allo script a ignorarle (già lo fa). Raccomandata: sì.

## Prove per l'utente
Nessuna.
