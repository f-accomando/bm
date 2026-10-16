title: Il tuo primo gioco in bm
accent: primo gioco
lang: it
kicker: Una guida rapida bm
subtitle: Crea un gioco 2D con un titolo, tre stage, un finale e record salvati sull'RGB30 e sul Pi Zero W.
footer: manuali bm - quickstart

::: readme
**Cos'è.** Una guida di una pagina che porta uno sviluppatore che non ha mai usato bm dal menu a un gioco 2D completo con la suite integrata.

**Come si usa.** Segui i sette capitoli numerati nell'ordine. Ogni azione a schermo è mostrata come tasto o voce di menu, seguita da ciò che appare sullo schermo.

**Cosa manca.** Le lacune in attesa di template, tabelle o prove hardware future sono contrassegnate con etichette DA COMPLETARE evidenziate.
:::

# Prima di iniziare

bm crea le cartucce direttamente sulla console o partendo dal codice sorgente.

| Ti serve | Per |
|---|---|
| PowKiddy RGB30 con una scheda bm | creare il gioco e provarlo sulla console portatile |
| Tastiera USB | digitare comodamente (il gamepad usa la tastiera a schermo) |
| Raspberry Pi Zero W con la sua scheda bm | provare e modificare il codice sul Pi (facoltativo, capitolo 5) |
| Un PC con il repository bm | copiare i file tra le schede o via rete |

Costruirai un platform 2D con schermata del titolo, 3 stage, un finale e record salvato.

# Creare il gioco sull'RGB30

1. Dal menu principale, vai in Dev e apri bm SDK.
2. Premi `Ctrl+N` per aprire `new project from a template`, scegli **Stages 2D** e premi Invio.
3. Premi `F5` per provare il gioco; premi Start + Select per tornare all'editor.
4. Premi `Esc` per aprire il menu di progetto, scegli `Save as...`, inserisci `MYGAME.BME` e premi Invio.

[DA COMPLETARE: il template Stages 2D non è ancora in bm SDK (RGB30): verifica questi passi quando sarà disponibile]

# Personalizzarlo

1. Premi `F2` per aprire l'editor del codice, e modifica il testo del titolo nel sorgente Lua.
2. Premi `F3` due volte per aprire l'editor della mappa: lo stage n occupa 45 righe a partire dalla riga (n-1)×48 sulle prime 128 colonne; la tile 7 è l'uscita.
3. Premi `Ctrl+S` per salvare le modifiche.

[DA COMPLETARE: il template Stages 2D non è ancora in bm SDK (RGB30): verifica questi passi quando sarà disponibile]

# Giocarlo dal menu

1. In bm SDK, premi `Ctrl+Shift+B`: la cartuccia `.b16` compilata viene salvata in `/bm/` e compare nella scheda Games.
2. Premi `Ctrl+B` per compilare `MYGAME.BM` in `/carts/`, con il messaggio `(the Games tab lists bm/)`: questo è il file per il Pi.

# Sul Pi: modificare il codice con bm Code

1. Copia `MYGAME.BME` e `MYGAME.BM` dalla cartella `carts/` della scheda RGB30 nella cartella `carts/` della scheda del Pi.
2. Nel menu principale del Pi, vai in Dev e apri Code.
3. Premi `Ctrl+O`, scegli `/carts/MYGAME.BME` e premi Invio.
4. Modifica il codice e premi `F5` per provare: in caso di errore salta alla riga, e `F9` lo spiega.
5. Premi `Ctrl+S` per salvare. L'SDK del Pi non contiene il template Stages 2D.

[DA COMPLETARE: un progetto creato sull'RGB30 aperto in bm Code sul Pi: non ancora provato]
[DA COMPLETARE: Ctrl+Q, l'aiuto sulla funzione sotto il cursore in bm Code: non ancora presente]

# Mantenerlo a 60 fps

1. Mantieni ogni frame entro il budget di 16.7 ms per conservare 60 fps stabili.
2. Premi `F11` per attivare l'overlay di sviluppo e controllare i tempi dei frame.
3. Usa `profile(true)` e `profile()` per misurare le routine; consulta `docs/PERFORMANCE.md` per i dettagli.

[DA COMPLETARE: la tabella del budget misurato per Pi e RGB30 non è ancora pubblicata]

# Dove proseguire

1. Leggi `docs/GAME-GUIDE.md` e `docs/design/games-architecture.md` per esplorare la macchina a stati del template.
2. Consulta i manuali *bm SDK* e *bm Code* e le schede API in `docs/api/`.
3. Se qualcosa ti blocca, riferisci a chi ti ha dato la scheda che cosa ti ha fermato, e in quale punto.
