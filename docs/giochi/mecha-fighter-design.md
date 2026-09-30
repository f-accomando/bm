# Titan Clash — progettazione (M20)

Risposta al punto 25 del [concept](mecha-fighter-concept.md). Il 2026-09-29 l'autore ha
chiesto di partire subito con una base giocabile (MVP): **un solo robot** per tutti i
giocatori, poche opzioni (**armatura leggera o pesante**, **spada o mitragliatori sulle
braccia**), sprite di alta qualità, libertà sulle scelte di dettaglio. Questo documento
fissa le decisioni prese per l'MVP e come si estendono verso il concept completo.

Codice: `carts/titan/` (cartuccia *Titan Clash*). Test: `make test-titan` (host) e
`test_titan` in `tests/qemu_test.py`.

## 1. Conflitti tra le meccaniche e come si risolvono

| Conflitto | Decisione |
|---|---|
| Robot "alti come grattacieli" ma leggibili come SF2 | Scala dello sprite di SF2 (robot ~190 px su 360, metà schermo); la scala gigantesca la raccontano il fondale (auto da 16 px, lampioni, palazzi alla stessa altezza) e l'hangar (operai da 11 px) |
| Tante combinazioni di equipaggiamento vs tanti frame | Robot **a strati**: ogni frame ha un'immagine base e strati sovrapposti (armatura pesante, spallaccio integro/crepato, cannoni, spada) resi dallo stesso scheletro; una combinazione costa zero frame in più |
| Pixel art ricca vs budget di memoria | Sprite **pre-renderizzati** (modello 3D procedurale → cel shading con rampe di 6 toni e contorni) e sheet con palette e RLE (sezione SHEET8, fino a 2048×4096) |
| Armatura come "seconda barra" vs armatura fisica | Una sola barra armatura, sincronizzata con lo spallaccio: integro, **crepato a metà**, **staccato a zero** (il pezzo vola via, rimbalza e resta in strada) |
| Armi a distanza vs "spam di proiettili" | Il calore: 6 colpi per raffica, surriscaldamento a 100 (2,5 s fermo, fumo); la spada costa energia, che serve anche agli scatti |
| Molte risorse vs HUD da arcade | In alto vita e armatura (le due che decidono il round), in basso energia e calore; il calore solo per chi ha i cannoni |
| Tag team fin dall'inizio vs MVP 1v1 | Rinviato (sotto): la logica dei lottatori non tiene stato globale, una squadra sarà una lista di lottatori con uno attivo |

## 2. Struttura tecnica

- **Arte** (Python, in git anche i risultati, `make` non li rigenera):
  `mkrobot.py` (modello del robot VANGUARD: solidi su uno scheletro di 16 ossa, 62 pose
  in 23 animazioni, render ortografico in vista 3/4, hurtbox e hitbox per frame dalle
  ossa), `art.py` (città in 4 piani di parallasse, hangar, effetti, testi),
  `mkassets.py` (impacchetta tutto in `sheet.png` e scrive `src/05_sprites.lua`).
  Gli strati sparsi sono spezzati in blocchi 8×8 e i pezzi uguali condivisi.
  P2 ha una **seconda livrea** (cremisi e oro, visore verde), ricolorando le rampe.
- **Kernel**: sezione `SHEET8` del formato `.bm` (palette ≤256 colori + RLE,
  decodificata al caricamento), `B33_SHEET_MAX` 4096; `mkb33.py --sheet8`.
- **Gioco** (Lua, file in `src/` uniti da `build.py`): `20_fighter` (controlli, stati,
  colpi), `30_fx` (particelle e proiettili), `40_stage` (arena), `50_hud`, `60_cpu`,
  `70_screens` (titolo, modalità, hangar, incontro, pausa), `80_audio`.
- Logica a 60 Hz fissi; il combattimento usa solo interi di frame (tick delle mosse,
  hitstop, stun), niente dipendenze dal frame rate.

## 3. Core gameplay loop

Titolo → modalità (1P contro CPU, 2 giocatori, CPU contro CPU; livello della CPU) →
hangar (ognuno configura il suo robot e dà READY) → incontro al meglio di 3 round da 99 s
(ROUND n, FIGHT!, K.O. o TIME OVER) → risultato (rivincita, hangar, titolo). Senza
giocatori il titolo mostra una demo CPU contro CPU.

## 4. Risorse

| Risorsa | MVP |
|---|---|
| Vita | 1000; a zero K.O. |
| Armatura | leggera 240, pesante 400; assorbe il 64% / 75% del danno finché dura; la parata la consuma un po' (12%) |
| Energia | 100, si ricarica (0,40 / 0,26 al frame); scatto 16 / 22, scatto aereo 20, fendente 30 |
| Calore | solo cannoni: +9 a colpo, −0,3 al frame; a 100 surriscaldato per 150 frame |
| Tag | rinviato (vedi 8) |

## 5. Movimento

Camminata avanti/indietro, accovacciamento, salto (verticale, avanti, indietro) con
controllo del peso: leggera salto 10,6 e **doppio salto**, pesante 8,7 e nessuno.
**Scatto** con doppio tocco avanti/indietro, anche **in aria**. Atterraggio pesante
con polvere e scossa dello schermo. I robot non si compenetrano (spinta a 88 px) e la
camera segue il punto medio in un'arena larga 1024 px.

## 6. Combo

Sei pulsanti ridotti a quattro (pad SNES/DS4): X pugno leggero, Y pugno pesante,
A calcio leggero, B calcio pesante; versioni accovacciate e in aria.
- **Chain/cancel**: una mossa che colpisce (o viene parata) si annulla in una di
  rango più alto (leggero → medio → pesante → arma).
- **Launcher**: il pugno pesante accovacciato lancia in aria; il robot in aria si può
  colpire ancora (**juggle**, al massimo 4 colpi).
- **Knockdown**: calcio pesante, spazzata, fendente.
- Danno scalato nelle combo (−12% a colpo, minimo 40%), contatore "N HITS".
- Arma: mezzaluna avanti (↓↘→) + pugno, oppure Y+B insieme (per chi inizia).

## 7. Parata

Indietro = parata alta, basso-indietro = parata bassa; le spazzate vanno parate basse,
gli attacchi in salto alti. La parata ferma il danno alla vita ma consuma armatura.

## 8. Tag (dopo l'MVP)

Come nel concept: 2 slot di cambio che si rigenerano lentamente, barra visibile sotto
la vita; il compagno entra con un attacco (tag offensivo) o per salvare. Nell'MVP:
struttura pronta (lottatori indipendenti, HUD con spazio sotto le barre), nessun tag.

## 9. Armatura e danni

MVP: una zona (lo **spallaccio**), 3 stati sincronizzati con la barra (tacca a metà):
integro → crepato (scintille azzurre, suono) → staccato (il pezzo vola via con
un'esplosione, resta a terra e poi sparisce). La scritta ARMOR BROKEN lampeggia nel
nome. Dopo: altre zone (testa, braccia con le armi, gambe), armi distruggibili, scudi
olografici (strato semitrasparente con stati di glitch).

## 10. Equipaggiamento

MVP: **armatura** (leggera / pesante: strati `hv` e spallaccio grande) e **arma**
(spada sulla schiena che passa in mano nel fendente / due cannoni sugli avambracci).
Cambiare nell'hangar cambia davvero lo sprite. Dopo: booster, scudo, altri chassis,
martello, missili (ogni pezzo = uno strato + statistiche).

## 11. Statistiche e mosse dalla configurazione

| | Leggera | Pesante |
|---|---|---|
| Camminata / indietro | 2,8 / 2,2 | 1,9 / 1,5 |
| Salto | 10,6 + doppio salto | 8,7 |
| Scatto | 9,5 per 15 frame | 7,2 per 12 frame |
| Armatura / assorbimento | 240 / 64% | 400 / 75% |
| Danno | ×1,05 | ×1,1 |

La spada dà il **fendente** (175, knockdown, ×1,5 contro l'armatura); i cannoni la
**raffica** (6 colpi da 24, anche a distanza). Nei test CPU contro CPU le quattro
combinazioni vincono tra il 44% e il 59% dei round.

## 12. Arene

MVP: **città abbandonata al tramonto**: cielo a bande con sole basso, skyline lontano
(parallasse 0,25) con esplosioni lontane, torri distrutte alla scala dei robot (0,55)
con colonne di fumo dietro, strada con auto e lampioni in miniatura (1,0) e piccoli
incendi, macerie davanti a tutto (1,3). Dopo: zona industriale, porto, canyon, foresta.

## 13. HUD

In alto: vita (con scia rossa del danno) e sotto l'armatura (lunga quanto l'armatura
del robot, tacca a metà), orologio a cifre grandi al centro, round vinti, nome e
configurazione. In basso: energia (tacca del fendente) e calore (lampeggia HOT! quando
surriscaldato). Etichetta P1/P2/CPU sopra la testa. Select mostra il tempo di frame.

## 14. Hangar

Sala industriale con portellone sulla notte, impalcature, passerelle, luci, container;
i due robot sulla piattaforma, operai minuscoli che camminano e saldano (scintille),
gru che corre sulla rotaia fino al robot modificato (che vibra tra le scintille), luci
d'allarme. Pannelli laterali: armatura, arma, READY, 5 statistiche a tacche, descrizione.

## 15. Vertical slice (questa base) e passi successivi

Fatto: 1v1 contro CPU (3 livelli) o 2 giocatori, lo stesso robot con 2×2
configurazioni, movimento, salto e doppio salto, scatti anche aerei, pugni, calci,
parate alta/bassa, combo con cancel, launcher e juggle, due armi (spada con energia,
cannoni con calore), vita, armatura con spallaccio distruttibile, arena in parallasse,
hangar animato, musica e suoni, pausa con la lista delle mosse, demo.

Prossimi passi, nell'ordine del concept: prova sul Pi e ritocco del feeling →
prese/proiezioni e super → danni localizzati (armi distruggibili) e scudi → altri
robot/chassis e booster → tag team e 2v2 → arcade con boss e altre arene.
