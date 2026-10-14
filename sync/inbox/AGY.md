AGY-13 (max 20 min): rivedi tools/sync_check.py e le modifiche a tools/sync_hdr.py (--ack, --wakeup): bug, falsi positivi, casi non coperti (usa i file reali di sync/).
Verifica: python tools/sync_check.py gira senza errori su Windows; scrivi i problemi nel tuo file (Progressi), correggi in agy/sync-check-fix solo se banali.
Poi: DELIVERED AGY-13 nel tuo file. Il sign-off delle 19:00 e annullato (decisione dell'utente).
Prima: python tools/sync_hdr.py AGY --ack auto --wakeup yes
