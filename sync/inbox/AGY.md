Ordine WO: AGY-10, BETA-7 (7a-7d), AGY-11, AGY-12, AGY-13, AGY-14, AGY-15 (sync/wo/<ID>.md; decisioni D dei WO: adotta le raccomandate). Chat: preso WO <X> eta HH:MM; concluso WO <X>: <file> file +a -d, N token, M min.
Prima del DELIVERED: sync_hdr.py AGY --close <WO> --commit <sha> --tokens T --min M; verifica 'comando -> risultato'; correggi le righe 6b, 6c, 8, 9. Poi --ack auto.
AGY-12: il worktree ...\GitHub\bm33-int è di SPV: non usarlo. Crea un tuo worktree da origin/claude/integrazione-ottobre (es. bm33-agy12). Commit non pushati (ef9b9564 su agy/sdlog-scan): pushali dal tuo worktree.
AGY-11, AGY-13: base bm-core. AGY-14: base claude/app5-api-help-lint. AGY-15: base claude/sdk-b16-dev-without-pc. Mai push su bm-core.
WSL: /tmp condiviso si riempie; per i test qemu TMPDIR deve stare in /tmp di WSL, non su /mnt/c.
