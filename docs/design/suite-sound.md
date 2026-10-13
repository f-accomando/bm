# Proposte di Sviluppo: bm Sound (Suite Audio e Sequencer)

Questo documento analizza cosa manca a **bm Sound** (`carts/sound`) per integrarsi in modo trasparente con il gameplay dei giochi, evitare la duplicazione di codice audio in Lua ed eliminare la fatica acustica nei titoli d'azione.

---

## 1. Stato Attuale di bm Sound

`bm Sound` è un sintetizzatore e sequencer a 8 voci completo e versatile:
- **Pagine Principali**:
  - **SOUNDS**: Editor dello strumento sintetico (14 forme d'onda: quadra, triangolare, dente di sega, rumore, seno, metal, FM, pluck, supersaw, organo, sample PCM, rumori pink/brown/crackle) con inviluppo ADSR, filtro dinamico ed eco.
  - **SFX**: Progettazione di effetti sonori rapidi con curve di intonazione ed effetti (glide, bend, vibrato, tremolo, arpeggio).
  - **PATTERN**: Step sequencer a 8 tracce (una traccia per voce).
  - **SONG**: Sequenza di pattern per comporre brani musicali complessi con gestione delle ripetizioni.
- **Assistente Musicale**: Apertura con F6 per generare motivi, bassi ed effetti su misura.

---

## 2. Cosa Manca per l'Integrazione nei Giochi

Sebbene la generazione del suono sia ricca, il modo in cui il codice del gioco comunica con il banco audio presenta limiti storici tipici dei vecchi motori retro:

### A. Assenza di un Dizionario SFX con Nome (Named Sound Events)
- Gli effetti sonori nel banco audio sono identificati unicamente da numeri interi (`0..63`).
- In quasi tutti i giochi del repository (*Hunter's Night*, *Star Shooter*, *Skyvale World*), gli sviluppatori hanno finito per **scrivere il codice degli effetti sonori direttamente in Lua tramite `note()`**:
  ```lua
  local function sfx(kind)
    if kind == "slash" then note(0, 2600, 90, NOISE, 70)
    elseif kind == "gun" then note(0, 900, 160, NOISE, 170); note(2, 70, 120, SQUARE, 110)
    elseif kind == "hit" then note(2, 380, 110, NOISE, 130)
    ...
  ```
- Questo accade perché associare `sfx(14)` o `sfx(22)` nel codice Lua costringe a consultare continuamente bm Sound per ricordare quale numero corrisponda a quale effetto, rendendo il codice opaco e facile a errori di disallineamento.

### B. Effetto "Mitragliatrice" e Mancanza di Micro-Variazioni (Jitter)
- In giochi ad alta frequenza d'azione (es. i colpi di pistola in *Hunter's Night* o i passi del robot in *Titan Clash*), riprodurre esattamente lo stesso effetto sonoro decine di volte al secondo con identica frequenza e volume genera monotonia e affaticamento auricolare.
- Attualmente non esiste in bm Sound un parametro di **randomizzazione controllata** (pitch jitter / volume variation). Lo sviluppatore deve programmare manualmente in Lua formule come `hz + math.random(-20, 20)`.

### C. Gestione dei Canali Audio e Priorità (Voice Stealing & Ducking)
- La console ha 8 voci polifoniche. Quando la musica suona sui canali 3–7 e compaiono molti effetti sonori simultanei (esplosioni, laser, monete), è facile che un effetto sonoro "rubi" una voce melodica o viceversa.
- Manca una gestione automatica dei **Bus Audio**:
  - *Bus SFX* (priorità alta, può interrompere altri SFX meno importanti).
  - *Bus Musica* (priorità bassa, protetta da interruzioni accidentali).
  - *Audio Ducking*: abbassamento automatico del volume della musica durante suoni importanti (dialoghi, boss roar, colpo critico).

### D. Sequencer di Jingle Semplificato
- In giochi come *Star Shooter* o *Hunter's Night*, le melodie brevi (es. raccogliere un potenziamento, salire di livello, accendere una lanterna) sono scritte come mini-sequencer dedicati in Lua (`tune = { {523, 4}, {659, 4}, {784, 8} }`).
- Manca la possibilità di salvare in bm Sound questi "Jingle" a 1 o 2 voci distinti dai pattern completi a 8 tracce della canzone.

---

## 3. Complessità "Fatta a Mano" vs Soluzione Integrata

| Esigenza | Approccio Attuale ("A mano") | Con l'Evoluzione di bm Sound |
|---|---|---|
| **Chiamata Effetti Sonori** | `sfx(12)` numerico anonimo o funzioni `note()` scritte a mano nel codice | `sfx("hero_slash")` con etichetta nominale assegnata direttamente nell'editor |
| **Variazione dei Passi/Colpi** | Formule matematiche in Lua per perturbare la frequenza | Slider `Pitch Jitter (+/- 5%)` e `Vol Jitter` nella scheda SFX |
| **Musica che si Abbassa sui Colpi** | Scrittura di inviluppi manuali sulle voci musicali | Canale con flag `Duck Music by 40% on play` |
| **Jingle di Vittoria / Ritiro** | Tabelle di note scritte a mano nel file Lua | Salvataggio di "Jingle" richiamabili con `jingle("quest_complete")` |

---

## 4. Proposte di Sviluppo & Roadmap per bm Sound

### Proposta 1: Named Sound Tags & Export
- Nella pagina **SFX**, aggiungere un campo **Nome / Tag** (es. `coin_get`, `slash_light`, `hit_shield`, `laser`).
- bm Sound salva queste etichette nella sezione `AUDIO` del file `.bm`.
- L'API runtime estende `sfx()` per accettare stringhe:
  ```lua
  sfx("slash_light") -- cerca il nome nel dizionario del banco e suona
  ```
- In questo modo il programmatore non deve più gestire tabelle numeriche né duplicare suoni in Lua.

### Proposta 2: Randomizer / Variation Sliders nella Pagina SFX
Aggiungere due parametri opzionali per ogni effetto sonoro:
- **Pitch Variance (`0..20%`)**: a ogni riproduzione, la frequenza fondamentale viene leggermente variata casualmente entro questo intervallo.
- **Volume Variance (`0..15%`)**: leggera variazione di guadagno per dare naturalezza all'impatto.
- Questa funzione risolve istantaneamente la ripetitività di passi, spari, rimbalzi e fendenti senza toccare una riga di codice nel gioco.

### Proposta 3: Canali Riservati e Auto-Ducking
- Nel menu di bm Sound (o nella pagina SONG), aggiungere una suddivisione predefinita delle voci:
  - **Voci 0–2**: *SFX Bus* (con furto intelligente della voce più vecchia se tutte e 3 sono occupate).
  - **Voci 3–7**: *Music Bus*.
- Possibilità di contrassegnare un SFX con l'attributo **Ducking**: quando l'effetto parte, le voci della musica si attenuano automaticamente di un valore configurabile per la durata del suono.

### Proposta 4: Integrazione "Audio Event" con bm Animator e bm Pixel
- Nella pagina SFX, un'opzione *Test on Animation*: permette di vedere l'anteprima di uno sprite in movimento (da bm Pixel o bm Animator) e riprodurre l'effetto sonoro per verificare a orecchio il perfetto tempismo tra impatto visivo e attacco audio prima di avviare il gioco.

---

## 5. Roadmap e Strumenti Audio Avanzati (M46, R9, R17, R25, R31–R35)

- **Import WAV / PCM da SD (R9, R25)**:
  - Possibilità di importare campioni audio WAV direttamente dalla scheda SD dentro bm Sound per strumenti con forma d'onda `SAMPLE`.
  - Editor di campioni (R32): funzioni integrate per ritaglio, normalizzazione ampiezza, impostazione punti di loop; registrazione live da microfono USB o mix interno.
- **Import / Export MIDI (R17, R25)**:
  - Importazione di file `.mid` standard per generare automaticamente le sequenze nei banchi PATTERN e SONG di bm Sound.
- **Gestione Campioni tra Banchi (R31, R33, R34)**:
  - Importazione di uno strumento `SAMPLE` da un'altra cartuccia con copia e rinumerazione automatica del campione associato (`MOD1`).
  - Distribuzione di pacchetti campioni audio `.bmau` v3 sul Market.
  - Supporto per kit multicampione in `riff` (`s "mykit:3"`).
- **Nuovi Preset ed Effetti nell'Assistente Musicale (R35)**:
  - Aggiornamento di `ai.music` e `predict` con supporto a preset moderni (`kit`, `choir`, `lush`, `bitbass`) ed effetti (`crush`, `vowel`, `duck` per sidechain).

