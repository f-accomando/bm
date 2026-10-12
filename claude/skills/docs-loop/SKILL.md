---
name: docs-loop
description: Riordina CLAUDE.md, docs/progress.md e docs/progress/*.md con un ciclo alla Karpathy (una modifica, una misura, tieni o scarta) per avere un solo indice, doc snelli, meno riletture e skill dai workflow ripetuti. Usala dopo aver modificato quei file (il mod crab-ledger lo ricorda), a fine milestone, o quando l'utente chiede di ottimizzare i doc o i token.
---

# docs-loop: doc snelli, un solo indice, meno riletture

Collegata al mod **crab-ledger**: legge il suo report (`~/.claude/crab-ledger-report.json`,
scritto a ogni aggiornamento del pannello `/ledger`) per sapere quali file si rileggono di più.
Misura = `python scripts/docs_audit.py score` (più basso è meglio): token sempre caricati
(CLAUDE.md + progress.md) + token medi del file d'area + penalità (orfani, link rotti, file
oltre ~120 righe, righe duplicate, CLAUDE.md senza il rimando all'indice).

## 1. Base

```bash
python scripts/docs_audit.py score
python scripts/docs_audit.py rereads      # costo delle riletture, 7 giorni (crab-ledger)
python scripts/docs_audit.py repeats 30   # prompt e comandi ripetuti in ≥3 sessioni
python scripts/docs_audit.py best         # ultimo punteggio tenuto
```

Se `rereads` dice "no report": apri `/ledger` una volta e riprova.

## 2. Ciclo (al massimo 8 giri, senza chiedere conferma tra un giro e l'altro)

Un giro = **una sola ipotesi**, scelta in quest'ordine:

1. una penalità dell'audit (orfano → linkalo dall'indice; rotto → correggilo; oltre 120 righe
   → spezza per tema `<area>-<tema>.md` e aggiungi la riga all'indice; duplicato → tieni una
   copia sola e rimanda);
2. CLAUDE.md è il file sempre caricato più grande: ciò che vale per una sola area va nella
   sezione *Rules* del suo `docs/progress/<area>.md`, lasciando in CLAUDE.md una riga;
3. un file nei più riletti: se è un doc, dividilo o indicizzalo; se è codice grosso, una riga
   in `docs/progress/runtime.md` o `reading.md` (banner o comando di ricerca, mai numeri di riga);
4. una riga di `docs/progress.md` che non serve a trovare un file.

Per ogni giro:
1. copia i file che tocchi in `.claude/docs-loop/backup/` (niente `git reset`: l'albero ha molto
   in staging);
2. modifica; rileggi il diff: l'informazione è **spostata, non persa**;
3. `python scripts/docs_audit.py score`;
4. punteggio sceso e nessuna informazione persa → `python scripts/docs_audit.py log <score> keep "<cosa>"`;
   altrimenti ripristina dal backup e `log <score> discard "<cosa>"`.

Si ferma a 8 giri, o dopo 2 scarti di fila, o quando non ci sono più ipotesi.

## 3. Workflow ripetuti → skill

Da `repeats`: un candidato è un prompt o una sequenza di comandi che torna in ≥3 sessioni e
che nessuna skill esistente copre (`git status`, `fetch`, `ls` da soli non sono skill). Per ogni
candidato (al massimo 2 per giro): nome breve, quando scatta, passi, comandi esatti. Scrivi
`claude/skills/<nome>/SKILL.md` (sotto ~60 righe, descrizione con il trigger) e copialo in
`.claude/skills/<nome>/`. Una riga in `docs/progress/tools.md` solo se la skill cambia il
modo di lavorare sul repo.

## 4. Regole

- Un solo indice: `docs/progress.md`; CLAUDE.md rimanda lì e non elenca altri doc.
- Cercare con `Grep -n` / `python scripts/cindex.py <file> [parola]`, poi `Read` offset/limit.
  Comandi per file: `docs/progress/reading.md`; `runtime.c`: `docs/progress/runtime.md`.
- Stato, non cronaca; righe corte; ogni argomento in un solo file. Comunicazione in inglese
  (CLAUDE.md in italiano). Non toccare `*-IT.md`, `OLD*`, i doc storici.
- Niente commit se l'utente non lo chiede.

## 5. Chiusura

Riporta: punteggio prima → dopo, giri tenuti/scartati (`.claude/docs-loop/results.tsv`), costo
delle riletture da crab-ledger, skill create o proposte.
