Ordine WO: AGY-10, BETA-7 (7a-7d), AGY-11, AGY-12, AGY-13 (sync/wo/<ID>.md). A WO chiuso rileggi la casella. Chat: preso WO <X> eta HH:MM; concluso WO <X>: <file> file +a -d, N token, M min.
Prima del DELIVERED: sync_hdr.py AGY --close <WO> --commit <sha> --tokens T --min M; verifica 'comando -> risultato'; correggi le righe 6b, 6c, 8, 9. Poi --ack auto.
AGY-12: il worktree ...\GitHub\bm33-int è di SPV: non usarlo. Crea un tuo worktree da origin/claude/integrazione-ottobre (es. bm33-agy12) e sposta lì il lavoro.
Se in bm33-int hai commit non pushati (ef9b9564 su agy/sdlog-scan), pushali dal tuo worktree e dimmelo in chat.
AGY-11 e AGY-13: base bm-core, mai push su bm-core. AGY-10 e BETA-7: repo indicato nei WO.
