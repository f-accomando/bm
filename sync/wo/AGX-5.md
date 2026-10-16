# AGX-5 - MAKE-TEST-TOOLS: `make test-tools` lancia i test di `tests/tools/` (e `make test-docs` usa `docs_audit.py check`, se esce 0)
**ATTENZIONE: questo WO DIPENDE da AGY-17** (`docs_audit.py check`, branch `claude/docs-audit-check`): inizia solo dopo `DELIVERED AGY-17` e `REVIEW: APPROVED` di SPV. Senza AGY-17 il comando `python scripts/docs_audit.py check` non esiste (stampa l'uso ed esce 1).
Assegnato ad AGX (16 ott). Oggi i test di `tests/tools/*.py` (`test_agent_audit.py`, `test_layout_side_by_side.py` e quelli dei WO AGX-4, 8, 9, 10, 11, 12 e AGY-16/17 quando saranno fusi) non girano da nessun target: nessuno li esegue.
Base: `bm-core` che contiene AGY-17 oppure, finché non è fuso, il suo branch approvato (`origin/claude/docs-audit-check`): (`git fetch origin` poi, dal worktree di AGX: `git worktree add -b claude/make-test-tools C:\Users\Utente\Documents\GitHub\bm33-agx5 origin/bm-core` se AGY-17 è già in `bm-core`, altrimenti `... origin/claude/docs-audit-check`; `git log --oneline -3 -- scripts/docs_audit.py` deve mostrare il commit di AGY-17, se no BLOCKED); branch `claude/make-test-tools`. Mai push su bm-core; push solo di questo branch.
Perimetro: solo `Makefile`: un target nuovo `test-tools` (3 righe + la riga `.PHONY`) e, SOLO se la condizione del passo 2 è vera, 1 riga di `test-docs`.
NON toccare: la variabile `HOST_TESTS` (la lista dei test del CI: se `test-tools` ci entra lo decide SPV), la grande riga `.PHONY` in alto, nessun altro target, `scripts/docs_audit.py`, `tests/*`, `docs/*`, `.github/workflows/*`, `sync/*`, `SYNC.md`.
Vincoli: nessuna prova su Pi o RGB30 (`not verified on hardware`: solo host; AGX-5 non lancia QEMU). Il tuo worktree sta sotto `C:\Users\Utente\Documents\GitHub\` e NON è `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120` (coda di almeno 120 righe); da PowerShell il comando va fra APICI SINGOLI (così `$` non viene espanso da PowerShell; un apice singolo interno si raddoppia), da Git Bash fra apici doppi con `\$`: gli esempi qui sotto sono scritti come li vede bash dentro WSL. Il `/tmp` di WSL è condiviso e piccolo (tmpfs da 3,9 GB, a volte pieno): non cancellare ciò che non hai creato tu; per i test host lancia con `TMPDIR=$PWD/build/tmp` (prima `mkdir -p build/tmp`), per i test QEMU lascia `TMPDIR` nel `/tmp` di WSL. I numeri di riga qui sotto sono stati aperti dal planner ma possono essere spostati (il catalogo li prendeva da copie diverse): PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi come descritto: scrivilo `non verificato` e fermati con `BLOCKED`, non indovinare.
Obiettivo: `make test-tools` esce 0 eseguendo ogni `tests/tools/test_*.py`; `make test-docs` esce 0.

## Stato reale (verificato dal planner su `origin/bm-core` @ `e6d7bfd7`, copia fuori dal repo; AGY-17 NON c'è su quella base)
- `Makefile`: `PYTHON ?= python3` (riga 15). `test-docs:` e il suo comando sono alle righe 1175-1176 (`grep -n "^test-docs:" Makefile` deve dare una riga; la riga dopo è `<TAB>$(PYTHON) scripts/docs_audit.py score`). `HOST_TESTS :=` è alle righe 830-833 e termina con `test-studio test-prompts test-docs $(if $(K7),test-hyp)`. Il catalogo dava `bm-core :1169-1170`: su `e6d7bfd7` è 1175-1176, come su `bm33-int2`; ri-trova comunque con `grep -n`.
- Il `Makefile` usa già righe `.PHONY:` separate accanto ai target (esempi: `.PHONY: test-reserved`, `.PHONY: manuals test-manuals`): `test-tools` segue lo stesso stile.
- Il blocco esatto che il planner ha provato (in WSL, con i 7 test di `tests/tools/` allora disponibili: `make test-tools` -> `EXIT=0`, ogni test stampa `OK`):
```
test-docs:
<TAB>$(PYTHON) scripts/docs_audit.py score

# The unit tests of the scripts (tests/tools/test_*.py), one after the other; the first failure stops
test-tools:
<TAB>@for t in tests/tools/test_*.py; do echo "== $$t"; $(PYTHON) $$t || exit 1; done
.PHONY: test-tools
```
  Nel Makefile le righe di comando iniziano con UNA TABULAZIONE (carattere TAB, codice 9), non con spazi: nei blocchi di questo WO è scritta `<TAB>` e va sostituita con il carattere TAB vero (senza il testo `<TAB>`).
- `docs_audit.py check`: NON verificato dal planner perché il comando esiste solo dopo AGY-17. Il planner ha solo visto che `docs_audit.py score` su `e6d7bfd7` dà `penalties 0` (nessun link rotto, `score 6646`), quindi è PROBABILE che `check` esca 0 su quella base. La decisione si prende con l'esperimento del passo 2, sulla TUA base.
- Se un test esistente fallisce sotto `make test-tools` (cioè `tests/tools/test_*.py` di un altro WO, o di `bm-core`): `BLOCKED` con l'output, NON si corregge qui.

## AGX-5a - il target (bm-coder, 20 min)
Passo 0: crea il worktree (riga "Base") e controlla che AGY-17 ci sia: `scripts/wsl-run.ps1 'python3 scripts/docs_audit.py check; echo EXIT=$?' -Tail 120`. Atteso: `docs_audit: 0 broken` e `EXIT=0` (oppure righe `  ! <messaggio>` ed `EXIT=1`). Se invece stampa l'uso del programma ed `EXIT=1` senza `docs_audit: 0 broken`, AGY-17 non c'è: `BLOCKED: manca AGY-17`.

Passo 1: il target. Trova con `grep -n "^test-docs:" Makefile` (una riga). Subito DOPO la riga di comando di `test-docs` (la riga con `docs_audit.py`) inserisci, nell'ordine: UNA riga vuota, poi ESATTAMENTE queste 4 righe (la riga 3 inizia con UNA TABULAZIONE, qui scritta `<TAB>`). La riga vuota che già segue nel Makefile resta dov'è, così prima del blocco seguente (`# The assistant (M30)...`) c'è ancora una riga vuota:
```
# The unit tests of the scripts (tests/tools/test_*.py), one after the other; the first failure stops
test-tools:
<TAB>@for t in tests/tools/test_*.py; do echo "== $$t"; $(PYTHON) $$t || exit 1; done
.PHONY: test-tools
```

Passo 2: `make test-docs` usa `check` solo se esce 0. Dal passo 0 conosci l'exit di `check` sulla tua base:
- Se `check` è uscito 0: cambia la riga di comando di `test-docs` da `<TAB>$(PYTHON) scripts/docs_audit.py score` a `<TAB>$(PYTHON) scripts/docs_audit.py check` (una riga cambiata, resta la tabulazione).
- Se `check` è uscito 1 (link rotti sulla base): NON cambiare `test-docs`; scrivi nella consegna `test-docs resta su score: check esce 1 sulla base (<elenco dei messaggi>)`.

Passo 3: verifica (dalla radice del TUO worktree):
- `scripts/wsl-run.ps1 'make test-tools; echo EXIT=$?' -Tail 120`. Atteso: per ogni file `== tests/tools/test_<nome>.py`, `OK` e alla fine `EXIT=0`. (I test lunghi: `test_ci_audit.py` dura circa 30 s; se manca PyYAML stampa `SKIP` ed esce 0.) Annota quanti file sono stati eseguiti (`make test-tools | grep -c "^=="`).
- `scripts/wsl-run.ps1 'make test-docs; echo EXIT=$?' -Tail 120`. Atteso: `EXIT=0` (con `check`: `docs_audit: 0 broken`; con `score`: la riga `score N ...`).
- `make -n test-tools` stampa il ciclo (controllo sintassi senza eseguire): atteso non vuoto, nessun errore `missing separator` (se compare, hai usato spazi invece della tabulazione).
- `git diff --stat`: solo `Makefile`, 5 righe aggiunte (4 + la riga vuota) e, se hai fatto il passo 2, 1 riga cambiata.

Criterio di fine: `make test-tools` -> exit 0; `make test-docs` -> exit 0; `git diff --stat` = solo `Makefile`.
Commit: un solo commit sul branch `claude/make-test-tools`, messaggio `Makefile: test-tools runs tests/tools; test-docs uses docs_audit check` (se non hai cambiato `test-docs`: `Makefile: test-tools runs tests/tools`). Push solo di questo branch.

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-5 --repo C:\Users\Utente\Documents\GitHub\bm33-agx5 --branch claude/make-test-tools --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGX-5 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGX.md` aggiungi la riga standard `DELIVERED AGX-5 @ claude/make-test-tools@<sha> · verifica: make test-tools -> exit 0 (<N> file di test); make test-docs -> exit 0 (<check|score>) · 1 file · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-5 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto); la sola decisione (passo 2) dipende dall'exit di `check` e va scritta nella consegna.
Stima: 20 minuti (nessuna compilazione).

## Decisioni (proposte del planner, da adottare da SPV)
- D1 `test-tools` NON entra in `HOST_TESTS`: il CI e `make test-host` restano come sono finché SPV non lo decide (alcuni test, come `test_manuals.py`, vogliono Python 3.11 o più; `test_ci_audit.py` vuole PyYAML o stampa SKIP). Raccomandata: sì.
- D2 `test-docs` passa a `check` solo se `check` esce 0 sulla base, altrimenti il CI si romperebbe per link rotti esistenti. Raccomandata: sì.
- D3 Il ciclo si ferma al primo fallimento (`|| exit 1`): un fallimento è un fallimento, l'output mostra quale file. Alternativa: eseguirli tutti e contare. Raccomandata: no (più semplice com'è).
- Coordinamento: API-1, APP-5b e `claude/ci-repair` toccano il `Makefile` in altri punti; il blocco nuovo sta accanto a `test-docs` (riga ~1176), lontano dalle loro righe. Un conflitto è di SPV.

## Prove per l'utente
Nessuna.
