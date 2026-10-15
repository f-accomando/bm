Compito: riparare la CI di bm e audit permanente, work order in `sync/wo/CI.md` (CI-1→D-MON→D-RES→D-LOAD→D-SCREEN→D-RGB→CI-AUDIT), decisioni 1-5 prese.
Branch: `claude/ci-repair` da `bm-core` aggiornato. Le cause sono IPOTESI da verificare; NON invocare bm-deep (scrivi a SPV e segnati WAITING).
Verifica: comando di ciascun WO (test selettivi); consegna `DELIVERED ...` nel tuo file per ogni WO; il primo run verde si vede solo con un push del branch di lavoro.
Prima: `## Profilo` e `## Requisiti` (WSL, QEMU 8.2, `gh` autenticato), `--wakeup`, poi `--ack auto` e `WORKING`.
