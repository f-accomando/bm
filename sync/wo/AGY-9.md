# AGY-9 - TRIAGE-BIS: correzioni di dettaglio al triage (AGY-6b)
Inizia dopo: DELIVERED AGY-8. Branch agy/beta6-triage (stesso del 6b, commit sopra 9e517b3a). Perimetro: docs/spec/beta6-triage.md. Mai push su bm-core.
Regola: prova = riga aperta e letta; altrimenti `non verificato`. Le classi sono proposte: dove sotto c'e' un dubbio di classe NON cambiarla da solo, scrivere nella riga il dubbio con la citazione e lasciare decidere a SPV.
- D32: sostituire src/kernel/settings.c:428 (e' un `#else`) con la riga che mostra davvero che sul Pi conferma A. Tenere pad.c:10 per RGB30.
- D35: rgb30.mk:66-67 elenca solo BT_SRCS e non prova "nessun host USB": citare la riga che lo prova oppure `non verificato`.
- D04: ui.c:1083 e' solo `pad_pressed()`; citare ui.c:1084 e bm_input.c:92 (la tastiera BT e' gia' letta) e la differenza "Start conferma" del toml; scrivere nella riga che accept-hardware e' contestabile (toml: cause=history, action "align Start and Tab for BT keyboards").
- D03: la voce e' sospesa dall'utente (2026-10-13, cause=unknown): scrivere che accept-proposed e' tirata perche' "players" riguarda input; lasciare la classe a SPV.
- D34: runtime.c:5065 e' lo screenshot Select+L1+R1, non il sostituto di F11/F12: separare i due fatti nel motivo.
- D13: carts.c:402 e' la coda di una funzione: citare la riga della chiamata a bm_tool_id_of_file.
- "toml stale" su D01, D02, D06, D07, D08, D13: scrivere che e' stale nei campi `pi`/`rgb30` e che il campo `note` descrive gia' lo stato attuale. D09: citare che il toml dichiara gia' l'elenco RGB30 ordinato per titolo.
- D28: carts.c:426 mostra che il Pi scansiona gia' /carts: scrivere `toml stale` sul campo `pi` e che l'evidence del toml (tools.md:127) e' fuori tema.
- Verifica: stesse del WO AGY-3 (21 righe, solo il file nel diff, consistency check 0 drift); in fondo la lista delle sole righe cambiate con le nuove citazioni.
Consegna: riga DELIVERED standard, poi FREE.
