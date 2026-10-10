# Testi del completamento delle parole (M30)

I testi da cui `scripts/mkwords.py` conta le parole per il completamento
(`src/ai/predict.lua`, guida in `docs/PREDICT.md`) e da cui `scripts/syllables.py`
conta le sillabe. Sono **scritti apposta per bm** (2026-10-01), in italiano moderno: nessun
testo preso da libri, siti o raccolte con licenze altrui.

| File | Cosa |
|---|---|
| `it_narrativa*.txt` | racconti brevi con dialoghi |
| `it_quotidiano*.txt` | messaggi, email, inviti, telefonate |
| `it_giochi*.txt` | dialoghi e testi dei videogiochi, tutorial, recensioni |
| `it_tecnica*.txt` | programmazione e tecnologia spiegate ai principianti |
| `it_informativi*.txt` | notizie, ricette, viaggi, scuola, sport |
| `it_lettere*.txt` | diario, lettere, cartoline |
| `it_lessico.txt` | ~6 500 forme comuni (una per riga), per le parole che i testi non hanno |
| `en_testi*.txt` | inglese: commenti del codice, giochi, messaggi |

Il nome del file prima di `_2`, `_3` è il gruppo: `make predict-bench` toglie un gruppo
alla volta dal dizionario e misura il completamento sui suoi testi. I testi di prova del
benchmark (`tests/predict/texts.lua`) non sono qui. Per migliorare la predizione si aggiungono
testi (UTF-8, accenti veri; il codice li porta in CP437), poi `make`.
