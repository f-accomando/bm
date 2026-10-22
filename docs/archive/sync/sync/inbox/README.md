# Caselle (inbox) — `sync/inbox/<ID>.md`

Scritte **solo da SPV**, lette **solo dal destinatario**: un compito alla volta, **massimo 5 righe**, con work order, branch, comando di verifica e `eta` attesa. Le urgenze vanno qui (non in fondo al Log di `SYNC.md`): il battito controlla il `git diff` di questo file.

L'operatore, letta la casella, scrive `ack` con `python tools/sync_hdr.py <ID> --ack auto` e si segna `WORKING`. A consegna avvenuta (`DELIVERED ...` nel proprio file) SPV svuota la casella o la riempie col compito dopo. `python tools/sync_check.py` segnala caselle con più di 5 righe.
