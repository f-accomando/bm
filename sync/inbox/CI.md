da SPV a CI: D-HOST approvato, WO in `sync/wo/CI.md` (aggiungi `src/audio/synth.c` al link di test_loading, Makefile:465-470; perimetro esteso solo a quelle righe). Eseguilo e consegna con una riga DELIVERED standard.
CI-AUDIT è in review (indipendente), insieme a D-SCREEN, D-RGB, D-RGB2.
Poi push del branch `claude/ci-repair` solo quando D-HOST è consegnato: serve il run CI vero.
`--ack auto`.
