# Proposte per lua32: da aggiungere a `docs/spec/s32-bm33.md`, sezione "Aperto"

Bozza preparata in bm33 perché la sessione non ha accesso in scrittura a lua32.
Il blocco qui sotto va copiato così com'è nella sezione **"Aperto"** di
`docs/spec/s32-bm33.md` in lua32 (al posto della riga "nessuna domanda in sospeso"),
con una pull request. Quando lua32 decide, le voci passano a "Deciso" e bm33 le recepisce
con `scripts/sync-s32-spec.sh`.

---

### 2026-09-27 — prestazioni del PPU su hardware reale (misure di bm33)

Misure su Raspberry Pi Zero W (ARM1176 a 1 GHz, cache dati 16 KB), kernel bm33
`5b70587`–`89452b3`, cartuccia `demo.cart`:

- **CPU s32**: 115–177 ns per istruzione, 3–5 µs per tick: la CPU non è mai il limite.
- **PPU**: 8,0 ms per tick (metà del budget di 16,7 ms) disegnando tutto il fotogramma
  320×224 e poi convertendolo, 5,6 ms senza la rilettura dalla memoria video. Il motivo
  è hardware: su questo SoC leggere la SDRAM costa circa 4 volte scriverla (`memcpy`
  10,3 ms/MiB contro `memset` 2,4 ms/MiB) e un fotogramma a 32 bit (287 KB) non sta in
  cache, quindi ogni passata in più sul fotogramma costa ~2 ms.
- bm33 ora disegna **a strisce di 8 righe** (che restano in cache): prima una passata
  sulle celle per la regola delle celle coperte, poi, per ogni striscia, le tile visibili
  e gli sprite nello stesso ordine della spec. I vettori di conformità passano byte per
  byte e il runner verifica a ogni tick che l'immagine a strisce sia identica a quella
  intera. Il tempo sul Pi va ancora rimisurato.

Nessuna di queste scelte cambia il comportamento di s32. Le domande per lua32:

**P1 — ordine di disegno definito per pixel (solo testo della spec).**
La spec (§7) definisce già il risultato come "sfondo, poi sprite dallo slot 0 al 511",
che è indipendente dall'ordine in cui un'implementazione percorre lo schermo. Proposta:
scriverlo esplicitamente, per esempio *"il colore di ogni pixel dipende solo dalle regole
di §7, non dall'ordine in cui l'implementazione disegna: è lecito disegnare per righe o
per strisce"*. E, se in futuro s32 avrà effetti per riga (scroll che cambia a metà
schermo, "raster"), definirli per riga di schermo, così i renderer a strisce restano
possibili. Ci sono piani per effetti del genere?

**P2 — costo massimo di un fotogramma (decisione).**
Oggi il costo di un fotogramma non ha limite: 512 sprite da 64×64 sono 2,1 milioni di
pixel. Sul Pi Zero bm33 misura ~14 ns per pixel di sprite in C (3,5 µs per sprite
16×16), cioè ~29 ms: niente 60 fps, e lo stesso vale per la sovrapposizione di tile
grandi sullo sfondo. Due possibilità:

- **(a) limite deterministico come le console vere**: per esempio al massimo *N* sprite
  (o *M* pixel di sprite) per riga di schermo, gli altri non disegnati in ordine di
  slot. È verificabile con i vettori di conformità e garantisce i 60 fps ovunque;
- **(b) solo un budget documentato**: "una cartuccia che resta sotto X sprite/pixel per
  fotogramma gira a 60 fps su bm33", senza cambiare il comportamento. Oltre il budget
  bm33 rallenta invece di saltare fotogrammi.

bm33 preferisce (b) per ora (nessuna cartuccia si avvicina al limite) e (a) solo se
lua32 vuole il comportamento da console d'epoca. Serve una decisione di lua32.

**P3 — tavolozza in cache (solo implementazione, informativo).**
Ogni pixel legge 3 byte di CGRAM (RGB888) con il mascheramento dell'indirizzo. Tenere
le 8 × 256 voci già convertite in una tabella a 32 bit, aggiornata quando la CGRAM
viene scritta (o ricalcolata una volta per tick: 2048 voci), toglie quel costo dal
ciclo dei pixel. È utile anche a lua32 (LuaJIT). Nessun effetto sulla spec.

**P4 — modo 16:9 (informativo).**
384×224 è il 20% di pixel in più: rientra nel budget di bm33 con il disegno a strisce.
Nessuna richiesta.
