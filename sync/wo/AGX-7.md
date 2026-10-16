# AGX-7 - CODE-LINT-STALE: in bm Code i segni rossi del lint spariscono quando si modifica il testo (non restano sulle righe sbagliate)
Assegnato ad AGX (16 ott). In `carts/code/main.lua` (l'editor di codice), dopo un salvataggio il lint segna in rosso i numeri di riga con nomi API sconosciuti (`t.lint_lines`). Se poi si inserisce o si toglie una riga, i segni restano ai vecchi numeri di riga e indicano righe sbagliate finché non si risalva.
Base: `origin/claude/app5-api-help-lint` @ `d2987835` (stessa base di AGY-14) (`git fetch origin` poi, dal worktree di AGX: `git worktree add -b claude/code-lint-stale C:\Users\Utente\Documents\GitHub\bm33-agx7 origin/claude/app5-api-help-lint`); branch `claude/code-lint-stale`. Mai push su bm-core; push solo di questo branch.
Perimetro: solo `carts/code/main.lua`: 2 righe nuove (una in `snapshot`, una in `undo`).
NON toccare: `lint_tab` (la funzione del lint), il disegno del margine (`t.lint_lines[i]`, già gestisce `nil`), `t.lint` (la lista resta), qualunque altro file (`tests/qemu_test.py`, `docs/*`, `Makefile`, `sync/*`, `SYNC.md`), i file di AGY-14 (`tests/...lint...`, se li tocca).
Vincoli: nessuna prova su Pi o RGB30 (`not verified on hardware`: solo host; il nuovo comportamento si vede solo a schermo: nessun test lo copre). Il tuo worktree sta sotto `C:\Users\Utente\Documents\GitHub\` e NON è `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120` (coda di almeno 120 righe); da PowerShell il comando va fra APICI SINGOLI (così `$` non viene espanso da PowerShell; un apice singolo interno si raddoppia), da Git Bash fra apici doppi con `\$`: gli esempi qui sotto sono scritti come li vede bash dentro WSL. Il `/tmp` di WSL è condiviso e piccolo (tmpfs da 3,9 GB, a volte pieno): non cancellare ciò che non hai creato tu; per i test host lancia con `TMPDIR=$PWD/build/tmp` (prima `mkdir -p build/tmp`), per i test QEMU lascia `TMPDIR` nel `/tmp` di WSL. I numeri di riga qui sotto sono stati aperti dal planner ma possono essere spostati (il catalogo li prendeva da copie diverse): PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi come descritto: scrivilo `non verificato` e fermati con `BLOCKED`, non indovinare.
Obiettivo: dopo un'azione che cambia il testo (digitare, cancellare, incollare, undo/redo) i segni rossi del lint di quella scheda spariscono e tornano al salvataggio successivo.

## Stato reale (verificato dal planner su `origin/claude/app5-api-help-lint` @ `d2987835`)
- `carts/code/main.lua`:
  - `local function snapshot(t, v, kind)` (riga 173): termina con `t.redo = {}`, `t.last_kind, t.last_line = kind, v.cy`, `t.dirty = true` (riga 179) e `end`. Ritrova con `grep -n "^local function snapshot" carts/code/main.lua` (una riga) e `grep -n "^  t.dirty = true" carts/code/main.lua`.
  - `local function undo(t, v, from, to)` (riga 182): contiene `t.last_kind, t.dirty = nil, true` (riga 187; `grep -n "t.last_kind, t.dirty = nil, true" carts/code/main.lua` deve dare UNA riga). `undo` serve sia per undo sia per redo (parametri `from`/`to`).
  - `function lint_tab(t)` (riga 729): dopo un salvataggio (righe 800 e 809, `if ok then lint_tab(t) end`) mette `t.lint` e `t.lint_lines = {}` con un `true` per ogni riga segnalata (righe 739-741); per i file non Lua/bm/bme mette entrambi a `nil` (riga 732).
  - Il disegno del margine (riga 1545): `local bad = not bp and t.lint_lines and t.lint_lines[i]`: gestisce già `lint_lines == nil` (nessun segno).
- `snapshot` è chiamato prima di ogni modifica del testo (digitazione, Invio, cancellazioni, incolla, taglia: `grep -n "snapshot(" carts/code/main.lua` mostra 17 chiamate). Limite noto, accettato: `snapshot` esce subito (riga 174, `return`) quando si continua lo stesso tipo di modifica (`kind`) sulla stessa riga, per esempio digitare altri caratteri di fila; lì il numero di righe del testo non cambia, quindi i segni restano comunque validi.
- Scelta del planner (decisione, non da rimettere in discussione): azzerare TUTTI i segni alla prima modifica, non spostare i numeri di riga. I segni tornano al salvataggio successivo (`lint_tab`, commento "after a save" riga 727).
- Il planner ha applicato le 2 righe su una copia di `d2987835` fuori dal repo (diff: solo `+  t.lint_lines = nil` dopo la riga 179 e dopo la riga 187), ha fatto `make -j4` completo (`EXIT=0`, `build/kernel.img` creato) e ha lanciato in WSL `python3 tests/qemu_test.py --build build -k code_api` -> `1/1 passed` e `-k code_editor` -> `1/1 passed`. Il comportamento nuovo (segni che spariscono a schermo) NON è stato verificato da nessun test: `non verificato`.
- Fine riga: `carts/code/main.lua` in Windows può avere CRLF (nel repo è LF): usa il fine riga che il file ha già; `git diff --stat` deve dire `carts/code/main.lua | 2 ++`.

## AGX-7a - le 2 righe (bm-coder, 10 min)
Passo 1: `snapshot`. Trova con `grep -n "^  t.dirty = true" carts/code/main.lua`: deve dare UNA riga (oggi la 179) e subito dopo c'è `end`. Subito DOPO quella riga aggiungi (2 spazi di rientro):
```
  t.lint_lines = nil
```
Risultato:
```
  t.redo = {}
  t.last_kind, t.last_line = kind, v.cy
  t.dirty = true
  t.lint_lines = nil
end
```
Passo 2: `undo`. Subito DOPO la riga `  t.last_kind, t.dirty = nil, true` (una sola nel file) aggiungi (2 spazi di rientro):
```
  t.lint_lines = nil
```
Risultato:
```
  t.lines, v.cx, v.cy = u.lines, u.cx, u.cy
  t.last_kind, t.dirty = nil, true
  t.lint_lines = nil
  clamp(t, v)
```
`git diff -U0 -- carts/code/main.lua | grep '^[+-][^+-]'` deve mostrare esattamente due righe `+  t.lint_lines = nil` e nessuna `-`.

Passo 3: verifica (con `scripts/wsl-run.ps1 '<cmd>' -Tail 120`):
- Controllo della sintassi Lua: `scripts/wsl-run.ps1 'make -j4 > build/make.log 2>&1; echo EXIT=$?' -Tail 120` (la prima volta dura 10-20 minuti: `run_in_background`; `make` impacchetta `carts/code/main.lua` in `build/code.bm`, un errore di sintassi lo fermerebbe). Atteso `EXIT=0`.
- `scripts/wsl-run.ps1 'python3 tests/qemu_test.py --build build -k code_api' -Tail 120` -> atteso `1/1 passed`; poi `-k code_editor` -> atteso `1/1 passed`. Questi due test controllano solo che l'editor e il lint al salvataggio non si rompano: NON verificano il comportamento nuovo (segni che spariscono alla modifica), che si vede solo a schermo -> scrivilo nella consegna come `comportamento nuovo non verificato da test`.
- Se QEMU non parte in WSL (NEXT §7): riporta le ultime 20 righe e scrivi `test QEMU non verificati`; il `make` verde e il diff restano la prova minima.

Criterio di fine: `git diff --stat` = `carts/code/main.lua | 2 ++`; `make` exit 0; `code_api` e `code_editor` `1/1 passed` (o `non verificato` con il motivo).
Commit: un solo commit sul branch `claude/code-lint-stale`, messaggio `bm Code: clear the lint marks when the text changes`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-7 --repo C:\Users\Utente\Documents\GitHub\bm33-agx7 --branch claude/code-lint-stale --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGX-7 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGX.md` aggiungi la riga standard `DELIVERED AGX-7 @ claude/code-lint-stale@<sha> · verifica: make exit 0; qemu_test -k code_api -> 1/1 passed; -k code_editor -> 1/1 passed (comportamento nuovo non verificato da test) · 1 file, 2 righe · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-7 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto).
Stima: 20 minuti di lavoro più 10-20 di build la prima volta.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Si azzerano tutti i segni invece di spostare i numeri di riga dopo ogni inserimento o cancellazione. Raccomandata: sì (2 righe, nessun caso limite). Alternativa: rimappare `lint_lines` sulle righe aggiunte o tolte (più codice, più rischio).
- D2 Il limite di `snapshot` (esce presto per la stessa riga e lo stesso `kind`) è accettato: lì le righe non si spostano. Raccomandata: sì.

## Prove per l'utente
Facoltativa, su RGB30 o Pi (non verificato su hardware): in bm Code scrivi una riga con un nome API inesistente, salva (la riga si colora in rosso nel margine), poi inserisci una riga sopra: il segno deve sparire.
