# AGY-33 - READIN-FIX: i `read_in` segnalati STALE da AGY-32 vengono corretti in `config-keys.toml`
Assegnato ad AGY (16 ott). Dopo AGY-32 si applicano i numeri che lo script stampa. Va eseguito DOPO che AGY-32 è consegnato.
Base: `origin/claude/config-readin-check` (branch di AGY-32, che deve essere consegnato prima). `git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/config-readin-fix C:\Users\Utente\Documents\GitHub\bm33-agy33 origin/claude/config-readin-check`; branch `claude/config-readin-fix`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/spec/config-keys.toml`, SOLO le righe `read_in =` che `config_readin.py` segnala STALE (tra queste `assist_lang`, riga 21, e `battery_icon`, riga 30).
NON toccare: ogni altra riga, anche la data a riga 2 (`# Date: ...`); ogni altro file; le righe UNCHECKED.
Disgiunzione: i file di questo WO non sono nei perimetri di AGY-10..25 (perimetri letti in `sync/wo/`), AGX-3..12, APP-7/8 e R-NET6..9; `docs/spec/config-keys.toml` non è nei loro perimetri.
Vincoli: solo dati, nessun WSL. `not verified on hardware`. I numeri di riga qui sotto sono stati riaperti dal planner con `git show origin/claude/integrazione-ottobre:<file>` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n -F` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: `BLOCKED`, non indovinare. Fine riga: `git ls-files --eol <file>` mostra `w/lf` o `w/crlf`: tieni quello del file; `git diff --stat` deve restare piccolo (vedi Criterio): se dice centinaia di righe hai cambiato i fine riga, `git checkout -- <file>` e rifai.
Obiettivo: `config_readin.py` -> `config_readin: 0 stale`, exit 0; `consistency.py check` uguale a prima.

## Stato reale
- Base = branch di AGY-32 (`origin/claude/config-readin-check`): contiene `scripts/config_readin.py`. Se quel branch non esiste ancora: `BLOCKED`.
- Atteso dal planner (su `origin/claude/integrazione-ottobre`): `assist_lang` `:4842` -> `:4897`, `battery_icon` `:8331` -> `:8395`; altri possibili: usa SOLO i numeri stampati dallo script.

## AGY-33a - misura PRIMA (bm-mechanic, 2 min)
`python scripts/config_readin.py > /tmp/ri_before.txt; echo "EXIT=$?"` e `python scripts/consistency.py check > /tmp/c_before.txt; echo "EXIT=$?"`.

## AGY-33b - le correzioni (bm-mechanic, 8 min)
Per ogni riga `STALE <name>: <path>:<N> -> <M>` di `/tmp/ri_before.txt`: nel blocco `name = "<name>"` di `docs/spec/config-keys.toml`, nella riga `read_in =` sostituisci `<path>:<N>` con `<path>:<M>` (se `read_in` ha più parti separate da `;`, cambia solo la parte indicata). Non toccare altro, nemmeno la data.

## Verifica
1. `python scripts/config_readin.py; echo "EXIT=$?"`: `config_readin: 0 stale`, EXIT=0.
2. `python scripts/consistency.py check` ha stessa uscita e codice di prima (diff vuoto).
3. `git diff --stat`: 1 file; ogni riga cambiata è una riga `read_in =`.

Criterio di fine: 1 file, solo righe `read_in =`, `0 stale`.
Commit: un solo commit sul branch `claude/config-readin-fix`, messaggio `spec: fix stale read_in lines of config-keys.toml`. Push solo di questo branch (`git push -u origin claude/config-readin-fix`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-33 --repo C:\Users\Utente\Documents\GitHub\bm33-agy33 --branch claude/config-readin-fix --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-33 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-33 @ claude/config-readin-fix@<sha> · verifica: config_readin.py -> 0 stale, exit 0; consistency.py check invariato · 1 file · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-33 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-mechanic (numeri forniti dallo script).
Stima: 10 minuti di lavoro.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Eseguire solo dopo AGY-32 (base = il suo branch). Raccomandata: sì.

## Prove per l'utente
Nessuna.
