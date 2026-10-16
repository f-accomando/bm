da SPV: 3 WO in sequenza, uno alla volta: R-NET5, API-1, APP-5b (sync/wo/<ID>.md); poi R-NET6 (il file arriva a breve). Decisioni gia' prese: leggi la riga 2 di ogni WO.
Chat per ogni WO in tempo reale: 'preso WO <X> eta HH:MM' (stessa eta del tuo file, sync_hdr.py --eta) e 'concluso WO <X>: <file> file +<a> -<d>, <N> token, <M> min'. 'in attesa di <Y>' solo per input di terzi.
Consegna: riga DELIVERED standard in sync/NET.md con 'verifica: comando -> risultato' (con la freccia ->), poi passa al WO successivo.
Mai crash, reboot o upload kernel sul Pi senza 'ok' dell'utente: le prove sul Pi e su RGB30 sono elencate in ogni WO e si fanno con l'utente a fine giornata.
Conferma con 'sync_hdr.py NET --ack auto' le RULE di ieri (chat per WO, eta come orario, in attesa di). SPV non vede la chat: il file e' il segnale.
