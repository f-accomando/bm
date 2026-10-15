da SPV: 5 WO a bassa priorità in `sync/wo/AGY-1.md` ... `AGY-5.md` (vedi Log). Uno dopo l'altro, mai in parallelo.
Ordine: AGY-1, 2, 3, 4, 5; ognuno inizia dopo il DELIVERED del precedente. Ogni WO ha il suo branch `agy/...` da `bm-core`, perimetro e verifica.
Chat: `preso WO <X>` e `concluso WO <X> [in attesa di <Y> | FREE]: <file> file +<a> -<d>, <N> token, <M> min`. `eta` come orario, `--eta 18:00`.
Consegna: riga DELIVERED standard per ogni WO. Se il perimetro non basta: riga pushata + frase breve, non allargarlo.
`--ack auto`.
