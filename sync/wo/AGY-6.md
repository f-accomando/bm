# AGY-6 - correzioni dalle revisioni di AGY-1, AGY-3, AGY-5
Dopo: leggi la riga REVIEW di ciascun WO in SYNC.md. In sequenza 6a, 6b, 6c. Mai push su bm-core.
Regola per tutti: la verifica e' il codice letto, non i comandi del WO. Riga file:riga = riga aperta e letta in questa sessione, che dice quello che la voce afferma. Se un punto non si riesce a verificare, scrivere `non verificato` e non inventare.

## AGY-6a - branch agy/api-source (perimetro di AGY-1)
- tests/tools/test_mkapi.py: la regex dei nomi del runtime diventa `\{\s*"([A-Za-z_0-9]+)",\s*\w+\s*\}` (non esige piu' `l_`). Rifare la lista attesa di NOT_IN_RUNTIME dall'output reale; instrument e instruments devono risultare presenti.
- scripts/mkapi.py: la SIG_RE accetta anche `a, b = nome(...)` (es. `s, port = udp_open([port])`, docs/api/system.md:39,41). Il nome `a` di riff.md (esempio `a(3,8,2)`) va escluso con una lista di rumore esplicita e commentata.
- I 9 duplicati: un test che fallisce se il numero di avvisi di duplicato supera quello atteso (scritto nel test, con l'elenco).
- Verifica: python tests/tools/test_mkapi.py -> OK; python scripts/mkapi.py --check -> exit 0. Il DELIVERED riporta le nuove liste UNDOCUMENTED e NOT_IN_RUNTIME con i nomi.

## AGY-6b - branch agy/beta6-triage (rifare docs/spec/beta6-triage.md)
- Rifare TUTTE le 21 righe: per ogni voce aprire il codice dei due kernel, trovare la riga che prova la differenza, citarla con una frase di cosa dice. `-` motivato se il kernel non e' coinvolto.
- Il toml e' un dato da confrontare: dove contraddice il codice scrivere `toml stale`; dove il toml ha una decisione (D03 sospeso dall'utente il 2026-10-13, D09 accettato per capability storage-path, D35 causa hardware e capability no-keyboard) citarla e dire se la classe proposta la rispetta.
- D28: il commit 32c4ec4b sta su claude/integrazione-ottobre, non su un branch agy/...; scrivere branch@commit reale.
- Verifica: stesse del WO AGY-3 piu' un elenco in fondo con le righe file:riga citate, una per riga, per il controllo a campione di SPV.

## AGY-6c - branch agy/sync-perimeter-quarantine (perimetro di AGY-5)
- tools/sync_perimeter.py righe 53 e 106: `lstrip("./")` -> togliere solo il prefisso "./" (removeprefix o normpath). `.github/workflows/ci.yml` con `NON toccare: .github/workflows/*` deve dare FORBIDDEN.
- Righe 83-99: con --section la parte iniziale e' solo il testo prima del primo "## "; le altre sezioni non contano. Togliere il ramo morto.
- Riga 89: la sezione si trova per ID intero (primo token dopo "## "): FRZ-1 non prende FRZ-10.
- --section inesistente: stderr e exit 2.
- Test nuovi in tests/tools/test_sync_perimeter.py: basename senza "/", `:N-M`, `.github`, sezione precedente non ereditata, ID prefisso, sezione mancante, --diff, file mancante.
- Verifica: python tests/tools/test_sync_perimeter.py -> OK; prova reale con sync/wo/AGY.md come nel WO AGY-5.
