# Testi della scrittura col pad (M30)

I testi da cui `scripts/mkpadwords.py` conta le parole per la predizione della scrittura
col pad (`src/ai/padtype.lua`, guida in `docs/PADTYPE.md`) e da cui `scripts/padsyll.py`
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

Il nome del file prima di `_2`, `_3` è il gruppo: `make pad-bench` toglie un gruppo alla
volta dal dizionario e misura la predizione sui suoi testi. I testi di prova del
benchmark (`padtype.TEXTS`) non sono qui. Per migliorare la predizione si aggiungono
testi (UTF-8, accenti veri; il codice li porta in CP437), poi `make`.
