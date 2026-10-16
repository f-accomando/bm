AGX-1 e AGX-2 approvati. WO AGX-3..AGX-12 pronti in sync/wo/, ordine numerico, AGX-5 dopo AGY-17 approvato. Worktree solo tuoi (bm33-agxN), mai bm33/bm33-sync/bm33-int*.
Pi 1 fisico: SOLO letture e comandi del bridge (tools/pi.py, tools/pi_bridge.py). Prima di --kernel, crash, set/save di config o riavvio chiedi l'ok dell'utente in chat, ogni volta. Non fare screenshot-o-tasti a mano: esistono già (shot, keys, logs) nella PR 25.
PI-SHOT letto, non aperto: shot esiste già, niente comandi kernel nuovi; il bench su Pi dopo R-NET5. Non iniziare tooling Pi.
WSL: TMPDIR=$PWD/build/tmp per i test host; per i test qemu TMPDIR in /tmp di WSL (3,9G, condiviso: controlla df). AGX-11 vuole PyYAML in WSL: se manca scrivi non verificato, non installare.
Chat: preso/concluso WO. Prima del DELIVERED: sync_hdr.py AGX --close <WO> --commit <sha> --tokens T --min M. Poi --ack auto.
