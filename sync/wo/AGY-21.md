# AGY-21 - IT2EN-D19: la `note` italiana della divergenza D19 in `docs/spec/divergences.toml` passa all'inglese
Assegnato ad AGY (16 ott), indipendente dagli altri WO. `docs/spec/` è in inglese (regola del repo); la `note` di D19 (log su SD, chiusa il 15 ott) è rimasta in italiano. È solo una traduzione di UNA riga: il significato, le date e i nomi non cambiano; `status = "resolved"` resta.
Base: `origin/claude/integrazione-ottobre` (D19 e la sua `note` esistono su quel ramo; `git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/docs-en-d19-note C:\Users\Utente\Documents\GitHub\bm33-agy21 origin/claude/integrazione-ottobre`); branch `claude/docs-en-d19-note`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/spec/divergences.toml`, UNA riga (la `note =` del blocco `id = "D19"`, oggi riga 226).
NON toccare: nessun altro file; in questo file nessuna altra riga. In particolare la `note` della divergenza D28 (oggi riga 325, `note = "test QEMU test_games_in_carts OK; provato dall'utente su RGB30 …`) è anch'essa italiana ma NON è in questo WO: lasciala com'è e segnalala al planner nella consegna. Non toccare `status`, `evidence`, `action`, `cause`, `priority` di D19, né i caratteri `"` che delimitano il valore.
Vincoli: solo documentazione, nessun WSL (il `python` di Windows basta), nessun hardware (`not verified on hardware`). Il numero di riga è stato aperto dal planner su `origin/claude/integrazione-ottobre` @ `f5cd71b7` ma può essere spostato: PRIMA di modificare ritrova la riga con `git grep -n "provato dall'utente su Pi Zero W" -- docs/spec/divergences.toml` (UNA riga, oggi la 226, subito dopo la riga `status = "resolved"` del blocco `id = "D19"`; controlla con `git grep -n -B7 "provato dall'utente su Pi Zero W" -- docs/spec/divergences.toml` che sopra ci sia `id = "D19"`). NON fidarti del numero; se le righe sono più di una o D19 non è sopra: `BLOCKED`.
Obiettivo: la `note` di D19 è inglese; `consistency.py check` resta a 0 drift; il file TOML resta valido.

## Stato reale (verificato dal planner su `f5cd71b7`)
- `python scripts/consistency.py check` ha stampato `consistency: 0 drift, 5 accepted, 0 stale`.
- Il file si legge con `tomllib` (43 blocchi `[[divergence]]`) prima della modifica.
- La riga attuale è una sola riga lunga: `note = "provato dall'utente su Pi Zero W il 2026-10-15 dopo SDLOG-FIX: lastrun, bootlog e bootprev corretti dopo reboot e shutdown; bootprev mostra la run prima. Crash volontario coperto da test_crash_report (QEMU), non provato a mano. Watchdog assente sul Pi Zero W: tempo di scrittura prima del reset non misurabile. Le password della console compaiono in chiaro nei log su SD"`.
- Fine riga: `git ls-files --eol docs/spec/divergences.toml` mostra `i/lf` e `w/crlf` o `w/lf`: la riga nuova deve avere lo stesso fine riga delle righe vicine. Se `git diff --stat` dice più di 1 riga cambiata, hai cambiato i fine riga: `git checkout -- docs/spec/divergences.toml` e rifai.

## AGY-21a - misura PRIMA (bm-mechanic, 2 min)
Dalla radice del worktree: `python scripts/consistency.py check` e segna l'ultima riga (atteso `consistency: 0 drift, 5 accepted, 0 stale`; se è diversa sulla tua base, segnala la differenza: il confronto è prima/dopo).

## AGY-21b - la traduzione (bm-light, 5 min)
Sostituisci la riga trovata con ESATTAMENTE questa (una sola riga, nessun a-capo, nessun carattere `"` dentro il testo):
```
note = "tried by the user on a Pi Zero W on 2026-10-15 after SDLOG-FIX: lastrun, bootlog and bootprev correct after reboot and shutdown; bootprev shows the previous run. A deliberate crash is covered by test_crash_report (QEMU), not tried by hand. No watchdog on the Pi Zero W: the write time before the reset cannot be measured. The console passwords appear in clear text in the SD logs"
```

## AGY-21c - verifica
1. `python scripts/consistency.py check`: atteso la stessa ultima riga di prima (`consistency: 0 drift, 5 accepted, 0 stale`).
2. `python -I -c "import tomllib; d = tomllib.load(open('docs/spec/divergences.toml','rb')); print(len(d['divergence']))"`: atteso `43` (il file è valido e ha lo stesso numero di blocchi).
3. `git grep -n "provato dall'utente su Pi Zero W" -- docs/spec/divergences.toml`: atteso NESSUNA riga. (Attenzione: `git grep -n "provato dall'utente" -- docs/spec/divergences.toml` darà ancora UNA riga, la 325 di D28: è voluto, fuori da questo WO.)
4. `git diff --stat`: 1 file, 1 inserimento, 1 cancellazione (`docs/spec/divergences.toml | 2 +-`). `git status --short`: solo quel file.

Criterio di fine: punti 1-4 verificati.
Commit: un solo commit sul branch `claude/docs-en-d19-note`, messaggio `spec: translate the D19 note to English`. Push solo di questo branch (`git push -u origin claude/docs-en-d19-note`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-21 --repo C:\Users\Utente\Documents\GitHub\bm33-agy21 --branch claude/docs-en-d19-note --commit <sha del commit> --tokens <N> --min <M> --state FREE` (stampa la riga chat `done WO AGY-21 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-21 @ claude/docs-en-d19-note@<sha> · verifica: consistency.py check -> 0 drift, 5 accepted, 0 stale; tomllib legge 43 blocchi; grep "provato dall'utente su Pi Zero W" -> 0 righe · 1 file, +1 -1 · non verificato su hardware · <ISO con fuso>`. Nella chat segnala a SPV che la `note` di D28 (riga 325) è ancora italiana.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-21 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light (traduzione; testo già fornito).
Stima: 10 minuti di lavoro; nessuna compilazione.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 La `note` di D28 (riga 325) non è nel catalogo: va in un WO separato o si aggiunge qui con una riga di SPV. Raccomandata: WO separato (una riga, 5 minuti, stesso ramo di base).
- D2 `provato dall'utente` tradotto `tried by the user` (come nel resto dei docs: "tried on the Pi"). Raccomandata: sì.

## Prove per l'utente
Nessuna.
