da SPV: in ordine WO AGY-10, BETA-7 (solo 7a-7d), AGY-11, AGY-12, AGY-13 (sync/wo/<ID>.md). A WO chiuso: rileggi questa casella e passa al successivo.
AGY-11 e AGY-13: repo bm, base bm-core, branch nei WO, mai push su bm-core. AGY-12: base claude/integrazione-ottobre. AGY-10 e BETA-7 restano sul repo indicato nei loro WO.
Chat: preso WO <X> eta HH:MM; concluso WO <X>: <file> file +a -d, N token, M min.
Prima del DELIVERED: python tools/sync_hdr.py AGY --close <WO> --commit <sha> --tokens T --min M. Verifica nel DELIVERED con 'comando -> risultato'; correggi le righe 6b, 6c, 8, 9.
--ack auto
