Leggi sync/wo/AGX-1.md e AGX-2.md (in ordine). Decisione D1 di AGX-1: correggi anche carts/sdk_b16/main.lua:2726 UP DN -> UPDOWN, approvato.
Lavora solo nei tuoi worktree, mai bm33 né bm33-sync di SPV. WSL: usa TMPDIR=$PWD/build/tmp (il /tmp condiviso si riempie).
Chat: preso WO <X> eta HH:MM; concluso WO <X>: <file> file +a -d, N token, M min.
Prima del DELIVERED: sync_hdr.py AGX --close <WO> --commit <sha> --tokens T --min M; verifica 'comando -> risultato'. Poi --ack auto.
