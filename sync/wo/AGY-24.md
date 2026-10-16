# AGY-24 - IT2EN-CIAUDIT: `scripts/ci_audit.py` passa dall'italiano all'inglese (docstring, commenti, messaggi, help) senza toccare la logica
Assegnato ad AGY (16 ott), indipendente dagli altri WO (AGX-11 scriverà un test su questo script, e legge solo identificatori e prefissi inglesi: non dipende dalla traduzione). Lo script è nato con testi italiani; il repo è in inglese. È solo una traduzione di testi: nessuna condizione, nessun nome, nessun codice cambia.
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/ci-audit-english C:\Users\Utente\Documents\GitHub\bm33-agy24 origin/claude/integrazione-ottobre`); branch `claude/ci-audit-english`. Mai push su bm-core; push solo di questo branch. (Il file `scripts/ci_audit.py` è identico anche su `origin/bm-core`: la base resta quella del catalogo.)
Perimetro: `scripts/ci_audit.py`, 38 righe di testo (tabella sotto). Nient'altro.
NON toccare: nessun altro file (`.github/workflows/ci.yml`, `tests/*`, `Makefile`, `docs/*`, `SYNC.md`, `sync/*`); in `ci_audit.py` nessuna riga che non sia nella tabella; nessuna condizione, nessuna espressione regolare, nessun nome di variabile o funzione, nessun f-string tranne le sole parole italiane. Restano INVARIATI: i prefissi `PROBLEM [ci]`, `PROBLEM [waits]`, `INFO`, `WARN`, `SUMMARY:` (`SUMMARY: OK, 0 problems`, `SUMMARY: {n} problem(s) found`), e ogni identificatore e valore tecnico (`timeout-minutes`, `concurrency`, `branches-ignore`, `sync/**`, `GAMES`, `KNOWN_WAITS`, `on.push`, `carts/<game>.bm`). Il tipo di virgolette di ogni riga resta lo stesso, l'indentazione pure.
Vincoli: solo host, nessun WSL necessario (il `python` di Windows basta), nessun hardware (`not verified on hardware`). I numeri di riga della tabella sono stati aperti dal planner su `origin/claude/integrazione-ottobre` @ `f5cd71b7` (e coincidono su `bm-core`) ma possono essere spostati: per ogni riga cerca il testo "attuale" con `git grep -n -F "<un pezzo distintivo>" -- scripts/ci_audit.py` (UNA riga) e NON fidarti del numero; se una riga "attuale" non la ritrovi identica (esattamente UNA volta), `BLOCKED` e scrivi quale. Nota: il catalogo elencava 32 righe; la tabella ne ha 38 perché mancavano le righe 20, 158, 188, 196, 209 e 212.
Obiettivo: nessuna parola italiana in `ci_audit.py`; l'uscita (righe e exit code) dello script è uguale a prima salvo il testo tradotto.

## Stato reale (verificato dal planner: ho applicato questa stessa tabella a una copia fuori dal repo, `py_compile` OK, e confrontato l'uscita prima/dopo)
- Su Windows senza PyYAML, prima e dopo l'uscita è di 25 righe, exit 1, e l'UNICA riga diversa è `PROBLEM [ci] PyYAML non installato (pip install pyyaml)` → `PROBLEM [ci] PyYAML not installed (pip install pyyaml)`. Con PyYAML installato `check_ci` esamina davvero `.github/workflows/ci.yml` (vedi sotto: passo misura).
- Controllo parole italiane: `git grep -cE "<elenco sotto>" -- scripts/ci_audit.py` dà 36 oggi e 0 dopo.
- Fine riga: `git ls-files --eol scripts/ci_audit.py` mostra `w/lf` o `w/crlf`: tieni quello del file in ogni riga cambiata. Se `git diff --stat` dice molto più di 38 righe cambiate, hai cambiato i fine riga: `git checkout -- scripts/ci_audit.py` e rifai.

## AGY-24a - misura PRIMA (bm-ops, 10 min)
Dalla radice del worktree (cartella `build` ignorata da git: `mkdir build` se manca):
1. `python scripts/ci_audit.py > build/ci_before.txt; "EXIT=$LASTEXITCODE"` (bash: `python scripts/ci_audit.py > build/ci_before.txt; echo EXIT=$?`). Segna EXIT e il numero di righe `PROBLEM`: `(Select-String -Path build/ci_before.txt -Pattern '^PROBLEM').Count` (bash: `grep -c '^PROBLEM' build/ci_before.txt`).
2. Se la prima riga `PROBLEM [ci]` dice `PyYAML non installato`: lo script non controlla davvero il workflow. Per misurare bene installa PyYAML (`python -m pip install --user pyyaml`, fuori dal repo; scrivilo nella consegna) e ripeti il punto 1. Se non puoi installarlo: prosegui lo stesso e scrivi nella consegna `non verificato: check_ci con PyYAML` (la traduzione dei messaggi di `check_ci` allora non è provata a runtime; vale il controllo del punto 3 di AGY-24c).
3. Fixture sbagliata per far scattare i messaggi di `check_ci` (solo con PyYAML): crea `build/ci_bad.yml` con questo contenuto ESATTO:
```
name: x
on:
  push:
    branches: [main]
jobs:
  host:
    runs-on: ubuntu-latest
```
poi `python scripts/ci_audit.py --ci build/ci_bad.yml > build/bad_before.txt; "EXIT=$LASTEXITCODE"`. Atteso (con PyYAML): EXIT=1 e 3 righe `PROBLEM [ci]` (job 'host' senza timeout-minutes; concurrency mancante; branches-ignore senza sync/**).

## AGY-24b - la traduzione (bm-light, 20 min)
Applica ESATTAMENTE le 38 sostituzioni della tabella, una riga alla volta, ciascuna sulla riga che contiene il testo "attuale". Il rientro (spazi iniziali) è già parte del testo mostrato: non cambiarlo. Dopo ogni gruppo di 10 sostituzioni lancia `python -m py_compile scripts/ci_audit.py` (nessuna uscita = ok).

Riga ~2
attuale:
```
"""Audit CI: workflow .github/workflows/ci.yml e timeout dei test QEMU.
```
nuova:
```
"""CI audit: the .github/workflows/ci.yml workflow and the timeouts of the QEMU tests.
```

Riga ~4
attuale:
```
Uso: python scripts/ci_audit.py [--gh] [--ci PATH]
```
nuova:
```
Usage: python scripts/ci_audit.py [--gh] [--ci PATH]
```

Riga ~6
attuale:
```
Exit 0 se nessun problema, 1 se ne viene trovato almeno uno.
```
nuova:
```
Exit 0 when there is no problem, 1 when at least one is found.
```

Riga ~7
attuale:
```
Le righe INFO e WARN non influenzano l'exit code.
```
nuova:
```
INFO and WARN lines do not affect the exit code.
```

Riga ~20
attuale:
```
except ImportError:  # gestito in check_ci
```
nuova:
```
except ImportError:  # handled in check_ci
```

Riga ~32
attuale:
```
# chiave (sottostringa del needle di expect) -> timeout minimo in secondi
```
nuova:
```
# key (substring of the expect needle) -> minimum timeout in seconds
```

Riga ~35
attuale:
```
# expect("needle", ... timeout=<int>): needle solo se letterale (no f-string)
```
nuova:
```
# expect("needle", ... timeout=<int>): needle only if literal (no f-string)
```

Riga ~71
attuale:
```
    """Check 1 (timeout-minutes per job, concurrency) e check 2 (sync/** in branches-ignore)."""
```
nuova:
```
    """Check 1 (timeout-minutes per job, concurrency) and check 2 (sync/** in branches-ignore)."""
```

Riga ~74
attuale:
```
        problem("ci", "PyYAML non installato (pip install pyyaml)")
```
nuova:
```
        problem("ci", "PyYAML not installed (pip install pyyaml)")
```

Riga ~77
attuale:
```
        problem("ci", f"{name}: file non trovato")
```
nuova:
```
        problem("ci", f"{name}: file not found")
```

Riga ~82
attuale:
```
        problem("ci", f"{name}: YAML non valido ({e})")
```
nuova:
```
        problem("ci", f"{name}: invalid YAML ({e})")
```

Riga ~85
attuale:
```
        problem("ci", f"{name}: il documento YAML non e' un mapping")
```
nuova:
```
        problem("ci", f"{name}: the YAML document is not a mapping")
```

Riga ~88
attuale:
```
    # Check 1: ogni job ha timeout-minutes; workflow ha concurrency top-level.
```
nuova:
```
    # Check 1: every job has timeout-minutes; the workflow has a top-level concurrency.
```

Riga ~91
attuale:
```
        problem("ci", f"{name}: 'jobs' non e' un mapping")
```
nuova:
```
        problem("ci", f"{name}: 'jobs' is not a mapping")
```

Riga ~95
attuale:
```
                problem("ci", f"{name}: job '{job_id}' senza timeout-minutes")
```
nuova:
```
                problem("ci", f"{name}: job '{job_id}' without timeout-minutes")
```

Riga ~97
attuale:
```
        problem("ci", f"{name}: concurrency top-level mancante")
```
nuova:
```
        problem("ci", f"{name}: top-level concurrency missing")
```

Riga ~99
attuale:
```
    # Check 2: on.push.branches-ignore contiene 'sync/**'.
```
nuova:
```
    # Check 2: on.push.branches-ignore contains 'sync/**'.
```

Riga ~100
attuale:
```
    # PyYAML legge la chiave 'on' come booleano True.
```
nuova:
```
    # PyYAML reads the key 'on' as the boolean True.
```

Riga ~103
attuale:
```
        problem("ci", f"{name}: chiave 'on' mancante")
```
nuova:
```
        problem("ci", f"{name}: key 'on' missing")
```

Riga ~107
attuale:
```
        problem("ci", f"{name}: on.push mancante o senza branches-ignore")
```
nuova:
```
        problem("ci", f"{name}: on.push missing or without branches-ignore")
```

Riga ~113
attuale:
```
        problem("ci", f"{name}: on.push.branches-ignore non contiene 'sync/**'")
```
nuova:
```
        problem("ci", f"{name}: on.push.branches-ignore does not contain 'sync/**'")
```

Riga ~117
attuale:
```
    """Check 3: expect() con needle noto e timeout inferiore al minimo in KNOWN_WAITS."""
```
nuova:
```
    """Check 3: expect() with a known needle and a timeout below the minimum in KNOWN_WAITS."""
```

Riga ~133
attuale:
```
    """Legge la lista GAMES dalla riga 'GAMES := ...' del Makefile."""
```
nuova:
```
    """Reads the GAMES list from the 'GAMES := ...' line of the Makefile."""
```

Riga ~143
attuale:
```
    """Check 4 (informativo): test functions che referenziano carts/<game>.bm."""
```
nuova:
```
    """Check 4 (informational): test functions that reference carts/<game>.bm."""
```

Riga ~158
attuale:
```
        info("nessuna test function riferisce carts/<game>.bm")
```
nuova:
```
        info("no test function references carts/<game>.bm")
```

Riga ~169
attuale:
```
    """Check 5 (con --gh, informativo): durata media e quota di successi delle ultime run."""
```
nuova:
```
    """Check 5 (with --gh, informational): average duration and success rate of the latest runs."""
```

Riga ~184
attuale:
```
        warn(f"gh non disponibile o fallito ({e}): statistiche CI saltate")
```
nuova:
```
        warn(f"gh not available or failed ({e}): CI statistics skipped")
```

Riga ~188
attuale:
```
        warn(f"gh run list fallito (exit {r.returncode}): {detail}: statistiche CI saltate")
```
nuova:
```
        warn(f"gh run list failed (exit {r.returncode}): {detail}: CI statistics skipped")
```

Riga ~193
attuale:
```
        warn("output di gh run list non e' JSON: statistiche CI saltate")
```
nuova:
```
        warn("gh run list output is not JSON: CI statistics skipped")
```

Riga ~196
attuale:
```
        info("gh: nessuna run di ci.yml")
```
nuova:
```
        info("gh: no runs of ci.yml")
```

Riga ~209
attuale:
```
    avg = f"{sum(durations) / len(durations):.1f} min" if durations else "n/d"
```
nuova:
```
    avg = f"{sum(durations) / len(durations):.1f} min" if durations else "n/a"
```

Riga ~212
attuale:
```
        f"CI ultime {len(runs)} run: durata media {avg} (su {len(durations)}), "
```
nuova:
```
        f"CI last {len(runs)} runs: average duration {avg} (over {len(durations)}), "
```

Riga ~221
attuale:
```
    ap = argparse.ArgumentParser(description="Audit CI di bm (workflow e timeout dei test).")
```
nuova:
```
    ap = argparse.ArgumentParser(description="bm CI audit (workflow and test timeouts).")
```

Riga ~222
attuale:
```
    ap.add_argument("--gh", action="store_true", help="statistiche delle ultime 20 run via gh (informativo)")
```
nuova:
```
    ap.add_argument("--gh", action="store_true", help="statistics of the latest 20 runs via gh (informational)")
```

Riga ~223
attuale:
```
    ap.add_argument("--ci", default=None, help="percorso di ci.yml (default .github/workflows/ci.yml)")
```
nuova:
```
    ap.add_argument("--ci", default=None, help="path of ci.yml (default .github/workflows/ci.yml)")
```

Riga ~233
attuale:
```
            problem("waits", f"{rel(test_file)}: file non trovato")
```
nuova:
```
            problem("waits", f"{rel(test_file)}: file not found")
```

Riga ~237
attuale:
```
        info("GAMES non trovato nel Makefile: check 4 saltato")
```
nuova:
```
        info("GAMES not found in the Makefile: check 4 skipped")
```

Riga ~241
attuale:
```
        info(f"{rel(TEST_MAIN)} non trovato: check 4 saltato")
```
nuova:
```
        info(f"{rel(TEST_MAIN)} not found: check 4 skipped")
```

## AGY-24c - verifica
1. `python -m py_compile scripts/ci_audit.py` senza uscita; poi cancella `scripts/__pycache__` se compare in `git status --short` (l'hai creata tu).
2. Uscita dopo: `python scripts/ci_audit.py > build/ci_after.txt; "EXIT=$LASTEXITCODE"`. Atteso: lo STESSO EXIT di prima e lo STESSO numero di righe `PROBLEM` e di righe totali (`(Get-Content build/ci_before.txt).Count` uguale a `(Get-Content build/ci_after.txt).Count`). `git diff --no-index --stat build/ci_before.txt build/ci_after.txt` deve mostrare differenze SOLO nelle righe tradotte (senza PyYAML: la sola riga `PyYAML not installed`; con PyYAML e senza problemi: nessuna differenza, perché le righe INFO sono nomi di test). Ogni altra differenza = hai cambiato qualcosa che non era un testo: torna indietro.
3. Con PyYAML: `python scripts/ci_audit.py --ci build/ci_bad.yml > build/bad_after.txt; "EXIT=$LASTEXITCODE"`. Atteso: EXIT=1, le stesse 3 righe `PROBLEM [ci]` ma in inglese, contenenti: `job 'host' without timeout-minutes`, `top-level concurrency missing`, `on.push.branches-ignore does not contain 'sync/**'`.
4. Parole italiane rimaste: `git grep -nE "\b(non|nessun|nessuna|trovato|saltato|statistiche|della|dei|delle|dello|sono|mancante|ultime|media|senza|contiene|ogni|chiave|legge|gestito|Uso|Audit|noto|inferiore|informativo|durata|percorso|solo|letterale|minimo|secondi|viene|almeno|uno|dalla|nel|del)\b" -- scripts/ci_audit.py`. Atteso: NESSUNA riga (oggi 36).
5. `git diff --stat`: 1 file, 38 inserimenti e 38 cancellazioni (`scripts/ci_audit.py | 76 ++++++++++++++++++++++++++--------------------------`). `git status --short`: solo `scripts/ci_audit.py` (la cartella `build/` è ignorata).

Criterio di fine: punti 1-5 verificati (il 3 solo se PyYAML c'è); nessuna riga fuori tabella cambiata.
Commit: un solo commit sul branch `claude/ci-audit-english`, messaggio `ci_audit: translate docstring, comments and messages to English`. Push solo di questo branch (`git push -u origin claude/ci-audit-english`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-24 --repo C:\Users\Utente\Documents\GitHub\bm33-agy24 --branch claude/ci-audit-english --commit <sha del commit> --tokens <N> --min <M> --state FREE` (stampa la riga chat `done WO AGY-24 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-24 @ claude/ci-audit-english@<sha> · verifica: ci_audit.py prima/dopo -> stesso exit <n> e stesse <m> righe PROBLEM; py_compile OK; grep parole italiane -> 0 righe; PyYAML <presente|assente: non verificato check_ci> · 1 file, +38 -38 · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-24 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light (traduzione; testi già forniti, ma serve giudizio se una riga non coincide).
Stima: 25 minuti di lavoro; nessuna compilazione (l'audit dura meno di 2 secondi).

## Decisioni (proposte del planner, da adottare da SPV)
- D1 `"n/d"` (non disponibile) diventa `"n/a"`. Raccomandata: sì.
- D2 I messaggi tradotti restano nella stessa forma (`{name}: job '{job_id}' without timeout-minutes`): il test di AGX-11 cerca `'host'`, `timeout-minutes`, `branches-ignore`, che restano. Raccomandata: sì.
- D3 PyYAML non è installato su questo Windows (verificato dal planner): `pip install --user pyyaml` è fuori dal repo. Se non è accettabile per l'operatore, la prova di `check_ci` va fatta in CI o da SPV. Raccomandata: installare.

## Prove per l'utente
Nessuna.
