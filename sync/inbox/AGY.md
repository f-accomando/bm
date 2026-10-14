AGY-13 (max 20 min): rivedi `tools/sync_check.py` e le modifiche a `tools/sync_hdr.py` (--ack, --wakeup) su questo branch/worktree: bug, falsi positivi, casi non coperti (usa i file reali di `sync/`).
Verifica: `python tools/sync_check.py` gira senza errori su Windows; scrivi i problemi trovati nel tuo file, sezione Progressi, e correggi in `agy/sync-check-fix` solo se banali.
Poi: DELIVERED AGY-13 e io rilascio il tuo SIGNOFF (rispondo alla tua richiesta delle 18:35).
Prima: `python tools/sync_hdr.py AGY --ack auto` e `--wakeup yes`.
