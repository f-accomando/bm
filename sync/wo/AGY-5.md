# AGY-5 — PERIM: perimetro dei WO controllato da script (P1) e quarantena dei test (D-R3) (SYS-4)
Inizia dopo: DELIVERED AGY-4.
Base: bm-core aggiornato; branch agy/sync-perimeter-quarantine (gli strumenti sync_*.py stanno già su bm-core, decisione D3 di SPV). Mai push su bm-core.
Perimetro: tools/sync_perimeter.py (nuovo), tests/tools/test_sync_perimeter.py (nuovo), tests/quarantine.txt (nuovo), scripts/quarantine_check.py (nuovo), tests/tools/test_quarantine_check.py (nuovo), docs/progress/sync.md (solo "## How it is today", 2 righe). NON toccare: tools/sync_hdr.py, sync_check.py, sync_watch.py, SYNC.md, sync/*, tests/qemu_test.py, tests/rgb30/qemu_test.py, .github/workflows/*.

## AGY-5a — tools/sync_perimeter.py (bm-coder, 45 min)
- CLI: --wo FILE [--section ID] (--files P... | --diff BASE...REF, che lancia git diff --name-only).
- Righe di perimetro: dopo aver tolto "- " e "**" iniziali, quelle che iniziano con "Perimetro" o "File" (senza distinzione di maiuscole). Il testo dopo "NON toccare" (senza distinzione di maiuscole) dà i nomi vietati, quello prima i permessi.
- Token = span tra backtick che contengono "/" oppure corrispondono a \w+\.\w+, senza spazi né "(". Si tolgono i suffissi :N e :N-M. "a.c|.h" si espande in a.c e a.h. "*" vale come in fnmatch. Un token senza "/" corrisponde a qualsiasi percorso con quel basename.
- Senza --section valgono tutte le righe del file. Con --section ID valgono la parte prima del primo "## " più la sezione "## ID ..." (fino al "## " successivo). Un nome vietato nella parte iniziale e permesso nella sezione è permesso.
- Output: "FORBIDDEN <path>" (vietato), "OUT <path>" (non permesso). Exit 0 se è tutto dentro, 1 se c'è almeno una riga, 2 per errori di uso o di I/O.
- Test con fixture testuale sul modello di sync/wo/AGY.md: perimetro con "|.h", "NON toccare", sezione che estende, token con ":96", glob carts/editor/*.
- Verifica: python tests/tools/test_sync_perimeter.py → OK. Prova reale: con il sync/wo/AGY.md del worktree di sync, "--section FRZ-6 --files src/kernel/reports.h src/kernel/sdlog.c" → exit 0; aggiungendo src/bm/runtime.c → exit 1 con "FORBIDDEN src/bm/runtime.c".

## AGY-5b — quarantena D-R3 (bm-coder, 30 min)
- tests/quarantine.txt: solo intestazione "# tests/<file>.py::<test_name> | <motivo> | <until YYYY-MM-DD>", nessuna voce (scegliere i test da mettere in quarantena spetta a CI e SPV).
- scripts/quarantine_check.py [--file tests/quarantine.txt] [--today YYYY-MM-DD]. Per ogni riga controlla: 3 campi; il file esiste; "def <test_name>(" è presente nel file; data valida e ≥ today; nessun duplicato. Errori su stderr come "line N: <motivo>"; exit 1 se ce n'è almeno uno, altrimenti 0.
- Test: voce valida, test inesistente, voce scaduta, duplicato, riga con 2 campi.
- Verifica: python tests/tools/test_quarantine_check.py → OK; python scripts/quarantine_check.py → exit 0.

## AGY-5c — documenti (bm-mechanic, 15 min)
- docs/progress/sync.md "## How it is today": una riga per tools/sync_perimeter.py (uso, exit 0/1/2) e una per tests/quarantine.txt + scripts/quarantine_check.py.
- Verifica: python scripts/docs_audit.py score senza link rotti né orfani.
Criterio di fine: le tre verifiche OK.
Consegna: riga DELIVERED standard (anche una sola per i tre passi, con l'elenco), poi FREE. Ultimo WO della sequenza.
