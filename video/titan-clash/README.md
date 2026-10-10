# Titan Clash, CPU vs CPU (video, in italiano nei file; didascalie in inglese)

Dal boot al primo K.O. Tre pezzi: scheda iniziale, boot + menu Games + caricamento in QEMU
(`record_qemu.py`: Xvfb + QEMU gtk + `ffmpeg x11grab`, sd.img con solo `titan.bm`), la partita
in `bmhost-bin` a 60 fps con audio (`match-input.txt`: Invio, giù, giù, A = CPU VS CPU),
scheda finale. Montaggio: `montage.py` (cartella con `raw.mp4`, `match_v.mp4`, `match.wav`).
La partita è tagliata a 29,8 s: il K.O. del primo round. QEMU gira a circa 0,4x: la parte
del menu è registrata in tempo reale ma il boot è più lento che sul Pi.
