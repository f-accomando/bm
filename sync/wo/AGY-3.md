# AGY-3 — TRIAGE: bozza di classificazione BETA-6 (solo documento, nessuna decisione)
Inizia dopo: DELIVERED AGY-2.
Base: bm-core aggiornato; branch agy/beta6-triage. Mai push su bm-core.
Perimetro: docs/spec/beta6-triage.md (nuovo). Sola lettura: docs/spec/divergences.toml, docs/spec/CONSISTENCY.md, src/**. NON toccare: docs/spec/divergences.toml, specs.toml, known.toml, scripts/consistency.py, qualsiasi sorgente.
Obiettivo: per D01-D13, D28 e D30-D36 (21 voci) una riga di tabella con la prova letta nel codice dei due kernel e una classe PROPOSTA.

Formato (intestazione del file: "Bozza AGY, <data>, letta su bm-core@<commit>: classi proposte, decidono SPV e l'utente"):
| ID | area | status attuale | prova Pi (file:riga) | prova RGB30 (file:riga) | classe proposta | motivo (≤200 caratteri) |
Classi ammesse, una sola per voce:
- fix-before-beta: tocca menu, input, Games, SDK o salvataggi e cause ≠ hardware;
- accept-hardware: la differenza discende da una capability di docs/spec/platforms.toml (citarla);
- accept-proposed: fuori dalle cinque aree, con motivo;
- fixed-on-branch: corretta su un branch non integrato (scrivere branch@commit, es. D28 sul branch AGY);
- needs-hardware-test: status untested e codice già allineato sui due kernel.
La prova è il codice letto in questa sessione, non la nota del toml; se la prova contraddice il toml, scriverlo nel motivo con "toml stale".

## AGY-3a — D01-D07 (bm-light, 40 min)
## AGY-3b — D08-D13 e D28 (bm-light, 40 min)
## AGY-3c — D30-D36 e tabella finale (bm-light, 40 min)
- In fondo al file: conteggio per classe e la lista fix-before-beta ordinata per priority del toml.
- Verifica: grep -cE '^\| D(0[1-9]|1[0-3]|28|3[0-6]) \|' docs/spec/beta6-triage.md → 21; git diff --name-only bm-core...HEAD → solo docs/spec/beta6-triage.md; python scripts/consistency.py check → stesso output di bm-core (toml intatto).
Criterio di fine: 21 righe, ognuna con almeno una prova file:riga (oppure "-" motivato per il kernel non coinvolto); verifiche OK.
Consegna: riga DELIVERED standard, poi FREE.
