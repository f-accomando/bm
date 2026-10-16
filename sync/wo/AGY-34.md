# AGY-34 - IT2EN-SYLLABLES: `scripts/syllables.py`: testi stampati e titoli in inglese
Assegnato ad AGY (16 ott). Lo script dei conteggi sillabici stampa testi italiani; il repo scrive gli strumenti in inglese. È solo una traduzione di stringhe.
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/syllables-en C:\Users\Utente\Documents\GitHub\bm33-agy34 origin/claude/integrazione-ottobre`); branch `claude/syllables-en`. Mai push su bm-core; push solo di questo branch.
Perimetro: `scripts/syllables.py`, SOLO le stringhe passate a `print(...)` e i titoli del ciclo `for title, cnt, of`, alle righe 99, 100, 104, 106, 108, 122, 124, 130, 133, 135, 137-142, 150-153, 159 (le parole `(vocale)` e `(aperta)` incluse).
NON toccare: la logica, i formati `%d`/`%.1f`/`%s`/`%%`, il dizionario `digraph` (dati italiani: `che chi ghe ghi` ecc.: restano), l'italiano come lingua analizzata, i commenti già inglesi; ogni altro file.
Disgiunzione: i file di questo WO non sono nei perimetri di AGY-10..25 (perimetri letti in `sync/wo/`), AGX-3..12, APP-7/8 e R-NET6..9; `scripts/syllables.py` non è nei loro perimetri (AGY-35 lo importa senza modificarlo).
Vincoli: solo host, nessun WSL (`python scripts/syllables.py` basta; legge `src/ai/words`). `not verified on hardware`. I numeri di riga qui sotto sono stati riaperti dal planner con `git show origin/claude/integrazione-ottobre:<file>` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n -F` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: `BLOCKED`, non indovinare. Fine riga: `git ls-files --eol <file>` mostra `w/lf` o `w/crlf`: tieni quello del file; `git diff --stat` deve restare piccolo (vedi Criterio): se dice centinaia di righe hai cambiato i fine riga, `git checkout -- <file>` e rifai.
Obiettivo: `python scripts/syllables.py` esce 0 con output in inglese.

## Stato reale (verificato su `origin/claude/integrazione-ottobre` @ f5cd71b7)
Le righe sono state riaperte con `git show` e coincidono: 99 (titolo), 100 (`In teoria`), 104, 106, 108, 122, 124, 130, 133, 135, 137, 138 (`(vocale)`), 139, 140, 141, 142 (`(aperta)`), 150-153 (i tre titoli `Sillabe`, `Consonante + vocale`, `Consonante iniziale (...)`), 159. Ritrovale con `git grep -n -E "print\(|Sillabe|Consonante" -- scripts/syllables.py`; non fidarti dei numeri.

## AGY-34a - misura PRIMA (bm-mechanic, 1 min)
`python scripts/syllables.py > /tmp/syl_before.txt; echo "EXIT=$?"` (EXIT=0; tieni il file: i NUMERI dopo devono essere uguali).

## AGY-34b - la traduzione (bm-light, 9 min)
Traduci in inglese ogni stringa delle righe elencate, mantenendo identici specificatori, numero e ordine degli argomenti. Esempi: `'Le sillabe dell\'italiano, accenti esclusi'` -> `'The syllables of Italian, accents excluded'`; `'\nIn teoria (scrittura):'` -> `'\nIn theory (spelling):'`; `... consonanti italiane (...) per 5 vocali: %d sillabe consonante + vocale, %d con le vocali da sole` -> `... Italian consonants (...) times 5 vowels: %d consonant + vowel syllables, %d with the vowels alone`; `piu\' %d scritte con due o tre lettere per un suono` -> `plus %d written with two or three letters for one sound`; `(vocale)` -> `(vowel)`; `(aperta)` -> `(open)`; `Sillabe` -> `Syllables`; `Consonante + vocale` -> `Consonant + vowel`; `a gruppi di 4 per frequenza` -> `in groups of 4 by frequency`; `in tutto` -> `in all`; `Nei testi di src/ai/words (%d parole, %d diverse; con il lessico %d parole diverse)` -> `In the texts of src/ai/words (%d words, %d distinct; with the lexicon %d distinct words)`. Le altre righe allo stesso modo. Evita apostrofi nelle stringhe nuove (o usa le virgolette doppie).

## Verifica
1. `python scripts/syllables.py > /tmp/syl_after.txt; echo "EXIT=$?"`: EXIT=0; confronta con `/tmp/syl_before.txt`: stesse cifre (il testo cambia, i numeri no; controlla a occhio tre righe).
2. `git grep -nE "sillab|vocal|consonant[ei] |frequenti|diversi" -- scripts/syllables.py`: nessuna uscita, o solo commenti/righe già inglesi (descrivile nella consegna).
3. `git diff --stat`: 1 file, circa 24 righe cambiate.

Criterio di fine: 1 file, solo stringhe, EXIT=0.
Commit: un solo commit sul branch `claude/syllables-en`, messaggio `tools: syllables.py prints in English`. Push solo di questo branch (`git push -u origin claude/syllables-en`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-34 --repo C:\Users\Utente\Documents\GitHub\bm33-agy34 --branch claude/syllables-en --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-34 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-34 @ claude/syllables-en@<sha> · verifica: python scripts/syllables.py -> exit 0, cifre uguali; git grep parole italiane -> nessuna uscita · 1 file · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-34 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light (traduzione).
Stima: 10 minuti di lavoro.

## Decisioni (proposte del planner, da adottare da SPV)
Nessuna.

## Prove per l'utente
Nessuna.
