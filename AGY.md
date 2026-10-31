---
schema: 1
id: AGY
state: DONE
task: note-sessione
waiting_on: 
progress: 100
wakeup: no
ack: 
updated: 2026-10-31
---
# AGY — note della sessione del 2026-10-31

> File ripulito il 2026-10-31 su richiesta dell'utente. Il contenuto precedente (registro Antigravity del 14 ottobre) resta nella storia git: `git log --follow -- docs/archive/sync/sync-tropic-trail/AGY.md`.

## Richieste dell'utente
1. RGB30: con la micro HDMI collegata si spegne il pannello e parte lo schermo esterno a 720p. Scollegata: il pannello si riaccende e l'uscita esterna si spegne.
2. Capire perché la RGB30 resta circa 12 secondi a schermo nero tra lo splash e il menu, da quando sono partiti gli sviluppi grafici.
3. Mettere gli sviluppi HDMI in un kernel da provare sulla console accesa.

## Fatto
- Branch `claude/rgb30-hdmi-external-720p`, già pubblicato su `origin`. Non è in `bm-core`.
- Nuovi file: `src/rgb30/rk_hdmi.c`, `src/rgb30/rk_hdmi.h` (trasmettitore DW-HDMI e PHY, 1280x720p60, RGB 8 bit, senza audio e senza EDID).
- Modificato `src/rgb30/rk_display.c`: porta VP0 del VOP2 verso HDMI, clock pixel da VPLL (1188 MHz / 16 = 74,25 MHz), lettura del pin HPD ogni 250 ms con due letture uguali, ripristino del pannello allo scollegamento.
- Chiavi di `bm/config.txt`: `hdmi=0` (solo pannello), `hdmi_swap=1` (rosso e verde scambiati), `hdmi_origin=1` (posizione dell'immagine). Sono in `docs/spec/config-keys.toml`.
- Documenti aggiornati: `docs/RGB30.md`, `docs/progress/platforms.md`, divergenza D56 in `docs/spec/divergences.toml`.
- `consistency.py check`: 0 drift. `docs_audit.py`: nessun link rotto.
- Installato il cross-compilatore (`gcc-aarch64-linux-gnu`, `picolibc-aarch64-linux-gnu`). `make TARGET=rgb30` produce `kernel8.img` (2,4 MB) senza warning nei file nuovi.
- Il kernel è stato inviato all'utente come file. Non l'ho caricato io sulla console.

## Non verificato
- Il driver HDMI non è mai girato su hardware. Il codice nasce dalle sequenze dei registri del kernel Linux.
- Rischi noti: il PHY può non agganciarsi se serve `clk_hdmi_ref` (HPLL), che non programmo. La posizione dell'immagine e l'ordine dei colori sono ipotesi.
- Test QEMU non eseguiti: l'integrazione in `bm-core` aspetta la prova sulla console.

## Nero all'avvio (indagine di un agente Opus, solo lettura)
- Ipotesi principale: `.bss` è circa 30 MB (di cui circa 18 MB in `mali.c` e circa 8 MB in `r3d.c`) e `src/rgb30/start.S:126-131` la azzera con MMU e cache spente. La dimensione l'ho verificata. La velocità di scrittura (circa 2,5 MB/s) non è misurata.
- Ipotesi minori: `cores=4` (al massimo circa 6 s se i core non partono); WiFi/BT prima del menu (qui si vede il logo, non il nero).
- Il bootlog non ha timestamp. Misura con un cronometro: fine dello splash, LED rosso, suono di avvio, menu.
- Correzione proposta, non applicata: azzerare la `.bss` grande in C dopo `mmu_init` (modifiche a `kernel.ld`, `start.S`, `mmu.c`, `main.c`). Prima va controllato che nulla scriva in `.bss` prima di `mmu_init`.

## Connessione alla console
- `192.168.1.233` non risponde da questa sessione cloud (timeout di `tools/bm_net.py`), anche dopo che l'utente ha aperto il bridge. È un indirizzo di rete locale.
- L'unico computer in Remote Control (`NB-ACCOMANDOFRANCESCO`, Windows) non è offerto alle sessioni cloud.
- Strada che funziona: una sessione Claude Code locale sul PC, sul branch `claude/rgb30-hdmi-external-720p`.

## Da fare
1. Caricare il kernel sulla console, provare collegamento, scollegamento e avvio con cavo inserito, e leggere le righe `display:` di `bm/bootlog.txt`.
2. Misurare il nero all'avvio con il cronometro. Se è prima del LED rosso, applicare il fix della `.bss` su un branch separato (`claude/rgb30-fast-bss-clear`).
3. Dopo la prova: chiudere D56, aggiornare `## Open work` in `docs/progress/platforms.md` e integrare in `bm-core` con fast-forward.
