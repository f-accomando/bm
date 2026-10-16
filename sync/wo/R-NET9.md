# R-NET9 - WATCHDOG-CHAINLOADER: test_chainloader rosso nella PR 25 (regressione di FRZ-3)
Assegnato a NET (16 ott), PRIORITÀ: prima di R-NET6, perché blocca la CI verde della PR 25.
Base: `claude/integrazione-ottobre` @f5cd71b7, in un tuo worktree (`C:\Users\Utente\Documents\GitHub\bm33-netwdg`, mai `bm33-int*`). Branch: `claude/watchdog-chainloader-fix`. Mai push su bm-core.
Perimetro: `src/kernel/watchdog.c` (cerca `watchdog_arm` e la sonda con `probe_flag`, `.noinit`) e il suo uso in `src/kernel/main.c`; `tests/qemu_test.py` solo se serve un test nuovo accanto a `test_chainloader`. NON toccare: `SYNC.md`, `sync/*`, `.github/*`, `src/net/*`, `tools/*`, i comandi del monitor.
## Sintomo (triage SPV, bm-runner)
- `python tests/qemu_test.py -k chainloader` → `test_chainloader` FAIL: `timeout waiting for b'cartridge menu'`. Passa su `bm-core` (run 38876113412, 68.9 s); fallisce in modo deterministico su f5cd71b7 (run 38890224645 e 38890207179) e in locale su WSL.
- Seriale: chainloader → upload → il kernel stampa banner e sysinfo → riappare `bm chainloader: waiting for kernel`: il kernel si resetta dentro il chainloader subito dopo `sysinfo_print_short()`, prima di ogni riga `watchdog:`.
- Sospetto (non verificato): commit `5d6754ae` "watchdog: enable countdown with RSTC and handle QEMU immediate reset (FRZ-3)": `watchdog_arm()` scrive `probe_flag` in `.noinit` e si aspetta che QEMU resetti subito su RSTC; sotto il chainloader il reset riavvia l'immagine del chainloader, non il kernel caricato, quindi il secondo boot non arriva mai nel kernel.
## Passi
1. (bm-light, 20 min) Riprodurre (TMPDIR in /tmp di WSL, controlla `df /tmp`), confermare la causa con un `git bisect` limitato o rimuovendo la sola sonda; scrivere in consegna la riga che la prova (`file:riga`).
2. (bm-light, 25 min) Correzione minima: la sonda non deve resettare quando il kernel è partito dal chainloader (o quando il secondo boot non può raggiungere il kernel), oppure la sonda diventa un controllo che non resetta. Sul Pi reale il watchdog deve armarsi come prima: se la correzione cambia il comportamento su hardware, scrivilo come `non verificato su hardware` e aggiungilo alle prove utente.
## Verifica
- `python tests/qemu_test.py -k chainloader` → `test_chainloader` e `test_chainloader_bad_crc` PASS.
- I test di watchdog già esistenti (`git grep -n "watchdog" tests/qemu_test.py`) restano PASS.
- `make` senza warning nuovi.
Consegna: riga DELIVERED standard; prima `sync_hdr.py NET --close R-NET9 --commit <sha> --tokens T --min M`. Prova utente a fine giornata: il Pi si avvia e il watchdog parte (riga `watchdog:` nei log), crash volontario ancora riavvia da solo.
