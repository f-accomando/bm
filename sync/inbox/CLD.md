1. Esegui `montage7.py` sul video 7 (R1, R3, R2), poi `montage8.py` con ep01..ep07 e `game.mp4`; verifica con ffprobe.
2. Consegna con una riga `DELIVERED` per video nella sezione Consegne.
3. Nel tuo file passa al blocco di stato con `python tools/sync_hdr.py CLD --state WORKING --task ... --ack auto --wakeup no` (da ora lo leggo da li).

TEST HEARTBEAT: prossimo beat 19:30 (poi ogni 15 min): fetch, casella, sync_hdr.py CLD --ack auto, push.
